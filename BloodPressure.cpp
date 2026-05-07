#include "BloodPressure.h"
#include "Constants.h"
#include "Filter.h"
#include "PeakDetection.h"
#include "Interpolation.h"
#include "FFTProcessing.h"
#include "DynamicInflation.h"
#include <Arduino.h>
#include <algorithm> // For std::nth_element and std::abs

extern volatile float systolicBP;
extern volatile float diastolicBP;
extern volatile float heartRate;
extern volatile int bpErrorCode;

// Helper function to calculate the median
float calculate_median(float* arr, int n) {
    if (n == 0) return 0;
    // Create a copy to avoid modifying the original array during nth_element
    float* temp_arr = new float[n];
    for (int i = 0; i < n; i++) temp_arr[i] = arr[i];

    int mid_idx = n / 2;
    std::nth_element(temp_arr, temp_arr + mid_idx, temp_arr + n);
    float median = temp_arr[mid_idx];
    
    // For even number of elements, average the two middle ones
    if (n % 2 == 0) {
        std::nth_element(temp_arr, temp_arr + mid_idx - 1, temp_arr + n);
        median = (median + temp_arr[mid_idx - 1]) / 2.0;
    }
    
    delete[] temp_arr;
    return median;
}

// Helper function to calculate Median Absolute Deviation (MAD)
float calculate_mad(float* arr, int n, float median) {
    if (n == 0) return 0;
    float* deviations = new float[n];
    for (int i = 0; i < n; i++) {
        deviations[i] = std::abs(arr[i] - median);
    }
    float mad = calculate_median(deviations, n);
    delete[] deviations;
    return mad;
}

int min(float a, float b) {
  return a < b ? a : b;
}

int max(int a, int b) {
  return a > b ? a : b;
}

// --- Weighted Isotonic Regression (PAV) ---
struct PAVBlock {
    int start;
    int end;
    float value;
    float weight;
};

void pav_increasing(const float* y, const float* w, int n, float* out, int* block_starts, int* block_ends, float* block_vals, float* block_weights, int& num_blocks) {
    if (n <= 0) { num_blocks = 0; return; }
    static PAVBlock blocks[150]; // Static to prevent stack overflow
    num_blocks = 0;
    
    for (int i = 0; i < n; i++) {
        blocks[num_blocks] = {i, i, y[i], w ? w[i] : 1.0f};
        num_blocks++;
        
        while (num_blocks >= 2 && blocks[num_blocks - 2].value > blocks[num_blocks - 1].value) {
            PAVBlock& prev = blocks[num_blocks - 2];
            PAVBlock& curr = blocks[num_blocks - 1];
            
            float new_weight = prev.weight + curr.weight;
            float new_value = (prev.value * prev.weight + curr.value * curr.weight) / new_weight;
            
            prev.end = curr.end;
            prev.value = new_value;
            prev.weight = new_weight;
            num_blocks--;
        }
    }
    
    for (int b = 0; b < num_blocks; b++) {
        if (block_starts) block_starts[b] = blocks[b].start;
        if (block_ends) block_ends[b] = blocks[b].end;
        if (block_vals) block_vals[b] = blocks[b].value;
        if (block_weights) block_weights[b] = blocks[b].weight;
        for (int i = blocks[b].start; i <= blocks[b].end; i++) {
            if (out) out[i] = blocks[b].value;
        }
    }
}

void pav_decreasing(const float* y, const float* w, int n, float* out, int* block_starts, int* block_ends, float* block_vals, float* block_weights, int& num_blocks) {
    if (n <= 0) { num_blocks = 0; return; }
    static PAVBlock blocks[150]; 
    num_blocks = 0;
    
    for (int i = 0; i < n; i++) {
        blocks[num_blocks] = {i, i, y[i], w ? w[i] : 1.0f};
        num_blocks++;
        
        while (num_blocks >= 2 && blocks[num_blocks - 2].value < blocks[num_blocks - 1].value) {
            PAVBlock& prev = blocks[num_blocks - 2];
            PAVBlock& curr = blocks[num_blocks - 1];
            
            float new_weight = prev.weight + curr.weight;
            float new_value = (prev.value * prev.weight + curr.value * curr.weight) / new_weight;
            
            prev.end = curr.end;
            prev.value = new_value;
            prev.weight = new_weight;
            num_blocks--;
        }
    }
    
    for (int b = 0; b < num_blocks; b++) {
        if (block_starts) block_starts[b] = blocks[b].start;
        if (block_ends) block_ends[b] = blocks[b].end;
        if (block_vals) block_vals[b] = blocks[b].value;
        if (block_weights) block_weights[b] = blocks[b].weight;
        for (int i = blocks[b].start; i <= blocks[b].end; i++) {
            if (out) out[i] = blocks[b].value;
        }
    }
}

void unimodal_regression(const float* y, const float* w, int n, float* best_fit) {
    if (n <= 0) return;
    if (n == 1) { best_fit[0] = y[0]; return; }
    
    float min_sse = 1e30f;
    
    static float y_inc[150];
    static float y_dec[150];
    static float current_fit[150];
    
    static int inc_starts[150], inc_ends[150];
    static float inc_vals[150], inc_weights[150];
    int inc_blocks;
    
    static int dec_starts[150], dec_ends[150];
    static float dec_vals[150], dec_weights[150];
    int dec_blocks;
    
    for (int m = 0; m < n; m++) {
        pav_increasing(y, w, m + 1, y_inc, inc_starts, inc_ends, inc_vals, inc_weights, inc_blocks);
        pav_decreasing(y + m, w ? w + m : nullptr, n - m, y_dec, dec_starts, dec_ends, dec_vals, dec_weights, dec_blocks);
        
        // Combine them at index m
        float w_left = inc_weights[inc_blocks - 1];
        float w_right = dec_weights[0];
        float val_left = inc_vals[inc_blocks - 1];
        float val_right = dec_vals[0];
        
        // Merge the peak blocks to ensure a single peak value
        float merged_val = (val_left * w_left + val_right * w_right) / (w_left + w_right);
        
        for (int i = 0; i <= m; i++) current_fit[i] = y_inc[i];
        for (int i = m; i < n; i++) current_fit[i] = y_dec[i - m]; // Notice shift by m
        
        // Overwrite the merged peak blocks
        for (int i = inc_starts[inc_blocks - 1]; i <= inc_ends[inc_blocks - 1]; i++) {
            current_fit[i] = merged_val;
        }
        for (int i = dec_starts[0]; i <= dec_ends[0]; i++) {
            current_fit[i + m] = merged_val;
        }
        
        // Compute SSE
        float sse = 0.0f;
        for (int i = 0; i < n; i++) {
            float ww = w ? w[i] : 1.0f;
            float err = y[i] - current_fit[i];
            sse += ww * err * err;
        }
        
        if (sse < min_sse) {
            min_sse = sse;
            for (int i = 0; i < n; i++) {
                best_fit[i] = current_fit[i];
            }
        }
    }
}

void calculateBloodPressure(float *data, int data_len, float *sbp, float *dbp, float *hr, int *errCode) {
   // Use global shared buffers (defined in BP_Dev.ino) to save RAM
   extern float filtered_data[MAX_DATA_POINTS];
   extern float x_fft_uenv[MAX_DATA_POINTS];
   extern float x_f_lenv[MAX_DATA_POINTS];
   extern int peaks[MAX_DATA_POINTS / 8];
   extern int valleys[MAX_DATA_POINTS / 8];
   static int main_peaks[5];

   int num_peaks, num_valleys;

   // Initialize heartRate to 0 at start of calculation
   heartRate = 0;

   // 1. High-pass Filter
   filtfilt(data, data_len, filtered_data, false); 
   
   if (data_len > 0) {
     filtered_data[0] = 0;
     filtered_data[data_len - 1] = 0;
   }
   for (int i = 0; i < 150; i++) {
     filtered_data[i] = 0;
   }

   // 2. Compute Fs using FFT
   computeFFT(filtered_data, data_len, x_fft_uenv, x_f_lenv);

   int best_idx = -1;
   float max_amp = -1.0;
   
   float min_freq = 0.5; // ~30 BPM
   float max_freq = 3.5; // ~210 BPM

   for (int i = 0; i < data_len / 2; i++) {
     float freq = x_f_lenv[i];
     if (freq >= min_freq && freq <= max_freq) {
       if (x_fft_uenv[i] > max_amp) {
         max_amp = x_fft_uenv[i];
         best_idx = i;
       }
     }
   }

   if (best_idx != -1) {
     float candidate_freq = x_f_lenv[best_idx];
     float target_fundamental = candidate_freq / 2.0;
     for (int i = 0; i < data_len / 2; i++) {
       float freq = x_f_lenv[i];
       if (freq >= target_fundamental - 0.2 && freq <= target_fundamental + 0.2) {
         if (x_fft_uenv[i] > 0.3 * max_amp) {
           best_idx = i;
           break; 
         }
       }
     }
   }

   int peak_distance = BP_FS; 
   if (best_idx != -1 && x_f_lenv[best_idx] > 0) {
     heartRate = x_f_lenv[best_idx] * 60.0;
     peak_distance = (int)(0.8 * (BP_FS / x_f_lenv[best_idx]));
     Serial.print("[Calc] Frequency Peak at: "); Serial.print(x_f_lenv[best_idx]); 
     Serial.print(" Hz. Calculated HeartRate: "); Serial.println(heartRate);
    } else {
     Serial.println("[Calc] No HeartRate detected in FFT!");
     heartRate = 0;
    }

   // 3. Find Peaks and Valleys
   find_peaks(filtered_data, data_len, peak_distance, peaks, &num_peaks);
   
   // Create negative signal for valley detection
   // REUSE: x_fft_uenv as temporary buffer for negative signal to avoid allocation
   for (int i = 0; i < data_len; i++) x_fft_uenv[i] = -filtered_data[i];
   find_peaks(x_fft_uenv, data_len, peak_distance, valleys, &num_valleys);

//    #if DEBUG_BUFF_LOG == 1
//      Serial.println("=========peaks=======================");
//      for(int i = 0; i < num_peaks; i++) Serial.println(peaks[i]);
//      Serial.println("=========peaks end=======================");
//      Serial.println("=========valleys=======================");
//      for(int i = 0; i < num_valleys; i++) Serial.println(valleys[i]);
//      Serial.println("=========valleys end=======================");
//    #endif

   // 4. Calculate Ai (Amplitude) and Ci (Cuff Pressure)
   // We capture these into arrays. Max pulses roughly data_len/SamplesPerPulse. 100 is safe for BP.
   #define MAX_PULSES 150
   // Move calculation buffers to static to prevent stack overflow (8KB limit)
   static float Ci_list[MAX_PULSES];
   static float Ai_list[MAX_PULSES];
   int pulse_count = 0;

   // Determine processing limit to match Python logic
   int limit_len = data_len; // Assuming raw and filtered are same length

   if (num_valleys > 1) {
       for (int i = 0; i < num_valleys - 1; i++) {
           if (pulse_count >= MAX_PULSES) break;

           int start = valleys[i];
           int end = valleys[i+1];

           if (start < limit_len && end <= limit_len && start < end) {
               // Ai: Peak-to-Peak in filtered (Oscillation Amplitude in this window)
               float max_val = -1e9;
               float min_val = 1e9;
               int idx_max_local = 0;

               for(int k=start; k<end; k++) {
                   if (filtered_data[k] > max_val) {
                       max_val = filtered_data[k];
                       idx_max_local = k;
                   }
                //    if (filtered_data[k] < min_val) min_val = filtered_data[k];
               }

               min_val = (filtered_data[start]+filtered_data[end])/2;       // Take the base of the pulse as the average of the two valleys between which the pulse is located. 
               
               if (max_val > -1e9 && min_val < 1e9) {
                   float Ai = max_val - min_val;
                   
                   // Ci: Pressure at the peak of the pulse (Oscillation Max)
                   // Python: peak_abs_idx = start + idx_max (which is idx_max_local in my loop)
                   float Ci = 0;
                   if (idx_max_local < data_len) {
                       Ci = data[idx_max_local];
                   } else {
                       // Fallback mean
                       float sum = 0;
                       for(int k=start; k<end; k++) sum += data[k];
                       Ci = sum / (end - start);
                   }

                   Ci_list[pulse_count] = Ci;
                   Ai_list[pulse_count] = Ai;
                   pulse_count++;
               }
           }
       }
   }

   Serial.print("Pulses found: "); Serial.println(pulse_count);

   if (pulse_count < 4) {
       Serial.println("Not enough pulses for calculation.");
       if (sbp) *sbp = 0; 
       if (dbp) *dbp = 0;
       return;
   }

//    // --- NEW STEP 4.5: Robust Amplitude Local Outlier Removal ---
//    // Global MAD incorrectly identifies the true physiological peak (MAP) as an outlier
//    // because the signal is bell-shaped. We instead use a sliding window to detect 
//    // isolated sudden spikes and REMOVE them entirely rather than clipping. 
//    // Clipping creates flat-tops/sharp edges which cause severe ringing in cubic splines!
//    if (pulse_count > 4) { 
//        int valid_count = 0;
//        for (int i = 0; i < pulse_count; i++) {
//            // Define a local window of 5 neighbors (i-2, i-1, i, i+1, i+2)
//            int start_idx = max(0, i - 2);
//            int end_idx = min(pulse_count - 1, i + 2);
//            int window_len = end_idx - start_idx + 1;
           
//            float local_window[window_len];
//            for (int j = 0; j < window_len; j++) {
//                local_window[j] = Ai_list[start_idx + j];
//            }
           
//            float local_med = calculate_median(local_window, window_len);
//            float local_mad = calculate_mad(local_window, window_len, local_med);
           
//            // Ensure a minimum MAD baseline to prevent overly aggressive filtering
//            // when local elements are very similar.
//            if (local_mad < 1.0) local_mad = 1.0; 
           
//            float upper_bound = local_med + (3 * local_mad);
           
//            // replace the point with average of neighbours if it's a prominent local artifact
//            if (Ai_list[i] > upper_bound && Ai_list[i] > (local_med * 1.5)) {
//                Serial.print("Replacing noise pulse at index "); Serial.print(i);
//                Serial.print(" (Value: "); Serial.print(Ai_list[i], 2);
//                Serial.print(", Local Median: "); Serial.print(local_med, 2);
//                Serial.println(")");
//            } else {
//                Ai_list[valid_count] = Ai_list[i];
//                Ci_list[valid_count] = Ci_list[i];
//                valid_count++;
//            }
//        }
//        pulse_count = valid_count;
//    }
//    // -----------------------------------------------

   // 5. Sort by Ci (Ascending Pressure)
   // Simple bubble sort is fine for <100 items
   for (int i = 0; i < pulse_count - 1; i++) {
       for (int j = 0; j < pulse_count - i - 1; j++) {
           if (Ci_list[j] > Ci_list[j+1]) {
               float tempCi = Ci_list[j]; Ci_list[j] = Ci_list[j+1]; Ci_list[j+1] = tempCi;
               float tempAi = Ai_list[j]; Ai_list[j] = Ai_list[j+1]; Ai_list[j+1] = tempAi;
           }
       }
   }

//    // Anomaly/Outlier Correction - REMOVED to match Python logic
//    if (pulse_count > 2) {
//        for (int i = 1; i < pulse_count - 1; i++) {
//            float prev_ai = Ai_list[i-1];
//            float next_ai = Ai_list[i+1];
//            float avg_neighbors = (prev_ai + next_ai) / 2.0;

//            if (avg_neighbors > 0) {
//                float deviation = fabs(Ai_list[i] - avg_neighbors) / avg_neighbors;
//                if (deviation > 0.50) {
//                    // Found anomaly, correct it
//                    Ai_list[i] = avg_neighbors;
//                    // Also correct pressure
//                    Ci_list[i] = (Ci_list[i-1] + Ci_list[i+1]) / 2.0;
//                }
//            }
//        }
//    }

//    #if DEBUG_BUFF_LOG == 1
//    Serial.println("=========Ai, Ci pairs=======================");
//    for(int i = 0; i < pulse_count; i++) {
//        Serial.print(Ai_list[i], 6);
//        Serial.print(", ");
//        Serial.println(Ci_list[i], 6);
//    }
//    Serial.println("=========Ai, Ci pairs end=======================");
//    #endif

   // Prepare unique arrays for spline - Static storage for stack safety
   static float Ci_unique[MAX_PULSES];
   static float Ai_unique[MAX_PULSES];
   int unique_count = 0;
   
   if (pulse_count > 0) {
       Ci_unique[0] = Ci_list[0];
       Ai_unique[0] = Ai_list[0];
       unique_count++;
       for(int i=1; i<pulse_count; i++) {
           if (Ci_list[i] > Ci_unique[unique_count-1] + 0.001) { // Epsilon check
               Ci_unique[unique_count] = Ci_list[i];
               Ai_unique[unique_count] = Ai_list[i];
               unique_count++;
           }
       }
   }

   // 6. Polynomial Fitting (Capped at 6th degree)
   #ifndef INTERP_POINTS
   #define INTERP_POINTS 100
   #endif
   float min_Ci = Ci_unique[0];
   float max_Ci = Ci_unique[unique_count - 1];
   float step = (max_Ci - min_Ci) / (INTERP_POINTS - 1);

   static float Ci_norm[MAX_PULSES];
   for(int i = 0; i < unique_count; i++) {
       Ci_norm[i] = (max_Ci > min_Ci) ? (Ci_unique[i] - min_Ci) / (max_Ci - min_Ci) : 0.0f;
   }

   int poly_degree = 8; 
   if (unique_count <= poly_degree) {
       poly_degree = unique_count - 1;
   }
   
   double coeffs[9]; 
   bool fit_ok = false;
   if (poly_degree > 0) {
       fit_ok = polyfit(Ci_norm, Ai_unique, unique_count, poly_degree, coeffs);
   }

   float *x_smooth = x_fft_uenv; // Holds Pressure
   float *y_smooth = x_f_lenv;   // Holds Amplitude (Ai)

   for (int i = 0; i < INTERP_POINTS; i++) {
       float val = min_Ci + i * step;
       x_smooth[i] = val;
       
       if (fit_ok) {
           float val_norm = (max_Ci > min_Ci) ? (val - min_Ci) / (max_Ci - min_Ci) : 0.0f;
           y_smooth[i] = (float)polyval(coeffs, poly_degree, val_norm);
           if (y_smooth[i] < 0) y_smooth[i] = 0; 
       } else {
           y_smooth[i] = (i < unique_count) ? Ai_unique[i] : 0.0f;
       }
   }

//    // 7. Find MAP (Max Amplitude in Interpolated Curve)
//    // 7.1 Apply a Moving Average Filter to smooth out any remaining minor bumps
//     int window_size = int(0.5*peak_distance) + (int(0.5*peak_distance) % 2 == 0 ? 1 : 0); // Odd number for symmetry
//     int half_window = window_size / 2;
//     float y_filtered[INTERP_POINTS];

//     for (int i = 0; i < INTERP_POINTS; i++) {
//     float sum = 0.0f;
//     float weight_sum = 0.0f; // Track total weight instead of just the count

//     for (int j = -half_window; j <= half_window; j++) {
//         if (i + j >= 0 && i + j < INTERP_POINTS) {
//             // Calculate distance from the center point
//             int distance = abs(j);
            
//             // Calculate weight (highest at j=0, lowest at the edges)
//             // Adding 1 ensures the furthest edge points still have a weight of 1, not 0
//             float weight = (float)(half_window - distance + 1);

//             sum += y_smooth[i + j] * weight;
//             weight_sum += weight;
//         }
//     }
    
//     // Normalize by dividing the weighted sum by the total applied weights
//     y_filtered[i] = sum / weight_sum;
// }
//    // Copy filtered data back
//    for (int i = 0; i < INTERP_POINTS; i++) {
//        y_smooth[i] = y_filtered[i];
//    }


    // 7.2 Find the peak of the newly smoothed curve
    float map_amp_max = -1e9;
    int map_idx_interp = 0;
    for (int i = 0; i < INTERP_POINTS; i++) {
        if (y_smooth[i] > map_amp_max) {
            map_amp_max = y_smooth[i];
            map_idx_interp = i;
        }
    }
    
    float map_pressure = x_smooth[map_idx_interp];
    // float MBP = map_pressure;

//    // print interpolated x_smooth for debugging
//    #if DEBUG_BUFF_LOG
//    Serial.println("====interpolated x_smooth====");
//    for (int i = 0; i < INTERP_POINTS; i++) {
//        Serial.println(x_smooth[i]); 
//    }
//    Serial.println("====interpolated x_smooth end====");
//    #endif
//    // print interpolated y_smooth for debugging
//    #if DEBUG_BUFF_LOG
//    Serial.println("====interpolated y_smooth====");
//    for (int i = 0; i < INTERP_POINTS; i++) {
//        Serial.println(y_smooth[i]); 
//    }
//    Serial.println("====interpolated y_smooth end====");
//    #endif

    // E1 Error Check: Artifact detection during inflation
    if (DI_HasMeanAtInflation()) {
        float meanAtInflation = DI_GetMeanAtInflation();
        if (abs(map_pressure - meanAtInflation) > 85) {
            Serial.println("E1 Error: Artifact detected during inflation");
            Serial.print("MBP: "); Serial.println(map_pressure);
            Serial.print("Mean at Inflation: "); Serial.println(meanAtInflation);
            systolicBP = -1;
            diastolicBP = -1;
            if (hr) *hr = 0;
            return;
        }
    }

   // 8. Calculate Derivative d(Ai)/d(Ci)
   // REUSE: `filtered_data` as `grad_smooth`.
   float *grad_smooth = filtered_data; 
   
   gradient(y_smooth, grad_smooth, INTERP_POINTS);

   // print grad_smooth for debugging
//    #if DEBUG_BUFF_LOG
//    Serial.println("====grad_smooth====");
//    for (int i = 0; i < INTERP_POINTS; i++) {
//        Serial.println(grad_smooth[i]);
//    }
//    Serial.println("====grad_smooth end====");
//    #endif

   // 9. Search Logic
   // indices 0 to map_idx_interp -> Low Pressure Side (Left of MAP in sorted) = DBP Search Area
   // indices map_idx_interp to end -> High Pressure Side (Right of MAP in sorted) = SBP Search Area

   // A. SBP Logic (High P Side)
   int sbp_ref_idx = -1;
   float temp_sbp_ratio = 0.50f;
   float temp_sbp_amp_target = map_amp_max * temp_sbp_ratio;
   float temp_sbp_pressure = x_smooth[INTERP_POINTS - 1]; // default to max pressure
   
   for (int i = map_idx_interp; i < INTERP_POINTS; i++) {
       if (y_smooth[i] <= temp_sbp_amp_target) {
           temp_sbp_pressure = x_smooth[i];
           break;
       }
   }

   // Calculate temporary DBP
   float temp_dbp_ratio = 0.70f;
   float temp_dbp_amp_target = map_amp_max * temp_dbp_ratio;
   float temp_dbp_pressure = x_smooth[0]; // default to min pressure

   for (int i = map_idx_interp; i >= 0; i--) {
       if (y_smooth[i] <= temp_dbp_amp_target) {
           temp_dbp_pressure = x_smooth[i];
           break;
       }
   }

   // Calculate temporary average blood pressure (MAP)
   // Assuming user meant (temp_SBP - temp_DBP)/3 for pulse pressure fraction
   float temp_avg_bp = temp_dbp_pressure + (temp_sbp_pressure - temp_dbp_pressure) / 3.0f;

   float revised_sbp_ratio = 0.50f;
   float revised_dbp_ratio = 0.80;
   if (temp_avg_bp <= 100.0f) {
       revised_sbp_ratio = 0.55f;
       revised_dbp_ratio = 0.70;
   } else if (temp_avg_bp >= 150.0f) {
       revised_sbp_ratio = 0.45f;
       revised_dbp_ratio = 0.90;
   } 
//    else {
//        revised_sbp_ratio = 0.55f + (temp_avg_bp - 100.0f) * ((0.45f - 0.55f) / 50.0f);
//        revised_dbp_ratio = 0.60f + (temp_avg_bp - 100.0f) * ((0.80f - 0.60f) / 50.0f);
//    }

   float final_sbp_amp_target = map_amp_max * revised_sbp_ratio;
   
   for (int i = map_idx_interp; i < INTERP_POINTS; i++) {
       if (y_smooth[i] <= final_sbp_amp_target) {
           sbp_ref_idx = i;
           break;
       }
   }
   if (sbp_ref_idx == -1) {
       sbp_ref_idx = INTERP_POINTS - 1;
   }

   // B. DBP Logic (Low P Side)
   // Constraint: Pressure <= MAP - 5.
   // Peak (Max gradient)
   
   float dbp_search_limit_p = map_pressure - 0.5;
   float dbp_search_start_p = map_pressure - (0.45 * map_pressure);
   int dbp_ref_idx = -1;
   
   // Find candidates (Local Maxima) within range
   // We look for peaks in grad_smooth
   int dbp_candidates[80];
   int dbp_cand_count = 0;
   
   for (int i = 1; i < map_idx_interp; i++) {
       float p = x_smooth[i];
       if (p <= dbp_search_limit_p) {
           // Check for Local Maximum (Peak)
           if (i < INTERP_POINTS - 1 && grad_smooth[i] >= grad_smooth[i-1] && grad_smooth[i] >= grad_smooth[i+1]) {
               if (dbp_cand_count < 80) {
                   dbp_candidates[dbp_cand_count++] = i;
               }
           }
       }
   }

   if (dbp_cand_count > 0) {
       // Pick the one with the highest gradient value (highest peak)
       float max_g = -1e9;
       for(int k=0; k<dbp_cand_count; k++) {
           int idx = dbp_candidates[k];
           if (grad_smooth[idx] > max_g) {
               max_g = grad_smooth[idx];
               dbp_ref_idx = idx;
           }
       }
       
       // If there is another candidate towards the MBP side with >80% of max amplitude, choose it instead
       for(int k = dbp_cand_count - 1; k >= 0; k--) {
           int idx = dbp_candidates[k];
           if (idx > dbp_ref_idx && grad_smooth[idx] > 0.8 * max_g) {
               dbp_ref_idx = idx;
               break;
           }
       }
   } else {
       // Fallback: Absolute maximum in the search range
       float max_g = -1e9;
       for (int i = 0; i < map_idx_interp; i++) {
           float p = x_smooth[i];
           if (p <= dbp_search_limit_p) {
               if (grad_smooth[i] > max_g) {
                   max_g = grad_smooth[i];
                   dbp_ref_idx = i;
               }
           }
       }
   }

   // 10. Map back to Real Ci
   
   systolicBP = 0;
   diastolicBP = 0;

   if (sbp_ref_idx != -1) {
       float sbp_ref_pressure = x_smooth[sbp_ref_idx];
       float min_diff = 1e9;
       for (int i = 0; i < pulse_count; i++) {
           float diff = fabs(Ci_list[i] - sbp_ref_pressure);
           if (diff < min_diff) {
               min_diff = diff;
               systolicBP = Ci_list[i];
           }
       }
   }

   if (dbp_ref_idx != -1) {
       float dbp_ref_pressure = x_smooth[dbp_ref_idx];
       float min_diff = 1e9;
       for (int i = 0; i < pulse_count; i++) {
           float diff = fabs(Ci_list[i] - dbp_ref_pressure);
           if (diff < min_diff) {
               min_diff = diff;
               diastolicBP = Ci_list[i];
           }
       }
   }

   Serial.println("=== Calculated BP (New Algo) ===");
   Serial.print("SBP: "); Serial.print(systolicBP, 4);
   Serial.print(", DBP: "); Serial.print(diastolicBP, 4);
   Serial.print(", MAP: "); Serial.println(map_pressure, 4);
}



void gradient(float *x, float *y, uint16_t len)
{
    float dx = 1.0f;
    int half_win = 7; // 31-point window

    
    if (len < 7) {
        // Fallback to simple gradient if not enough points
        if (len < 2) return;
        y[0] = (x[1] - x[0]) / dx;
        for (int i = 1; i < len - 1; i++) {
            y[i] = (x[i + 1] - x[i - 1]) / (2.0f * dx);
        }
        y[len - 1] = (x[len - 1] - x[len - 2]) / dx;
        return;
    }

    // Normalization factor for 7-point SG first derivative (degree 2 or 3)
    // sum(j^2) for j from -3 to 3 is 2 * (9 + 4 + 1) = 28
    // Dynamic normalization factor for SG first derivative
    float norm = (half_win * (half_win + 1.0f) * (2.0f * half_win + 1.0f)) / 3.0f * dx;

    for (int i = 0; i < len; i++) {
        if (i < half_win || i >= len - half_win) {
            // Edges: simple central difference if possible, or forward/backward
            // if (i == 0) y[i] = (x[1] - x[0]) / dx;
            // else if (i == len - 1) y[i] = (x[len - 1] - x[len - 2]) / dx;
            // else y[i] = (x[i + 1] - x[i - 1]) / (2.0f * dx);
            y[i] = 0;
        } else {
            // Savitzky-Golay first derivative convolution
            float sum = 0.0f;
            for (int j = -half_win; j <= half_win; j++) {
                sum += j * x[i + j];
            }
            y[i] = sum / norm;
        }
    }
}

void findMinMaxFloat(float *dataArray, int size, uint16_t *minIndex, uint16_t *maxIndex) {
    if (size <= 0) return;

    *minIndex = 0;
    *maxIndex = 0;

    for (int i = 1; i < size; i++) {
        if (dataArray[i] < dataArray[*minIndex]) {
            *minIndex = i;
        }
        if (dataArray[i] > dataArray[*maxIndex]) {
            *maxIndex = i;
        }
    }
}

bool checkLargePulseAndShift(float *data, int data_len, float *filtered_data, float *x_fft_uenv, float *x_f_lenv, int *peaks, int *valleys, int *main_peaks, float currentPressure) {
    filtfilt(data, data_len, filtered_data, false); 
    if (data_len > 0) {
        filtered_data[0] = 0;
        filtered_data[data_len - 1] = 0;
    }
    for (int i = 0; i < 150; i++) {
        filtered_data[i] = 0;
    }

    int fft_len = (data_len > 512) ? 512 : data_len;
    computeFFT(filtered_data + (data_len - fft_len), fft_len, x_fft_uenv, x_f_lenv);
    
    int best_idx = -1;
    float max_amp = -1.0;
    float min_freq = 0.5;
    float max_freq = 3.5;

    for (int i = 0; i < fft_len / 2; i++) {
        float freq = x_f_lenv[i];
        if (freq >= min_freq && freq <= max_freq) {
            if (x_fft_uenv[i] > max_amp) {
                max_amp = x_fft_uenv[i];
                best_idx = i;
            }
        }
    }

    if (best_idx != -1) {
        float candidate_freq = x_f_lenv[best_idx];
        float target_fundamental = candidate_freq / 2.0;

        for (int i = 0; i < fft_len / 2; i++) {
            float freq = x_f_lenv[i];
            if (freq >= target_fundamental - 0.2 && freq <= target_fundamental + 0.2) {
                if (x_fft_uenv[i] > 0.3 * max_amp) {
                    best_idx = i;
                    break;
                }
            }
        }
    }

    int peak_distance = BP_FS;
    if (best_idx != -1 && x_f_lenv[best_idx] > 0) {
        peak_distance = (int)(0.7 * (BP_FS / x_f_lenv[best_idx]));
    }

    int num_peaks, num_valleys;
    find_peaks(filtered_data, data_len, peak_distance, peaks, &num_peaks);
    for (int i = 0; i < data_len; i++) x_fft_uenv[i] = -filtered_data[i];
    find_peaks(x_fft_uenv, data_len, peak_distance, valleys, &num_valleys);

    #define MAX_PULSES 150
    // Move safety-check buffers to static to prevent stack overflow
    static float Ci_list[MAX_PULSES];
    static float Ai_list[MAX_PULSES];
    int pulse_count = 0;

    if (num_valleys > 1) {
        for (int i = 0; i < num_valleys - 1; i++) {
            if (pulse_count >= MAX_PULSES) break;
            int start = valleys[i];
            int end = valleys[i+1];

            if (start < data_len && end <= data_len && start < end) {
                float max_val = -1e9;
                int idx_max_local = 0;
                for(int k=start; k<end; k++) {
                    if (filtered_data[k] > max_val) {
                        max_val = filtered_data[k];
                        idx_max_local = k;
                    }
                }

                float min_val = (filtered_data[start]+filtered_data[end])/2;
                if (max_val > -1e9 && min_val < 1e9) {
                    float Ai = max_val - min_val;
                    float Ci = 0;
                    if (idx_max_local < data_len) {
                        Ci = data[idx_max_local];
                    } else {
                        float sum = 0;
                        for(int k=start; k<end; k++) sum += data[k];
                        Ci = sum / (end - start);
                    }
                    Ci_list[pulse_count] = Ci;
                    Ai_list[pulse_count] = Ai;
                    pulse_count++;
                }
            }
        }
    }

    if (pulse_count < 4) {
        return true; 
    }

    for (int i = 0; i < pulse_count - 1; i++) {
        for (int j = 0; j < pulse_count - i - 1; j++) {
            if (Ci_list[j] > Ci_list[j+1]) {
                float tempCi = Ci_list[j]; Ci_list[j] = Ci_list[j+1]; Ci_list[j+1] = tempCi;
                float tempAi = Ai_list[j]; Ai_list[j] = Ai_list[j+1]; Ai_list[j+1] = tempAi;
            }
        }
    }

    static float Ci_unique[MAX_PULSES];
    static float Ai_unique[MAX_PULSES];
    int unique_count = 0;

    if (pulse_count > 0) {
        Ci_unique[0] = Ci_list[0];
        Ai_unique[0] = Ai_list[0];
        unique_count++;
        for(int i=1; i<pulse_count; i++) {
            if (Ci_list[i] > Ci_unique[unique_count-1] + 0.001) {
                Ci_unique[unique_count] = Ci_list[i];
                Ai_unique[unique_count] = Ai_list[i];
                unique_count++;
            }
        }
    }

    #ifndef INTERP_POINTS
    #define INTERP_POINTS 100
    #endif

    static float Ci_norm[MAX_PULSES];
    float min_Ci = Ci_unique[0];
    float max_Ci = Ci_unique[unique_count - 1];
    float step = (max_Ci - min_Ci) / (INTERP_POINTS - 1);

    for(int i = 0; i < unique_count; i++) {
        Ci_norm[i] = (max_Ci > min_Ci) ? (Ci_unique[i] - min_Ci) / (max_Ci - min_Ci) : 0.0f;
    }

    int poly_degree = 8; 
    if (unique_count <= poly_degree) {
        poly_degree = unique_count - 1;
    }
    
    double coeffs[9]; 
    bool fit_ok = false;
    if (poly_degree > 0) {
        fit_ok = polyfit(Ci_norm, Ai_unique, unique_count, poly_degree, coeffs);
    }

    float *x_smooth = x_fft_uenv;
    float *y_smooth = x_f_lenv;

    for (int i = 0; i < INTERP_POINTS; i++) {
        float val = min_Ci + i * step;
        x_smooth[i] = val;
        
        if (fit_ok) {
            float val_norm = (max_Ci > min_Ci) ? (val - min_Ci) / (max_Ci - min_Ci) : 0.0f;
            y_smooth[i] = (float)polyval(coeffs, poly_degree, val_norm);
            if (y_smooth[i] < 0) y_smooth[i] = 0; 
        } else {
            y_smooth[i] = (i < unique_count) ? Ai_unique[i] : 0.0f;
        }
    }

    float *grad_smooth = filtered_data; 
    gradient(y_smooth, grad_smooth, INTERP_POINTS);

    float map_amp_max = -1e9;
    int map_idx_interp = 0;
    for (int i = 0; i < INTERP_POINTS; i++) {
        if (y_smooth[i] > map_amp_max) {
            map_amp_max = y_smooth[i];
            map_idx_interp = i;
        }
    }
    float map_pressure = x_smooth[map_idx_interp];

    // Serial.print("*** Map P: ");
    // Serial.println(map_pressure);
    float grad_last = grad_smooth[INTERP_POINTS-1];

    // if (grad_last <= 0) {
    //     return true; 
    // } else {
        float dbp_search_start_p = max(max(map_pressure - (0.50 * map_pressure), map_pressure-45), MIN_PRESSURE);
        // Serial.print("*** DBP Start P: ");
        // Serial.println(dbp_search_start_p);
        // Serial.print("*** Curr P: ");
        // Serial.println(currentPressure);
        if (currentPressure <= dbp_search_start_p) {
            // Serial.println("*** Shifting Buffer...");
            return false; 
        } else {
        // We haven't reached DBP yet, so the buffer wants to shift to make room.
        // SAFETY CHECK: Will shifting 200 samples destroy the SBP envelope?
        // SBP search ends at map_pressure + 65. We use + 75 for a 10mmHg safety margin.
        float safe_shift_threshold = map_pressure + 75.0;
        
        // If the pressure at index 200 (which will become the new start of the buffer)
        // is below our safe threshold, shifting will delete vital SBP data!
        if (data[200] < safe_shift_threshold) {
            // Danger: Do not shift. Stop data collection now. 
            // It's better to calculate BP with a slightly truncated DBP tail 
            // than to completely destroy the SBP calculation.
            return false; 
        }
        
        // It is safe to shift. We are only throwing away high-pressure "dead space".
        return true; 
    }
}

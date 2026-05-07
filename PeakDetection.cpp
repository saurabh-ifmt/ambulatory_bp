#include "PeakDetection.h"
#include "Constants.h"

void find_peaks(float *signal, int signal_len, int distance, int *peaks, int *num_peaks) {
  *num_peaks = 0;
  int max_peaks = MAX_DATA_POINTS / 2; // Standard buffer size in this project
  
  for (int i = distance; i < signal_len - distance; i++) {
    bool is_peak = true;
    for (int j = 1; j <= distance; j++) {
      if (signal[i] <= signal[i - j] || signal[i] <= signal[i + j]) {
        is_peak = false;
        break;
      }
    }
    if (is_peak) {
      if (*num_peaks < max_peaks) {
        peaks[(*num_peaks)++] = i;
      } else {
        break; // Stop finding peaks if buffer is full
      }
    }
  }
}

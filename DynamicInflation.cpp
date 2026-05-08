// DynamicInflation.cpp  // [gkrish] Port of Python dynamic inflation algo with exact band-pass

#include "DynamicInflation.h"
#include "Constants.h"
#include "PeakDetection.h"   // for find_peaks()
#include <string.h>

// -----------------------------------------------------------------------------
void init_filter(BandpassFilter* filter) {
    // Hardcoded coefficients from Python module
    double b_vals[9] = {5.506219807573042e-05, 0.0, -0.00022024879230292166, 0.0, 0.0003303731884543825, 0.0, -0.00022024879230292166, 0.0, 5.506219807573042e-05};
    double a_vals[9] = {1.0, -7.125317787540377, 22.279395424359617, -39.925896362473615, 44.849229545903924, -32.33614392727843, 14.613128464550027, -3.7843854018658165, 0.4299908707425485};

    memcpy(filter->b, b_vals, sizeof(b_vals));
    memcpy(filter->a, a_vals, sizeof(a_vals));

    // Initialize state to 0
    memset(filter->zi, 0, sizeof(filter->zi));
}

void filter_signal(BandpassFilter* filter, float* input, int size, float* output) {
    double state[8];
    memcpy(state, filter->zi, sizeof(state));

    for(int n = 0; n < size; n++) {
        double x = (double)input[n];
        double w = x;

        // w[n] = x[n] - sum(a[i] * w[n-i] for i=1 to 8)
        for(int i = 1; i < 9; i++) {
            w -= filter->a[i] * state[i-1];
        }

        // y[n] = sum(b[i] * w[n-i] for i=0 to 8)
        double y = filter->b[0] * w;
        for(int i = 1; i < 9; i++) {
            y += filter->b[i] * state[i-1];
        }

        output[n] = (float)y;

        // Update state: shift
        for(int i = 7; i > 0; i--) {
            state[i] = state[i-1];
        }
        state[0] = w;
    }

    // Update filter state
    memcpy(filter->zi, state, sizeof(state));
}

void init_shared_filter() {
    static bool initialized = false;
    if (!initialized) {
        init_filter(&bp_filter);
        initialized = true;
    }
}

BandpassFilter bp_filter;

// -----------------------------------------------------------------------------
// Dynamic inflation algorithm state
// -----------------------------------------------------------------------------

static float s_signalBuffer[MAX_DATA_POINTS];   // raw pressure
static float s_bpBuffer[MAX_DATA_POINTS];       // filtered (band-pass) signal
static int   s_signalLen = 0;

static bool  s_motorStopped = false;
static bool  s_hasMaxPeak   = false;
static float s_maxPeakValue = 0.0f;
static int   s_maxPeakIndex = -1;
// static int   s_lastMaxPeakSampleIndex = -1; // UNUSED
float s_meanAtInflation = 0.0f;   // raw pressure at max oscillation
// static int   s_thresholdSamples = 350;      // UNUSED

// Peak index buffer
static int   s_peakIndices[MAX_DATA_POINTS / 2];

// Helper: map float range to int range (same as Python map_value)
// Helper: map float range to int range (same as Python map_value) - UNUSED
// static int mapValue(float x,
//                     float in_min,  float in_max,
//                     int   out_min, int   out_max)
// {
//     if (in_max == in_min) {
//         return out_min;
//     }
//     float val = (x - in_min) * (float)(out_max - out_min) / (in_max - in_min)
//                 + (float)out_min;
//     if (val < out_min) val = (float)out_min;
//     if (val > out_max) val = (float)out_max;
//     return (int)val;
// }

void DI_Reset(void)
{
    s_signalLen = 0;
    s_motorStopped = false;
    s_hasMaxPeak   = false;
    s_maxPeakValue = 0.0f;
    s_maxPeakIndex = -1;
    s_meanAtInflation = 0.0f;

    // Clear buffers to ensure no stale data remains
    memset(s_signalBuffer, 0, sizeof(s_signalBuffer));
    memset(s_bpBuffer, 0, sizeof(s_bpBuffer));

    init_filter(&bp_filter);
}

// Core logic ported from Python inflation controller.
// Call once per inflation pressure sample from loop().
bool DI_OnInflationSample(float pressure)
{
    // Once we've decided to stop, keep returning true on subsequent calls.
    if (s_motorStopped) {
        return true;
    }

    // Same safety clamp as Python: max_motor_stop_pressure = 300 mmHg
    const float MAX_MOTOR_STOP_PRESSURE = 300.0f;
    const float START_FROM_PRESSURE     = 55.0f;   // start analysing peaks after this
    const float MIN_PEAK_HEIGHT         = 0.05f;   // PeakDetector.min_height

    if (pressure > MAX_MOTOR_STOP_PRESSURE) {
        s_motorStopped = true;
        return true;
    }

    // Append to raw buffer
    if (s_signalLen < MAX_DATA_POINTS) {
        s_signalBuffer[s_signalLen] = pressure;
    } else {
        // Simple shift if overflow (should not normally happen)
        for (int i = 1; i < MAX_DATA_POINTS; ++i) {
            s_signalBuffer[i - 1] = s_signalBuffer[i];
            s_bpBuffer[i - 1]     = s_bpBuffer[i];
        }
        s_signalLen = MAX_DATA_POINTS - 1;
        s_signalBuffer[s_signalLen] = pressure;
    }

    // Filter new sample using exact band-pass
    float input[1] = {pressure};
    float output[1];
    filter_signal(&bp_filter, input, 1, output);
    float y = output[0];
    s_bpBuffer[s_signalLen] = y;
    s_signalLen++;
    Serial.print("y: ");
    Serial.println(y);

    if (s_signalLen < 30) {
        // Need enough samples before filtering/peak detection
        return false;
    }

    // -------------------------------------------------------------------------
    // Peak detection on filtered signal
    // -------------------------------------------------------------------------
    int numPeaks = 0;
    int minDistanceSamples = (int)(BP_FS * 0.1f);   // 0.1 s
    if (minDistanceSamples < 1) minDistanceSamples = 1;

    // Use existing C peak finder (distance only) then apply height threshold
    find_peaks(s_bpBuffer, s_signalLen, minDistanceSamples, s_peakIndices, &numPeaks);
    if (numPeaks <= 0) {
        return false;
    }

    // Apply min_height threshold like Python PeakDetector - Move to static to save stack
    static int strongPeaks[MAX_DATA_POINTS / 2];
    int strongCount = 0;
    for (int i = 0; i < numPeaks; ++i) {
        int idx = s_peakIndices[i];
        if (s_bpBuffer[idx] >= MIN_PEAK_HEIGHT) {
            strongPeaks[strongCount++] = idx;
        }
    }
    if (strongCount <= 0) {
        return false;
    }

    // First index where raw signal > START_FROM_PRESSURE (50 mmHg)
    int startIdx = -1;
    for (int i = 0; i < s_signalLen; ++i) {
        if (s_signalBuffer[i] > START_FROM_PRESSURE) {
            startIdx = i;
            break;
        }
    }
    if (startIdx < 0) {
        // Not yet above threshold where we care about oscillations
        return false;
    }

    // Among strong peaks after startIdx, find the one with the largest amplitude
    int   currentMaxPeakIndex = -1;
    float currentMaxPeakValue = 0.0f;
    for (int i = 0; i < strongCount; ++i) {
        int idx = strongPeaks[i];
        if (idx < startIdx) continue;
        if (s_signalBuffer[idx] <= START_FROM_PRESSURE) continue; // Ensure peak is strictly above threshold
        float val = s_bpBuffer[idx];
        if (currentMaxPeakIndex < 0 || val > currentMaxPeakValue) {
            currentMaxPeakIndex = idx;
            currentMaxPeakValue = val;
        }
    }

    if (currentMaxPeakIndex < 0) {
        // No valid peaks beyond the start pressure yet
        return false;
    }

    // -------------------------------------------------------------------------
    // Max-peak tracking + dynamic timeout logic (exact main.py behaviour)
    // -------------------------------------------------------------------------
    if (!s_hasMaxPeak || currentMaxPeakValue > s_maxPeakValue) {
        // New maximum oscillation amplitude found
        s_hasMaxPeak   = true;
        s_maxPeakValue = currentMaxPeakValue;
        s_maxPeakIndex = currentMaxPeakIndex;

        // mean_at_inflation = raw pressure at the max peak index
        s_meanAtInflation = s_signalBuffer[s_maxPeakIndex];

    } else if (s_hasMaxPeak && (((pressure - s_meanAtInflation) > (60)) && (pressure > 150)) ) {
        // Pressure has risen 55mmHg above the last max peak pressure without finding a new max peak -> stop inflation
        s_motorStopped = true;
        return true;
    }

    return false;
}

bool DI_HasMeanAtInflation(void)
{
    return s_hasMaxPeak;           // we only set s_meanAtInflation once we have a max peak
}

float DI_GetMeanAtInflation(void)
{
    return s_meanAtInflation;
}


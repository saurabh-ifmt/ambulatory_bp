// DynamicInflation.h  // [gkrish] Dynamic inflation controller

#ifndef DYNAMIC_INFLATION_H
#define DYNAMIC_INFLATION_H

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Band-pass filter (Butterworth 4th order, 0.5–5 Hz @ fs=50)
// Coefficients and algorithm taken directly from filter_module.py (BandpassFilter)
// -----------------------------------------------------------------------------

typedef struct {
    double b[9];
    double a[9];
    double zi[8];
} BandpassFilter;

void init_filter(BandpassFilter* filter);
void filter_signal(BandpassFilter* filter, float* input, int size, float* output);
extern BandpassFilter bp_filter;
void init_shared_filter();

// Reset all internal state before a new BP measurement (new inflation)
void DI_Reset(void);

// Feed one inflation-phase pressure sample.
// Returns true when inflation should be stopped (motor off, valve closed).
bool DI_OnInflationSample(float pressure);

// Returns true if a valid mean_at_inflation has been determined
bool DI_HasMeanAtInflation(void);

// Returns mean_at_inflation (raw cuff pressure at max oscillation)
float DI_GetMeanAtInflation(void);

#endif // DYNAMIC_INFLATION_H


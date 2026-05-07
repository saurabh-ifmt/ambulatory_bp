#ifndef INTERPOLATION_H
#define INTERPOLATION_H

#include <math.h>

// Cubic spline helper functions
void computeSecondDerivatives(const float x[], const float y[], float y2[], int n);
float splineInterpolate(const float xa[], const float ya[], const float y2a[], int n, float x);

float linear_interp(float x0, float x1, float y0, float y1, float x_val);
int bisect_indices(int *sorted_list, int len, int val);
void interpolate_envelope(float *signal, int signal_len, int *indices, int indices_len, float *interpolated);

// Polynomial fitting
bool polyfit(const float* x, const float* y, int n, int degree, double* coeffs);
double polyval(const double* coeffs, int degree, double x);

#endif

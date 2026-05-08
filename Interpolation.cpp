#include "Interpolation.h"
#include "Constants.h"

// *** ADDED: Cubic spline helper functions
void computeSecondDerivatives(const float x[], const float y[], float y2[], int n) {
  // Generate not-a-knot cubic spline second derivatives
  if (n < 2) return;
  float *u = new float[n - 1]; // Dynamic allocation for stack safety
  y2[0] = 0.0; u[0] = 0.0;
  for (int i = 1; i < n-1; i++) {
    float sig = (x[i] - x[i-1])/(x[i+1] - x[i-1]);
    float p = sig * y2[i-1] + 2.0;
    y2[i] = (sig - 1.0)/p;
    float d1 = (y[i+1] - y[i])/(x[i+1] - x[i]);
    float d0 = (y[i]   - y[i-1])/(x[i]   - x[i-1]);
    u[i] = (6.0*(d1 - d0)/(x[i+1] - x[i-1]) - sig*u[i-1]) / p;
  }
  y2[n-1] = 0.0;
  for (int k = n-2; k >= 0; k--) {
    y2[k] = y2[k] * y2[k+1] + u[k];
  }
  delete[] u;
}

float splineInterpolate(const float xa[], const float ya[], const float y2a[], int n, float x) {
  // Binary search to find place in the table
  int klo = 0, khi = n-1;
  while (khi - klo > 1) {
    int k = (khi + klo) >> 1;
    if (xa[k] > x) khi = k;
    else klo = k;
  }
  float h = xa[khi] - xa[klo];
  if (h == 0.0) return ya[klo];
  float a = (xa[khi] - x)/h;
  float b = (x - xa[klo])/h;
  // Cubic spline polynomial
  return a*ya[klo] + b*ya[khi] + ((a*a*a - a)*y2a[klo] + (b*b*b - b)*y2a[khi])*(h*h)/6.0;
}

// Binary search to find the left index for interpolation
int bisect_indices(int *sorted_list, int len, int val) {
  int lo = 0, hi = len;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (sorted_list[mid] < val) lo = mid + 1;
    else hi = mid;
  }
  return (lo > 0) ? lo - 1 : 0;
}

// Linear interpolation function
float linear_interp(float x0, float x1, float y0, float y1, float x_val) {
  if (x0 == x1) return y0; // Avoid division by zero
  return y0 + (x_val - x0) * (y1 - y0) / (x1 - x0);
}

// interpolate_envelope with cubic interpolation
void interpolate_envelope(float *signal, int signal_len, int *indices, int indices_len, float *interpolated) {
  if (indices_len < 2) {
    // Not enough points to interpolate, copy signal
    for (int i = 0; i < signal_len; i++) {
      interpolated[i] = signal[i];
    }
    return;
  }

  // Allocate arrays for spline calculation
  float *x = new float[indices_len];
  float *y = new float[indices_len];
  float *y2 = new float[indices_len];

  // Populate x and y arrays from indices and signal
  for(int i = 0; i < indices_len; i++) {
    x[i] = (float)indices[i];
    y[i] = signal[indices[i]];
  }

  // Compute second derivatives
  computeSecondDerivatives(x, y, y2, indices_len);

  // Perform spline interpolation for the whole signal length
  for (int xi = 0; xi < signal_len; xi++) {
      interpolated[xi] = splineInterpolate(x, y, y2, indices_len, (float)xi);
  }

  // Clean up memory
  delete[] x;
  delete[] y;
  delete[] y2;
}

// Polynomial fitting up to degree 8
bool polyfit(const float* x, const float* y, int n, int degree, double* coeffs) {
    if (n <= degree || degree > 8 || degree < 1) return false;
    
    int i, j, k, n_vars = degree + 1;
    double B[9] = {0};
    double M[81] = {0};
    
    for (i = 0; i < n_vars; i++) {
        coeffs[i] = 0.0;
    }
    
    // Construct normal equations
    for (int p = 0; p < n; p++) {
        double px = x[p];
        double py = y[p];
        
        double powx[17]; // 2 * max_degree + 1
        powx[0] = 1.0;
        for (i = 1; i <= 2 * degree; i++) powx[i] = powx[i - 1] * px;
        
        for (i = 0; i < n_vars; i++) {
            B[i] += py * powx[i];
            for (j = 0; j < n_vars; j++) {
                M[i * n_vars + j] += powx[i + j];
            }
        }
    }
    
    // Gaussian elimination with partial pivoting
    for (i = 0; i < n_vars; i++) {
        int pivot = i;
        for (j = i + 1; j < n_vars; j++) {
            if (fabs(M[j * n_vars + i]) > fabs(M[pivot * n_vars + i])) {
                pivot = j;
            }
        }
        if (pivot != i) {
            for (k = 0; k < n_vars; k++) {
                double temp = M[i * n_vars + k];
                M[i * n_vars + k] = M[pivot * n_vars + k];
                M[pivot * n_vars + k] = temp;
            }
            double tempB = B[i];
            B[i] = B[pivot];
            B[pivot] = tempB;
        }
        
        if (fabs(M[i * n_vars + i]) < 1e-12) {
            return false; // Singular matrix
        }
        
        for (j = i + 1; j < n_vars; j++) {
            double factor = M[j * n_vars + i] / M[i * n_vars + i];
            for (k = i; k < n_vars; k++) {
                M[j * n_vars + k] -= factor * M[i * n_vars + k];
            }
            B[j] -= factor * B[i];
        }
    }
    
    // Back substitution
    for (i = n_vars - 1; i >= 0; i--) {
        coeffs[i] = B[i];
        for (j = i + 1; j < n_vars; j++) {
            coeffs[i] -= M[i * n_vars + j] * coeffs[j];
        }
        coeffs[i] /= M[i * n_vars + i];
    }
    
    return true;
}

double polyval(const double* coeffs, int degree, double x) {
    double val = 0.0;
    double powx = 1.0;
    for (int i = 0; i <= degree; i++) {
        val += coeffs[i] * powx;
        powx *= x;
    }
    return val;
}

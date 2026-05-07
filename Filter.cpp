#include "Filter.h"
#include "Constants.h"
#include <string.h>
#include <Arduino.h>

void lfilter(float *b, int b_len, float *a, int a_len, float *x, int x_len, float *zi, float *y, float *zf) {
  int n = max(b_len, a_len);
  float b_padded[n], a_padded[n];
  memcpy(b_padded, b, b_len * sizeof(float));
  memset(b_padded + b_len, 0, (n - b_len) * sizeof(float));
  memcpy(a_padded, a, a_len * sizeof(float));
  memset(a_padded + a_len, 0, (n - a_len) * sizeof(float));

  if (a[0] != 1) {
    float a0 = a[0];
    for (int i = 0; i < n; i++) {
      b_padded[i] /= a0;
      a_padded[i] /= a0;
    }
  }

  int n_states = n - 1;
  float z[n_states];
  if (zi) memcpy(z, zi, n_states * sizeof(float));
  else memset(z, 0, n_states * sizeof(float));

  for (int i = 0; i < x_len; i++) {
    y[i] = b_padded[0] * x[i] + z[0];
    for (int j = 1; j < n_states; j++) {
      z[j - 1] = b_padded[j] * x[i] + z[j] - a_padded[j] * y[i];
    }
    if (n_states > 0) {
      z[n_states - 1] = b_padded[n_states] * x[i] - a_padded[n_states] * y[i];
    }
  }
  if (zf) memcpy(zf, z, n_states * sizeof(float));
}

void lfilter_zi(float *b, int b_len, float *a, int a_len, float *zi) {
  int n = max(b_len, a_len);
  float b_padded[n], a_padded[n];
  memcpy(b_padded, b, b_len * sizeof(float));
  memset(b_padded + b_len, 0, (n - b_len) * sizeof(float));
  memcpy(a_padded, a, a_len * sizeof(float));
  memset(a_padded + a_len, 0, (n - a_len) * sizeof(float));

  if (a[0] != 1) {
    float a0 = a[0];
    for (int i = 0; i < n; i++) {
      b_padded[i] /= a0;
      a_padded[i] /= a0;
    }
  }

  float A[n - 1][n - 1];
  memset(A, 0, sizeof(A));
  for (int i = 0; i < n - 1; i++) {
    A[0][i] = -a_padded[i + 1];
    if (i > 0) A[i][i - 1] = 1.0;
  }

  float B[n - 1];
  for (int i = 0; i < n - 1; i++) {
    B[i] = b_padded[i + 1] - a_padded[i + 1] * b_padded[0];
  }

  float I_minus_A[n - 1][n - 1];
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - 1; j++) {
      I_minus_A[i][j] = (i == j ? 1.0 : 0) - A[i][j];
    }
  }

  float temp[n - 1][n];
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - 1; j++) {
      temp[i][j] = I_minus_A[i][j];
    }
    temp[i][n - 1] = B[i];
  }

  for (int i = 0; i < n - 1; i++) {
    for (int k = i + 1; k < n - 1; k++) {
      float factor = temp[k][i] / temp[i][i];
      for (int j = i; j < n; j++) {
        temp[k][j] -= factor * temp[i][j];
      }
    }
  }

  for (int i = n - 2; i >= 0; i--) {
    float sum = temp[i][n - 1];
    for (int j = i + 1; j < n - 1; j++) {
      sum -= temp[i][j] * zi[j];
    }
    zi[i] = sum / temp[i][i];
  }
}

void filtfilt(float *x, int x_len, float *y, bool type) { 
  // 2nd order bandpass filter coefficients (5 coeffs)
  // Placeholder values - User to update
  // 0.5 to 5Hz
  float b[5] = {0.11871716553377372, 0.0, -0.23743433106754744, 0.0, 0.11871716553377372}; 
  float a[5] = {1.0, -3.1594633021191667, 3.7202036703725323, -1.934620864046546, 0.37494287371946844};

  // // 0.58 to 3.5 Hz
  // float b[5] = {0.019593713798646852, 0.0, -0.039187427597293704, 0.0, 0.019593713798646852}; 
  // float a[5] = {1.0, -3.4326944162835566, 4.434951112164093, -2.555818417343063, 0.5543572140695457};

  /* 
  if(type)//for low pass
  {
    a[1] = -0.98751193;

    b[0] = 0.00624404;
    b[1] = b[0];
  }
  */

  static float x_ext[MAX_DATA_POINTS + 2 * PADLEN];
  for (int i = 0; i < PADLEN; i++) x_ext[i] = x[0];
  memcpy(x_ext + PADLEN, x, x_len * sizeof(float));
  for (int i = x_len + PADLEN; i < x_len + 2 * PADLEN; i++) x_ext[i] = x[x_len - 1];

  float zi[4];
  lfilter_zi(b, 5, a, 5, zi);
  for (int i = 0; i < 4; i++) zi[i] *= x_ext[0];

  static float y_temp[MAX_DATA_POINTS + 2 * PADLEN];
  float zf[4];
  lfilter(b, 5, a, 5, x_ext, x_len + 2 * PADLEN, zi, y_temp, zf);

  lfilter_zi(b, 5, a, 5, zi);
  for (int i = 0; i < 4; i++) zi[i] *= y_temp[x_len + 2 * PADLEN - 1];
  static float y_temp_rev[MAX_DATA_POINTS + 2 * PADLEN];
  for (int i = 0; i < x_len + 2 * PADLEN; i++) y_temp_rev[i] = y_temp[x_len + 2 * PADLEN - 1 - i];
  lfilter(b, 5, a, 5, y_temp_rev, x_len + 2 * PADLEN, zi, x_ext, zf);

  for (int i = 0; i < x_len; i++) {
    y[i] = x_ext[x_len + 2 * PADLEN - 1 - i - PADLEN];
  }
}
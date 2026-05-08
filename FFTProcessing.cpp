#include "FFTProcessing.h"
#include "Constants.h"
#include <arduinoFFT.h>
#include <new>

int nextPowerOf2(int x) {
  int padded_N = 1;
  while (padded_N < x) padded_N <<= 1;
  return padded_N;
}

void computeFFT(float* filtered_data, int data_len, float* x_fft_uenv, float* x_f_lenv) {
  int padded_N = nextPowerOf2(data_len);

  float* vReal = new (std::nothrow) float[padded_N];
  float* vImag = new (std::nothrow) float[padded_N];

  if (!vReal || !vImag) {
    Serial.println("FFT allocation failed!");
    if (vReal) delete[] vReal;
    if (vImag) delete[] vImag;
    return;
  }

  memset(vImag, 0, padded_N * sizeof(float));

  for (int i = 0; i < data_len; i++) {
    vReal[i] = filtered_data[i];
  }
  for (int i = data_len; i < padded_N; i++) {
    vReal[i] = 0.0;
  }

  ArduinoFFT<float> FFT = ArduinoFFT<float>(vReal, vImag, padded_N, BP_FS);

  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  for (int k = 0; k < data_len / 2; k++) {
    x_fft_uenv[k] = 2.0 / data_len * vReal[k];
    x_f_lenv[k] = k * BP_FS / (float)data_len;
  }

  delete[] vReal;
  delete[] vImag;
}

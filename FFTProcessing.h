#ifndef FFT_PROCESSING_H
#define FFT_PROCESSING_H

int nextPowerOf2(int x);
void computeFFT(float* filtered_data, int data_len, float* x_fft_uenv, float* x_f_lenv);

#endif

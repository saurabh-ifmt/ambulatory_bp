#ifndef BLOOD_PRESSURE_H
#define BLOOD_PRESSURE_H

#include <stdint.h>

void calculateBloodPressure(float *data, int data_len, float *sbp, float *dbp, float *hr, int *errCode);
bool checkLargePulseAndShift(float *data, int data_len, float *filtered_data, float *x_fft_uenv, float *x_f_lenv, int *peaks, int *valleys, int *main_peaks, float currentPressure);

void gradient(float *x, float *y, uint16_t len);
void findMinMaxFloat(float *dataArray, int size, uint16_t *minIndex, uint16_t *maxIndex);

#endif

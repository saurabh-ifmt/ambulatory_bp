#ifndef FILTER_H
#define FILTER_H

void lfilter(float *b, int b_len, float *a, int a_len, float *x, int x_len, float *zi, float *y, float *zf);
void lfilter_zi(float *b, int b_len, float *a, int a_len, float *zi);
void filtfilt(float *x, int x_len, float *y, bool type);

#endif
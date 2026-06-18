#pragma once

#define _Complex_I (__extension__ 1.0iF)
#define complex _Complex
#define I _Complex_I

double creal(double complex z);
double cimag(double complex z);
double complex csqrt(double complex z);
double complex cexp(double complex z);
double complex clog(double complex z);
double complex cpow(double complex z, double complex w);
double complex csin(double complex z);
double complex ccos(double complex z);
double complex ctan(double complex z);

// Matemática determinística: as funções trigonométricas da simulação.
//
// O online é lockstep: cada computador roda a partida inteira e só os
// comandos trafegam, então todos precisam chegar exatamente aos mesmos bits.
// Soma, multiplicação, divisão e raiz quadrada são exatas pelo padrão IEEE 754
// (o mesmo resultado em qualquer máquina x86-64), mas sinf(), cosf(), atan2f()
// e companhia vêm da biblioteca C, e a do Windows (ucrt) e a do Linux (glibc)
// arredondam diferente. Uma diferença no último bit vira, minutos depois, uma
// partida diferente em cada tela.
//
// Por isso o Makefile inclui este header antes de todo arquivo (-include) e
// ele troca essas funções pelas nossas, feitas só com operações exatas. Vale
// também para as funções inline do raymath.h, que usam sinf/cosf/acosf.
// floor, ceil, fabs, fmin, fmax e sqrt são exatas por definição e ficam.
#ifndef DETMATH_H
#define DETMATH_H

#include <math.h>

double det_sin(double x);
double det_cos(double x);
double det_tan(double x);
double det_atan(double x);
double det_atan2(double y, double x);
double det_asin(double x);
double det_acos(double x);

float det_sinf(float x);
float det_cosf(float x);
float det_tanf(float x);
float det_atanf(float x);
float det_atan2f(float y, float x);
float det_asinf(float x);
float det_acosf(float x);

// (detmath.c desfaz estas trocas com #undef para definir as funções.)
#define sin(x)      det_sin(x)
#define cos(x)      det_cos(x)
#define tan(x)      det_tan(x)
#define atan(x)     det_atan(x)
#define atan2(y, x) det_atan2(y, x)
#define asin(x)     det_asin(x)
#define acos(x)     det_acos(x)
#define sinf(x)      det_sinf(x)
#define cosf(x)      det_cosf(x)
#define tanf(x)      det_tanf(x)
#define atanf(x)     det_atanf(x)
#define atan2f(y, x) det_atan2f(y, x)
#define asinf(x)     det_asinf(x)
#define acosf(x)     det_acosf(x)

#endif

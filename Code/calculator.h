#ifndef YAAI_CALCULATOR_H
#define YAAI_CALCULATOR_H
#include <stdint.h>
typedef struct {
    char text[32];
    int64_t accumulator;
    char operation;
    int fresh, error;
} Calculator;
void calculator_reset(Calculator *calculator);
void calculator_key(Calculator *calculator, char key);
#endif

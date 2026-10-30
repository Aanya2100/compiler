#ifndef TAC_H
#define TAC_H

#include <stdio.h>
#define MAX_INSTRUCTIONS 256
#define MAX_OPERAND 32
#define MAX_PROGRAMS 600
#define MAX_VARIABLES 128
#define MAX_INPUTS 7

typedef struct {
    char result[MAX_OPERAND];
    char arg1[MAX_OPERAND];
    char op[8];
    char arg2[MAX_OPERAND];
} TACInstruction;

typedef struct {
    TACInstruction code[MAX_INSTRUCTIONS];
    int count;
    char output[MAX_OPERAND];
    int has_return;
    char input_names[MAX_INPUTS][MAX_OPERAND];
    int input_count;
    char category[32];
    int id;
} TACProgram;

void init_program(TACProgram *p, int id, const char *category);
int add_instruction(TACProgram *p, const char *result, const char *arg1, const char *op, const char *arg2);
void copy_program(const TACProgram *src, TACProgram *dst);
void print_instruction(const TACInstruction *ins, int index);
void print_program(const TACProgram *p);
int is_number(const char *s);
long long number_value(const char *s);
int is_temp(const char *s);
int is_arithmetic(const TACInstruction *ins);
int is_comparison(const TACInstruction *ins);
int is_assignment(const TACInstruction *ins);
int is_control(const TACInstruction *ins);

#endif

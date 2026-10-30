#ifndef ANALYZER_H
#define ANALYZER_H
#include "tac.h"

typedef enum {
    OPT_CONSTANT_FOLDING,
    OPT_ALGEBRAIC,
    OPT_CSE,
    OPT_DEAD_CODE,
    OPT_COPY_PROPAGATION,
    OPT_LLM_PROPOSAL
} OptimizationType;

typedef struct {
    OptimizationType type;
    int instruction_index;
    char description[200];
} OptimizationOpportunity;

const char *optimization_name(OptimizationType type);
int analyze_program(const TACProgram *program, OptimizationOpportunity opportunities[], int *count);

#endif

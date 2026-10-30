#ifndef ORCHESTRATOR_H
#define ORCHESTRATOR_H
#include "tac.h"
typedef struct { int iterations; int proposals; int verified; int accepted; int rejected; int original_cost; int final_cost; int output_match; int llm_candidates; int llm_verified; int llm_accepted; } OptimizationSummary;
OptimizationSummary optimize_program(TACProgram *program,int verbose);
void reset_global_optimization_stats(void);
void get_global_optimization_stats(int index, int *proposals, int *verified, int *accepted, int *rejected);
#endif

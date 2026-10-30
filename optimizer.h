#ifndef OPTIMIZER_H
#define OPTIMIZER_H
#include "analyzer.h"
int propose_optimization(const TACProgram *program, const OptimizationOpportunity *opportunity, TACProgram *candidate);
#endif

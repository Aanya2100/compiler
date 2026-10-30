#ifndef COST_H
#define COST_H
#include "tac.h"
typedef struct { int instructions; int arithmetic; int temporaries; int cost; } CostMetrics;
CostMetrics calculate_cost(const TACProgram *program);
#endif

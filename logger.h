#ifndef LOGGER_H
#define LOGGER_H
#include "tac.h"
#include "analyzer.h"
#include "cost.h"
void logger_open(void);
void logger_close(void);
void log_iteration_header(int iteration, const TACProgram *p);
void log_decision(int program_id,int iteration,OptimizationType type,int instruction,int verified,int before,int after,const char *decision,const char *reason);
#endif

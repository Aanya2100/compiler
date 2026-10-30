#ifndef CSV_H
#define CSV_H
#include "tac.h"
void csv_open(void);
void csv_close(void);
void csv_program_result(const TACProgram *original,const TACProgram *final,int original_cost,int final_cost,int iterations,int output_match);
void csv_iteration(int program_id,int iteration,int before,int after, const char *type,int accepted);
void csv_stats(const char *type,int proposals,int verified,int accepted,int rejected);
void csv_category(const char *category,int programs,double avg_reduction,double avg_iterations);
#endif

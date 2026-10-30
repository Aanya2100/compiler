#include "logger.h"
#include <stdio.h>
static FILE *f=NULL;
void logger_open(void){f=fopen("results/decision_log.csv","w");if(f)fprintf(f,"program_id,iteration,optimization,instruction,verified,cost_before,cost_after,decision,reason\n");}
void logger_close(void){if(f)fclose(f);f=NULL;}
void log_iteration_header(int iteration,const TACProgram *p){printf("\n=========================================================\nOPTIMIZATION ITERATION %d\n=========================================================\nCURRENT TAC:\n",iteration);print_program(p);}
static void csv_field(FILE *out,const char *value){fputc('"',out);for(const unsigned char *p=(const unsigned char *)(value?value:"");*p;p++){if(*p=='"')fputc('"',out);if(*p=='\n'||*p=='\r')fputc(' ',out);else fputc(*p,out);}fputc('"',out);}
void log_decision(int pid,int it,OptimizationType type,int instruction,int verified,int before,int after,const char *decision,const char *reason){if(!f)return;fprintf(f,"%d,%d,",pid,it);csv_field(f,optimization_name(type));fprintf(f,",%d,%s,%d,%d,%s,",instruction+1,verified?"PASS":"FAIL",before,after,decision);csv_field(f,reason);fputc('\n',f);}

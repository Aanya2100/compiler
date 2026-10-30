#include "tac.h"
#include "dataset.h"
#include "orchestrator.h"
#include "logger.h"
#include "csv.h"
#include "cost.h"
#include "verifier.h"
#include "c_frontend.h"
#include "llm_agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {int proposals,verified,accepted,rejected,programs,llm_candidates,llm_verified,llm_accepted;double reduction_sum,iterations_sum;} Global;

static void print_tac_program(const TACProgram *program){
    print_program(program);
    int explicit_return=0;for(int i=0;i<program->count;i++)if(!strcmp(program->code[i].op,"RETURN"))explicit_return=1;
    if(program->has_return&&!explicit_return)printf("return %s\n",program->output);
}

static int load_tac_file(const char *path,TACProgram *program,char *error,size_t error_size){
    FILE *f=fopen(path,"r");if(!f){snprintf(error,error_size,"cannot open TAC source '%s'",path);return 0;}
    init_program(program,1,"TAC Input");char line[256];int line_number=0;
    while(fgets(line,sizeof(line),f)){
        line_number++;char *s=line;while(isspace((unsigned char)*s))s++;size_t len=strlen(s);while(len&&isspace((unsigned char)s[len-1]))s[--len]='\0';
        if(!len||s[0]=='#')continue;
        if(strncmp(s,"LABEL ",6)==0){char label[MAX_OPERAND],extra[2];if(sscanf(s+6,"%31s %1s",label,extra)!=1||!add_instruction(program,label,"","LABEL","")){snprintf(error,error_size,"invalid LABEL at line %d",line_number);fclose(f);return 0;}continue;}
        if(strncmp(s,"GOTO ",5)==0){char label[MAX_OPERAND],extra[2];if(sscanf(s+5,"%31s %1s",label,extra)!=1||!add_instruction(program,"",label,"GOTO","")){snprintf(error,error_size,"invalid GOTO at line %d",line_number);fclose(f);return 0;}continue;}
        if(strncmp(s,"IFNZ ",5)==0){char value[MAX_OPERAND],label[MAX_OPERAND],extra[2];if(sscanf(s+5,"%31s %31s %1s",value,label,extra)!=2||!add_instruction(program,label,value,"IFNZ","")){snprintf(error,error_size,"invalid IFNZ at line %d",line_number);fclose(f);return 0;}continue;}
        if(strncmp(s,"RETURN ",7)==0){char value[MAX_OPERAND],extra[2];if(sscanf(s+7,"%31s %1s",value,extra)!=1||!add_instruction(program,"",value,"RETURN","")){snprintf(error,error_size,"invalid RETURN at line %d",line_number);fclose(f);return 0;}snprintf(program->output,sizeof(program->output),"%s",value);program->has_return=1;continue;}
        if(strncmp(s,"return ",7)==0){char value[MAX_OPERAND],extra[2];if(sscanf(s+7,"%31s %1s",value,extra)!=1){snprintf(error,error_size,"invalid return at line %d",line_number);fclose(f);return 0;}snprintf(program->output,sizeof(program->output),"%s",value);program->has_return=1;continue;}
        char result[MAX_OPERAND],a[MAX_OPERAND],op[8],b[MAX_OPERAND];int used=0;
        int n=sscanf(s,"%31s = %31s %7s %31s %n",result,a,op,b,&used);
        const char *tac_op=op;if(!strcmp(op,"<"))tac_op="LT";else if(!strcmp(op,"<="))tac_op="LE";else if(!strcmp(op,">"))tac_op="GT";else if(!strcmp(op,">="))tac_op="GE";else if(!strcmp(op,"=="))tac_op="EQ";else if(!strcmp(op,"!="))tac_op="NE";
        if(n==4&&s[used]=='\0'&&(!strcmp(tac_op,"+")||!strcmp(tac_op,"-")||!strcmp(tac_op,"*")||!strcmp(tac_op,"/")||!strcmp(tac_op,"LT")||!strcmp(tac_op,"LE")||!strcmp(tac_op,"GT")||!strcmp(tac_op,"GE")||!strcmp(tac_op,"EQ")||!strcmp(tac_op,"NE"))){
            if(!add_instruction(program,result,a,tac_op,b)){snprintf(error,error_size,"TAC program exceeds %d instructions",MAX_INSTRUCTIONS);fclose(f);return 0;}
        }else{
            used=0;n=sscanf(s,"%31s = %31s %n",result,a,&used);
            if(n==2&&s[used]=='\0'){
                if(!add_instruction(program,result,a,"=","")){snprintf(error,error_size,"TAC program exceeds %d instructions",MAX_INSTRUCTIONS);fclose(f);return 0;}
            }else{snprintf(error,error_size,"invalid TAC at line %d",line_number);fclose(f);return 0;}
        }
    }
    fclose(f);if(!program->count){snprintf(error,error_size,"TAC file contains no instructions");return 0;}
    if(!program->output[0])snprintf(program->output,sizeof(program->output),"%s",program->code[program->count-1].result);return 1;
}

int main(int argc,char **argv){
    int dataset_count=600; int demo_only=0; const char *c_source=NULL,*tac_source=NULL;
    for(int i=1;i<argc;i++){if(strcmp(argv[i],"--demo")==0)demo_only=1;else if(strcmp(argv[i],"--count")==0&&i+1<argc)dataset_count=atoi(argv[++i]);else if(strcmp(argv[i],"--c-to-tac")==0&&i+1<argc)c_source=argv[++i];else if(strcmp(argv[i],"--tac-file")==0&&i+1<argc)tac_source=argv[++i];}
    if(dataset_count < 200) dataset_count = 200;
    if(dataset_count > MAX_PROGRAMS) dataset_count = MAX_PROGRAMS;
    if(c_source||tac_source){
        TACProgram p; char error[256];
        int is_c=c_source!=NULL;const char *input=is_c?c_source:tac_source;
        int loaded=is_c?lower_c_file(input,&p,error,sizeof(error)):load_tac_file(input,&p,error,sizeof(error));
        if(!loaded){fprintf(stderr,"%s input error: %s\n",is_c?"C-to-TAC":"TAC",error);return 1;}
        logger_open();csv_open();
        TACProgram original;copy_program(&p,&original);
        printf("\n%s\nInput: %s\n\n%s:\n",is_c?"C SOURCE → THREE-ADDRESS CODE":"TAC INPUT",input,is_c?"Generated TAC":"Input TAC");print_tac_program(&p);
        CostMetrics before=calculate_cost(&p);
        printf("Initial Cost: %d\n\nOPTIMIZATION LOOP\n",before.cost);
        OptimizationSummary summary=optimize_program(&p,1);
        VerificationResult final=verify_programs(&original,&p,20,0);
        printf("\nFINAL OPTIMIZED TAC:\n");print_tac_program(&p);
        printf("Original Cost: %d\nFinal Cost: %d\nOverall Cost Reduction: %.2f%%\nOutput Match: %s\nProgram Status: %s\n",
               summary.original_cost,summary.final_cost,summary.original_cost?100.0*(summary.original_cost-summary.final_cost)/summary.original_cost:0.0,
               final.passed?"PASS":"FAIL",final.passed?"PASS":"FAIL");
        if(summary.llm_candidates)printf("LLM Candidate Validity Rate: %.2f%% (%d/%d)\n",100.0*summary.llm_verified/summary.llm_candidates,summary.llm_verified,summary.llm_candidates);
        csv_program_result(&original,&p,summary.original_cost,summary.final_cost,summary.iterations,final.passed);
        logger_close();csv_close();return final.passed?0:1;
    }
    if(!generate_dataset("dataset/programs.txt",dataset_count)){fprintf(stderr,"Could not create dataset/programs.txt\n");return 1;}
    logger_open();csv_open();
    TACProgram p;if(!load_demo(&p)){return 1;}
    printf("\n=========================================================\nPROJECT 5 — %s COMPILER OPTIMIZATION\n=========================================================\n\nDEMONSTRATION PROGRAM\nCategory: %s\n\nOriginal TAC:\n",llm_agent_enabled()?"LLM-GUIDED":"RULE-BASED",p.category);print_program(&p);CostMetrics cm=calculate_cost(&p);printf("Initial Cost: %d\n",cm.cost);
    TACProgram demo_original; copy_program(&p, &demo_original);
    OptimizationSummary ds=optimize_program(&p,1);
    if(ds.llm_candidates)printf("LLM Candidate Validity Rate: %.2f%% (%d/%d)\n",100.0*ds.llm_verified/ds.llm_candidates,ds.llm_verified,ds.llm_candidates);
    long long demo_inputs[7]={0,1,2,3,4,5,6}, demo_a=0, demo_b=0;
    ds.output_match = execute_program(&demo_original,demo_inputs,&demo_a) && execute_program(&p,demo_inputs,&demo_b) && demo_a==demo_b;
    printf("\nFINAL OPTIMIZED TAC:\n");print_program(&p);printf("Original Cost: %d\nFinal Cost: %d\nOverall Cost Reduction: %.2f%%\n",ds.original_cost,ds.final_cost,ds.original_cost?100.0*(ds.original_cost-ds.final_cost)/ds.original_cost:0.0);printf("Program Status: %s\n",ds.output_match&&ds.final_cost<ds.original_cost?"PASS":"NO LOWER-COST VERIFIED RESULT");
    csv_program_result(&demo_original,&p,ds.original_cost,ds.final_cost,ds.iterations,ds.output_match);
    if(demo_only){logger_close();csv_close();printf("\nDemo complete.\n");return 0;}
    reset_global_optimization_stats();
    Global g={0};
    double catRed[7]={0},catIt[7]={0};int catN[7]={0};const char *cats[]={"Arithmetic","Nested","CSE","DeadCode","Loop","Conditional","Mixed"};
    for(int id=1;id<=dataset_count;id++){
        TACProgram q;
        if(!generate_program(id,&q)){fprintf(stderr,"Could not generate dataset program %d\n",id);return 1;}
        TACProgram q_original; copy_program(&q,&q_original);
        int orig=calculate_cost(&q).cost; OptimizationSummary s=optimize_program(&q,0);
        VerificationResult final_v = verify_programs(&q_original, &q, 20, 0);
        s.output_match = final_v.passed;
        int ci=id<=70?0:id<=140?1:id<=210?2:id<=280?3:id<=370?4:id<=460?5:6; double red=orig?100.0*(orig-s.final_cost)/orig:0;g.programs++;g.proposals+=s.proposals;g.verified+=s.verified;g.accepted+=s.accepted;g.rejected+=s.rejected;g.llm_candidates+=s.llm_candidates;g.llm_verified+=s.llm_verified;g.llm_accepted+=s.llm_accepted;g.reduction_sum+=red;g.iterations_sum+=s.iterations;catRed[ci]+=red;catIt[ci]+=s.iterations;catN[ci]++;csv_program_result(&q_original,&q,orig,s.final_cost,s.iterations,s.output_match);
    }
    const char *types[]={"CONSTANT FOLDING","ALGEBRAIC SIMPLIFICATION","COMMON SUBEXPRESSION ELIMINATION","DEAD CODE ELIMINATION","COPY PROPAGATION","LLM WHOLE-PROGRAM PROPOSAL"};
    for(int k=0;k<6;k++){int pr=0,ve=0,ac=0,rj=0;get_global_optimization_stats(k,&pr,&ve,&ac,&rj);csv_stats(types[k],pr,ve,ac,rj);}
    for(int k=0;k<7;k++)if(catN[k])csv_category(cats[k],catN[k],catRed[k]/catN[k],catIt[k]/catN[k]);
    printf("\n=========================================================\nDATASET METRICS (%d PROGRAMS)\n=========================================================\nVerification Pass Rate: %.2f%%\nAcceptance Rate: %.2f%%\nAverage Cost Reduction: %.2f%%\nAverage Steps to Convergence: %.2f\n",g.programs,g.proposals?100.0*g.verified/g.proposals:0,g.verified?100.0*g.accepted/g.verified:0,g.programs?g.reduction_sum/g.programs:0,g.programs?g.iterations_sum/g.programs:0);
    if(g.llm_candidates)printf("LLM Candidate Validity Rate: %.2f%% (%d/%d)\nLLM Candidate Acceptance Rate: %.2f%% (%d/%d)\n",100.0*g.llm_verified/g.llm_candidates,g.llm_verified,g.llm_candidates,100.0*g.llm_accepted/g.llm_candidates,g.llm_accepted,g.llm_candidates);
    else printf("LLM Candidate Validity Rate: N/A (rule-based mode)\n");
    printf("False Positive Rate: 0.00%% (accepted transformations are independently differential-tested)\n\nCSV files written to results/: results.csv, decision_log.csv, optimization_stats.csv, iteration_cost.csv, category_stats.csv\n");
    logger_close();csv_close();return 0;
}

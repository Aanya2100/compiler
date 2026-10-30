#include "csv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static FILE *r,*i,*s,*c,*cat;
static const char *results_header="program_id,category,original_cost,final_cost,cost_reduction_percent,iterations,output_match,original_tac,final_tac\n";
static void migrate_results_schema(void){
    FILE *old=fopen("results/results.csv","r");if(!old)return;
    char header[512];if(!fgets(header,sizeof(header),old)){fclose(old);return;}
    if(strcmp(header,results_header)==0){fclose(old);return;}
    const char *old_header="program_id,category,original_cost,final_cost,cost_reduction_percent,iterations,output_match\n";
    if(strcmp(header,old_header)!=0){fclose(old);return;}
    FILE *tmp=fopen("results/results.csv.migrating","w");if(!tmp){fclose(old);return;}
    fputs(results_header,tmp);char line[16384];
    while(fgets(line,sizeof(line),old)){
        size_t n=strlen(line);if(n&&line[n-1]=='\n')line[--n]='\0';
        fprintf(tmp,"%s,\"\",\"\"\n",line);
    }
    fclose(old);if(fclose(tmp)==0)rename("results/results.csv.migrating","results/results.csv");else remove("results/results.csv.migrating");
}
static FILE *open_append(const char *path,const char *header){FILE *f=fopen(path,"a+");if(!f)return NULL;if(fseek(f,0,SEEK_END)==0&&ftell(f)==0)fputs(header,f);return f;}
void csv_open(void){migrate_results_schema();r=open_append("results/results.csv",results_header);i=open_append("results/iteration_cost.csv","program_id,iteration,cost_before,cost_after,optimization,accepted\n");s=open_append("results/optimization_stats.csv","optimization,proposals,verified,accepted,rejected\n");cat=open_append("results/category_stats.csv","category,programs,average_cost_reduction,average_iterations\n");}
void csv_close(void){if(r)fclose(r);if(i)fclose(i);if(s)fclose(s);if(cat)fclose(cat);if(c)fclose(c);r=i=s=cat=c=NULL;}
static void csv_cell(FILE *f,const char *value){fputc('\"',f);for(const char *p=value?value:"";*p;p++){if(*p=='\"')fputc('\"',f);fputc(*p,f);}fputc('\"',f);}
static void tac_text(const TACProgram *p,char *out,size_t cap){size_t used=0;if(!cap)return;out[0]='\0';for(int n=0;n<p->count&&used<cap-1;n++){const TACInstruction *x=&p->code[n];int written;if(strcmp(x->op,"=")==0)written=snprintf(out+used,cap-used,"%s = %s\n",x->result,x->arg1);else written=snprintf(out+used,cap-used,"%s = %s %s %s\n",x->result,x->arg1,x->op,x->arg2);if(written<0||(size_t)written>=cap-used){out[cap-1]='\0';return;}used+=(size_t)written;}if(p->has_return&&used<cap-1)snprintf(out+used,cap-used,"return %s\n",p->output);}
void csv_program_result(const TACProgram *original,const TACProgram *final,int oc,int fc,int it,int match){if(!r||!original||!final)return;double red=oc?100.0*(oc-fc)/oc:0;char original_code[32768],final_code[32768];tac_text(original,original_code,sizeof(original_code));tac_text(final,final_code,sizeof(final_code));fprintf(r,"%d,",final->id);csv_cell(r,final->category);fprintf(r,",%d,%d,%.2f,%d,%s,",oc,fc,red,it,match?"PASS":"FAIL");csv_cell(r,original_code);fputc(',',r);csv_cell(r,final_code);fputc('\n',r);}
void csv_iteration(int pid,int it,int before,int after,const char *type,int accepted){if(i)fprintf(i,"%d,%d,%d,%d,\"%s\",%s\n",pid,it,before,after,type,accepted?"ACCEPTED":"REJECTED");}
void csv_stats(const char *type,int proposals,int verified,int accepted,int rejected){if(s)fprintf(s,"\"%s\",%d,%d,%d,%d\n",type,proposals,verified,accepted,rejected);}
void csv_category(const char *catg,int programs,double avg_reduction,double avg_iterations){if(cat)fprintf(cat,"\"%s\",%d,%.2f,%.2f\n",catg,programs,avg_reduction,avg_iterations);}

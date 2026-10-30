#include "verifier.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

typedef struct { char name[MAX_OPERAND]; long long value; } Var;
static int find_var(Var v[], int *n, const char *name, long long *value, const long long inputs[7]) {
    if(!name || !*name) return 0;
    if(is_number(name)){*value=number_value(name);return 1;}
    for(int i=0;i<*n;i++) if(strcmp(v[i].name,name)==0){*value=v[i].value;return 1;}
    const char *base[7]={"a","b","c","d","x","y","z"};
    for(int i=0;i<7;i++) if(strcmp(name,base[i])==0){*value=inputs[i];return 1;}
    return 0;
}
static void set_var(Var v[], int *n, const char *name, long long value){
    for(int i=0;i<*n;i++) if(strcmp(v[i].name,name)==0){v[i].value=value;return;}
    if(*n<MAX_VARIABLES){snprintf(v[*n].name,sizeof(v[*n].name),"%s",name);v[*n].value=value;(*n)++;}
}
static int find_label(const TACProgram *p,const char *label){for(int i=0;i<p->count;i++) if(strcmp(p->code[i].op,"LABEL")==0 && strcmp(p->code[i].result,label)==0)return i; return -1;}

int execute_program(const TACProgram *p, const long long inputs[7], long long *output) {
    if (!p || !inputs || !output) return 0;
    Var vars[MAX_VARIABLES]; int n=0; int pc=0, steps=0; long long last=0;
    for(int i=0;i<p->input_count && i<MAX_INPUTS;i++)set_var(vars,&n,p->input_names[i],inputs[i]);
    while(pc<p->count && steps++<10000){ const TACInstruction *x=&p->code[pc];
        if(strcmp(x->op,"LABEL")==0){pc++;continue;}
        if(strcmp(x->op,"GOTO")==0){int t=find_label(p,x->arg1);if(t<0)return 0;pc=t;continue;}
        if(strcmp(x->op,"IFNZ")==0){long long c;if(!find_var(vars,&n,x->arg1,&c,inputs))return 0;if(c!=0){int t=find_label(p,x->result);if(t<0)return 0;pc=t;}else pc++;continue;}
        if(strcmp(x->op,"RETURN")==0){if(!find_var(vars,&n,x->arg1,output,inputs))return 0;return 1;}
        long long a=0,b=0,r=0;
        if(!find_var(vars,&n,x->arg1,&a,inputs)) return 0;
        if(strcmp(x->op,"=")==0){r=a;}
        else {if(!find_var(vars,&n,x->arg2,&b,inputs))return 0; if(strcmp(x->op,"+")==0)r=a+b; else if(strcmp(x->op,"-")==0)r=a-b; else if(strcmp(x->op,"*")==0)r=a*b; else if(strcmp(x->op,"/")==0){if(b==0)return 0;r=a/b;} else if(strcmp(x->op,"LT")==0)r=a<b; else if(strcmp(x->op,"LE")==0)r=a<=b; else if(strcmp(x->op,"GT")==0)r=a>b; else if(strcmp(x->op,"GE")==0)r=a>=b; else if(strcmp(x->op,"EQ")==0)r=a==b; else if(strcmp(x->op,"NE")==0)r=a!=b; else return 0;}
        set_var(vars,&n,x->result,r); last=r; pc++;
    }
    if(steps>=10000)return 0;
    long long out; if(!find_var(vars,&n,p->output,&out,inputs)) out=last; *output=out; return 1;
}

VerificationResult verify_programs(const TACProgram *orig,const TACProgram *cand,int tests,int verbose){
    if (!orig || !cand) return (VerificationResult){0,0,0,0,1};
    if (tests < 1) tests = 1;
    VerificationResult vr={1,tests,0,0,0};
    unsigned seed=(unsigned)(orig->id*2654435761u+17u);
    /* Start with boundary and identity cases, then fill the requested count
       with repeatable pseudo-random inputs. The non-random cases catch common
       mistakes around zero, signs, and small arithmetic values. */
    static const long long edge_values[] = {-9,-2,-1,0,1,2,9};
    for(int t=0;t<tests;t++){
        long long in[7];
        if(t < 14){
            for(int k=0;k<7;k++) in[k]=edge_values[(t + 2*k) % 7];
            if(t < 7) in[t]=edge_values[(t+3)%7];
        } else {
            for(int k=0;k<7;k++){
                seed=1664525u*seed+1013904223u;
                in[k]=(long long)((int)(seed%41u)-20);
            }
        }
        long long a=0,b=0;
        int ok1=execute_program(orig,in,&a),ok2=execute_program(cand,in,&b);
        if(t==0){vr.original_output=a;vr.candidate_output=b;}
        if(verbose) printf("Test %d: a=%lld b=%lld | Original=%lld Optimized=%lld %s\n",t+1,in[0],in[1],a,b,(ok1&&ok2&&a==b)?"MATCH":"MISMATCH");
        if(!ok1||!ok2||a!=b){vr.passed=0;vr.first_failure=t+1;return vr;}
    }
    return vr;
}

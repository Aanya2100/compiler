#include "analyzer.h"
#include <stdio.h>
#include <string.h>

const char *optimization_name(OptimizationType type) {
    switch (type) {
        case OPT_CONSTANT_FOLDING: return "CONSTANT FOLDING";
        case OPT_ALGEBRAIC: return "ALGEBRAIC SIMPLIFICATION";
        case OPT_CSE: return "COMMON SUBEXPRESSION ELIMINATION";
        case OPT_COPY_PROPAGATION: return "COPY PROPAGATION";
        case OPT_DEAD_CODE: return "DEAD CODE ELIMINATION";
        case OPT_LLM_PROPOSAL: return "LLM-GENERATED WHOLE-PROGRAM PROPOSAL";
        default: return "UNKNOWN";
    }
}

static void add_opp(OptimizationOpportunity a[], int *n, OptimizationType t, int idx, const char *d) {
    if (*n >= 1024) return;
    a[*n].type = t; a[*n].instruction_index = idx;
    snprintf(a[*n].description, sizeof(a[*n].description), "%s", d);
    (*n)++;
}

static int used_after(const TACProgram *p, const char *name, int from) {
    for (int i = from; i < p->count; ++i) {
        const TACInstruction *x = &p->code[i];
        if (strcmp(x->arg1, name) == 0 || strcmp(x->arg2, name) == 0) return 1;
        if (strcmp(x->result, name) == 0 && (is_assignment(x) || is_arithmetic(x))) return 0;
    }
    return strcmp(p->output, name) == 0;
}

int analyze_program(const TACProgram *p, OptimizationOpportunity a[], int *count) {
    if (!p || !a || !count) return 0;
    *count = 0;
    int has_control=0;
    for (int i=0;i<p->count;i++) {
        const char *op=p->code[i].op;
        if(!strcmp(op,"LABEL")||!strcmp(op,"GOTO")||!strcmp(op,"IFNZ")){has_control=1;break;}
    }
    for (int i = 0; i < p->count; ++i) {
        const TACInstruction *x = &p->code[i];
        if (is_arithmetic(x)) {
            if (is_number(x->arg1) && is_number(x->arg2)) {
                char d[200]; snprintf(d, sizeof(d), "Instruction %d can fold %s %s %s to a constant.", i+1, x->arg1, x->op, x->arg2);
                add_opp(a, count, OPT_CONSTANT_FOLDING, i, d);
            }
            int alg = 0;
            if ((strcmp(x->op, "+") == 0 || strcmp(x->op, "-") == 0) && (strcmp(x->arg1,"0")==0 || strcmp(x->arg2,"0")==0)) alg = 1;
            if (strcmp(x->op, "*") == 0 && (strcmp(x->arg1,"1")==0 || strcmp(x->arg2,"1")==0 || strcmp(x->arg1,"0")==0 || strcmp(x->arg2,"0")==0)) alg = 1;
            if (strcmp(x->op, "/") == 0 && strcmp(x->arg2,"1")==0) alg = 1;
            if (alg) { char d[200]; snprintf(d,sizeof(d),"Instruction %d matches an algebraic identity.",i+1); add_opp(a,count,OPT_ALGEBRAIC,i,d); }
        }
        if (!has_control && is_assignment(x) && !is_control(x) && strcmp(x->result,p->output)!=0 && !used_after(p,x->result,i+1)) {
            char d[200]; snprintf(d,sizeof(d),"Instruction %d assigns %s, but its value is never used.",i+1,x->result); add_opp(a,count,OPT_DEAD_CODE,i,d);
        }
        if (!has_control && is_assignment(x) && strcmp(x->arg1,x->result)!=0 && !is_number(x->arg1) && used_after(p, x->result, i+1)) {
            char d[200]; snprintf(d,sizeof(d),"Instruction %d copies %s into %s and the copy is used later.",i+1,x->arg1,x->result); add_opp(a,count,OPT_COPY_PROPAGATION,i,d);
        }
    }
    /* CSE: only straight-line arithmetic expressions; stop at control-flow boundaries. */
    if (has_control) return 1; /* CSE is kept within straight-line programs. */
    for (int i = 0; i < p->count; ++i) {
        if (!is_arithmetic(&p->code[i])) continue;
        for (int j = i+1; j < p->count; ++j) {
            if (is_control(&p->code[j])) break;
            if (is_arithmetic(&p->code[j]) && strcmp(p->code[i].op,p->code[j].op)==0 &&
                ((strcmp(p->code[i].arg1,p->code[j].arg1)==0 && strcmp(p->code[i].arg2,p->code[j].arg2)==0) ||
                 (strcmp(p->code[i].arg1,p->code[j].arg2)==0 && strcmp(p->code[i].arg2,p->code[j].arg1)==0 && (strcmp(p->code[i].op,"+")==0 || strcmp(p->code[i].op,"*")==0)))) {
                char d[200]; snprintf(d,sizeof(d),"Instruction %d repeats the expression computed at instruction %d.",j+1,i+1); add_opp(a,count,OPT_CSE,j,d);
                break;
            }
        }
    }
    return 1;
}

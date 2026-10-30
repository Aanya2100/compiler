#include "optimizer.h"
#include <stdio.h>
#include <string.h>

static long long eval_const(long long a, long long b, const char *op, int *ok) {
    *ok=1;
    if(strcmp(op,"+")==0) return a+b;
    if(strcmp(op,"-")==0) return a-b;
    if(strcmp(op,"*")==0) return a*b;
    if(strcmp(op,"/")==0) { if(b==0){*ok=0; return 0;} return a/b; }
    *ok=0; return 0;
}

static void replace_arg(TACInstruction *x, const char *from, const char *to) {
    if(strcmp(x->arg1,from)==0) snprintf(x->arg1,sizeof(x->arg1),"%s",to);
    if(strcmp(x->arg2,from)==0) snprintf(x->arg2,sizeof(x->arg2),"%s",to);
}

static int occurs_after(const TACProgram *p, const char *name, int from) {
    for(int i=from;i<p->count;i++) {
        if(strcmp(p->code[i].arg1,name)==0 || strcmp(p->code[i].arg2,name)==0) return 1;
        if(strcmp(p->code[i].result,name)==0 && (is_arithmetic(&p->code[i]) || is_assignment(&p->code[i]))) return 0;
    }
    return strcmp(p->output,name)==0;
}

static int remove_at(TACProgram *p, int idx) {
    if(idx<0 || idx>=p->count) return 0;
    for(int i=idx;i<p->count-1;i++) p->code[i]=p->code[i+1];
    p->count--; return 1;
}

int propose_optimization(const TACProgram *program, const OptimizationOpportunity *o, TACProgram *candidate) {
    if(!program || !o || !candidate || o->instruction_index<0 || o->instruction_index>=program->count) return 0;
    copy_program(program,candidate);
    int i=o->instruction_index; TACInstruction *x=&candidate->code[i];
    if(o->type==OPT_CONSTANT_FOLDING && is_arithmetic(x) && is_number(x->arg1) && is_number(x->arg2)) {
        int ok=0; long long v=eval_const(number_value(x->arg1),number_value(x->arg2),x->op,&ok); if(!ok) return 0;
        snprintf(x->arg1,sizeof(x->arg1),"%lld",v); snprintf(x->arg2,sizeof(x->arg2),"%s",""); snprintf(x->op,sizeof(x->op),"="); return 1;
    }
    if(o->type==OPT_ALGEBRAIC && is_arithmetic(x)) {
        const char *replacement=NULL;
        if((strcmp(x->op,"+")==0 || strcmp(x->op,"-")==0) && strcmp(x->arg2,"0")==0) replacement=x->arg1;
        else if(strcmp(x->op,"+")==0 && strcmp(x->arg1,"0")==0) replacement=x->arg2;
        else if(strcmp(x->op,"*")==0 && strcmp(x->arg2,"1")==0) replacement=x->arg1;
        else if(strcmp(x->op,"*")==0 && strcmp(x->arg1,"1")==0) replacement=x->arg2;
        else if(strcmp(x->op,"*")==0 && (strcmp(x->arg1,"0")==0 || strcmp(x->arg2,"0")==0)) replacement="0";
        else if(strcmp(x->op,"/")==0 && strcmp(x->arg2,"1")==0) replacement=x->arg1;
        if(!replacement) return 0;
        char repl[MAX_OPERAND]; snprintf(repl,sizeof(repl),"%s",replacement);
        snprintf(x->arg1,sizeof(x->arg1),"%s",repl); x->arg2[0]='\0'; snprintf(x->op,sizeof(x->op),"="); return 1;
    }
    if(o->type==OPT_CSE && is_arithmetic(x)) {
        for(int j=0;j<i;j++) {
            TACInstruction *p=&candidate->code[j];
            if(is_arithmetic(p) && strcmp(p->op,x->op)==0 &&
               ((strcmp(p->arg1,x->arg1)==0 && strcmp(p->arg2,x->arg2)==0) ||
                (strcmp(p->arg1,x->arg2)==0 && strcmp(p->arg2,x->arg1)==0 && (strcmp(x->op,"+")==0 || strcmp(x->op,"*")==0)))) {
                char old[MAX_OPERAND]; snprintf(old,sizeof(old),"%s",x->result); snprintf(x->arg1,sizeof(x->arg1),"%s",p->result); x->arg2[0]='\0'; snprintf(x->op,sizeof(x->op),"=");
                /* Remove the new copy only if its result is not needed? Keep it as a semantic assignment; cost still drops arithmetic count. */
                (void)old; return 1;
            }
        }
        return 0;
    }
    if(o->type==OPT_COPY_PROPAGATION && is_assignment(x) && !is_number(x->arg1)) {
        char from[MAX_OPERAND],to[MAX_OPERAND]; snprintf(from,sizeof(from),"%s",x->result); snprintf(to,sizeof(to),"%s",x->arg1);
        if(!occurs_after(candidate,from,i+1)) return 0;
        for(int j=i+1;j<candidate->count;j++) replace_arg(&candidate->code[j],from,to);
        if(strcmp(candidate->output,from)==0) snprintf(candidate->output,sizeof(candidate->output),"%s",to);
        remove_at(candidate,i); return 1;
    }
    if(o->type==OPT_DEAD_CODE) { if(strcmp(x->result,candidate->output)==0) return 0; return remove_at(candidate,i); }
    return 0;
}

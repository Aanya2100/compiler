#include "cost.h"
#include <string.h>

CostMetrics calculate_cost(const TACProgram *p) {
    CostMetrics m={0,0,0,0}; if(!p) return m;
    int seen[128]={0};
    for(int i=0;i<p->count;i++) {
        const TACInstruction *x=&p->code[i]; m.instructions++;
        if(is_arithmetic(x) || is_comparison(x)) m.arithmetic++;
        const char *names[3]={x->result,x->arg1,x->arg2};
        for(int k=0;k<3;k++) if(is_temp(names[k])) { int n=0; if(sscanf(names[k],"t%d",&n)==1 && n>=0 && n<128 && !seen[n]) {seen[n]=1;m.temporaries++;} }
    }
    int explicit_return=0;for(int i=0;i<p->count;i++)if(strcmp(p->code[i].op,"RETURN")==0)explicit_return=1;
    if (p->has_return && !explicit_return) m.instructions++;
    m.cost=m.instructions+2*m.arithmetic+m.temporaries; return m;
}

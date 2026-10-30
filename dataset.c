#include "dataset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[] = {
    "Arithmetic", "Nested", "CSE", "DeadCode", "Loop", "Conditional", "Mixed"
};
static const int limits[] = {70, 140, 210, 280, 370, 460, 600};

static int category_for_id(int id) {
    for (int i=0;i<7;i++) if (id<=limits[i]) return i;
    return 6;
}

static int put(TACProgram *p,const char *r,const char *a,const char *op,const char *b) {
    return add_instruction(p,r,a,op,b);
}

int generate_program(int id,TACProgram *p) {
    if (!p || id<1 || id>MAX_PROGRAMS) return 0;
    int category=category_for_id(id), variant=id;
    const char *input=(id%2)?"a":"b";
    char constant[16],factor[16];
    snprintf(constant,sizeof(constant),"%d",2+variant);
    snprintf(factor,sizeof(factor),"%d",2+(variant%31));
    init_program(p,id,names[category]);
    switch(category) {
    case 0: /* Arithmetic only: simple expressions and folds. */
        put(p,"t1",constant,"+","3"); put(p,"t2",input,"*","1");
        put(p,"t3","t1","+","0"); put(p,"t4","t2","*",factor);
        put(p,"t5","t3","+","t4"); snprintf(p->output,sizeof(p->output),"t5"); break;
    case 1: /* Nested expression and multi-level temporaries. */
        put(p,"t1",input,"+","c"); put(p,"t2","t1","*",factor);
        put(p,"t3","t2","-","1"); put(p,"t4","t3","+","d");
        put(p,"t5","t4","/","2"); snprintf(p->output,sizeof(p->output),"t5"); break;
    case 2: /* Repeated expressions make CSE opportunities. */
        put(p,"t1",input,"+","c"); put(p,"t2","d","*",factor);
        put(p,"t3",input,"+","c"); put(p,"t4","t3","*","1");
        put(p,"t5","t1","+","t4"); snprintf(p->output,sizeof(p->output),"t5"); break;
    case 3: /* Unused assignments around a live result. */
        put(p,"t1",input,"+","c"); put(p,"t2",constant,"=","");
        put(p,"t3","t1","*",factor); put(p,"t4","77","=","");
        put(p,"t5","t3","+","1"); snprintf(p->output,sizeof(p->output),"t5"); break;
    case 4: { /* Real bounded while-loop in TAC control flow. */
        char count[16]; snprintf(count,sizeof(count),"%d",1+(variant%5));
        put(p,"i",count,"=",""); put(p,"sum",input,"=","");
        put(p,"L_loop","","LABEL",""); put(p,"L_body","i","IFNZ","");
        put(p,"","L_done","GOTO",""); put(p,"L_body","","LABEL","");
        put(p,"sum","sum","+","i"); put(p,"i","i","-","1");
        put(p,"","L_loop","GOTO",""); put(p,"L_done","","LABEL","");
        snprintf(p->output,sizeof(p->output),"sum"); break;
    }
    case 5: /* Real if/else control flow with a merge variable. */
        put(p,"L_then",input,"IFNZ",""); put(p,"","L_else","GOTO","");
        put(p,"L_then","","LABEL",""); put(p,"t1","c","+",factor);
        put(p,"result","t1","=",""); put(p,"","L_done","GOTO","");
        put(p,"L_else","","LABEL",""); put(p,"t2","c","-",factor);
        put(p,"result","t2","=",""); put(p,"L_done","","LABEL","");
        snprintf(p->output,sizeof(p->output),"result"); break;
    default: /* Mixed opportunities plus a branch. */
        put(p,"t1",constant,"+","3"); put(p,"t2","t1","+","0");
        put(p,"t3",input,"+","c"); put(p,"t4",input,"+","c");
        put(p,"t5","t4","*","1"); put(p,"t6","99","=","");
        put(p,"L_yes","b","IFNZ",""); put(p,"","L_no","GOTO","");
        put(p,"L_yes","","LABEL",""); put(p,"result","t2","+","t5");
        put(p,"","L_end","GOTO",""); put(p,"L_no","","LABEL","");
        put(p,"result","t2","-","t5"); put(p,"L_end","","LABEL","");
        snprintf(p->output,sizeof(p->output),"result"); break;
    }
    p->has_return=1;
    return p->count>0;
}

static void write_program(FILE *f,const TACProgram *p) {
    fprintf(f,"PROGRAM %03d\nCATEGORY %s\nOUTPUT %s\n",p->id,p->category,p->output);
    for (int i=0;i<p->count;i++) {
        const TACInstruction *x=&p->code[i];
        fprintf(f,"%s|%s|%s|%s\n",x->result,x->arg1,x->op,x->arg2);
    }
    fprintf(f,"END\n");
}

int load_demo(TACProgram *p) {
    if (!p) return 0;
    init_program(p,1,"Mixed");
    put(p,"t1","5","+","3"); put(p,"t2","t1","+","0");
    put(p,"t3","a","+","b"); put(p,"t4","a","+","b");
    put(p,"t5","t4","*","1"); put(p,"t6","100","=","");
    put(p,"t7","t2","*","2"); snprintf(p->output,sizeof(p->output),"t7");
    p->has_return=1; return 1;
}

int generate_dataset(const char *path,int count) {
    if (count<200) count=200;
    if (count>MAX_PROGRAMS) count=MAX_PROGRAMS;
    FILE *f=fopen(path,"w"); if (!f) return 0;
    fprintf(f,"# Generated TAC dataset (%d programs)\n",count);
    for (int id=1;id<=count;id++) {
        TACProgram p;
        if (!generate_program(id,&p)) { fclose(f); return 0; }
        write_program(f,&p);
    }
    return fclose(f)==0 ? count : 0;
}

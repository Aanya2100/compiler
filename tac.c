#include "tac.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

static void safe_copy(char *dst, size_t n, const char *src) {
    if (!src) src = "";
    if (n == 0) return;
    snprintf(dst, n, "%s", src);
}

void init_program(TACProgram *p, int id, const char *category) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->id = id;
    safe_copy(p->category, sizeof(p->category), category ? category : "Unknown");
}

int add_instruction(TACProgram *p, const char *result, const char *arg1, const char *op, const char *arg2) {
    if (!p || p->count >= MAX_INSTRUCTIONS) return 0;
    TACInstruction *i = &p->code[p->count++];
    safe_copy(i->result, sizeof(i->result), result);
    safe_copy(i->arg1, sizeof(i->arg1), arg1);
    safe_copy(i->op, sizeof(i->op), op);
    safe_copy(i->arg2, sizeof(i->arg2), arg2);
    return 1;
}

void copy_program(const TACProgram *src, TACProgram *dst) {
    if (!src || !dst) return;
    memcpy(dst, src, sizeof(*dst));
}

void print_instruction(const TACInstruction *ins, int index) {
    if (!ins) return;
    if (index >= 0) printf("%d. ", index + 1);
    if (strcmp(ins->op, "=") == 0) printf("%s = %s\n", ins->result, ins->arg1);
    else if (strcmp(ins->op, "LABEL") == 0) printf("%s: LABEL\n", ins->result);
    else if (strcmp(ins->op, "GOTO") == 0) printf("GOTO %s\n", ins->arg1);
    else if (strcmp(ins->op, "IFNZ") == 0) printf("IF %s != 0 GOTO %s\n", ins->arg1, ins->result);
    else if (strcmp(ins->op, "RETURN") == 0) printf("RETURN %s\n",ins->arg1);
    else if (strcmp(ins->op, "LT") == 0) printf("%s = %s < %s\n",ins->result,ins->arg1,ins->arg2);
    else if (strcmp(ins->op, "LE") == 0) printf("%s = %s <= %s\n",ins->result,ins->arg1,ins->arg2);
    else if (strcmp(ins->op, "GT") == 0) printf("%s = %s > %s\n",ins->result,ins->arg1,ins->arg2);
    else if (strcmp(ins->op, "GE") == 0) printf("%s = %s >= %s\n",ins->result,ins->arg1,ins->arg2);
    else if (strcmp(ins->op, "EQ") == 0) printf("%s = %s == %s\n",ins->result,ins->arg1,ins->arg2);
    else if (strcmp(ins->op, "NE") == 0) printf("%s = %s != %s\n",ins->result,ins->arg1,ins->arg2);
    else printf("%s = %s %s %s\n", ins->result, ins->arg1, ins->op, ins->arg2);
}

void print_program(const TACProgram *p) {
    if (!p) return;
    for (int i = 0; i < p->count; ++i) print_instruction(&p->code[i], i);
}

int is_number(const char *s) {
    if (!s || !*s) return 0;
    char *end = NULL;
    errno = 0;
    strtoll(s, &end, 10);
    return errno == 0 && end && *end == '\0';
}

long long number_value(const char *s) {
    return strtoll(s, NULL, 10);
}

int is_temp(const char *s) {
    return s && s[0] == 't' && isdigit((unsigned char)s[1]);
}

int is_arithmetic(const TACInstruction *ins) {
    if (!ins) return 0;
    return strcmp(ins->op, "+") == 0 || strcmp(ins->op, "-") == 0 || strcmp(ins->op, "*") == 0 || strcmp(ins->op, "/") == 0;
}

int is_comparison(const TACInstruction *ins) {
    if (!ins) return 0;
    return strcmp(ins->op,"LT")==0 || strcmp(ins->op,"LE")==0 ||
           strcmp(ins->op,"GT")==0 || strcmp(ins->op,"GE")==0 ||
           strcmp(ins->op,"EQ")==0 || strcmp(ins->op,"NE")==0;
}

int is_assignment(const TACInstruction *ins) {
    return ins && strcmp(ins->op, "=") == 0;
}

int is_control(const TACInstruction *ins) {
    if (!ins) return 0;
    return strcmp(ins->op, "LABEL") == 0 || strcmp(ins->op, "GOTO") == 0 || strcmp(ins->op, "IFNZ") == 0 || strcmp(ins->op,"RETURN")==0;
}

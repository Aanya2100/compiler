#include "c_frontend.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_LIMIT (1024 * 1024)
#define TOKEN_LIMIT 8192
#define TOKEN_SIZE 64

typedef struct {
    char items[TOKEN_LIMIT][TOKEN_SIZE];
    int count;
    int pos;
    TACProgram *program;
    char *error;
    size_t error_size;
    int temp_number;
    int label_number;
    int returned;
} Parser;

static void fail(Parser *p, const char *fmt, ...) {
    if (!p->error || p->error_size == 0 || p->error[0]) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->error, p->error_size, fmt, ap);
    va_end(ap);
}

static int add_token(Parser *p, const char *start, size_t length) {
    if (p->count >= TOKEN_LIMIT || length >= TOKEN_SIZE) {
        fail(p, "C source contains too many or overlong tokens");
        return 0;
    }
    memcpy(p->items[p->count], start, length);
    p->items[p->count][length] = '\0';
    p->count++;
    return 1;
}

static int tokenize(Parser *p, const char *src) {
    const char *s = src;
    while (*s && !p->error[0]) {
        if (isspace((unsigned char)*s)) { s++; continue; }
        if (*s=='#') { while(*s && *s!='\n')s++; continue; }
        if (s[0] == '/' && s[1] == '/') {
            s += 2; while (*s && *s != '\n') s++; continue;
        }
        if (s[0] == '/' && s[1] == '*') {
            s += 2;
            while (*s && !(s[0] == '*' && s[1] == '/')) s++;
            if (!*s) { fail(p, "unterminated C comment"); return 0; }
            s += 2; continue;
        }
        if (isalpha((unsigned char)*s) || *s == '_') {
            const char *b = s++;
            while (isalnum((unsigned char)*s) || *s == '_') s++;
            if (!add_token(p, b, (size_t)(s - b))) return 0;
            continue;
        }
        if (isdigit((unsigned char)*s)) {
            const char *b = s++;
            while (isalnum((unsigned char)*s) || *s == '_') s++;
            if (!add_token(p, b, (size_t)(s - b))) return 0;
            continue;
        }
        if ((s[0]=='='&&s[1]=='=') || (s[0]=='!'&&s[1]=='=') || (s[0]=='<'&&s[1]=='=') ||
            (s[0]=='>'&&s[1]=='=') || (s[0]=='+'&&s[1]=='+') || (s[0]=='-'&&s[1]=='-')) {
            if (!add_token(p,s,2)) return 0;
            s+=2; continue;
        }
        if (strchr("+-*/=;(),{}<>!", *s)) {
            if (!add_token(p, s, 1)) return 0;
            s++; continue;
        }
        fail(p, "unsupported character '%c' in C source", *s);
        return 0;
    }
    return !p->error[0];
}

static const char *peek(Parser *p) {
    return p->pos < p->count ? p->items[p->pos] : "<end of file>";
}

static int take(Parser *p, const char *token) {
    if (strcmp(peek(p), token) != 0) return 0;
    p->pos++;
    return 1;
}

static int identifier(const char *s) {
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (int i = 1; s[i]; ++i)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
    return 1;
}

static int integer_literal(const char *s) {
    if (!s[0]) return 0;
    for (int i = 0; s[i]; ++i) if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static int new_temp(Parser *p, char out[MAX_OPERAND]) {
    int collision;
    do {
        snprintf(out, MAX_OPERAND, "t%d", ++p->temp_number);
        collision = 0;
        for (int i = 0; i < p->count; ++i) {
            if (strcmp(p->items[i], out) == 0) { collision = 1; break; }
        }
    } while (collision);
    return 1;
}

static int emit(Parser *p, const char *dst, const char *left, const char *op, const char *right) {
    if (!add_instruction(p->program, dst, left, op, right)) {
        fail(p, "C function lowers to more than %d TAC instructions", MAX_INSTRUCTIONS);
        return 0;
    }
    return 1;
}

static int expression(Parser *p, char out[MAX_OPERAND]);

static int primary(Parser *p, char out[MAX_OPERAND]) {
    if (take(p, "(")) {
        if (!expression(p, out)) return 0;
        if (!take(p, ")")) { fail(p, "expected ')' near '%s'", peek(p)); return 0; }
        return 1;
    }
    if (take(p, "-")) {
        char value[MAX_OPERAND], temp[MAX_OPERAND];
        if (!primary(p, value)) return 0;
        new_temp(p, temp);
        if (!emit(p, temp, "0", "-", value)) return 0;
        snprintf(out, MAX_OPERAND, "%s", temp);
        return 1;
    }
    if (integer_literal(peek(p)) || identifier(peek(p))) {
        snprintf(out, MAX_OPERAND, "%s", p->items[p->pos++]);
        return 1;
    }
    fail(p, "expected a number, variable, or parenthesized expression near '%s'", peek(p));
    return 0;
}

static int term(Parser *p, char out[MAX_OPERAND]) {
    if (!primary(p, out)) return 0;
    while (strcmp(peek(p), "*") == 0 || strcmp(peek(p), "/") == 0) {
        char op[TOKEN_SIZE], rhs[MAX_OPERAND], temp[MAX_OPERAND];
        snprintf(op, sizeof(op), "%s", p->items[p->pos++]);
        if (!primary(p, rhs)) return 0;
        new_temp(p, temp);
        if (!emit(p, temp, out, op, rhs)) return 0;
        snprintf(out, MAX_OPERAND, "%s", temp);
    }
    return 1;
}

static int expression(Parser *p, char out[MAX_OPERAND]) {
    if (!term(p, out)) return 0;
    while (strcmp(peek(p), "+") == 0 || strcmp(peek(p), "-") == 0) {
        char op[TOKEN_SIZE], rhs[MAX_OPERAND], temp[MAX_OPERAND];
        snprintf(op, sizeof(op), "%s", p->items[p->pos++]);
        if (!term(p, rhs)) return 0;
        new_temp(p, temp);
        if (!emit(p, temp, out, op, rhs)) return 0;
        snprintf(out, MAX_OPERAND, "%s", temp);
    }
    return 1;
}

static int statement(Parser *p);

static void new_label(Parser *p,char out[MAX_OPERAND]) {
    snprintf(out,MAX_OPERAND,"L%d",++p->label_number);
}

static int emit_label(Parser *p,const char *name) { return emit(p,name,"","LABEL",""); }
static int emit_goto(Parser *p,const char *name) { return emit(p,"",name,"GOTO",""); }
static int emit_branch(Parser *p,const char *condition,const char *label) { return emit(p,label,condition,"IFNZ",""); }

static int condition(Parser *p,char out[MAX_OPERAND]) {
    char left[MAX_OPERAND],right[MAX_OPERAND],temp[MAX_OPERAND],op[8];
    if(!expression(p,left)) return 0;
    const char *token=peek(p),*mapped=NULL;
    if(!strcmp(token,"<"))mapped="LT";else if(!strcmp(token,"<="))mapped="LE";
    else if(!strcmp(token,">"))mapped="GT";else if(!strcmp(token,">="))mapped="GE";
    else if(!strcmp(token,"=="))mapped="EQ";else if(!strcmp(token,"!="))mapped="NE";
    if(!mapped) { snprintf(out,MAX_OPERAND,"%s",left); return 1; }
    snprintf(op,sizeof(op),"%s",mapped); p->pos++;
    if(!expression(p,right)) return 0;
    new_temp(p,temp);
    if(!emit(p,temp,left,op,right)) return 0;
    snprintf(out,MAX_OPERAND,"%s",temp); return 1;
}

static int statement_body(Parser *p) {
    if(!take(p,"{")) return statement(p);
    while(strcmp(peek(p),"}")!=0 && strcmp(peek(p),"<end of file>")!=0 && !p->error[0])
        if(!statement(p)) return 0;
    if(!take(p,"}")) { fail(p,"expected '}' near '%s'",peek(p)); return 0; }
    return 1;
}

static int assignment_clause(Parser *p,int semicolon) {
    char name[MAX_OPERAND],value[MAX_OPERAND],one[]="1";
    if(!identifier(peek(p))) { fail(p,"expected an assignment target near '%s'",peek(p)); return 0; }
    snprintf(name,sizeof(name),"%s",p->items[p->pos++]);
    if(take(p,"++") || take(p,"--")) {
        const char *step=p->items[p->pos-1];
        if(!emit(p,name,name,!strcmp(step,"++")?"+":"-",one)) return 0;
    } else {
        if(!take(p,"=")) { fail(p,"expected '=' in assignment near '%s'",peek(p)); return 0; }
        if(!expression(p,value) || !emit(p,name,value,"=","")) return 0;
    }
    if(semicolon && !take(p,";")) { fail(p,"expected ';' near '%s'",peek(p)); return 0; }
    return 1;
}

static int type_word(const char *s) {
    return !strcmp(s, "int") || !strcmp(s, "long") || !strcmp(s, "short") ||
           !strcmp(s, "unsigned") || !strcmp(s, "signed") || !strcmp(s, "const");
}

static int declaration(Parser *p) {
    while (type_word(peek(p))) p->pos++;
    if (!identifier(peek(p))) { fail(p, "expected a variable name in declaration near '%s'", peek(p)); return 0; }
    for (;;) {
        char name[MAX_OPERAND], value[MAX_OPERAND];
        if (strlen(peek(p)) >= sizeof(name)) { fail(p, "variable name is too long"); return 0; }
        snprintf(name, sizeof(name), "%s", p->items[p->pos++]);
        if (take(p, "=")) {
            if (!expression(p, value) || !emit(p, name, value, "=", "")) return 0;
        }
        if (!take(p, ",")) break;
        if (!identifier(peek(p))) { fail(p, "expected a variable name after ','"); return 0; }
    }
    if (!take(p, ";")) { fail(p, "expected ';' after declaration near '%s'", peek(p)); return 0; }
    return 1;
}

static int statement(Parser *p) {
    if (type_word(peek(p))) return declaration(p);
    if (take(p,"if")) {
        char cond[MAX_OPERAND],then_label[MAX_OPERAND],else_label[MAX_OPERAND],done_label[MAX_OPERAND];
        new_label(p,then_label);new_label(p,else_label);new_label(p,done_label);
        if(!take(p,"(")) { fail(p,"expected '(' after if"); return 0; }
        if(!condition(p,cond) || !take(p,")")) { if(!p->error[0]) fail(p,"expected ')' after if condition"); return 0; }
        if(!emit_branch(p,cond,then_label)||!emit_goto(p,else_label)||!emit_label(p,then_label)||!statement_body(p))return 0;
        if(take(p,"else")) {
            if(!emit_goto(p,done_label)||!emit_label(p,else_label)||!statement_body(p)||!emit_label(p,done_label))return 0;
        } else if(!emit_label(p,else_label)) return 0;
        return 1;
    }
    if (take(p,"while")) {
        char cond[MAX_OPERAND],start_label[MAX_OPERAND],body_label[MAX_OPERAND],done_label[MAX_OPERAND];
        new_label(p,start_label);new_label(p,body_label);new_label(p,done_label);
        if(!take(p,"(")) { fail(p,"expected '(' after while"); return 0; }
        if(!emit_label(p,start_label)||!condition(p,cond)||!take(p,")")) { if(!p->error[0]) fail(p,"expected ')' after while condition"); return 0; }
        if(!emit_branch(p,cond,body_label)||!emit_goto(p,done_label)||!emit_label(p,body_label)||!statement_body(p)||!emit_goto(p,start_label)||!emit_label(p,done_label))return 0;
        return 1;
    }
    if (take(p,"for")) {
        char cond[MAX_OPERAND],start_label[MAX_OPERAND],body_label[MAX_OPERAND],step_label[MAX_OPERAND],done_label[MAX_OPERAND];
        TACProgram steps; init_program(&steps,0,"for-step");
        if(!take(p,"(")) { fail(p,"expected '(' after for"); return 0; }
        if(type_word(peek(p))) { if(!declaration(p))return 0; }
        else if(!take(p,";")) { if(!assignment_clause(p,1))return 0; }
        new_label(p,start_label);new_label(p,body_label);new_label(p,step_label);new_label(p,done_label);
        if(!emit_label(p,start_label))return 0;
        if(take(p,";")) snprintf(cond,sizeof(cond),"1");
        else if(!condition(p,cond)||!take(p,";")) { if(!p->error[0]) fail(p,"expected ';' after for condition"); return 0; }
        TACProgram *outer=p->program; p->program=&steps;
        int parsed_step=0;
        if(strcmp(peek(p),")")!=0) parsed_step=assignment_clause(p,0);
        p->program=outer;
        if(!parsed_step && p->error[0])return 0;
        if(!take(p,")")) { fail(p,"expected ')' after for clauses"); return 0; }
        if(!emit_branch(p,cond,body_label)||!emit_goto(p,done_label)||!emit_label(p,body_label)||!statement_body(p)||!emit_label(p,step_label))return 0;
        for(int i=0;i<steps.count;i++) if(!emit(p,steps.code[i].result,steps.code[i].arg1,steps.code[i].op,steps.code[i].arg2))return 0;
        if(!emit_goto(p,start_label)||!emit_label(p,done_label))return 0;
        return 1;
    }
    if (take(p, "return")) {
        char value[MAX_OPERAND];
        if (!expression(p, value)) return 0;
        if (!take(p, ";")) { fail(p, "expected ';' after return expression"); return 0; }
        snprintf(p->program->output, sizeof(p->program->output), "%s", value);
        p->program->has_return = 1;
        p->returned = 1;
        return emit(p,"",value,"RETURN","");
    }
    if (identifier(peek(p))) return assignment_clause(p,1);
    fail(p, "unsupported statement near '%s'", peek(p));
    return 0;
}

int lower_c_source(const char *source, TACProgram *program, char *error, size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!source || !program || !error || error_size == 0) return 0;
    Parser p = {0}; p.program = program; p.error = error; p.error_size = error_size;
    init_program(program, 1, "C Source");
    if (!tokenize(&p, source)) return 0;

    /* Capture up to seven scalar parameter names so the TAC interpreter can
       provide repeatable values to the differential verifier. */
    int in_parameters=0;
    while (p.pos < p.count && strcmp(peek(&p), "{") != 0) {
        const char *token=peek(&p);
        if(!strcmp(token,"(")){in_parameters=1;p.pos++;continue;}
        if(!strcmp(token,")")){in_parameters=0;p.pos++;continue;}
        if(in_parameters && identifier(token) && strcmp(token,"void")!=0 && !type_word(token)) {
            int duplicate=0;
            for(int i=0;i<program->input_count;i++)if(!strcmp(program->input_names[i],token))duplicate=1;
            if(!duplicate && program->input_count<MAX_INPUTS)
                snprintf(program->input_names[program->input_count++],MAX_OPERAND,"%s",token);
        }
        p.pos++;
    }
    if (!take(&p, "{")) { fail(&p, "expected a function body starting with '{'"); return 0; }
    while (p.pos < p.count && strcmp(peek(&p), "}") != 0 && !p.error[0]) {
        if (!statement(&p)) return 0;
    }
    if (!take(&p, "}")) { fail(&p, "expected '}' at end of function body"); return 0; }
    if (!p.returned) { fail(&p, "the function must contain a return statement"); return 0; }
    return !p.error[0];
}

int lower_c_file(const char *path, TACProgram *program, char *error, size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!path || !program || !error || error_size == 0) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(error, error_size, "cannot open C source '%s'", path); return 0; }
    char *source = malloc(SOURCE_LIMIT + 1);
    if (!source) { fclose(f); snprintf(error, error_size, "out of memory reading C source"); return 0; }
    size_t n = fread(source, 1, SOURCE_LIMIT + 1, f);
    int io_error = ferror(f);
    fclose(f);
    if (io_error || n > SOURCE_LIMIT) { free(source); snprintf(error, error_size, "C source is too large or could not be read"); return 0; }
    source[n] = '\0';
    int result = lower_c_source(source, program, error, error_size);
    free(source);
    return result;
}

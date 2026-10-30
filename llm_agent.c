#define _POSIX_C_SOURCE 200809L
#include "llm_agent.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RESPONSE_LIMIT (1024 * 1024)
#define PROMPT_LIMIT (96 * 1024)

int llm_agent_enabled(void) {
    const char *mode = getenv("OPTIMIZER_AGENT");
    return mode && strcmp(mode, "llm") == 0;
}

static int write_all(FILE *f, const char *s) {
    return fputs(s, f) >= 0;
}

static int json_string(FILE *f, const char *s) {
    if (fputc('"', f) == EOF) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        switch (*p) {
            case '"': if (!write_all(f, "\\\"")) return 0; break;
            case '\\': if (!write_all(f, "\\\\")) return 0; break;
            case '\n': if (!write_all(f, "\\n")) return 0; break;
            case '\r': if (!write_all(f, "\\r")) return 0; break;
            case '\t': if (!write_all(f, "\\t")) return 0; break;
            default:
                if (*p < 0x20) { if (fprintf(f, "\\u%04x", *p) < 0) return 0; }
                else if (fputc(*p, f) == EOF) return 0;
        }
    }
    return fputc('"', f) != EOF;
}

static int safe_key(const char *key) {
    if (!key || !*key) return 0;
    for (const unsigned char *p=(const unsigned char *)key; *p; ++p)
        if (!(isalnum(*p) || *p=='_' || *p=='-' || *p=='.')) return 0;
    return 1;
}

static int render_program(const TACProgram *p, char *out, size_t cap) {
    size_t used=0;
    for (int i=0;i<p->count;i++) {
        const TACInstruction *x=&p->code[i];
        int n;
        if (strcmp(x->op,"RETURN")==0) n=snprintf(out+used,cap-used,"RETURN %s\n",x->arg1);
        else if (strcmp(x->op,"LABEL")==0) n=snprintf(out+used,cap-used,"LABEL %s\n",x->result);
        else if (strcmp(x->op,"GOTO")==0) n=snprintf(out+used,cap-used,"GOTO %s\n",x->arg1);
        else if (strcmp(x->op,"IFNZ")==0) n=snprintf(out+used,cap-used,"IFNZ %s %s\n",x->arg1,x->result);
        else if (is_arithmetic(x) || is_comparison(x)) {
            const char *op=x->op;
            if(strcmp(op,"LT")==0)op="<";else if(strcmp(op,"LE")==0)op="<=";
            else if(strcmp(op,"GT")==0)op=">";else if(strcmp(op,"GE")==0)op=">=";
            else if(strcmp(op,"EQ")==0)op="==";else if(strcmp(op,"NE")==0)op="!=";
            n=snprintf(out+used,cap-used,"%s = %s %s %s\n",x->result,x->arg1,op,x->arg2);
        }
        else if (is_assignment(x)) n=snprintf(out+used,cap-used,"%s = %s\n",x->result,x->arg1);
        else return 0;
        if (n<0 || (size_t)n>=cap-used) return 0;
        used+=(size_t)n;
    }
    int explicit_return=0;for(int i=0;i<p->count;i++)if(!strcmp(p->code[i].op,"RETURN"))explicit_return=1;
    if ((p->has_return || p->output[0]) && !explicit_return) {
        int n=snprintf(out+used,cap-used,"RETURN %s\n",p->output);
        if (n<0 || (size_t)n>=cap-used) return 0;
        used+=(size_t)n;
    }
    return 1;
}

static char *extract_json_string(char *json) {
    char *tag=strstr(json,"\"type\":\"output_text\"");
    char *field=tag ? strstr(tag,"\"text\"") : NULL;
    if (!field) field=strstr(json,"\"output_text\"");
    if (!field) field=strstr(json,"\"text\"");
    if (!field) return NULL;
    char *p=strchr(field,':');
    if (!p) return NULL;
    while (*++p && isspace((unsigned char)*p)) {}
    if (*p!='"') return NULL;
    char *src=++p,*dst=p;
    while (*src && *src!='"') {
        if (*src=='\\') {
            ++src;
            if (!*src) return NULL;
            switch (*src) {
                case 'n': *dst++='\n'; break;
                case 'r': *dst++='\r'; break;
                case 't': *dst++='\t'; break;
                case 'b': *dst++='\b'; break;
                case 'f': *dst++='\f'; break;
                case '"': *dst++='"'; break;
                case '\\': *dst++='\\'; break;
                case '/': *dst++='/'; break;
                default: return NULL; /* Candidate TAC should be plain ASCII. */
            }
            ++src;
        } else *dst++=*src++;
    }
    if (*src!='"') return NULL;
    *dst='\0';
    return p;
}

static int parse_candidate(const char *text, const TACProgram *original, TACProgram *out) {
    init_program(out,original->id,original->category);
    out->input_count=original->input_count;
    memcpy(out->input_names,original->input_names,sizeof(out->input_names));
    char *copy=strdup(text);
    if (!copy) return 0;
    int saw_instruction=0,saw_return=0,ok=1;
    char *save=NULL;
    for (char *line=strtok_r(copy,"\n",&save); line; line=strtok_r(NULL,"\n",&save)) {
        while (isspace((unsigned char)*line)) ++line;
        size_t len=strlen(line); while (len && isspace((unsigned char)line[len-1])) line[--len]='\0';
        if (!len) continue;
        if (line[0]=='#') continue;
        if (strncmp(line,"```",3)==0) continue;
        if (strncmp(line,"return ",7)==0 || strncmp(line,"RETURN ",7)==0) {
            char value[MAX_OPERAND],extra[2];
            if (sscanf(line+7,"%31s %1s",value,extra)!=1 || !add_instruction(out,"",value,"RETURN","")) { ok=0; break; }
            snprintf(out->output,sizeof(out->output),"%s",value); out->has_return=1; saw_return=1;saw_instruction=1;continue;
        }
        if (strncmp(line,"LABEL ",6)==0) {
            char label[MAX_OPERAND],extra[2];
            if (sscanf(line+6,"%31s %1s",label,extra)!=1 || !add_instruction(out,label,"","LABEL","")) { ok=0; break; }
            saw_instruction=1; continue;
        }
        if (strncmp(line,"GOTO ",5)==0) {
            char label[MAX_OPERAND],extra[2];
            if (sscanf(line+5,"%31s %1s",label,extra)!=1 || !add_instruction(out,"",label,"GOTO","")) { ok=0; break; }
            saw_instruction=1; continue;
        }
        if (strncmp(line,"IFNZ ",5)==0) {
            char value[MAX_OPERAND],label[MAX_OPERAND],extra[2];
            if (sscanf(line+5,"%31s %31s %1s",value,label,extra)!=2 || !add_instruction(out,label,value,"IFNZ","")) { ok=0; break; }
            saw_instruction=1; continue;
        }
        char result[MAX_OPERAND],a[MAX_OPERAND],op[8],b[MAX_OPERAND]; int used=0;
        int n=sscanf(line,"%31s = %31s %7s %31s %n",result,a,op,b,&used);
        if (n==4 && line[used]=='\0') {
            const char *tac_op=op;
            if(!strcmp(op,"<"))tac_op="LT";else if(!strcmp(op,"<="))tac_op="LE";
            else if(!strcmp(op,">"))tac_op="GT";else if(!strcmp(op,">="))tac_op="GE";
            else if(!strcmp(op,"=="))tac_op="EQ";else if(!strcmp(op,"!="))tac_op="NE";
            if ((!strcmp(tac_op,"+")||!strcmp(tac_op,"-")||!strcmp(tac_op,"*")||!strcmp(tac_op,"/")||
                 !strcmp(tac_op,"LT")||!strcmp(tac_op,"LE")||!strcmp(tac_op,"GT")||!strcmp(tac_op,"GE")||!strcmp(tac_op,"EQ")||!strcmp(tac_op,"NE")) &&
                add_instruction(out,result,a,tac_op,b)) { }
            else { ok=0; break; }
        } else {
            used=0; n=sscanf(line,"%31s = %31s %n",result,a,&used);
            if (n!=2 || line[used]!='\0' || !add_instruction(out,result,a,"=","")) { ok=0; break; }
        }
        saw_instruction=1;
    }
    free(copy);
    if (!ok || !saw_instruction) return 0;
    if (!saw_return) snprintf(out->output,sizeof(out->output),"%s",original->output);
    out->has_return=original->has_return || saw_return;
    return 1;
}

static int read_file(const char *path, char *buffer, size_t cap) {
    FILE *f=fopen(path,"rb"); if (!f) return 0;
    size_t n=fread(buffer,1,cap-1,f); int ok=!ferror(f);
    if (n==cap-1 && !feof(f)) ok=0;
    buffer[n]='\0'; fclose(f); return ok;
}

static void cleanup(const char *request,const char *response,const char *config) {
    if (request) unlink(request); if (response) unlink(response); if (config) unlink(config);
}

int propose_with_llm(const TACProgram *program,const OptimizationOpportunity *opportunity,
                     TACProgram *candidate,char *reason,size_t reason_size,
                     int *response_received,
                     char *error,size_t error_size) {
    if (!program || !opportunity || !candidate) return 0;
    if(reason&&reason_size)reason[0]='\0';
    if(response_received)*response_received=0;
    const char *key=getenv("OPENAI_API_KEY");
    if (!safe_key(key)) { snprintf(error,error_size,"OPENAI_API_KEY is missing or malformed"); return 0; }
    const char *model=getenv("OPTIMIZER_MODEL"); if (!model || !*model) model="gpt-6-luna";
    const char *endpoint=getenv("OPENAI_BASE_URL"); if (!endpoint || !*endpoint) endpoint="https://api.openai.com/v1/responses";
    char *tac=calloc(1,PROMPT_LIMIT),*prompt=calloc(1,PROMPT_LIMIT),*response=calloc(1,RESPONSE_LIMIT);
    if (!tac || !prompt || !response) { snprintf(error,error_size,"out of memory preparing model request"); goto fail_mem; }
    if (!render_program(program,tac,PROMPT_LIMIT)) { snprintf(error,error_size,"TAC is too large or contains unsupported control flow"); goto fail; }
    int pn;
    if(opportunity->type==OPT_LLM_PROPOSAL) pn=snprintf(prompt,PROMPT_LIMIT,
        "You are the optimization proposal agent in a compiler. The analyzer has supplied the full TAC program; identify and apply one useful semantics-preserving optimization. An independent interpreter verifies your candidate.\n"
        "Keep control flow and labels valid. Preserve exact integer semantics, avoid division by zero, and return the complete candidate program. If nothing improves the cost, return the original unchanged.\n"
        "Output first one short line 'REASON: ...', then only TAC lines: 'result = operand', 'result = left op right', 'LABEL name', 'GOTO label', 'IFNZ value label', and 'RETURN operand'. No markdown.\n"
        "Current program:\n%s",tac);
    else pn=snprintf(prompt,PROMPT_LIMIT,
        "You are the optimization proposal agent in a compiler. A separate interpreter verifies your candidate; it does not trust your claim.\n"
        "Task: optimize the TAC for the analyzer opportunity '%s' at 1-based instruction %d.\n"
        "Rules: preserve exact integer semantics, avoid division by zero, keep all instructions in order, and return the entire candidate program.\n"
        "Output first one short line 'REASON: ...', then only TAC lines: 'result = operand', 'result = left op right', 'LABEL name', 'GOTO label', 'IFNZ value label', and 'RETURN operand'. No markdown.\n"
        "Current program:\n%s",optimization_name(opportunity->type),opportunity->instruction_index+1,tac);
    if (pn<0 || pn>=PROMPT_LIMIT) { snprintf(error,error_size,"TAC prompt exceeded size limit"); goto fail; }
    char req_path[]="/tmp/tac_optimizer_req_XXXXXX",res_path[]="/tmp/tac_optimizer_res_XXXXXX",cfg_path[]="/tmp/tac_optimizer_cfg_XXXXXX";
    int req_fd=mkstemp(req_path),res_fd=mkstemp(res_path),cfg_fd=mkstemp(cfg_path);
    if (req_fd<0 || res_fd<0 || cfg_fd<0) { snprintf(error,error_size,"cannot create temporary API files: %s",strerror(errno)); if(req_fd>=0)close(req_fd);if(res_fd>=0)close(res_fd);if(cfg_fd>=0)close(cfg_fd);cleanup(req_path,res_path,cfg_path); goto fail; }
    fchmod(req_fd,0600);fchmod(res_fd,0600);fchmod(cfg_fd,0600);
    FILE *req=fdopen(req_fd,"w"),*cfg=fdopen(cfg_fd,"w");
    int built=req&&cfg&&write_all(req,"{\"model\":")&&json_string(req,model)&&write_all(req,",\"store\":false,\"input\":")&&json_string(req,prompt)&&write_all(req,"}")&&fflush(req)==0&&fprintf(cfg,"silent\nshow-error\nmax-time = 45\nheader = \"Authorization: Bearer %s\"\nheader = \"Content-Type: application/json\"\n",key)>0&&fflush(cfg)==0;
    if (req) fclose(req); else close(req_fd);
    if (cfg) fclose(cfg); else close(cfg_fd);
    close(res_fd);
    if (!built) { snprintf(error,error_size,"cannot write API request");cleanup(req_path,res_path,cfg_path);goto fail; }
    int pipefd[2];
    if (pipe(pipefd)<0) { snprintf(error,error_size,"cannot start API request: %s",strerror(errno));cleanup(req_path,res_path,cfg_path);goto fail; }
    pid_t pid=fork();
    if (pid==0) {
        dup2(pipefd[1],STDOUT_FILENO); close(pipefd[0]); close(pipefd[1]);
        char file_arg[sizeof(req_path)+2]; snprintf(file_arg,sizeof(file_arg),"@%s",req_path);
        execlp("curl","curl","--config",cfg_path,"--output",res_path,"--write-out","%{http_code}","--request","POST","--data-binary",file_arg,endpoint,(char *)NULL);
        _exit(127);
    }
    close(pipefd[1]); char status[32]={0}; ssize_t got=read(pipefd[0],status,sizeof(status)-1); close(pipefd[0]);
    int child_status=0; if(pid<0 || waitpid(pid,&child_status,0)<0 || !WIFEXITED(child_status) || WEXITSTATUS(child_status)!=0) {
        snprintf(error,error_size,"curl could not complete the model request (is curl/network available?)");cleanup(req_path,res_path,cfg_path);goto fail;
    }
    cleanup(req_path,NULL,cfg_path);
    if (got<3 || strncmp(status,"200",3)!=0 || !read_file(res_path,response,RESPONSE_LIMIT)) {
        snprintf(error,error_size,"model API returned HTTP %.*s",got>0?(int)got:0,status);cleanup(NULL,res_path,NULL);goto fail;
    }
    cleanup(NULL,res_path,NULL);
    char *answer=extract_json_string(response);
    if (!answer) { snprintf(error,error_size,"model response did not contain output text");goto fail; }
    if(response_received)*response_received=1;
    char *candidate_text=answer;
    if(!strncmp(answer,"REASON:",7)) {
        char *line_end=strchr(answer,'\n');
        if(line_end) {
            *line_end='\0';
            if(reason&&reason_size)snprintf(reason,reason_size,"%.220s",answer+7);
            candidate_text=line_end+1;
        }
    }
    if (!parse_candidate(candidate_text,program,candidate)) { snprintf(error,error_size,"model response did not contain valid TAC");goto fail; }
    free(tac);free(prompt);free(response);return 1;
fail:
    free(tac);free(prompt);free(response);return 0;
fail_mem:
    free(tac);free(prompt);free(response);return 0;
}

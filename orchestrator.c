#include "orchestrator.h"
#include "analyzer.h"
#include "optimizer.h"
#include "llm_agent.h"
#include "verifier.h"
#include "cost.h"
#include "logger.h"
#include "csv.h"
#include <stdio.h>
#include <string.h>

static int g_stats[6][4]; /* proposals, verified, accepted, rejected */

static int priority(OptimizationType t) {
    switch (t) {
        case OPT_CONSTANT_FOLDING: return 1;
        case OPT_ALGEBRAIC: return 2;
        case OPT_CSE: return 3;
        case OPT_COPY_PROPAGATION: return 4;
        case OPT_DEAD_CODE: return 5;
        case OPT_LLM_PROPOSAL: return 0;
        default: return 99;
    }
}

static int choose(const OptimizationOpportunity *a, int n) {
    int best = -1;
    for (int i = 0; i < n; ++i) {
        if ((int)a[i].type < 0) continue;
        if (best < 0 || priority(a[i].type) < priority(a[best].type)) best = i;
    }
    return best;
}

void reset_global_optimization_stats(void) { for (int i=0;i<6;i++) for (int j=0;j<4;j++) g_stats[i][j]=0; }

void get_global_optimization_stats(int index, int *proposals, int *verified, int *accepted, int *rejected) {
    if (index < 0 || index >= 6) return;
    if (proposals) *proposals = g_stats[index][0];
    if (verified) *verified = g_stats[index][1];
    if (accepted) *accepted = g_stats[index][2];
    if (rejected) *rejected = g_stats[index][3];
}

static int stat_index(OptimizationType t) {
    return (int)t;
}

OptimizationSummary optimize_program(TACProgram *program, int verbose) {
    OptimizationSummary s = {0};
    if (!program) return s;
    TACProgram original;
    copy_program(program, &original);
    s.original_cost = calculate_cost(program).cost;

    /* A rejected proposal is skipped for this exact program state. If another
       proposal is accepted, analysis restarts against the changed program. */
    struct { OptimizationType type; int index; TACInstruction instruction; } skipped[1024];
    int skipped_count = 0;
    /* Every iteration is a fresh Analyze -> Propose -> Verify -> Cost -> Decide cycle. */
    for (int guard = 0; guard < 100; ++guard) {
        OptimizationOpportunity ops[1024];
        int n = 0;
        analyze_program(program, ops, &n);
        if (llm_agent_enabled() && n < 1024) {
            ops[n].type=OPT_LLM_PROPOSAL;
            ops[n].instruction_index=-1;
            snprintf(ops[n].description,sizeof(ops[n].description),"Ask the LLM proposal specialist to optimize the full TAC program.");
            n++;
        }
        if (n == 0) break;

        int chosen = -1;
        for (int pass=0; pass<n; ++pass) {
            int candidate_index = choose(ops, n);
            if (candidate_index < 0) break;
            OptimizationOpportunity *possible = &ops[candidate_index];
            const TACInstruction *ins = possible->instruction_index>=0 ? &program->code[possible->instruction_index] : NULL;
            int was_skipped = 0;
            for (int k=0;k<skipped_count;k++) {
                if (skipped[k].type==possible->type && skipped[k].index==possible->instruction_index &&
                    ((!ins && possible->type==OPT_LLM_PROPOSAL) || (ins &&
                    strcmp(skipped[k].instruction.result,ins->result)==0 &&
                    strcmp(skipped[k].instruction.arg1,ins->arg1)==0 &&
                    strcmp(skipped[k].instruction.arg2,ins->arg2)==0 &&
                    strcmp(skipped[k].instruction.op,ins->op)==0))) { was_skipped=1; break; }
            }
            if (!was_skipped) { chosen=candidate_index; break; }
            /* Temporarily remove this opportunity from this selection pass. */
            ops[candidate_index].type = (OptimizationType)-1;
        }
        if (chosen < 0) break;
        OptimizationOpportunity *o = &ops[chosen];
        s.iterations++;

        if (verbose) log_iteration_header(s.iterations, program);
        printf("\nANALYSIS\nDetected %d opportunities. Selected: %s",n,optimization_name(o->type));
        if(o->instruction_index>=0)printf(" at instruction %d.\n",o->instruction_index+1);
        else printf(" for the whole program.\n");
        printf("\nOPTIMIZATION PROPOSAL\n");
        if(o->instruction_index>=0) print_instruction(&program->code[o->instruction_index], o->instruction_index);
        else printf("LLM asked to propose one semantics-preserving whole-program change.\n");
        printf("Type: %s\n", optimization_name(o->type));

        TACProgram candidate;
        int proposal_ok;
        int llm_responded=0;
        char agent_error[256] = "";
        char agent_reason[256] = "";
        if (llm_agent_enabled()) {
            printf("Proposal source: OpenAI LLM agent\n");
            proposal_ok = propose_with_llm(program, o, &candidate, agent_reason, sizeof(agent_reason), &llm_responded, agent_error, sizeof(agent_error));
            if(llm_responded)s.llm_candidates++;
            if(agent_reason[0])printf("LLM reasoning: %s\n",agent_reason);
        } else if (o->type==OPT_LLM_PROPOSAL) {
            proposal_ok=0;
            snprintf(agent_error,sizeof(agent_error),"LLM proposal selected without LLM mode");
        } else {
            proposal_ok = propose_optimization(program, o, &candidate);
        }
        int before = calculate_cost(program).cost;
        int after = before;
        VerificationResult vr = {0};
        s.proposals++;
        g_stats[stat_index(o->type)][0]++;

        if (!proposal_ok) {
            s.rejected++;
            g_stats[stat_index(o->type)][3]++;
            printf("Proposal could not be generated%s%s. REJECTED.\n",
                   agent_error[0] ? ": " : "", agent_error);
            log_decision(program->id, s.iterations, o->type, o->instruction_index,
                         0, before, after, "REJECTED", agent_error[0] ? agent_error : "Proposal generation failed");
            csv_iteration(program->id, s.iterations, before, after, optimization_name(o->type), 0);
            /* No valid candidate exists; continue only if another opportunity can be selected. */
            if (skipped_count < 1024) { skipped[skipped_count].type=o->type; skipped[skipped_count].index=o->instruction_index; if(o->instruction_index>=0) skipped[skipped_count].instruction=program->code[o->instruction_index]; else memset(&skipped[skipped_count].instruction,0,sizeof(TACInstruction)); skipped_count++; }
            continue;
        }
        after = calculate_cost(&candidate).cost;
        vr = verify_programs(program, &candidate, 20, verbose);
        if (vr.passed) { s.verified++; g_stats[stat_index(o->type)][1]++; if(llm_agent_enabled())s.llm_verified++; }

        printf("\nVERIFICATION\nResult: %s (%d/20 tests passed)\n",
               vr.passed ? "VERIFIED" : "INVALID", vr.passed ? 20 : vr.first_failure - 1);
        printf("\nCOST EVALUATION\nBefore Cost: %d\nAfter Cost: %d\n", before, after);

        int accept = vr.passed && after < before;
        if (accept) {
            s.accepted++;
            if(llm_agent_enabled())s.llm_accepted++;
            g_stats[stat_index(o->type)][2]++;
            *program = candidate;
            skipped_count = 0;
            printf("Cost Reduction: %.2f%%\n", before ? 100.0 * (before - after) / before : 0.0);
            printf("\nFINAL DECISION\nACCEPTED\nReason: Optimization is correct and reduces cost.\n");
            char log_reason[320];
            if(agent_reason[0])snprintf(log_reason,sizeof(log_reason),"Correct and cheaper. LLM reasoning:%s",agent_reason);
            else snprintf(log_reason,sizeof(log_reason),"Correct and cheaper");
            log_decision(program->id, s.iterations, o->type, o->instruction_index,
                         1, before, after, "ACCEPTED", log_reason);
        } else {
            s.rejected++;
            g_stats[stat_index(o->type)][3]++;
            char reason[320];
            snprintf(reason,sizeof(reason),"%s%s%s",vr.passed ? "New cost is not lower" : "Verification failed",agent_reason[0]?". LLM reasoning: ":"",agent_reason);
            printf("\nFINAL DECISION\nREJECTED\nReason: %s.\n", reason);
            log_decision(program->id, s.iterations, o->type, o->instruction_index,
                         vr.passed, before, after, "REJECTED", reason);
            if (skipped_count < 1024) { skipped[skipped_count].type=o->type; skipped[skipped_count].index=o->instruction_index; if(o->instruction_index>=0) skipped[skipped_count].instruction=program->code[o->instruction_index]; else memset(&skipped[skipped_count].instruction,0,sizeof(TACInstruction)); skipped_count++; }
        }
        csv_iteration(program->id, s.iterations, before, after, optimization_name(o->type), accept);

        /* Rejections are remembered above so other opportunities can proceed. */
    }

    s.final_cost = calculate_cost(program).cost;
    long long zero_inputs[7] = {0,0,0,0,0,0,0};
    long long original_output = 0, final_output = 0;
    int a = execute_program(&original, zero_inputs, &original_output);
    int b = execute_program(program, zero_inputs, &final_output);
    s.output_match = a && b && (original_output == final_output);
    return s;
}

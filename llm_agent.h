#ifndef LLM_AGENT_H
#define LLM_AGENT_H

#include "analyzer.h"

/* Uses the OpenAI Responses API when OPENAI_API_KEY is configured. */
int llm_agent_enabled(void);
int propose_with_llm(const TACProgram *program,
                     const OptimizationOpportunity *opportunity,
                     TACProgram *candidate,
                     char *reason,
                     size_t reason_size,
                     int *response_received,
                     char *error,
                     size_t error_size);

#endif

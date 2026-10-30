#ifndef VERIFIER_H
#define VERIFIER_H
#include "tac.h"
typedef struct { int passed; int tests; long long original_output; long long candidate_output; int first_failure; } VerificationResult;
VerificationResult verify_programs(const TACProgram *original, const TACProgram *candidate, int number_of_tests, int verbose);
int execute_program(const TACProgram *program, const long long inputs[7], long long *output);
#endif

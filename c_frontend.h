#ifndef C_FRONTEND_H
#define C_FRONTEND_H

#include "tac.h"

/* Lower the supported integer C subset (including if/else, while and for) into TAC.
   Returns 1 on success and writes a diagnostic on failure. */
int lower_c_source(const char *source, TACProgram *program, char *error, size_t error_size);
int lower_c_file(const char *path, TACProgram *program, char *error, size_t error_size);

#endif

#ifndef DATASET_H
#define DATASET_H
#include "tac.h"
int generate_dataset(const char *path,int count);
int generate_program(int id,TACProgram *program);
int load_demo(TACProgram *p);
#endif

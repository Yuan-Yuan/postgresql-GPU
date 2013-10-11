#include <stdio.h>

#include "postgres.h"
#include "executor/executor.h"
#include "gpu/gpu.h" 

int gpuExec(QueryDesc * querydesc){
    printf("This part will be executed on OpenCL supported device\n");
    return 0;
}

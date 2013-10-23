#include <stdio.h>
#include <assert.h>

#include "postgres.h"
#include "executor/executor.h"
#include "storage/bufmgr.h"
#include "gpu/gpu.h"

/*
 * Execute SCAN opeartion on GPU.
 */
static void gpuExecuteScan(struct gpuScanNode* node, QueryDesc * querydesc){
    int relid = node->table.tid;
    long tableSize = 0;
    struct clContext * context = querydesc->context;
    cl_int error = 0;
    void * clTmp;
    int i = 0;

    Relation r = RelationIdGetRelation(relid);
    BlockNumber bln = RelationGetNumberOfBlocks(r);

    /*
     * Calculate table size.
     * Allocate the needed memory using CL_MEM_ALLOC_HOST_PTR flag.
     * Very inefficient here.
     * Copy the data to the opencl managed memory (on the host side).
     */

    node->table.blockNum = bln;
    tableSize = bln * BLCKSZ;
    node->table.memory = (char*)clCreateBuffer(context->context,CL_MEM_READ_ONLY|CL_MEM_ALLOC_HOST_PTR, tableSize, NULL, &error);

    if(error != CL_SUCCESS){
        printf("Failed tot allocate opencl memory with size :%ld\n",tableSize);
    }

    clTmp = clEnqueueMapBuffer(context->queue,(cl_mem)node->table.memory,CL_TRUE,CL_MAP_WRITE,0,tableSize,0,0,0,&error);
    if(error != CL_SUCCESS){
        printf("Failed to map opencl memory\n");
    }

    /*
     * The relation must have been opened with lock already.
     * Here we simply get Relation for the relation id.
     */
    for(i=0;i<bln;i++){
        Buffer buf = ReadBuffer(r,i);
        Page dp = BufferGetPage(buf);
        memcpy((char *)clTmp + i*BLCKSZ, dp, BLCKSZ);
        ReleaseBuffer(buf);
    }
    RelationClose(r);
    clEnqueueUnmapMemObject(context->queue,(cl_mem)node->table.memory,clTmp,0,0,0);

    /*
     * The data will be transferred from OpenCL managed memory to GPU.
     */

    if(tableSize <= context->max_alloc_size){
        node->table.gpuMemory = clCreateBuffer(context->context, CL_MEM_READ_ONLY, tableSize, NULL, &error);
        if(error != CL_SUCCESS){
            printf("Failed to allocate memory on GPU device\n");
        }

        clEnqueueCopyBuffer(context->queue,(cl_mem)node->table.memory,node->table.gpuMemory,0,0,tableSize,0,0,0);
    }

    /* FIXME*/
    /*
     * Execute the query on GPU.
     */ 

}

/*
 * gpuExecutePlan: execute the query plan on the GPU device.
 */
static void gpuExecutePlan(struct gpuPlan *plan, QueryDesc *querydesc){

    switch(plan->type){
        case GPU_SCAN:
            {
                struct gpuScanNode * scannode = (struct gpuScanNode *)plan;
                gpuExecuteScan(scannode, querydesc);
                break;
            }

        /* FIXME add support for other query operations */

        default:
            printf("Query operation not supported yet\n");
            break;
    }
}


/*
 * @gpuExec: Executing query on the opencl supported device
 */

int gpuExec(QueryDesc * querydesc){

    struct clContext * context = querydesc->context;
    struct gpuQueryDesc * gpuquerydesc = context->querydesc;
    ListCell *l;

    gpuExecutePlan(gpuquerydesc->plan, querydesc); 
    return 0;
}

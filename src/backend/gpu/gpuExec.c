#include <stdio.h>
#include <assert.h>

#include "postgres.h"
#include "executor/executor.h"
#include "storage/bufmgr.h"
#include "gpu/gpu.h"

/*
 * @gpuExec: Executing query on the opencl supported device
 */

int gpuExec(QueryDesc * querydesc){

    PlannedStmt * plannedstmt = querydesc->plannedstmt;
    List *rangeTable = plannedstmt->rtable;
    EState * estate = querydesc->estate;
    struct clContext * context = querydesc->context;
    ListCell *l;
    BlockNumber totalBlocks = 0;
    PlanState * outerPlan;
    cl_int error = 0;
    void * clTmp;
    int i = 0;
    long offset = 0;

    /*
     * The query must have a lock state 
     */
    assert(nodeTag(querydesc->planstate) == T_LockRowsState);

    outerPlan = outerPlanState(querydesc->planstate);

    /*
     * Calculate the total number of blocks for all relations.
     */ 

    foreach(l, estate->es_rowMarks){
        ExecRowMark * erm = (ExecRowMark *)lfirst(l);
        Relation r = erm->relation;

        totalBlocks += RelationGetNumberOfBlocks(r);
    }

    /*
     * Calculate the total size of the needed memory.
     * Allocate the needed memory using CL_MEM_ALLOC_HOST_PTR flag.
     * Very inefficient here.
     */

    context->size = totalBlocks * BLCKSZ;
    context->memory = (char *)clCreateBuffer(context->context, CL_MEM_READ_ONLY|CL_MEM_ALLOC_HOST_PTR, context->size,NULL,&error);
    if(error != CL_SUCCESS){
        printf("Failed to allocate opencl memory with size:%ld\n",context->size);
    }

    /*
     * Copy all the table data into OpenCl memory.
     */ 

    clTmp = clEnqueueMapBuffer(context->queue,(cl_mem)context->memory,CL_TRUE,CL_MAP_WRITE,0,context->size,0,0,0,&error);
    if(error != CL_SUCCESS){
        printf("Failed to map OpenCL memory\n");
    }

    foreach(l, rangeTable){
        RangeTblEntry * rt = (RangeTblEntry *)lfirst(l);
        Relation r = RelationIdGetRelation(rt->relid);
        BlockNumber bln = RelationGetNumberOfBlocks(r);

        /*
         * Only support simple scan operation. More complexed operation will be supported later.
         */ 
        assert(rt->rtekind == RTE_RELATION);

        /*
         * The relation must have been opened with lock already.
         * Here we simply get Relation for the relation id.
         */
        for(i=0;i<bln;i++, offset += BLCKSZ){
            Buffer buf = ReadBuffer(r,i);
            Page dp = BufferGetPage(buf);
            memcpy((char *)clTmp + offset, dp, BLCKSZ);
            ReleaseBuffer(buf);
        }
        RelationClose(r);
    }

    /*
     * The data will be transferred from OpenCL memory to GPU and query will be executed on GPU.
     */

    printf("This part will be executed on OpenCL supported device\n");
    return 0;
}

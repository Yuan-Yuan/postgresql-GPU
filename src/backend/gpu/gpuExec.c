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
    long totalSize = 0;
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

    printf("xxx %d\n", list_length(outerPlan->ps_ProjInfo->pi_targetlist));
    return 0;

    /*
     * Calculate the total number of blocks for all relations.
     */ 

    context->tableNum = list_length(rangeTable);
    context->table = palloc(sizeof(struct gpuTable)*context->tableNum);

    /*
     * Calculate the total size of each table.
     * Allocate the needed memory using CL_MEM_ALLOC_HOST_PTR flag.
     * Very inefficient here.
     * Copy the data to the opencl managed memory (on the host side).
     */

    foreach(l, rangeTable){
        RangeTblEntry * rt = (RangeTblEntry *)lfirst(l);
        Relation r = RelationIdGetRelation(rt->relid);
        BlockNumber bln = RelationGetNumberOfBlocks(r);

        /*
         * Only support simple scan operation. More complexed operation will be supported later.
         */ 

        assert(rt->rtekind == RTE_RELATION);

        context->table[offset].size = bln* BLCKSZ;
        context->table[offset].memory = (char*)clCreateBuffer(context->context,CL_MEM_READ_ONLY|CL_MEM_ALLOC_HOST_PTR, context->table[offset].size, NULL, &error);

        if(error != CL_SUCCESS){
            printf("Failed tot allocate opencl memory with size :%ld\n",context->table[offset].size);
        }

        clTmp = clEnqueueMapBuffer(context->queue,(cl_mem)context->table[offset].memory,CL_TRUE,CL_MAP_WRITE,0,context->table[offset].size,0,0,0,&error);
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
        clEnqueueUnmapMemObject(context->queue,(cl_mem)context->table[offset].memory,clTmp,0,0,0);

        totalSize += context->table[offset].size;
        offset += 1;
    }
    context->totalSize = totalSize;

    /*
     * The data will be transferred from OpenCL managed memory to GPU.
     */

    for(i=0;i<context->tableNum;i++){
        struct gpuTable  table = context->table[i];
        if(table.size > context->max_alloc_size){
            printf("Not supported yet: table size is larger than the max supported allcoate size\n");
            table.gpuMemory = clCreateBuffer(context->context, CL_MEM_READ_ONLY, table.size, NULL, &error);
            if(error != CL_SUCCESS){
                printf("Failed to allocate memory on GPU device\n");
            }

            clEnqueueCopyBuffer(context->queue,(cl_mem)table.memory,table.gpuMemory,0,0,table.size,0,0,0);
        }
    }

    /*
     * Execute the query on GPU.
     */ 

    printf("This part will be executed on OpenCL supported device\n");
    return 0;
}

#include <stdio.h>
#include <assert.h>

#include "postgres.h"
#include "executor/executor.h"
#include "access/htup_details.h"
#include "storage/bufmgr.h"
#include "utils/tqual.h"
#include "gpu/gpu.h"

/*
 * Execute SCAN opeartion on GPU.
 * Need to handle MVCC check and Serialization check when getting tuples from the storage.
 */

static void gpuExecuteScan(struct gpuScanNode* node, QueryDesc * querydesc){

    int i = 0, j = 0, k = 0;
    int relid = node->plan.table.tid;
    int nattr = node->plan.attrNum;
    int attrLen = 0, attrIndex = 0;
    long tableSize = 0;
    long tupleNum = 0, totalTupleNum = 0, offset = 0;

    HeapTupleData tupledata;
    ItemId lpp;
    Buffer buffer;
    Page page;
    PageHeader ph;

    struct clContext * context = querydesc->context;
    cl_int error = 0;
    void * clTmp;


    Relation r = RelationIdGetRelation(relid);
    BlockNumber bln = RelationGetNumberOfBlocks(r);

    /*
     * Calculate table size.
     * Allocate the needed memory using CL_MEM_ALLOC_HOST_PTR flag.
     * Very inefficient here.
     * Copy the data to the opencl managed memory (on the host side).
     */

    node->plan.table.blockNum = bln;
    tableSize = bln * BLCKSZ;
    node->plan.table.memory = clCreateBuffer(context->context,CL_MEM_READ_ONLY|CL_MEM_ALLOC_HOST_PTR, tableSize, NULL, &error);

    if(error != CL_SUCCESS){
        printf("Failed tot allocate opencl memory with size :%ld\n",tableSize);
    }

    clTmp = clEnqueueMapBuffer(context->queue,(cl_mem)node->plan.table.memory,CL_TRUE,CL_MAP_WRITE,0,tableSize,0,0,0,&error);
    if(error != CL_SUCCESS){
        printf("Failed to map opencl memory\n");
    }

    for(i = 0; i<node->plan.attrNum;i++ ){
        //node->plan.table.gpuMemory[i] = clCreateBuffer(context->context, CL_MEM_READ_WRITE, node->plan.table.attrSize[i] * totalTupleNum, NULL, &error);

        if(error != CL_SUCCESS){
            printf("Failed to allocate memory on GPU device\n");
        }
    }

    /*
     * The relation must have been opened with lock already.
     * Here we simply get Relation for the relation id.
     * The logic here is similar to heap_fetch.
     * We first get a relation page, and for each tuple do MVCC check and serialization check.
     * If the tuple pass both checks, we will put the buffer into OpenCL managed space.
     *
     * Potential optimizations:
     *  1) do predicate evaluation before MVCC check and serialization check.
     *  2) put MVCC check and serialization check on GPU to accelerate the speed.
     *  3) optimize MVCC check and serialization check conditions.
     */

    for(i=0, offset= 0, totalTupleNum = 0; i<bln; i++){
        buffer = ReadBuffer(r,i);
        LockBuffer(buffer, BUFFER_LOCK_SHARE);
        page = BufferGetPage(buffer);

        ph = (PageHeader) (page);
        tupleNum = (ph->pd_lower - (long)(&(((PageHeader)0)->pd_linp)))/sizeof(ItemIdData);

        for(k=0;k<tupleNum;k++){
                lpp = &(ph->pd_linp[k]);

                if(!ItemIdIsNormal(lpp))
                    continue;

                tupledata.t_data = (HeapTupleHeader)((char *)ph + lpp->lp_off);

                if(HeapTupleSatisfiesMVCC(tupledata.t_data, querydesc->snapshot, buffer)){

                    /*
                     * If the tuple passes both check, copy the actual data to OpenCL memory (from row to column).
                     * When copying data, we need first skip the HeapTupleHeader offset.
                     * For each variable length attribute, we store it in a fixed length memory when processing on gpu. 
                     * VARSIZE_ANY(any) to get the actual length of variable length attribute.
                     */

                    offset = 0, attrIndex = 0;

                    for(j = 0; j < nattr; j ++){
                        if(j == node->plan.table.attrIndex[attrIndex]){
                            attrIndex ++ ;
                        }
                    }

                    totalTupleNum ++;
                }
        }

        LockBuffer(buffer,BUFFER_LOCK_UNLOCK);
        ReleaseBuffer(buffer);
    }

    RelationClose(r);

    clEnqueueUnmapMemObject(context->queue,(cl_mem)node->plan.table.memory,clTmp,0,0,0);

    /*
     * Execute the Scan query on GPU.
     * Initialize the data structure and call the gpudb primitives.
     */


    /*
     * Release the OpenCL unnessceary memory.
     */ 

}

static void gpuExecuteJoin(struct gpuJoinNode * joinnode, QueryDesc * querydesc){

}

static void gpuExecuteAgg(struct gpuAggNode * aggnode, QueryDesc * querydesc){

}

static void gpuExecuteSort(struct gpuSortNode * sortnode, QueryDesc * querydesc){

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

        case GPU_JOIN:
            {
                struct gpuJoinNode * joinnode = (struct gpuJoinNode*) plan;
                gpuExecuteJoin(joinnode, querydesc);
                break;
            }
        case GPU_AGG:
            {
                struct gpuAggNode * aggnode = (struct gpuAggNode *)plan;
                gpuExecuteAgg(aggnode, querydesc);
                break;
            }
        case GPU_SORT:
            {
                struct gpuSortNode * sortnode = (struct gpuSortNode *)plan;
                gpuExecuteSort(sortnode, querydesc);
                break;
            }

        default:
            printf("Query operation not supported yet\n");
            break;
    }
}

/*
 * Fill up the execution queue.
 * The query plan tree is traversed in post order sequence.
 * @index points the next queue node that needs to be filled.
 */

static void gpuExecQueue(struct gpuPlan * plan, struct gpuPlan ** queue, int *index){

    switch(plan->type){
        case GPU_SCAN:
            {
                queue[*index] = plan;
                *index = *index + 1;
                break;
            }
        case GPU_JOIN:
            {
                gpuExecQueue(plan->leftPlan, queue, index);
                gpuExecQueue(plan->rightPlan, queue, index);
                queue[*index] = plan;
                *index = *index + 1;
                break;
            }
        case GPU_AGG:
            {
                gpuExecQueue(plan->leftPlan, queue, index);
                *index = *index + 1;
                break;
            }
        case GPU_SORT:
            {
                gpuExecQueue(plan->leftPlan, queue, index);
                *index = *index + 1;
                break;
            }

        default:
            printf("gpuExecQueue: Query operation not supported yet!\n");
            break;
    }
}


/*
 * @gpuExec: Executing query on the opencl supported device
 */

void gpuExec(QueryDesc * querydesc){

    struct clContext * context = querydesc->context;
    struct gpuQueryDesc * gpuquerydesc = context->querydesc;
    struct gpuPlan ** execQueue = NULL;
    int queueIndex = 0, i;

    execQueue = (struct gpuPlan **)palloc(sizeof(struct gpuPlan*)*gpuquerydesc->nodeNum);

    gpuExecQueue(gpuquerydesc->plan, execQueue, &queueIndex);

    for(i = 0;i<queueIndex;i++){
        gpuExecutePlan(execQueue[i], querydesc);
    }

}

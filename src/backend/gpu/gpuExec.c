#include <stdio.h>
#include <assert.h>

#include "postgres.h"
#include "executor/executor.h"
#include "access/htup_details.h"
#include "storage/bufmgr.h"
#include "utils/tqual.h"
#include "gpu/gpu.h"

#include "gpu/common.h"
#include "gpu/hashJoin.h"
#include "gpu/cpuOpenclLib.h"

/*
 * countWhereAttr: given a whereExpr in plan node, count how many attributes are used
 */

static void countWhereAttr(struct gpuExpr * expr, int *attr, int n){

}

/*
 * Setup the whereExp in scanNode based on gpuExpr
 */

static void setupGpuWhere(int * index, struct gpuScanNode *node, struct scanNode *sn, struct gpuExpr * expr ){

}

/*
 * Execute SCAN opeartion on GPU.
 * Need to handle MVCC check and Serialization check when getting tuples from the storage.
 */

static void gpuExecuteScan(struct gpuScanNode* node, QueryDesc * querydesc){

    int i = 0, j = 0, k = 0;
    int relid = node->plan.table.tid;
    int nattr = node->plan.attrNum;
    int attrLen = 0, attrIndex = 0;
    long tupleNum = 0, totalTupleNum = 0;
    int index, offset = 0, tupleSize = 0;
    struct gpuTable * table = &(node->plan.table);

    HeapTupleData tupledata;
    ItemId lpp;
    Buffer buffer;
    Page page;
    PageHeader ph;

    struct clContext * context = querydesc->context;
    cl_int error = 0;
    void * clTmp;


    tupleSize = table->tupleSize;
    Relation r = RelationIdGetRelation(relid);
    BlockNumber bln = RelationGetNumberOfBlocks(r);

    /*
     * Calculate table size.
     * Allocate the needed memory using CL_MEM_ALLOC_HOST_PTR flag.
     */

    table->blockNum = bln;
    tupleNum = (bln * BLCKSZ + tupleSize)/ tupleSize;

    /*
     * FIXME: how to determine the size of the allocated space.
     * 1. bln * BLCKSZ / tupleSize to estimate the number of tuples.
     */ 

    for(i=0;i<table->usedAttr;i++){
        index = table->attrIndex[i];
        table->cpuCol[i] = (char*)palloc(tupleNum * table->attrSize[index]);
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
                     * FIXME: how to handle header of the variable length data.
                     * FIXME: how to accelerate seriliaztion check.
                     */

                    offset = tupledata.t_data->t_hoff, attrIndex = 0;

                    for(j = 0; j < nattr; j ++){
                        if(j == table->attrIndex[attrIndex]){

                            if(table->variLen[j] == 1){
                                memcpy(table->cpuCol[attrIndex] + totalTupleNum *table->attrSize[j], (char*)tupledata.t_data+offset, VARSIZE_ANY((char*)tupledata.t_data+offset));
                                offset += VARSIZE_ANY(((char*)tupledata.t_data + offset));
                            }else{
                                memcpy(table->cpuCol[attrIndex] + totalTupleNum * table->attrSize[j], (char*)tupledata.t_data + offset, table->attrSize[j]);
                                offset += table->attrSize[j];
                            }

                            attrIndex ++ ;
                        }

                        if(node->plan.table.variLen[j] == 1){
                            offset += VARSIZE_ANY(((char*)tupledata.t_data + offset));
                        }else{
                            offset += node->plan.table.attrSize[j];
                        }

                    }

                    totalTupleNum ++;
                }
        }

        LockBuffer(buffer,BUFFER_LOCK_UNLOCK);
        ReleaseBuffer(buffer);
    }

    RelationClose(r);

    /*
     * Execute the Scan query on GPU.
     * Initialize the data structure and call the gpudb primitives.
     */

    struct tableNode * tnRes = (struct tableNode *)palloc(sizeof(struct tableNode));
    struct tableNode * tn = (struct tableNode *)palloc(sizeof(struct tableNode));
    struct statistic pp;
    pp.total = pp.kernel = pp.pcie = 0;

    initTable(tn);
    tn->totalAttr = table->usedAttr;
    tn->attrSize = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->attrType = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->attrIndex = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->attrTotalSize = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->dataPos = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->dataFormat = (int *)palloc(sizeof(int) * tn->totalAttr);
    tn->content = (char **)palloc(sizeof(int) * tn->totalAttr);

    for(i = 0; i<tn->totalAttr;i++){
        index = table->attrIndex[i];
        tn->attrSize[i] = table->attrSize[index];
        tn->attrType[i] = table->attrType[index];
        tn->attrIndex[i] = index;
        tn->attrTotalSize[i] = totalTupleNum * tn->attrSize[index];

        tn->dataPos[i] = MEM;
        tn->dataFormat[i] = UNCOMPRESSED;
        tn->content[i] = (char *) table->cpuCol[i];
    }

    tn->tupleSize = table->tupleSize;
    tn->tupleNum = totalTupleNum;
    
    struct scanNode sn;
    sn.tn = tn;
    sn.whereAttrNum = node->plan.whereNum;

    /*
     * FIXME: currently we don't support complex where conditions
     */

    assert(node->plan.whereNum == 1);

    int * attr = (int*)palloc(sizeof(int)* table->attrNum);
    memset(attr,0,sizeof(int) * table->attrNum);

    countWhereAttr(node->plan.whereexpr[0], attr, table->attrNum);

    index = 0;
    for(i=0;i<table->attrNum;i++){
        if(attr[i] != 0)
            index ++;
    }
    
    sn.whereIndex = (int*)palloc(sizeof(int) * index);
    sn.outputIndex = (int*)palloc(sizeof(int) * node->plan.attrNum);

    index = 0;
    setupGpuWhere(&index, node, &sn, node->plan.whereexpr[0]);

    tnRes = tableScan(&sn,&context,&pp);

    for(i = 0; i<node->plan.attrNum;i++){
        table->gpuCol[i] = tnRes->content[i];
    }


    /*
     * Release the unused OpenCL memory
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

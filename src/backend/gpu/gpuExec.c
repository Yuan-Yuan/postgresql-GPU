#include <stdio.h>
#include <assert.h>

#include "postgres.h"
#include "executor/executor.h"
#include "storage/bufmgr.h"
#include "gpu/gpu.h"

/*
 * @opencl_allocate_memory:
 *  Allocate pinnned memory for future query processing on OpenCL devices.
 *
 *  @size: the size of the memory that will be allocated.
 */

static void opencl_allocate_memory(long size){

}

/*
 * @opencl_copy_data
 * Copy all table data from postgresql storage space into opencl allocated memory.
 */

static void opencl_copy_data(void){

}

/*
 * @gpuExec: Executing query on the opencl supported device
 */

int gpuExec(QueryDesc * querydesc){

    PlannedStmt * plannedstmt = querydesc->plannedstmt;
    List *rangeTable = plannedstmt->rtable;
    EState * estate = querydesc->estate;
    ListCell *l;

    /*
     * The query must have a lock state 
     */
    assert(nodeTag(querydesc->planstate) == T_LockRowsState);

    PlanState * outerPlan = outerPlanState(querydesc->planstate);

    /*
     * Currently we only support sequential scan with where predicates.
     * All other operations will be supported later.
     */ 

    foreach(l, rangeTable){
        RangeTblEntry * rt = (RangeTblEntry *)lfirst(l);

        /*
         * Only support simple scan operation. More complexed operation will be supported later.
         */ 

        assert(rt->rtekind == RTE_RELATION);
    }

    /*
     * Calculate the total number of blocks for all relations.
     */ 

    BlockNumber totalBlocks = 0;

    foreach(l, estate->es_rowMarks){
        ExecRowMark * erm = (ExecRowMark *)lfirst(l);
        Relation r = erm->relation; 

        totalBlocks += RelationGetNumberOfBlocks(r);
    }

    printf("This part will be executed on OpenCL supported device\n");
    return 0;
}


__kernel void scan(__global int *col, int tupleNum, __global int * res){
    size_t tid = get_global_id(0); 
    size_t stride = get_global_size(0);

    for(size_t i = tid; i<tupleNum; i += stride)
        res[i] = col[i];
}


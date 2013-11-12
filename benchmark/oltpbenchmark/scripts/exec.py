#! /usr/bin/python

import sys
import os
import time

dbpath="/home/yuanyuan/psql/bin/"

cmd = dbpath + "dropdb tpcc"
os.system(cmd)
time.sleep(2)
cmd = dbpath + "createdb tpcc"
os.system(cmd)
time.sleep(2)

cmd="operf -g -c --pid 10078 &"
#os.system(cmd)

#cmd="./oltpbenchmark -b tpcc,chbenchmark -c myconfig/datagen.xml --create=true --load=true --execute=true --histograms -s 1 -o result/datagen/gpuPost_7threads &> result/datagen/gpuPost_7threads"
cmd="./oltpbenchmark -b tpcc,chbenchmark -c myconfig/datagen.xml --create=true --load=true --execute=true --histograms -s 1 -o result/datagen/gpuPostgres &> result/datagen/gpuPostgres"
os.system(cmd)

#cmd="./oltpbenchmark -b tpcc,chbenchmark -c myconfig/tpcc_only.xml --execute=true --histograms -s 1 -o result/tpcc/gpuPost_7threads_memcpy &> result/tpcc/gpuPost_7threads_memcpy"
cmd="./oltpbenchmark -b tpcc,chbenchmark -c myconfig/tpcc_only.xml --execute=true --histograms -s 1 -o result/tpcc/gpuPostgres &> result/tpcc/gpuPostgres_sum"
#cmd="./oltpbenchmark -b tpcc,chbenchmark -c myconfig/mix1.xml --execute=true --histograms -s 1 -o result/mix/1 &> result/mix/1_sum"
#cmd="./oltpbenchmark -b tpcc,chbenchmark -c myconfig/mix_scan.xml --execute=true --histograms -s 1 -o result/mix/mix_nation &> result/mix/mix_nation_sum"
os.system(cmd)

cmd="./oltpbenchmark -b tpcc,chbenchmark -c myconfig/tpcc_only.xml --create=true --load=true --execute=true --histograms -s 1 -o result/test/tpcc &> result/test/tpcc"
#os.system(cmd)
cmd="killall -9 operf"
#os.system(cmd)

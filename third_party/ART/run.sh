#!/usr/bin/env bash

output_file=output.dat
rm -f $output_file
touch $output_file
g++ -O3 -o orginalART orginalART.cpp -march=native
g++ -O3 -o ART ART.cpp -lnuma -march=native

for i in $(seq 0.0 0.2 3.0)
do
  ./orginalART 1000000 0 z $i 2>&1 | tee -a $output_file # sorted
#  ./orginalART 1000000 2 z $i 2>&1 | tee -a $output_file # sparse
#  ./ART 10000000 0 z $i 2>&1 | tee -a $output_file # sorted
#  ./ART 10000000 0 z $i 2>&1 | tee -a $output_file # sparse
done
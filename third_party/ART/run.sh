#!/usr/bin/bash

output_file=output.dat
rm -f $output_file
touch $output_file
g++ -O3 -o orginalART orginalART.cpp
g++ -O3 -o ART ART.cpp -lnuma

for i in $(seq 0.0 0.2 3.0)
do
  ./orginalART 10000000 0 z $i >> $output_file # sorted
#  ./orginalART 10000000 2 z $i >> $output_file # sparse
#  ./ART 10000000 0 z $i >> $output_file # sorted
#  ./ART 10000000 0 z $i >> $output_file # sparse
done
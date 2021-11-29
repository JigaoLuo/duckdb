#!/usr/bin/bash

output_file=output.dat
rm -f $output_file
touch $output_file

for i in $(seq 0.0 0.2 3.0)
do
  ./orginalART 10000000 0 z $i >> $output_file
done
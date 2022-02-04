#!/usr/bin/env bash

output_file=output.dat
rm -f $output_file
touch $output_file

for i in $(seq 0.0 0.2 3.0)
do
  ../build/release/art_standalone/art_standalone 1000000 0 z $i >> $output_file # sorted
#  ../build/release/art_standalone/art_standalone 1000000 2 z $i >> $output_file # sparse
done
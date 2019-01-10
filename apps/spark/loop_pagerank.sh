#!/bin/bash

for ((i=0;i<=10;i++))
do
	./test.sh pagerank 80 $1
done

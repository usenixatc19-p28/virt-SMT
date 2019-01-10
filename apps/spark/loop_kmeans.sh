#!/bin/bash

for ((i=0;i<=20;i++))
do
	./test.sh kmeans 80 $1 
done

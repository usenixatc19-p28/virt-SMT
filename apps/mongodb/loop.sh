#!/bin/bash

for ((i=0;i<=20;i++))
do
	#./flush
	./run.sh 32 &>> $1 
done

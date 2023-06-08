#!/bin/sh

for i in `ls | grep -vE '\.(asm|s|c|sh)$'`;
do
	echo $i;
	./$i || exit 1;
	echo;
done

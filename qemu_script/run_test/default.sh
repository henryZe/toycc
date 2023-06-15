#!/bin/sh

for i in `ls | grep -vE '\.(asm|s|c|sh|o)$'`;
do
	echo $i;
	./$i || exit 1;
	echo;
done

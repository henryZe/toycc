#!/bin/sh

for i in `ls | grep -vE '\.(asm|s|c|sh|o|h)$'`;
do
	echo $i;
	./$i || exit 1;
	echo;
done

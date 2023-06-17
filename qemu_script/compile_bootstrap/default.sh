#!/bin/sh

cc=./toycc

cd /root
sh /tmp/driver.sh /tmp/toycc || exit 1;
cd -

# generate test/*.s
for i in `ls test/*.c`;
do
	echo $i;
	asm=`echo $i | sed 's/\.c/\.s/g'`
	$cc -c -S $i -o $asm || exit 1;
	[ -f $asm ]
	check -o
	echo;
done

echo OK

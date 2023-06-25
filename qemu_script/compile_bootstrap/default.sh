#!/bin/sh

cc=./toycc

cd /root
# sh /tmp/driver.sh /tmp/toycc || exit 1;
cd -

# generate test/*.s
for i in `ls test/*.c`;
do
	asm=`echo $i | sed 's/\.c/\.s/g'`
	$cc -S $i -o $asm || exit 1;
	[ -f $asm ] || exit 1;
	echo $i '=>' $asm;
done

echo OK

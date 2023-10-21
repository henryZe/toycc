#!/bin/sh

dir=./output/selfhost
cc=$dir/toycc

# generate test/*.s
for i in `ls test/*.c`;
do
	if [ $i != "test/common.c" ]; then
		out=$dir/`echo $i | sed 's/\.c/\.s/g'`
		echo "$cc -Iinclude -Itest -S $i -o $out"
		$cc -Iinclude -Itest -S $i -o $out || exit 1;
		[ -f $out ] || exit 1;
	fi
done

echo OK

#!/bin/sh

dir=.
cc=$dir/toycc

fail() {
	echo "selfhost: test fail"
	exit 1
}

# generate test/*.s
for i in `ls test/*.c`;
do
	if [ $i != "test/lib.c" ]; then
		out=$dir/`echo $i | sed 's/\.c/\.s/g'`
		echo "$cc -Itest -S $i -o $out"
		$cc -Itest -S $i -o $out || fail
		[ -f $out ] || fail
	fi
done

echo "selfhost: test OK"

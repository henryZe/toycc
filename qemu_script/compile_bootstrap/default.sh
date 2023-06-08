#!/bin/sh

cc=./toycc

# driver.sh
check() {
	if [ $? -eq 0 ]; then
		echo "testing $1 ... passed"
	else
		echo "testing $1 ... failed"
		exit 1
	fi
}

# --help
$cc --help 2>&1 | grep -q toycc
check --help

# -o
for i in `ls test/*.c`;
do
	echo $i;
	asm=`echo $i | sed 's/\.c/\.s/g'`
	$cc $i -o $asm || exit 1;
	[ -f $asm ]
	check -o
	echo;
done

echo OK

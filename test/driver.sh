#!/bin/bash

tmp=`mktemp -d /tmp/toycc-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT
echo > $tmp/empty.c

check() {
	if [ $? -eq 0 ]; then
		echo "testing $1 ... passed"
	else
		echo "testing $1 ... failed"
		exit 1
	fi
}

# -o
rm -f $tmp/out
output/toycc -o $tmp/out $tmp/empty.c
[ -f $tmp/out ]
check -o

# --help
output/toycc --help 2>&1 | grep -q toycc
check --help

echo OK

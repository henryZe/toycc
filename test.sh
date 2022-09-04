#!/bin/bash

assert()
{
	expected="$1"
	input="$2"

	output/toycc "$input" > output/tmp.S || exit
	"$CROSS_COMPILE"gcc -march=rv64gv -static output/tmp.S -o output/tmp

	/opt/RV64/bin/spike /usr/riscv64-linux-gnu/bin/pk output/tmp
	actual="$?"

	if [ "$actual" = "$expected" ]; then
		echo "$input => $actual"
	else
		echo "$input => $expected expected, but got $actual"
		exit 1
	fi
}

assert 0 0
assert 42 42

echo ok

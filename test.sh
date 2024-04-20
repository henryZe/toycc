#!/bin/bash

uname -a | grep -q riscv64
if [ $? -ne 0 ]; then
	exec='qemu-riscv64'
	cross_toolchain='riscv64-linux-gnu-'
fi

assert()
{
	expected="$1"
	input="$2"

	output/toycc "$input" > output/tmp.S || exit 1
	"$cross_toolchain"gcc -march=rv64gv -static output/tmp.S -o output/tmp

	"$exec" output/tmp
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
assert 21 '5+20-4'
assert 41 ' 12 + 34 - 5 '
assert 47 '5+6*7'
assert 15 '5*(9-6)'
assert 4 '(3+5)/2'

echo ok

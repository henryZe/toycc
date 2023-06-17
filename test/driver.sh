#!/bin/bash
cc=$1

# Create a temporary directory
tmp=`mktemp -d ./toycc-test-XXXXXX`
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
$cc -o $tmp/out $tmp/empty.c
[ -f $tmp/out ]
check -o

# --help
$cc --help 2>&1 | grep -q toycc
check --help

# -S
echo 'int main() {}' | $cc -S -o - - | grep -q 'main:'
check -S

# Default output file
rm -f $tmp/out.o $tmp/out.s
echo 'int main() {}' > $tmp/out.c
(cd $tmp; $OLDPWD/$cc out.c)
[ -f $tmp/out.o ]
check 'default output file: -o'

(cd $tmp; $OLDPWD/$cc -S out.c)
[ -f $tmp/out.s ]
check 'default output file: -S'

# Multiple input files
rm -f $tmp/foo.o $tmp/bar.o
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$cc foo.c bar.c)
[ -f $tmp/foo.o ] && [ -f $tmp/bar.o ]
check 'multiple input files: -o'

rm -f $tmp/foo.s $tmp/bar.s
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$cc -S foo.c bar.c)
[ -f $tmp/foo.s ] && [ -f $tmp/bar.s ]
check 'multiple input files: -S'

echo OK

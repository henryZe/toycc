#!/bin/bash
cc=$1
exec='qemu-riscv64'

reset='\33[0m'
red='\33[31m'
green='\33[32m'

# Create a temporary directory
tmp=`mktemp -d ./toycc-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT
echo > $tmp/empty.c

check() {
	if [ $? -eq 0 ]; then
		echo "testing $1 ... passed"
	else
		echo "${red}testing $1 ... failed${reset}"
		exit 1
	fi
}

# -o
rm -f $tmp/out
$cc -c -o $tmp/out $tmp/empty.c
[ -f $tmp/out ]
check -o

# --help
$cc --help 2>&1 | grep -q toycc
check --help

# -S
echo 'int main() {}' | $cc -c -S -o - - | grep -q 'main:'
check -S

# Default output file
rm -f $tmp/out.o $tmp/out.s
echo 'int main() {}' > $tmp/out.c
(cd $tmp; $OLDPWD/$cc -c out.c)
[ -f $tmp/out.o ]
check 'default output file: -o'

(cd $tmp; $OLDPWD/$cc -c -S out.c)
[ -f $tmp/out.s ]
check 'default output file: -S'

# Multiple input files
rm -f $tmp/foo.o $tmp/bar.o
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$cc -c foo.c bar.c)
[ -f $tmp/foo.o ] && [ -f $tmp/bar.o ]
check 'multiple input files: -o'

rm -f $tmp/foo.s $tmp/bar.s
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$cc -c -S foo.c bar.c)
[ -f $tmp/foo.s ] && [ -f $tmp/bar.s ]
check 'multiple input files: -S'

# Run linker
rm -f $tmp/foo
echo 'int main() { return 0; }' | $cc -o $tmp/foo -
$exec $tmp/foo
check linker

rm -f $tmp/foo
echo 'int bar(); int main() { return bar(); }' > $tmp/foo.c
echo 'int bar() { return 42; }' > $tmp/bar.c
$cc -o $tmp/foo $tmp/foo.c $tmp/bar.c
$exec $tmp/foo
[ "$?" = 42 ]
check linker

# a.out
rm -f $tmp/a.out
echo 'int main() {}' > $tmp/foo.c
(cd $tmp; $OLDPWD/$cc foo.c)
[ -f $tmp/a.out ]
check a.out

# -E
echo foo > $tmp/out
echo "#include \"$tmp/out\"" | $cc -E - | grep -q foo
check -E

echo foo > $tmp/out1
echo "#include \"$tmp/out1\"" | $cc -E -o $tmp/out2 -
cat $tmp/out2 | grep -q foo
check '-E and -o'

# -I
mkdir $tmp/dir
echo foo > $tmp/dir/i-option-test
echo "#include \"i-option-test\"" | $cc -I$tmp/dir -E - | grep -q foo
check -I

# -D
echo foo | $cc -Dfoo -E - | grep -q 1
check -D

# -D
echo foo | $cc -D foo -E - | grep -q 1
check -D

# -D
echo foo | $cc -Dfoo=bar -E - | grep -q bar
check -D

# -U
echo foo | $cc -Dfoo=bar -Ufoo -E - | grep -q foo
check -U

# -U
echo foo | $cc -Dfoo=bar -U foo -E - | grep -q foo
check -U

# ignored options
$cc -c -O -Wall -g -std=c11 -ffreestanding -fno-builtin \
         -fno-omit-frame-pointer -fno-stack-protector -fno-strict-aliasing \
         -m64 -mno-red-zone -w -o /dev/null $tmp/empty.c
check 'ignored options'

echo "${green}OK${reset}"

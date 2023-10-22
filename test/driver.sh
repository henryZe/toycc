#!/bin/bash
cc=$1
exec='qemu-riscv64'

# bash not support
# reset='\33[0m'
# red='\33[31m'
# green='\33[32m'

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
echo 'int main() {}' | $cc -c -S -o - -xc - | grep -q 'main:'
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
echo 'int main() { return 0; }' | $cc -o $tmp/foo -xc -
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
echo "#include \"$tmp/out\"" | $cc -E -xc - | grep -q foo
check -E

echo foo > $tmp/out1
echo "#include \"$tmp/out1\"" | $cc -E -o $tmp/out2 -xc -
cat $tmp/out2 | grep -q foo
check '-E and -o'

# -I
mkdir $tmp/dir
echo foo > $tmp/dir/i-option-test
echo "#include \"i-option-test\"" | $cc -I$tmp/dir -E -xc - | grep -q foo
check -I

# -D
echo foo | $cc -Dfoo -E -xc - | grep -q 1
check -D

# -D
echo foo | $cc -D foo -E -xc - | grep -q 1
check -D

# -D
echo foo | $cc -Dfoo=bar -E -xc - | grep -q bar
check -D

# -U
echo foo | $cc -Dfoo=bar -Ufoo -E -xc - | grep -q foo
check -U

# -U
echo foo | $cc -Dfoo=bar -U foo -E -xc - | grep -q foo
check -U

# ignored options
$cc -c -O -Wall -g -std=c11 -ffreestanding -fno-builtin \
         -fno-omit-frame-pointer -fno-stack-protector -fno-strict-aliasing \
         -m64 -mno-red-zone -w -o /dev/null $tmp/empty.c
check 'ignored options'

# BOM marker
printf '\xef\xbb\xbfxyz\n' | $cc -E -o- -xc - | grep -q '^xyz'
check 'BOM marker'

# Inline functions
echo 'inline void foo() {}' > $tmp/inline1.c
echo 'inline void foo() {}' > $tmp/inline2.c
echo 'int main() { return 0; }' > $tmp/inline3.c
$cc -o /dev/null $tmp/inline1.c $tmp/inline2.c $tmp/inline3.c
check inline

echo 'extern inline void foo() {}' > $tmp/inline1.c
echo 'int foo(); int main() { foo(); }' > $tmp/inline2.c
$cc -o /dev/null $tmp/inline1.c $tmp/inline2.c
check inline

echo 'static inline void f1() {}' | $cc -o- -S -xc - | grep -v -q f1:
check inline

echo 'static inline void f1() {} void foo() { f1(); }' | $cc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f1(); }' | $cc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f1(); }' | $cc -o- -S -xc - | grep -v -q f2:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f2(); }' | $cc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f2(); }' | $cc -o- -S -xc - | grep -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() {}' | $cc -o- -S -xc - | grep -v -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() {}' | $cc -o- -S -xc - | grep -v -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f1(); }' | $cc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f1(); }' | $cc -o- -S -xc - | grep -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f2(); }' | $cc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f2(); }' | $cc -o- -S -xc - | grep -q f2:
check inline

# -idirafter
mkdir -p $tmp/dir1 $tmp/dir2
echo foo > $tmp/dir1/idirafter
echo bar > $tmp/dir2/idirafter
echo "#include \"idirafter\"" | $cc -I$tmp/dir1 -I$tmp/dir2 -E -xc - | grep -q foo
check -idirafter
echo "#include \"idirafter\"" | $cc -idirafter $tmp/dir1 -I$tmp/dir2 -E -xc - | grep -q bar
check -idirafter

# -fcommon
echo 'int foo;' | $cc -S -o- -xc - | grep -q '\.comm foo'
check '-fcommon (default)'

echo 'int foo;' | $cc -fcommon -S -o- -xc - | grep -q '\.comm foo'
check '-fcommon'

# -fno-common
echo 'int foo;' | $cc -fno-common -S -o- -xc - | grep -q '^foo:'
check '-fno-common'

# -include
echo foo > $tmp/out.h
echo bar | $cc -include $tmp/out.h -E -o- -xc - | grep -q -z 'foo.*bar'
check -include
echo NULL | $cc -Iinclude -include stdio.h -E -o- -xc - | grep -q 0
check -include

# -x
echo 'int x;' | $cc -c -xc -o $tmp/foo.o -
check -xc
echo 'x:' | $cc -c -x assembler -o $tmp/foo.o -
check '-x assembler'

echo 'int x;' > $tmp/foo.c
$cc -c -x assembler -x none -o $tmp/foo.o $tmp/foo.c
check '-x none'

# -E
echo foo | $cc -E - | grep -q foo
check -E

echo "${green}OK${reset}"

#!/bin/bash
cc=$1

uname -a | grep -q riscv64
if [ $? -ne 0 ]; then
	exec='qemu-riscv64'
	libopt='-L /usr/riscv64-linux-gnu/'
	cross_toolchain='riscv64-linux-gnu-'
fi

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
$exec $libopt $tmp/foo
check linker

rm -f $tmp/foo
echo 'int bar(); int main() { return bar(); }' > $tmp/foo.c
echo 'int bar() { return 42; }' > $tmp/bar.c
$cc -o $tmp/foo $tmp/foo.c $tmp/bar.c
$exec $libopt $tmp/foo
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

# .a file
echo 'void foo() {}' | $cc -c -xc -o $tmp/foo.o -
echo 'void bar() {}' | $cc -c -xc -o $tmp/bar.o -
$cross_toolchain'ar' rcs $tmp/foo.a $tmp/foo.o $tmp/bar.o
echo 'void foo(); void bar(); int main() { foo(); bar(); }' > $tmp/main.c
$cc -o $tmp/foo $tmp/main.c $tmp/foo.a
check '.a'

# .so file
echo 'void foo() {}' | $cross_toolchain'gcc' -fPIC -c -xc -o $tmp/foo.o -
echo 'void bar() {}' | $cross_toolchain'gcc' -fPIC -c -xc -o $tmp/bar.o -
$cross_toolchain'gcc' -shared -o $tmp/foo.so $tmp/foo.o $tmp/bar.o
echo 'void foo(); void bar(); int main() { foo(); bar(); }' > $tmp/main.c
$cc -o $tmp/foo $tmp/main.c $tmp/foo.so
check '.so'

$cc -hashmap-test
check 'hashmap'

# -M
echo '#include "out2.h"' > $tmp/out.c
echo '#include "out3.h"' >> $tmp/out.c
touch $tmp/out2.h $tmp/out3.h
$cc -M -I$tmp $tmp/out.c | grep -q -z '^out.o: .*/out\.c .*/out2\.h .*/out3\.h'
check -M

# -MF
$cc -MF $tmp/mf -M -I$tmp $tmp/out.c
grep -q -z '^out.o: .*/out\.c .*/out2\.h .*/out3\.h' $tmp/mf
check -MF

# -MP
$cc -MF $tmp/mp -MP -M -I$tmp $tmp/out.c
grep -q '^.*/out2.h:' $tmp/mp
check -MP
grep -q '^.*/out3.h:' $tmp/mp
check -MP

# -MT
$cc -MT foo -M -I$tmp $tmp/out.c | grep -q '^foo:'
check -MT
$cc -MT foo -MT bar -M -I$tmp $tmp/out.c | grep -q '^foo bar:'
check -MT

# -MD
echo '#include "out2.h"' > $tmp/md2.c
echo '#include "out3.h"' > $tmp/md3.c
(cd $tmp; $OLDPWD/$cc -c -MD -I. md2.c md3.c)
grep -q -z '^md2.o: .*md2\.c .*./out2\.h' $tmp/md2.d
check -MD
grep -q -z '^md3.o: .*md3\.c .*./out3\.h' $tmp/md3.d
check -MD
[ -f $tmp/md2.o ] && [ -f $tmp/md3.o ]
check -MD

(cd $tmp; $OLDPWD/$cc -c -MD -MF md-mf.d -I. md2.c)
grep -q -z '^md2.o: .*md2\.c .*./out2\.h' $tmp/md-mf.d
check -MD

echo 'extern int bar; int foo() { return bar; }' | $cc -fPIC -xc -c -o $tmp/foo.o -
check -fPIC
$cross_toolchain'gcc' -shared -o $tmp/foo.so $tmp/foo.o
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/main.c
$cc -o $tmp/foo $tmp/main.c $tmp/foo.so
check -fPIC

# #include_next
mkdir -p $tmp/next1 $tmp/next2 $tmp/next3
echo '#include "file1.h"' > $tmp/file.c
echo '#include_next "file1.h"' > $tmp/next1/file1.h
echo '#include_next "file2.h"' > $tmp/next2/file1.h
echo 'foo' > $tmp/next3/file2.h
$cc -I$tmp/next1 -I$tmp/next2 -I$tmp/next3 -E $tmp/file.c | grep -q foo
check '#include_next'

# -static
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$cc -static -o $tmp/foo $tmp/foo.c $tmp/bar.c
check -static
file $tmp/foo | grep -q 'statically linked'
check -static

# -shared
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$cc -fPIC -shared -o $tmp/foo.so $tmp/foo.c $tmp/bar.c
file $tmp/foo.so | grep -q 'shared object'
check -shared

# -L
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
$cc -fPIC -shared -o $tmp/libfoobar.so $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$cc -o $tmp/foo $tmp/bar.c -L$tmp -lfoobar
check -L
file $tmp/foo | grep -q 'dynamically linked'
check -L

# -Wl,
echo 'int foo() {}' | $cc -c -o $tmp/foo.o -xc -
echo 'int foo() {}' | $cc -c -o $tmp/bar.o -xc -
echo 'int main() {}' | $cc -c -o $tmp/baz.o -xc -
$cc -Wl,-z,muldefs,--gc-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
check -Wl,

# -Xlinker
echo 'int foo() {}' | $cc -c -o $tmp/foo.o -xc -
echo 'int foo() {}' | $cc -c -o $tmp/bar.o -xc -
echo 'int main() {}' | $cc -c -o $tmp/baz.o -xc -
$cc -Xlinker -z -Xlinker muldefs -Xlinker --gc-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
check -Xlinker

echo "${green}OK${reset}"

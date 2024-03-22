#!/bin/bash

repo='git@github.com:python/cpython.git'
. test/thirdparty/common
git reset --hard c75330605d4795850ec74fdc4d69aa5d92f76c00

# Python's './configure' command misidentifies chibicc as icc
# (Intel C Compiler) because icc is a substring of chibicc.
# Modify the configure file as a workaround.
sed -i -e 1996,2011d configure.ac
autoreconf

CC=$CC ./configure --host=riscv64-linux-gnu --build=x86_64 --disable-ipv6 ac_cv_file__dev_ptmx=no ac_cv_file__dev_ptc=no
$make clean
$make
$make test

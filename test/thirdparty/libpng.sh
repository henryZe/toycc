#!/bin/bash

repo='git@github.com:rui314/libpng.git'
. test/thirdparty/common
git reset --hard dbe3e0c43e549a1602286144d94b0666549b18e6

CC=$CC ./configure --host riscv64
sed -i 's/^wl=.*/wl=-Wl,/; s/^pic_flag=.*/pic_flag=-fPIC/' libtool
$make clean
$make
$make test

#!/bin/bash

repo='git@github.com:sqlite/sqlite.git'
. test/thirdparty/common
git reset --hard 86f477edaa17767b39c7bae5b67cac8580f7a8c1

CC=$CC CFLAGS=-D_GNU_SOURCE ./configure --host riscv64
sed -i 's/^wl=.*/wl=-Wl,/; s/^pic_flag=.*/pic_flag=-fPIC/' libtool
$make clean
$make
$make test

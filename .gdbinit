set debuginfod enabled on
target remote 127.0.0.1:1234
set architecture riscv:rv64
# add-symbol-file vmlinux
# add-symbol-file selfhost/toycc
# set follow-fork-mode child
# set args -Iinclude -Itest -c -S test/line.c -o output/test/line.s

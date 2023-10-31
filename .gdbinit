set debuginfod enabled on
# add-symbol-file vmlinux
# add-symbol-file selfhost/toycc
target remote 127.0.0.1:1234
set architecture riscv:rv64

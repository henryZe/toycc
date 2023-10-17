#!/bin/sh

mount -t 9p -o trans=virtio,version=9p2000.L hostshare /tmp/
cd /tmp
sh qemu_script/run_compile.sh

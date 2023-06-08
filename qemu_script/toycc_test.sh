#!/bin/sh

mount -t 9p -o trans=virtio,version=9p2000.L hostshare /tmp/
cd /tmp
sh default.sh

qemu-system-riscv64 \
	-M virt \
	-m 512M \
	-nographic \
	-kernel Image \
	-drive file=riscv.img,format=raw,id=hd0 \
	-device virtio-blk-device,drive=hd0 \
	-append "root=/dev/vda rw console=ttyS0" \
	-fsdev local,security_model=passthrough,id=fsdev0,path=output/test \
	-device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=hostshare \
	-gdb tcp::1234 \
	-snapshot \

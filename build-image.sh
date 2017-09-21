#!/bin/bash
lz4c ./arch/arm64/boot/Image Image.lz4
mkimage -f Image.its Image.fit
num=$(date +%Y%m%d_%H%M%S)
mkbootimg --kernel /kernel/linux-4.13-rc4/Image.fit --ramdisk /kernel/ramdisk/custom-initramfs.cpio.gz -o /kernel/kitchen/boot-4.13-rc4_$num.img.unsigned
echo "boot-4.13-rc4_$num.img.unsigned built"

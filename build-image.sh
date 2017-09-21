#!/bin/bash
RELEASE="4.14-rc1"
lz4c ./arch/arm64/boot/Image Image.lz4
mkimage -f Image.its Image.fit
num=$(date +%Y%m%d_%H%M%S)
mkbootimg --kernel /kernel/linux-smaug/Image.fit --ramdisk /kernel/ramdisk/custom-initramfs.cpio.gz -o /kernel/kitchen/boot-$release_$num.img.unsigned
echo "boot-$release_$num.img.unsigned built"

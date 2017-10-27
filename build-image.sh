#!/bin/bash
BUILDTAG="`git symbolic-ref HEAD 2> /dev/null | cut -b 12-`-`git log --pretty=format:\"%h\" -1`"
RELEASE="$BUILDTAG"
echo "Building $BUILDTAG"
lz4c ./arch/arm64/boot/Image Image.lz4
mkimage -f Image.its Image.fit
num=$(date +%Y%m%d_%H%M%S)
mkbootimg --kernel /kernel/linux-smaug/Image.fit --ramdisk /kernel/ramdisk/custom-initramfs.cpio.gz -o /kernel/kitchen/boot-${RELEASE}_$num.img.unsigned
echo "boot-${RELEASE}_$num.img.unsigned built"

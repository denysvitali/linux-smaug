#!/bin/bash
BUILDTAG="`git symbolic-ref HEAD 2> /dev/null | cut -b 12-`-`git log --pretty=format:\"%h\" -1`"
RELEASE="$BUILDTAG"
RELEASE=${RELEASE/\//-}
echo "Building $BUILDTAG"
lz4c ./arch/arm64/boot/Image Image.lz4
mkimage -f Image.its Image.fit
num=$(date +%Y%m%d_%H%M%S)

cleanup() {
        rm -f ${EMPTY}
}
EMPTY=$(mktemp /tmp/tmp.XXXXXXXX)
trap cleanup EXIT
echo " " > ${EMPTY}

mkbootimg --kernel /kernel/linux-smaug/Image.fit --ramdisk /kernel/ramdisk/custom-initramfs.cpio.gz -o /kernel/kitchen/boot-${RELEASE}_$num.img.unsigned

futility vbutil_keyblock --pack boot.img.keyblock --datapubkey /usr/share/vboot/devkeys/kernel_data_key.vbpubk --signprivate /usr/share/vboot/devkeys/kernel_data_key.vbprivk
futility vbutil_kernel --pack /kernel/kitchen/boot-${RELEASE}_$num.img --keyblock boot.img.keyblock --signprivate /usr/share/vboot/devkeys/kernel_data_key.vbprivk --version 1 --vmlinuz /kernel/kitchen/boot-${RELEASE}_$num.img.unsigned --config ${EMPTY} --arch arm --bootloader ${EMPTY} --flags 0x1

echo "boot-${RELEASE}_$num.img.unsigned built"

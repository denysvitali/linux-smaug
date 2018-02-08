#!/bin/bash
cd /kernel;
mkdir -p /kernel/ramdisk/
git clone https://github.com/denysvitali/smaug-custom-initram
ln -s /kernel/ramdisk/ramdisk.cpio.gz /kernel/ramdisk/custom-initramfs.cpio.gz
cat > /kernel/ramdisk/build.sh <<- EOF
#!/bin/bash
(cd ../smaug-custom-initram/ && find . | cpio -H newc -o | gzip -9 > /kernel/ramdisk/ramdisk.cpio.gz)
EOF

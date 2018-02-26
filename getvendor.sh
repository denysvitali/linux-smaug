#!/bin/bash

force=false

if [ $1 = "-f" ]; then
	force=true
fi

function log(){
	echo -en "\e[1m\e[34m"
	echo "=> $1"
	echo -en "\e[21m\e[39m"
}

function warn(){
	echo -en "\e[1m\e[33m"
	echo "=> $1"
	echo -en "\e[21m\e[39m"
}

function download_vendor_files(){
	vendor_name=$1
	extract_name=$2
	vendor_url=$3
	extract_file=extract-$extract_name-dragon.sh

	log "Getting $vendor_name files..."
	basedir="/tmp/vendor/${vendor_name,,}"
	extract_file_path="${basedir}/$extract_file"
	download_path="$basedir/${vendor_name,,}-dragon.tgz"
	log "Download path: $download_path"
	log "Vendor URL: $vendor_url"
	rm -rf $basedir
	mkdir -p $basedir
	wget "$vendor_url" -O $download_path
	tar -C $basedir -xvf $download_path
	log "$vendor_name script extracted at $basedir"
	mkdir -p $basedir/extracted
	tailcmd=$(grep -oP --text "^tail -n \+\d+" $extract_file_path)
	$tailcmd $extract_file_path | tar -zxv -C $basedir/extracted
	log "$vendor_name files extracted at $basedir/extracted"
}

if [ -d "/tmp/vendor/google/" ] && [ "$force" != true ]
then
	warn "Google vendor files exist in /tmp/vendor/google/, won't download them again (use -f to force)"
else
	download_vendor_files 'Google' 'google_devices' 'https://dl.google.com/dl/android/aosp/google_devices-dragon-opr1.170623.027-dd1b444b.tgz'
fi

if [ -d "/tmp/vendor/nvidia/" ] && [ "$force" != true ]
then
	warn "Nvidia vendor files exist in /tmp/vendor/nvidia/, won't download them again (use -f to force)"
else
	download_vendor_files 'Nvidia' 'nvidia' 'https://dl.google.com/dl/android/aosp/nvidia-dragon-opr1.170623.027-3f71473f.tgz'
fi

# /tmp/vendor/nvidia/extracted/vendor/nvidia/dragon/proprietary/ => firmware/nvidia/tegra210/
# /tmp/vendor/google/extracted/vendor/google_devices/dragon/proprietary/vendor.img => 

# Google
simg2img /tmp/vendor/google/extracted/vendor/google_devices/dragon/proprietary/vendor.img /tmp/vendor/google/extracted/vendor/google_devices/dragon/proprietary/vendor-img.img
mkdir /tmp/vendor/google/mount
mount /tmp/vendor/google/extracted/vendor/google_devices/dragon/proprietary/vendor-img.img /tmp/vendor/google/mount
tree /tmp/vendor/google/mount
cp -Rv /tmp/vendor/google/mount/firmware/* firmware/
umount /tmp/vendor/google/mount
# Nvidia
cp -Rv /tmp/vendor/nvidia/extracted/vendor/nvidia/dragon/proprietary/* firmware/nvidia/tegra210/

# Broadcom
wget 'https://android.googlesource.com/platform/hardware/broadcom/wlan/+/android-o-mr1-preview-2/bcmdhd/firmware/bcm4354/fw_bcm4354.bin?format=TEXT' -O firmware/fw_bcmdhd.bin.b64 && cat firmware/fw_bcmdhd.bin.b64 | base64 -d > firmware/fw_bcmdhd.bin

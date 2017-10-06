#!/bin/bash
vendor_url='https://dl.google.com/dl/android/aosp/nvidia-dragon-opr1.170623.027-3f71473f.tgz'
basedir='/tmp/vendor/nvidia'
extractfile="${basedir}/extract-nvidia-dragon.sh"
rm -rf $basedir
wget "$vendor_url" -O nvidia-dragon.tgz
mkdir -p $basedir/extracted
tar -C $basedir -xvf nvidia-dragon.tgz 
ls -la $basedir
tailcmd=$(grep -oP --text "^tail -n \+\d+" $extractfile)
$tailcmd $extractfile | tar zxv -C $basedir/extracted
cp -r $basedir/extracted/vendor/nvidia/dragon/proprietary/* firmware/nvidia/tegra210/

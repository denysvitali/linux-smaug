#!/bin/bash

force=false

if [ "$1" = "-f" ]; then
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

tmpdir=$(mktemp -d)
tx1file="tx1-driver.tbz2"
tx1_path="$tmpdir/$tx1file"
tx1_ext_path="$tmpdir/tx1-ext"
tx1_drivers_path="$tmpdir/tx1-drivers"

mkdir $tx1_ext_path
mkdir $tx1_drivers_path

wget -O $tx1_path https://developer.nvidia.com/embedded/dlc/tx1-driver-package-r2422
bzip2 -dc $tmpdir/$tx1file | tar -C $tx1_ext_path -xvf -
bzip2 -dc $tx1_ext_path/Linux_for_Tegra/nv_tegra/nvidia_drivers.tbz2 | tar -C $tx1_drivers_path -xvf -
cp -R $tx1_drivers_path/lib/firmware .
rm -rf $tmpdir

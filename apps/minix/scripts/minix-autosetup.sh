#!/bin/bash
set -e

# invoke this script from llvm-apps/apps/minix to automatically set up the
# network, package management and SSH om a MINIX disk image

export HYPER
export MEMSIZE
export ROOT_SIZE
export USR_SIZE

if [ ! -f scripts/minix-autosetup-rc.sh ]; then
	echo Wrong directory, please execute from llvm-apps/apps/minix >&2
	exit 1
fi

cp scripts/minix-autosetup-rc.sh minix/minix_x86.rc
./clientctl buildimage
./clientctl run


#!/bin/bash

set -e

# expected incoming parameters:
# - APPEND
# - CROSS_TOOLS
# - FAULTSPEC (only needed when common_run is invoked)
# - HYPER
# - MEMSIZE
# - MODULEDIR
# - PATHAPP
# - PATHOUT
# - PATHQEMUIMG
# - PATHROOT
# - PATHTEMPBASE
# - PATHVMIMG
# - PATHVMIMGTYPE
# - PRUN_CPU_RANK (from prun)
# - SELTESTINDEXFIRST

# parameters to clientctl
export APPEND
export HYPER
export IMAGE
export MEMSIZE
export MODULEDIR
export RUNARGS

# paths
SELTESTINDEX="`expr "$SELTESTINDEXFIRST" + "${PRUN_CPU_RANK}"`"
PATHHYPERMEMLOG="$PATHOUT/hypermemlog-$SELTESTINDEX.txt"
PATHSERIALLOG="$PATHOUT/serial-$SELTESTINDEX.txt"
if mkdir -p "$PATHTEMPBASE" 2> /dev/null; then
	chmod 700 "$PATHTEMPBASE"
else
	PATHTEMPBASE=/tmp
fi
PATHTEMP="$PATHTEMPBASE/tmp-$SELTESTINDEX"
PATHDELTAIMG="$PATHTEMP/image_delta.qcow2"
PATHTESTIMGDIR="$PATHTEMP/image_test"
PATHTESTIMG="$PATHTESTIMGDIR/disk.img"

rm -rf "$PATHTEMP"
mkdir -p "$PATHTEMP"
mkdir -p "$PATHTESTIMGDIR/files"

common_setup_disk_delta()
{
	# set up disk image for deltas from the primary disk (which is shared)
	echo "Creating delta disk image $PATHDELTAIMG for primary disk $PATHVMIMG"
	"$PATHQEMUIMG" create -f qcow2 -o "backing_file=$PATHVMIMG,backing_fmt=$PATHVMIMGTYPE" "$PATHDELTAIMG"
}

common_setup_disk_config()
{
	# set up secondary disk image for configuration
	echo "Creating secondary disk image"
	mkdir -p "$PATHTESTIMGDIR/files/hypermemclient"
	cp "$PATHROOT/llvm/tools/hypermemclient/"{Makefile,*.c,*.h} "$PATHTESTIMGDIR/files/hypermemclient"
	cd "$PATHTESTIMGDIR/files"
	find . -type d | sed 's/$/ type=dir uid=0 gid=0 mode=755/' > "$PATHTESTIMGDIR/input"
	find . -type f -name '*.sh' | sed 's/$/ type=file uid=0 gid=0 mode=755/' >> "$PATHTESTIMGDIR/input"
	find . -type f ! -name '*.sh' | sed 's/$/ type=file uid=0 gid=0 mode=644/' >> "$PATHTESTIMGDIR/input"
	cat "$PATHTESTIMGDIR/input" | "${CROSS_TOOLS}/nbtoproto" -b "$PATHTESTIMGDIR/files" -o "$PATHTESTIMGDIR/proto"
	rm "$PATHTESTIMGDIR/input"
	"${CROSS_TOOLS}/nbmkfs.mfs" "$PATHTESTIMG" "$PATHTESTIMGDIR/proto"
	rm "$PATHTESTIMGDIR/proto"
}

common_run()
{
	# run the job
	echo "Starting QEMU for test $SELTESTINDEX"
	cd "$PATHAPP"

	if [ -z "$FAULTSPEC" ]; then
		FAULTSPECPARAM=""
	else
		FAULTSPECPARAM=",faultspec=$FAULTSPEC"
	fi

	APPEND="$APPEND cttyline=0"
	IMAGE="$PATHDELTAIMG"
	RUNARGS="-device hypermem,logpath=$PATHHYPERMEMLOG$FAULTSPECPARAM -hdb $PATHTESTIMG -serial file:$PATHSERIALLOG"

	echo "APPEND=$APPEND"
	echo "HYPER=$HYPER"
	echo "IMAGE=$IMAGE"
	echo "MEMSIZE=$MEMSIZE"
	echo "MODULEDIR=$MODULEDIR"
	echo "RUNARGS=$RUNARGS"

	./clientctl run

	echo "Done with test $SELTESTINDEX"
}

common_cleanup()
{
	# delete temp files
	rm -rf "$PATHTEMP"
}

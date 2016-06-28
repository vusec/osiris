#!/bin/bash

set -e

# expected to be defined by parent script:
# - PATHAPP
# - PRUNSCRIPTNAME
# - RUNNAMEFAULTY
# - RUNNAMEGOLDEN
# - SETTINGSNAME

if [ ! -f "$PATHAPP/prun-scripts/edfi-common.inc" ]; then
	echo "PATHAPP set incorrectly (PATHAPP=\"$PATHAPP\")" >&2
	exit 1
fi
[ ! -f "$PATHAPP/autosetup-paths.inc" ] || source "$PATHAPP/autosetup-paths.inc"

# script settings
: ${MEMSIZE:=3072}
: ${PRUNITER:=1}       # integer, how often to invoke prun
: ${PRUNOPTS:=""}      # string, options to specify to prun (like "-t 30:00" to extend reservation duration)
: ${PRUNNODES:=1}      # integer, number of nodes to use with prun
: ${PRUNPERNODE:=1}    # integer, number of instances per node with prun
: ${ROOT_SIZE:=1048576}# integer, size of / partition
: ${USR_SIZE:=8388608} # integer, size of /usr partition

# script settings for fault/test selection (not needed otherwise)
: ${FORCETESTS:=""}    # space-separated list of strings, force a golden run with specified tests
: ${NUMFAULTS:=10000}  # integer, number of faults to inject; used only in the first run after the golden run
: ${SELECTTESTS:=0}    # boolean, non-zero means force fault+test selection even if previously selected
: ${TESTAVOIDBOOT:=1}  # boolean, non-zero means inject no boot-time faults
: ${TESTONLYONE:=0}    # boolean, non-zero means select just one test per run and perform twice
: ${TESTSRECOVERY:="recovery-ds recovery-pm recovery-rs recovery-vfs recovery-vm"} # space-separated list of strings, pre-tests to test recovery
: ${TESTSRECOVERYDISABLE:=0} # boolean, non-zero means no recovery pre-tests

[ "$TESTSRECOVERYDISABLE" -eq 0 ] || TESTSRECOVERY=""

# path specifications
: ${PATHROOT:="$PATHAPP/../.."}
: ${PATHOBJ:="$PATHAPP/obj.i386"}
: ${PATHMAPEXT:=.bcl.map}
: ${PATHUCPATCHEXT:=.ucpatch}

: ${PATHSETTINGSBIN:="$PATHAPP/bin/$SETTINGSNAME"}
: ${PATHSETTINGSBINLOGS:="$PATHSETTINGSBIN/buildlogs"}
: ${PATHVMIMG:="$PATHSETTINGSBIN/vm.img"}
: ${PATHVMIMGTYPE:=raw}

: ${PATHTEMPBASE="/local/$USER"}

: ${CROSS_TOOLS:="$PATHOBJ/tooldir.`uname -s`-`uname -r`-`uname -m`/bin"}
: ${PATHTOOLS:="$PATHROOT/autosetup.dir/install"}
: ${PATHQEMU:="$PATHTOOLS/bin/qemu-system-x86_64"}
: ${PATHQEMUIMG:="$PATHTOOLS/bin/qemu-img"}
: ${HYPER:="$PATHQEMU --enable-kvm"}

: ${PATHOUTBASE:="$PATHROOT/results/minix"}
: ${PATHOUT:="$PATHOUTBASE/faulty-$RUNNAMEFAULTY-`date '+%Y%m%d-%H%M%S'`"}
: ${PATHGOLDEN:="$PATHOUTBASE/golden-$RUNNAMEGOLDEN"}

: ${PATHSELFAULTS:="$PATHAPP/selected-faults-$RUNNAMEGOLDEN.txt"}
: ${PATHSELTESTS:="$PATHAPP/selected-tests-$RUNNAMEGOLDEN.txt"}
: ${PATHSELTESTINDEX:="$PATHAPP/selected-test-index-$RUNNAMEFAULTY.txt"}
: ${PRUNSCRIPT:="$PATHAPP/prun-scripts/$PRUNSCRIPTNAME"}

if [ -z "$RUNNAMEFAULTY" ]; then
	echo "RUNNAMEFAULTY set incorrectly (RUNNAMEFAULTY=\"$RUNNAMEFAULTY\")" >&2
	exit 1
fi
if [ -z "$RUNNAMEGOLDEN" ]; then
	echo "RUNNAMEGOLDEN set incorrectly (RUNNAMEGOLDEN=\"$RUNNAMEGOLDEN\")" >&2
	exit 1
fi
if [ -z "$SETTINGSNAME" ]; then
	echo "SETTINGSNAME set incorrectly (SETTINGSNAME=\"$SETTINGSNAME\")" >&2
	exit 1
fi
if [ ! -x "$PATHQEMU" ]; then
	echo "PATHQEMU or PATHTOOLS set incorrectly or QEMU not built by autosetup (PATHQEMU=\"$PATHQEMU\")" >&2
	exit 1
fi
if [ ! -x "$PATHQEMUIMG" ]; then
	echo "PATHQEMUIMG or PATHTOOLS set incorrectly or QEMU not built by autosetup (PATHQEMUIMG=\"$PATHQEMUIMG\")" >&2
	exit 1
fi
if [ ! -x "$PRUNSCRIPT" ]; then
	echo "PRUNSCRIPT set incorrectly (PRUNSCRIPT=\"$PRUNSCRIPT\")" >&2
	exit 1
fi

# parameters to minix-autosetup.sh
export HYPER
export MEMSIZE
export ROOT_SIZE
export USR_SIZE

# other initialization
faultcount=0

common_print_settings()
{
	echo "Running EDFI experiment with the following settings:"
	echo "- FORCETESTS      = $FORCETESTS"
	echo "- HYPER           = $HYPER"
	echo "- NUMFAULTS       = $NUMFAULTS"
	echo "- PATHOUT         = $PATHOUT"
	echo "- PATHSETTINGSBIN = $PATHSETTINGSBIN"
	echo "- PATHTEMPBASE    = $PATHTEMPBASE"
	echo "- PATHVMIMG       = $PATHVMIMG"
	echo "- PRUNITER        = $PRUNITER"
	echo "- PRUNOPTS        = $PRUNOPTS"
	echo "- PRUNNODES       = $PRUNNODES"
	echo "- PRUNPERNODE     = $PRUNPERNODE"
	echo "- PRUNSCRIPT      = $PRUNSCRIPT"
	echo "- SELECTTESTS     = $SELECTTESTS"
	echo "- TESTAVOIDBOOT   = $TESTAVOIDBOOT"
	echo "- TESTONLYONE     = $TESTONLYONE"
	echo "- TESTSRECOVERY   = $TESTSRECOVERY"
}

common_must_build()
{
	[ ! -f "$PATHSETTINGSBIN/done" ]
}

common_cleanup()
{
	echo "Cleaning up"
	rm -rf "$PATHSETTINGSBIN"
	find "$PATHOBJ" -name "*$PATHMAPEXT" -type f -print0 | xargs -0 rm -f
	find "$PATHOBJ" -name "*$PATHUCPATCHEXT" -type f -print0 | xargs -0 rm -f
}

common_prepare_pathout()
{
	echo "Writing output to $PATHOUT"
	mkdir -p "$PATHOUTBASE"
	mkdir "$PATHOUT"
	set > "$PATHOUT/settings.txt"
	cp -r "$PATHSETTINGSBINLOGS" "$PATHOUT"
	cp -r "$PATHSETTINGSBIN/maps" "$PATHOUT"
	cp -r "$PATHSETTINGSBIN/ucpatch" "$PATHOUT"
	ln -s "$PATHSETTINGSBIN" "$PATHOUT/bin"
}

common_build_vm()
{
	echo "Building MINIX VM"
	cd "$PATHAPP"
	scripts/minix-autosetup.sh > "$PATHAPP/minix-autosetup.log"
}

common_build_done()
{
	mkdir -p "$PATHSETTINGSBIN/binaries"
	mv "$PATHOBJ/destdir.i386/service" "$PATHSETTINGSBIN/binaries"
	cp "$PATHOBJ/destdir.i386/boot/minix/.temp/kernel" "$PATHSETTINGSBIN/binaries"

	mkdir -p "$PATHSETTINGSBIN/maps"
	for f in `find "$PATHOBJ" -name "*$PATHMAPEXT" -type f`; do
		mv "$f" "$PATHSETTINGSBIN/maps"
	done

	mkdir -p "$PATHSETTINGSBIN/modules"
	mv "$PATHOBJ/destdir.i386/boot/minix/.temp"/* "$PATHSETTINGSBIN/modules"

	mkdir -p "$PATHSETTINGSBIN/ucpatch"
	for f in `find "$PATHOBJ" -name "*$PATHUCPATCHEXT" -type f`; do
		mv "$f" "$PATHSETTINGSBIN/ucpatch"
	done

	mv "$PATHAPP/minix/minix_x86.img" "$PATHVMIMG"

	mkdir -p "$PATHSETTINGSBINLOGS"
	mv "$PATHAPP/minix-autosetup.log" "$PATHSETTINGSBINLOGS"

	touch "$PATHSETTINGSBIN/done"
}

common_select_faults()
{
	[ "$SELECTTESTS" -eq 0 ] || rm -f "$PATHSELFAULTS" "$PATHSELFAULTS" "$PATHSELTESTINDEX"
	faultcount=0
	if [ -z "$FORCETESTS" -a ! -s "$PATHSELTESTS" ]; then
		if [ ! -s "$PATHSELFAULTS" -a -d "$PATHGOLDEN" ]; then
			echo "Selecting faults to inject based on the golden run in $PATHGOLDEN"
			make -C "$PATHROOT/llvm/tools/hypermemloganalyze/faultpicker" faultpicker
			"$PATHROOT/llvm/tools/hypermemloganalyze/faultpicker/faultpicker" -n "$NUMFAULTS" "$PATHGOLDEN/maps"/*.map "$PATHGOLDEN"/hypermemlog-*.txt > "$PATHSELFAULTS"
		fi
		if [ -s "$PATHSELFAULTS" ]; then
			echo "Selecting tests to run based on the golden run in $PATHGOLDEN and selected tests in $PATHSELFAULTS"
			make -C "$PATHROOT/llvm/tools/hypermemloganalyze/testpicker" testpicker
			testpickerargs=""
			[ "$TESTAVOIDBOOT" -eq 0 ] || testpickerargs="$testpickerargs -b"
			[ "$TESTONLYONE" -eq 0 ] || testpickerargs="$testpickerargs -o"
			[ "$TESTONLYONE" -ne 0 ] || testpickerargs="$testpickerargs -r 360"
			for testrecovery in $TESTSRECOVERY; do
				testpickerargs="$testpickerargs -x $testrecovery -X $testrecovery"
			done
			"$PATHROOT/llvm/tools/hypermemloganalyze/testpicker/testpicker" -f $testpickerargs -X isofs "$PATHSELFAULTS" "$PATHGOLDEN/maps"/*.map "$PATHGOLDEN"/hypermemlog-*.txt > "$PATHSELTESTS"
		fi
		rm -f "$PATHSELTESTINDEX"
	fi
	if [ -n "$FORCETESTS" ]; then
		echo "Running forced tests: $FORCETESTS"
	elif [ -s "$PATHSELTESTS" ]; then
		echo "Using faults and tests specified in $PATHSELTESTS for faulty run"
		faultcount="`wc -l "$PATHSELTESTS" | cut -d" " -f1`"
	else
		echo "No faults have been selected, performing golden run"
	fi
	[ ! -s "$PATHSELFAULTS" ] || cp "$PATHSELFAULTS" "$PATHOUT"
	[ ! -s "$PATHSELTESTS" ] || cp "$PATHSELTESTS" "$PATHOUT"
}

common_check_map()
{
	if [ -z "$FORCETESTS" -a -d "$PATHGOLDEN" ]; then
		mapfiles="`find "$PATHGOLDEN/maps" "$PATHOUT/maps" -name "*.map" | sed 's,.*/,,' | sort | uniq`"
		badmap=0
		for f in $mapfiles; do
			if ! diff "$PATHGOLDEN/maps/$f" "$PATHOUT/maps/$f"; then
				echo "Map file $f differs between $PATHGOLDEN/maps and $PATHOUT/maps" >&2
				badmap=1
			fi
		done
		[ "$badmap" = 0 ]
	fi
}

common_prun()
{
	if which prun > /dev/null; then
		prun -export-env -np "$PRUNNODES" "-$PRUNPERNODE" -o "$OUTFILE" $PRUNOPTS "$PRUNSCRIPT"
	else
		# emulate parallel run functionality on the local system
		for node in `seq 1 "$PRUNNODES"`; do
			for instance in `seq 1 "$PRUNPERNODE"`; do
				PRUN_CPU_RANK="$node" "$PRUNSCRIPT" > "$OUTFILE.$node.$instance" 2>&1 &
			done
			wait
		done
	fi
}

common_run_experiment()
{
	echo "Starting experiments"

	# parameters needed by edfi-prun-common.inc
	export APPEND
	export CROSS_TOOLS
	export HYPER
	export MEMSIZE
	export MODULEDIR="$PATHSETTINGSBIN/modules"
	export PATHAPP
	export PATHOUT
	export PATHQEMUIMG
	export PATHROOT
	export PATHTEMPBASE
	export PATHVMIMG
	export PATHVMIMGTYPE
	export SELTESTINDEXFIRST

	# parameters possibly needed by $PRUNSCRIPT
	export FORCETESTS
	export PATHSELTESTS
	export TESTSRECOVERY

	cd "$PATHOUT"
	if [ -s "$PATHSELTESTINDEX" ]; then
		SELTESTINDEXFIRST="`cat "$PATHSELTESTINDEX"`"
	else
		SELTESTINDEXFIRST=1
	fi
	for iter in `seq 1 "$PRUNITER"`; do
		runcount="`expr "$PRUNNODES" \* "$PRUNPERNODE"`"
		if [ "$faultcount" -gt 0 ]; then 
			faultavail="`expr "$faultcount" - "$SELTESTINDEXFIRST" + 1`"
			if [ "$faultavail" -le 0 ]; then
				echo "No more faults available"
				break
			fi
			[ "$runcount" -le "$faultavail" ] || runcount="$faultavail"
		fi
		SELTESTINDEXLAST="`expr "$SELTESTINDEXFIRST" + "$runcount"`"
		echo "Starting experiments $SELTESTINDEXFIRST-`expr "$SELTESTINDEXLAST" - 1`"
		OUTFILE="qemu-out-$SELTESTINDEXFIRST" common_prun > "$PATHOUT/prun-out-$SELTESTINDEXFIRST.txt" 2>&1 || true

		SELTESTINDEXFIRST="$SELTESTINDEXLAST"
		echo "$SELTESTINDEXFIRST" > "$PATHSELTESTINDEX"
	done

	# if we did a golden run, rename the output directory to use it on the next invocation
	if [ -z "$FORCETESTS" -a ! -s "$PATHSELTESTS" -a ! -d "$PATHGOLDEN" ]; then
		echo "Retaining golden run results as $PATHGOLDEN"
		mv "$PATHOUT" "$PATHGOLDEN"
		rm -f "$PATHSELTESTINDEX"
	fi

	echo "Completed succesfully"
}

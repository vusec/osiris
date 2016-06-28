#!/bin/bash

set -e

: ${PATHROOT:="$PWD"}
if [ ! -f "$PATHROOT/autosetup.inc" ]; then
	echo "Please execute from the root of the repository or set PATHROOT" >&2
	exit 1
fi

LLVMBRANCH=RELEASE_34/final
LLVMVERSION=3.4
LLVMVERSIONCONF=minix

source "$PATHROOT/autosetup.inc"

# store settings
(
echo "export PATH=\"$PATHAUTOPREFIX/bin:\$PATH\""
echo "PATHTOOLS=\"$PATHAUTOPREFIX\""
echo "PATHQEMU=\"$PATHAUTOPREFIX/bin/qemu-system-x86_64\""
) > "$PATHROOT/apps/minix/autosetup-paths.inc"

# build app
echo "Building minix"
cd "$PATHROOT/apps/minix"
export JOBS
scripts/setup_distr_recovery.sh "$PATHROOT" || (
	echo "ERROR: see $PATHROOT/apps/minix/osiris.setup.log for details" >&2
	exit 1
)


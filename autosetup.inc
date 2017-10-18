#!/bin/bash

set -e

# This script compiles a number of important packages and installs them
# in the autosetup.dir/install directory to avoid problems with bad versions
# or packages that have been broken by the distribution.
# However, a number of packages needs to be installed beforehand.
# The script has been tested on a clean Ubuntu 14.04.4 LTS Server system
# with the following packages installed:
#
# sudo apt-get install bison flex g++ gcc gcc-multilib gettext git \
#                      libboost-dev libboost-system-dev libboost-thread-dev \
#                      make pkg-config ssh subversion texlive zlib1g-dev

# incoming variables expected:
# - LLVMBRANCH
# - LLVMVERSION
# - LLVMVERSIONCONF
# - PATHROOT

corecount="`grep '^processor' /proc/cpuinfo|wc -l`"

: ${CLEAN=0}
: ${JOBS="`expr "$corecount" "*" 2`"}

: ${PATHAUTOSETUP="$PATHROOT/autosetup.dir"}
: ${PATHAUTOPREFIX="$PATHAUTOSETUP/install"}
: ${PATHAUTOSRC="$PATHAUTOSETUP/src"}
: ${PATHAUTOSTATE="$PATHAUTOSETUP/state"}
: ${PATHLLVM:="$PATHAUTOSETUP/llvm-$LLVMVERSION"}
: ${PATHLOG="$PATHROOT/autosetup-log.txt"}
: ${PATHVMS:="$PATHAUTOSETUP/vms"}

: ${VERSIONBASH=bash-4.3}
: ${VERSIONBINUTILS=binutils-2.25}
: ${VERSIONCMAKE=cmake-3.4.1}
: ${VERSIONCMAKEURL=v3.4}
: ${VERSIONCOREUTILS=coreutils-8.22}
: ${VERSIONGLIB=glib-2.44.1}
: ${VERSIONGLIBURL=2.44}
: ${VERSIONLIBFFI=libffi-3.2.1}
: ${VERSIONPIXMAN=pixman-0.32.6}

PATHBINUTILS="$PATHAUTOSRC/$VERSIONBINUTILS"

usage()
(
	echo "$@" >&2

	echo "usage:"
	echo "  autosetup-linux.sh"
	echo "  autosetup-minix.sh"
	echo ""
	echo "relevant environment variables:"
	echo "- CLEAN can be set to 1 to start with a clean slate"
	echo "- JOBS can be set to the number of concurrent processes make can use"
	echo "- PATHROOT can be set to the root of the repository (default: cwd)"
	exit 1
)

[ -f "$PATHROOT/autosetup.inc" ] || usage "ERROR: PATHROOT is incorrect"
[ "$LLVMBRANCH" != "" ] || usage "ERROR: LLVMBRANCH is not set"
[ "$LLVMVERSION" != "" ]|| usage "ERROR: LLVMVERSION is not set"
[ "$LLVMVERSIONCONF" != "" ]|| usage "ERROR: LLVMVERSIONCONF is not set"
[ "$#" -eq 0 ] || usage "ERROR: invalid number of arguments"

if [ "$CLEAN" -ne 0 ]; then
	echo "Cleaning up old state"
	rm -rf "$PATHAUTOSETUP"
fi

export PATH="$PATHAUTOPREFIX/bin:$PATH"

exec 5> "$PATHLOG"

run()
{
	echo -------------------------------------------------------------------------------- >&5
	echo "command:          $*"               >&5
	echo "\$PATH:            $PATH"            >&5
	echo "working dir:      $PWD"             >&5
	echo -------------------------------------------------------------------------------- >&5
	if "$@" >&5 2>&5; then
		echo "[done]" >&5
	else
		echo "Command '$*' failed in directory $PWD with exit code $?, please check $PATHLOG for details" >&2
		exit 1
	fi
}

echo "Creating directories"
run mkdir -p "$PATHAUTOSRC"
run mkdir -p "$PATHAUTOSTATE"

# build bash to override the system's default shell
echo "Building bash"
cd "$PATHAUTOSRC"
[ -f "$VERSIONBASH.tar.gz" ] || run wget "http://ftp.gnu.org/gnu/bash/$VERSIONBASH.tar.gz"
[ -d "$VERSIONBASH" ] || run tar xf "$VERSIONBASH.tar.gz"
cd "$VERSIONBASH"
[ -f Makefile ] || run ./configure --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install
[ -f "$PATHAUTOPREFIX/bin/sh" ] || ln -s "$PATHAUTOPREFIX/bin/bash" "$PATHAUTOPREFIX/bin/sh"

# build a sane version of coreutils
echo "Building coreutils"
cd "$PATHAUTOSRC"
[ -f "$VERSIONCOREUTILS.tar.xz" ] || run wget "http://ftp.gnu.org/gnu/coreutils/$VERSIONCOREUTILS.tar.xz"
[ -d "$VERSIONCOREUTILS" ] || run tar xf "$VERSIONCOREUTILS.tar.xz"
cd "$VERSIONCOREUTILS"
[ -f Makefile ] || run ./configure --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install

# build libffi, needed by qemu
echo "Building libffi"
cd "$PATHAUTOSRC"
[ -f "$VERSIONLIBFFI.tar.gz" ] || run wget "ftp://sourceware.org/pub/libffi/$VERSIONLIBFFI.tar.gz"
[ -d "$VERSIONLIBFFI" ] || run tar xf "$VERSIONLIBFFI.tar.gz"
cd "$VERSIONLIBFFI"
[ -f Makefile ] || run ./configure --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install

# build binutils to ensure we have gold
echo "Building binutils"
cd "$PATHAUTOSRC"
[ -f "$VERSIONBINUTILS.tar.bz2" ] || run wget "http://ftp.gnu.org/gnu/binutils/$VERSIONBINUTILS.tar.bz2"
[ -d "$VERSIONBINUTILS" ] || run tar xf "$VERSIONBINUTILS.tar.bz2"
cd "$PATHBINUTILS"
confopts="--enable-gold --enable-plugins --disable-werror"
[ -n "`gcc -print-sysroot`" ] && confopts="$confopts --with-sysroot" # match system setting to avoid 'this linker was not configured to use sysroots' error or failure to find libpthread.so
[ -f Makefile ] || run ./configure --prefix="$PATHAUTOPREFIX" $confopts
run make -j"$JOBS"
run make -j"$JOBS" all-gold
run make install
run rm "$PATHAUTOPREFIX/bin/ld"
run cp "$PATHAUTOPREFIX/bin/ld.gold" "$PATHAUTOPREFIX/bin/ld" # replace ld with gold

# build cmake
echo "Building cmake"
cd "$PATHAUTOSRC"
[ -f "$VERSIONCMAKE.tar.gz" ] || run wget "https://cmake.org/files/$VERSIONCMAKEURL/$VERSIONCMAKE.tar.gz"
[ -d "$VERSIONCMAKE" ] || run tar xf "$VERSIONCMAKE.tar.gz"
cd "$VERSIONCMAKE"
[ -f Makefile ] || run ./configure --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install

# build glib, needed by qemu
echo "Building glib"
cd "$PATHAUTOSRC"
[ -f "$VERSIONGLIB.tar.xz" ] || run wget "http://ftp.gnome.org/pub/GNOME/sources/glib/$VERSIONGLIBURL/$VERSIONGLIB.tar.xz"
[ -d "$VERSIONGLIB" ] || run tar xf "$VERSIONGLIB.tar.xz"
cd "$VERSIONGLIB"
[ -f Makefile ] || PKG_CONFIG_PATH="$PATHAUTOPREFIX/lib/pkgconfig" run ./configure --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install

# build pixman, needed by qemu
echo "Building pixman"
cd "$PATHAUTOSRC"
[ -f "$VERSIONPIXMAN.tar.gz" ] || run wget "http://cairographics.org/releases/$VERSIONPIXMAN.tar.gz"
[ -d "$VERSIONPIXMAN" ] || run tar xf "$VERSIONPIXMAN.tar.gz"
cd "$VERSIONPIXMAN"
[ -f Makefile ] || run ./configure --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install

# build qemu with hypermem support
echo "Building qemu with hypermem support"
cd "$PATHAUTOSRC"
[ -d qemu-hypercall ] || run git clone -b hypermem-v2.3.0 https://github.com/vusec/qemu-hypercall.git
cd qemu-hypercall
[ -f config-host.h ] || PKG_CONFIG_PATH="$PATHAUTOPREFIX/lib/pkgconfig" run ./configure --target-list=i386-softmmu,x86_64-softmmu --prefix="$PATHAUTOPREFIX" --enable-hypermem --disable-stack-protector
run make -j"$JOBS"
run make install

# build LLVM
echo "Building llvm-$LLVMVERSION"
LLVMDIR="$PATHAUTOSRC/llvm-$LLVMVERSION"
run mkdir -p "$LLVMDIR"
cd "$LLVMDIR"
[ -d llvm ] || run svn co "http://llvm.org/svn/llvm-project/llvm/tags/$LLVMBRANCH" llvm
[ -d llvm/tools/clang ] || run svn co "http://llvm.org/svn/llvm-project/cfe/tags/$LLVMBRANCH" llvm/tools/clang
if [ "$LLVMVERSION" = 3.3 -a ! -f .autosetup.patched-llvm3.3-objPath-use-after-free.patch ]; then
	cd "$LLVMDIR/llvm"
	run patch -p0 < "$PATHROOT/conf/llvm-patches/llvm3.3-objPath-use-after-free.patch"
	cd "$LLVMDIR"
	touch .autosetup.patched-llvm3.3-objPath-use-after-free.patch
fi
run mkdir -p "$LLVMDIR/llvm-objects" "$PATHLLVM/bin"
cd "$LLVMDIR/llvm-objects"
[ -f Makefile ] || run ../llvm/configure --prefix="$PATHLLVM/bin" --enable-bindings=none --disable-debug-symbols --enable-optimized --enable-assertions --enable-jit --with-binutils-include="$PATHAUTOPREFIX/include"
run make -j"$JOBS"
run make install
run mkdir -p bin/bfd_bin
run ln -f -s "$PATHAUTOPREFIX/bin/ld.bfd" bin/bfd_bin/ld # required to support two-step linking
PATH="$PATHLLVM/bin/bin:$PATH"

# configure llvm-apps
statefile="$PATHAUTOSTATE/llvm-apps-configure-$LLVMVERSIONCONF"
if [ ! -f "$statefile" ]; then
	echo "Configuring llvm-apps"
	cd "$PATHROOT"
	run rm -f "$PATHAUTOSTATE"/llvm-apps-configure-*
	run rm -f common.overrides.*.inc
	clean="y" install_pkgs="n" have_llvm="y" have_di="n" have_dr="n" llvm_version="$LLVMVERSIONCONF" llvm_basedir="$PATHLLVM/.." have_pin="n" have_dune="n" minix_conf="n" ov_PIE="n" ov_dsa="n" ov_gdb="y" ov_segfault="n" run ./configure
	run cp conf/common.overrides.das5.inc .
	touch "$statefile"
fi


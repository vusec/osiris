#!/bin/bash
# build poolalloc for the minix toolchain if needed
# assumes that the gold plugin was build in the object dir obj_llvm.i386
set -e

# Poollalloc commit for which we know our patch works fine.
POOLALLOC_COMMIT="85eae01c5064c5d6d61378deb08a4c155256695c"

download_and_patch_poolalloc() {
		cd $LLVMSRCDIR/projects
		rm -rf poolalloc/
		git clone https://github.com/llvm-mirror/poolalloc.git
		cd $LLVMSRCDIR/projects/poolalloc
		git reset ${POOLALLOC_COMMIT}
		git checkout .
		git apply ${ROOT}/conf/poolalloc-patches/llvm3.4_pa_aa_esc.patch
}


print_info() {
	echo ${NETBSDSRCDIR}
	echo ${LLVMSRCDIR}
	echo ${OBJ_LLVM}
	echo ${OBJ}
	echo ${CROSS_TOOLS}
}


clean_up() {
	rm -rf $LLVMSRCDIR/projects/poolalloc
}


reconfigure_llvm() {
	cd  ${OBJ_LLVM}
	${LLVMSRCDIR}/configure \
    --enable-targets=x86 \
    --with-c-include-dirs=/usr/include/clang-3.4:/usr/include \
    --disable-timestamps \
    --prefix=${OBJ_LLVM} \
    --sysconfdir=/etc/llvm \
    --with-clang-srcdir=${LLVMSRCDIR}/clang \
    --host=i586-elf32-minix \
    --with-binutils-include=${NETBSDSRCDIR}/external/gpl3/binutils/dist/include \
    --disable-debug-symbols \
    --enable-assertions \
    --enable-bindings=none \
    llvm_cv_gnu_make_command=make \
    ac_cv_path_CIRCO="echo circo" \
    ac_cv_path_DOT="echo dot" \
    ac_cv_path_DOTTY="echo dotty" \
    ac_cv_path_FDP="echo fdp" \
    ac_cv_path_NEATO="echo neato" \
    ac_cv_path_TWOPI="echo twopi" \
    ac_cv_path_XDOT="echo xdot" \
    --enable-optimized
}


build_poolalloc() {
	local JOBS=${JOBS:-$(nproc)}
	cd  ${OBJ_LLVM}/projects/poolalloc/
	make -j ${JOBS}
	make install
}

copy_dsa_lib() {
	cd  ${OBJ_LLVM}/projects/poolalloc/
	cp Release+Asserts/lib/LLVMDataStructure.so ${CROSS_TOOLS}/../lib
}


main() {
	print_info
	download_and_patch_poolalloc
	reconfigure_llvm
	build_poolalloc
	copy_dsa_lib
	clean_up
}

main $*

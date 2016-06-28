#/bin/sh

SCRIPT_PATH=$( cd $(dirname $0) ; pwd -P )
ROOT="$SCRIPT_PATH/../../../.."

export LLVMGOLD_OPTFLAGS_EXTRA="-tol=NULLWEFWEFWEFWEFWE"
sudo -v
(
SPECDIR=$ROOT/apps/SPEC_CPU2006
cd $SPECDIR

export CXXFLAGS="-DLTCKPT_CFG_CHECKPOINT_APPROACH=1 -DBITMAP_TYPE=5"
make -C $ROOT/llvm/static/ltckpt clean install
./relink.llvm ltckpt
./build.llvm basicaa ltckpt ltckptbasic
time ( ./clientctl run &> logs/ltckpt_bitmap.log) &> logs/ltckpt_bitmap.total


export CXXFLAGS="-DLTCKPT_CFG_CHECKPOINT_APPROACH=3"
make -C $ROOT/llvm/static/ltckpt clean install
./relink.llvm ltckpt
./build.llvm basicaa ltckpt ltckptbasic
time (./clientctl run &> logs/ltckpt_writelog.log) &> logs/ltckpt_writelog.total


#export CXXFLAGS="-DLTCKPT_CFG_CHECKPOINT_APPROACH=4"
make -C $ROOT/llvm/static/ltckpt install
sudo insmod $ROOT/llvm/lkm/smmap/smmap.ko
./relink.llvm ltckpt
./build.llvm basicaa ltckpt ltckptbasic ltckpt_inline
time ( ./clientctl run &> logs/ltckpt_smmap.log) &> logs/ltckpt_smmap.total
sudo rmmod $ROOT/llvm/lkm/smmap/smmap.ko

./relink.llvm null
export LLVMGOLD_OPTFLAGS_EXTRA=""
./build.llvm basicaa
time ( ./clientctl run &> logs/baseline.log ) &> logs/baseline.total
)

#/bin/sh

SCRIPT_PATH=$( cd $(dirname $0) ; pwd -P )
ROOT="$SCRIPT_PATH/../../../.."

export LLVMGOLD_OPTFLAGS_EXTRA=-tol=main

(
SPECDIR=$ROOT/apps/SPEC_CPU2006
cd $SPECDIR

export CXXFLAGS="-DLTCKPT_CFG_CHECKPOINT_APPROACH=1 -DBITMAP_TYPE=5"
make -C $ROOT/llvm/static/ltckpt install
./relink.llvm ltckpt
./build.llvm basicaa ltckpt ltckptbasic
time ./clientctl run > logs/ltckpt_bitmap.log 2> logs/ltckpt_bitmap.total


export CXXFLAGS="-DLTCKPT_CFG_CHECKPOINT_APPROACH=3 -DLTCKPT_WRITELOG_SWITCHABLE=1"
make -C $ROOT/llvm/static/ltckpt install
./relink.llvm ltckpt
./build.llvm basicaa ltckpt ltckptbasic
time ./clientctl run > logs/ltckpt_writelog.log 2> logs/ltckpt_writelog.total


export CXXFLAGS="-DLTCKPT_CFG_CHECKPOINT_APPROACH=4"
make -C $ROOT/llvm/static/ltckpt install
./relink.llvm ltckpt
./build.llvm basicaa ltckpt ltckptbasic ltckpt_inline
time ./clientctl run > logs/ltckpt_smmap.log 2> logs/ltckpt_smmap.total

./relink.llvm null
time ./clientctl run > logs/baseline.log 2> logs/baseline.total
)

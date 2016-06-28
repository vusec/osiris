#!/bin/bash
./relink.llvm ltckpt
./build.llvm ltckpt ltckptbasic tol=sef_receive_status ltckpt-method=undolog

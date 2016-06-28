#!/bin/sh

# run the ptrace tests. Since we don't have any gdb on minix
# we will settle for test42
cd /usr/tests/minix-posix
./test42
cd -

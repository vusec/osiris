#!/bin/sh

# test vim regression suite
cd /usr/ast/vim
make testclean
make test
cd -

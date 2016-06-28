#!/bin/bash

# test vim regression suite
cd /home/skl/vim
make testclean
make test
cd -


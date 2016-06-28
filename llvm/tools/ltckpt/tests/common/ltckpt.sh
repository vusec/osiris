#!/bin/bash

#
# ltckpt variables
#
LTCKPT_LIBS_ROOT=$ROOT/../../static/ltckpt
LTCKPT_PASS_ROOT=$ROOT/../../passes/ltckpt
LTCKPT_SHARED_LIBS_ROOT=$ROOT/../../shared/ltckpt
SMMAP_LKM_ROOT=$ROOT/../../lkm/smmap
SMMAP_INCLUDE_ROOT=$ROOT/../../include/smmap

LIBS_ROOT=$LTCKPT_LIBS_ROOT
SHARED_LIBS_ROOT=$LTCKPT_SHARED_LIBS_ROOT
SMMAP_KO=$ROOT/../../lkm/smmap/smmap.ko




hput () {
  eval hash"$1"='$2'
}

hget () {
  eval echo '${hash'"$1"'#hash}'
}

hput nginx nginx-0.8.54
hput lighttpd lighttpd-1.4.28
hput httpd httpd-2.2.23
hput vsftpd vsftpd-1.2.1
hput proftpd proftpd-1.3.3e
hput pureftpd pure-ftpd-1.0.36
hput postgresql postgresql-9.0.10
hput bind bind-9.9.3-P1
hput spec SPEC_CPU2006


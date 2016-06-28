#!/bin/sh
set -ex
if [ -f /root/done.test ]; then
    /root/hypermemclient/hyper print "Aborting tests after previous restart"
else
    touch /root/done.test
    cp -r /mnt/hypermemclient /root
    make -C /root/hypermemclient clean all
    cd /usr/tests/minix-posix
    HYPER=/root/hypermemclient/hyper USENETWORK=yes QUICKTEST=yes INSTANTDEATH=yes ./run -t "{{{TESTS}}}" < /dev/console || true # console redirection for test 3
fi
/root/hypermemclient/hyper quit || true
shutdown -pD now
poweroff


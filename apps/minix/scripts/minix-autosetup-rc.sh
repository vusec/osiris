#!/bin/sh
set -ex

# we only care about boot time
case "$1" in
autoboot|start)
    ;;
*)
    exit
    ;;
esac

# set up network, then reboot
if [ ! -f /root/done.netconf ]; then
    netconf < /dev/null
    touch /root/done.netconf
    shutdown -rD now
    reboot
fi

# set up package management and SSH
if [ ! -f /root/done.ssh ]; then
    cd /root
if false; then
    ftp http://www.minix3.org/pkgsrc/packages/3.3.0/i386/All/pkg_install-20130902nb1.tgz
    cd /
    tar xzf /root/pkg_install-20130902nb1.tgz
    rm /root/pkg_install-20130902nb1.tgz
    rm /+*
    pkg_add http://www.minix3.org/pkgsrc/packages/3.3.0/i386/All/pkgin-0.6.4nb5.tgz
    yes | pkgin update
    yes | pkgin install openssh
fi
    touch /root/done.ssh
    shutdown -pD now
    poweroff
fi

# allow an additional setup script to be supplied on a second disk image
if mount /dev/c0d1 /mnt; then
    /mnt/boot.sh
fi


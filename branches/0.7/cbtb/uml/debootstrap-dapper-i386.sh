#!/bin/sh -x

#
# Make a tarball of the root system of a basic Ubunutu/dapper system,
# for use in preparing UML root partition images
#

# $Id: debootstrap-dapper-i386.sh 1279 2008-01-15 04:44:35Z drake.diedrich $
# Copyright 2007 Google Inc.
# Author: Drake Diedrich <dld@google.com>
# License: GPLv2

set -e

pushd ../..
SRC=$PWD
BUILD_DIR="$SRC/build"
SVNREV=`awk '/^[0-9]+$/ { print $1; }' SVNREV || svnversion | tr [A-Z] [a-z] || svn info zumastor | grep ^Revision:  | cut -d\  -f2`
popd

ARCH=i386
DEBOOTSTRAP=/usr/sbin/debootstrap
SUDO=sudo

# the testenv setup, see cbtb/host-setup/
VIRTHOST=192.168.23.1
[ -x /etc/default/testenv ] && . /etc/default/testenv

rootdir=`mktemp -d`
stagefile=`mktemp`

TESTDEPENDENCIES=openssh-server,cron,postfix,dmsetup
BUILDDEPEDENCIES=build-essential,lvm2,fakeroot,kernel-package,devscripts,subversion,debhelper,libpopt-dev,zlib1g-dev,debhelper,bzip2
EXCLUDE=alsa-base,alsa-utils,eject,console-data,libasound2,linux-sound-base,memtest86,mii-diag,module-init-tools,wireless-tools,wpasupplicant,pcmciautils

# Basic dapper software installed into $rootdir
$SUDO $DEBOOTSTRAP --arch $ARCH \
  --include=$TESTDEPENDENCIES,$BUILDDEPEDENCIES \
  --exclude=$EXCLUDE \
  dapper $rootdir http://$VIRTHOST/ubuntu

# create and authorize a local ssh key for root
$SUDO mkdir -p $rootdir/root/.ssh
$SUDO ssh-keygen -q -P '' -N '' -t dsa -f $rootdir/root/.ssh/id_dsa
$SUDO cat $rootdir/root/.ssh/id_dsa.pub >$stagefile

# Authorize the user to ssh into this virtual instance
cat ~/.ssh/*.pub >>$stagefile
$SUDO mv $stagefile $rootdir/root/.ssh/authorized_keys
$SUDO chown root:root $rootdir/root/.ssh/authorized_keys
$SUDO chmod 600 $rootdir/root/.ssh/authorized_keys


echo unassigned >$stagefile
$SUDO mv $stagefile $rootdir/etc/hostname


cat >$stagefile <<EOF
# Used by ifup(8) and ifdown(8). See the interfaces(5) manpage or
# /usr/share/doc/ifupdown/examples for more information.

# The loopback network interface
auto lo
iface lo inet loopback

# The primary network interface
auto eth0
iface eth0 inet dhcp
EOF
$SUDO mv $stagefile $rootdir/etc/network/interfaces

# clean up the downloaded packages to conserve space
$SUDO rm -f $rootdir/var/cache/apt/archives/*.deb

# create and mount an empty ext3 filesystem
ext3dev=`mktemp`
ext3dir=`mktemp -d`
dd if=/dev/zero bs=1M seek=1024 count=0 of=$ext3dev
$SUDO mkfs.ext3 -F $ext3dev
$SUDO mount -oloop,rw $ext3dev $ext3dir

# tar it up the new filesystem onto the new ext3 device
$SUDO tar cf - -C $rootdir . | \
  $SUDO tar xf - -C $ext3dir

# create symlinks from sd* to the ubd* devices
$SUDO ln -s /dev/ubda $ext3dir/dev/sda
$SUDO ln -s /dev/ubdb $ext3dir/dev/sdb
$SUDO ln -s /dev/ubdc $ext3dir/dev/sdc
$SUDO ln -s /dev/ubdd $ext3dir/dev/sdd
cat >$stagefile <<EOF
KERNEL=="ubda",                 SYMLINK+="sda"
KERNEL=="ubdb",                 SYMLINK+="sdb"
KERNEL=="ubdc",                 SYMLINK+="sdc"
KERNEL=="ubdd",                 SYMLINK+="sdd"
EOF
$SUDO mv $stagefile $ext3dir/etc/udev/rules.d/61-ubd-symlinks.rules

$SUDO umount $ext3dir

# copy it back into the build/ directory, avoiding NFS issues and
# potential races.
$SUDO chown $USER $ext3dev
mv $ext3dev $BUILD_DIR/dapper-$ARCH-r$SVNREV.ext3
ln -sf dapper-$ARCH-r$SVNREV.ext3 $BUILD_DIR/dapper-$ARCH.ext3 

# cleanup
$SUDO rm -rf $rootdir
$SUDO rmdir $ext3dir

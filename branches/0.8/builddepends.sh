#!/bin/sh -x
#
# $Id: builddepends.sh 1571 2008-04-22 01:04:33Z williamanowak $
#
# Install build dependencies if they are missing on the qemu build machine

apt-get update

# I wish these worked
pushd zumastor
apt-get build-dep zumastor
popd

pushd ddsnap
apt-get build-dep ddsnap
popd

# manually install all build dependencies, including kernel build
aptitude -y install \
  fakeroot kernel-package devscripts subversion \
  debhelper libpopt-dev zlib1g-dev \
  debhelper bzip2, rsync


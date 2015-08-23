#!/bin/sh -x
#
# $Id: builddepends.sh 1656 2008-05-09 19:09:52Z williamanowak $
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
apt-get -q -y install \
  fakeroot kernel-package devscripts subversion \
  debhelper libpopt-dev zlib1g-dev \
  debhelper bzip2 rsync build-essential


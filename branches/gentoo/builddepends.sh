#!/bin/sh -x
#
# $Id: builddepends.sh 844 2007-11-06 01:08:05Z drake.diedrich $
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
  debhelper bzip2


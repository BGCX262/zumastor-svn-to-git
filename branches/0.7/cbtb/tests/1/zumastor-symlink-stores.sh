#!/bin/sh -x
#
# $Id$
#
# Test that zumastor initialize works even when the origin and
# snapshot storage specified are just symlinks to the real devices.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# The required sizes of the sdb and sdc devices in M.
# Read only by the test harness.
HDBSIZE=32
HDCSIZE=32

# Terminate test in 40 minutes.  Read by test harness.
TIMEOUT=600

# necessary at the moment, looks like a zumastor bug
SLEEP=5

echo "1..3"

apt-get update
aptitude install -y e2fsprogs

mount
ls -l /dev/sdb /dev/sdc
ln -s /dev/sdb /dev/originstor
ln -s /dev/sdc /dev/snapstor2
ln -s /dev/snapstor2 /dev/snapstor

zumastor define volume testvol /dev/originstor /dev/snapstor --initialize --mountopts nouuid
mkfs.ext3 -F /dev/mapper/testvol
zumastor define master testvol; zumastor define schedule testvol -h 24 -d 7

echo ok 1 - testvol set up

sync
zumastor snapshot testvol hourly 
sleep $SLEEP

date >> /var/run/zumastor/mount/testvol/testfile
sleep $SLEEP

if [ ! -f /var/run/zumastor/snapshot/testvol/hourly.0/testfile ] ; then
  echo "ok 3 - testfile not present in first snapshot"
else
  ls -lR /var/run/zumastor/
  echo "not ok 3 - testfile not present in first snapshot"
  exit 3
fi

exit 0

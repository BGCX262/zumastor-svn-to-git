#!/bin/sh -x
#
# $Id: snapshot-ddsnap.sh 789 2007-10-11 11:53:50Z drake.diedrich $
#
# Use ddsnap directly to create a snapshot, modify the origin, and create
# another snapshot.  Verify that checksums change and remain correct.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

rc=0


# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=1200

# necessary at the moment, ddsnap just sends requests and doesn't wait
SLEEP=10

echo "1..11"

lvcreate --size 4m -n test sysvg
lvcreate --size 8m -n test_snap sysvg
dd if=/dev/zero bs=32k count=128 of=/dev/sysvg/test
dd if=/dev/zero bs=32k count=256 of=/dev/sysvg/test_snap
echo ok 1 - lvm set up

ddsnap initialize /dev/sysvg/test_snap /dev/sysvg/test
echo ok 2 - ddsnap initialize

ddsnap agent /tmp/control
echo ok 3 - ddsnap agent

ddsnap server /dev/sysvg/test_snap /dev/sysvg/test /tmp/control /tmp/server
echo ok 4 - ddsnap server

size=`ddsnap status /tmp/server --size` 
echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test /tmp/control -1 | dmsetup create testvol
echo ok 5 - create testvol

ddsnap create /tmp/server 0
echo ok 6 - ddsnap create [snapshot]

sleep $SLEEP
echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test /tmp/control 0 | dmsetup create testvol\(0\)
echo ok 7 - create testvol\(0\)

hash=`md5sum </dev/mapper/testvol`
hash0=`md5sum </dev/mapper/testvol\(0\)`
if [ "$hash" != "$hash0" ] ; then
  echo -e "not "
  rc=8
fi
echo ok 8 - testvol==testvol\(0\)


dd if=/dev/urandom bs=32k count=128 of=/dev/mapper/testvol  
hash02=`md5sum </dev/mapper/testvol\(0\)`
if [ "$hash0" != "$hash02" ] ; then
  echo -e "not "
  rc=9
fi
echo ok 9 - testvol\(0\)==testvol\(0\) snapshot stayed unchanged
  

ddsnap create /tmp/server 2
echo ok 10 - ddsnap create [snapshot 2]

sleep $SLEEP
echo 0 $size ddsnap /dev/sysvg/test_snap /dev/sysvg/test /tmp/control 2 | dmsetup create testvol\(2\)
echo ok 11 - create testvol\(2\)


hash=`md5sum </dev/mapper/testvol`
hash2=`md5sum </dev/mapper/testvol\(2\)`
if [ "$hash" != "$hash2" ] ; then
  echo -e "not "
  rc=11
fi
echo ok 12 - testvol==testvol\(2\) new snapshot correct

exit $rc


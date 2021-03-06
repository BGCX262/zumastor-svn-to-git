#!/bin/sh -x
#
# $Id: snapshot-ddsnap.sh 1234 2008-01-05 05:24:09Z drake.diedrich $
#
# Use ddsnap directly to create a snapshot, modify the origin, and create
# another snapshot.  Verify that checksums change and remain correct.
#
# Copyright 2007 Google Inc.  All rights reserved
# Author: Drake Diedrich (dld@google.com)


set -e

# The required sizes of the sdb and sdc devices in M.  2045G
# Read only by the test harness.
# 8M for the snapsho storeand 4M for the origin.
HDBSIZE=8
HDCSIZE=4

rc=0


# Terminate test in 10 minutes.  Read by test harness.
TIMEOUT=1200

# necessary at the moment, ddsnap just sends requests and doesn't wait
SLEEP=10

echo "1..11"

ddsnap initialize /dev/sdb  /dev/sdc
echo ok 2 - ddsnap initialize

ddsnap agent /tmp/control
echo ok 3 - ddsnap agent

ddsnap server /dev/sdb /dev/sdc /tmp/control /tmp/server
echo ok 4 - ddsnap server

size=`ddsnap status /tmp/server --size` 
echo 0 $size ddsnap /dev/sdb /dev/sdc /tmp/control -1 | dmsetup create testvol
echo ok 5 - create testvol

ddsnap create /tmp/server 0
echo ok 6 - ddsnap create [snapshot]

sleep $SLEEP
echo 0 $size ddsnap /dev/sdb /dev/sdc /tmp/control 0 | dmsetup create testvol\(0\)
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
echo 0 $size ddsnap /dev/sdb /dev/sdc /tmp/control 2 | dmsetup create testvol\(2\)
echo ok 11 - create testvol\(2\)


hash=`md5sum </dev/mapper/testvol`
hash2=`md5sum </dev/mapper/testvol\(2\)`
if [ "$hash" != "$hash2" ] ; then
  echo -e "not "
  rc=11
fi
echo ok 12 - testvol==testvol\(2\) new snapshot correct

exit $rc


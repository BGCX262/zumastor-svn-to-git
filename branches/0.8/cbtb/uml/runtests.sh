#!/bin/bash

# $Id: runtests.sh 1605 2008-04-29 01:27:34Z williamanowak $
#
# Tests run without privileges, other than those granted by tunbr, which
# must be installed first, along with interfaces-bridge.sh, proxy.sh, and
# dnsmasq.sh as described in cbtb/host-setup/README.
# Defaults to testing on dapper/i386 installed template images, DIST=etch
# and ARCH=i386 may also currently be specified.
#

set -e

smoketests1="snapshot-zumastor-xfs-2045G.sh"
smoketests2="replication-zumastor.sh"
  
if [ "x$ARCH" = "x" ] ; then
  ARCH=`dpkg --print-arch || echo i386`
fi

if [ "x$DIST" = "x" ] ; then
  DIST=etch
fi

usage() {
  cat <<EOF
$0 [--all]

--all   Run all tests in cbtb/tests/1/ and cbtb/tests/2/, rather than just
        the smoketests: $smoketests1 $smoketests2.

Environment variables:
ARCH - the CPU/ABI architecture to test.     currently: $ARCH
DIST - the distribution release to test on.  currently: $DIST
EOF
}

testparent=../tests

tests1=$smoketests1
tests2=$smoketests2

case $1 in
--all)
  tests1=`cd $testparent/1 && echo *.sh`
  tests2=`cd $testparent/2 && echo *.sh`
  tests3=`cd $testparent/3 && echo *.sh`
  shift
  ;;

--help|-h)
  usage
  exit 1
  ;;
esac
  
summary=`mktemp`

for test in $tests1
do
  if DIST=$DIST ARCH=$ARCH time tunbr ./test-zuma-uml.sh $testparent/1/$test
  then
    echo PASS $test >>$summary
  else
    echo FAIL $test >>$summary
  fi
done

for test in $tests2
do
  if  DIST=$DIST ARCH=$ARCH time tunbr tunbr ./test-zuma-uml.sh $testparent/2/$test
  then
    echo PASS $test >>$summary
  else
    echo FAIL $test >>$summary
  fi
done

for test in $tests3
do
  if  DIST=$DIST ARCH=$ARCH time tunbr tunbr tunbr ./test-zuma-uml.sh $testparent/3/$test
  then
    echo PASS $test >>$summary
  else
    echo FAIL $test >>$summary
  fi
done

echo
cat $summary
rm -f $summary

# Kill any testers still hanging around
pgrep test-zuma-uml|xargs kill -9

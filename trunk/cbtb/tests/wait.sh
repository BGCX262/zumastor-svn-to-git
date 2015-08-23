#!/bin/sh -x

# $Id: wait.sh 1004 2007-11-30 16:05:47Z drake.diedrich $

# simple paramter script for zuma-test-dapper-i386.sh script
# will wait until return is pressed

set -e

env
hostname
ifconfig eth0 || true
ifconfig eth1 || true

echo "slogin root@ the IP address above to work interactively"
echo "Press return to end the session"

read ret

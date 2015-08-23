#!/bin/sh -x

# $Id: wait.sh 632 2007-08-30 01:36:33Z drake.diedrich $

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

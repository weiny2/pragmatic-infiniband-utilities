#!/bin/bash

declare -r prog=${0##*/}
declare -r usage="\
[-hp] -C <channel_adaptor> -P <port>\n\
   Check the QoS settings for proper router set up\n\
   -h this help message\n\
   --print, -p print the config line based on the router guids found\n\
   --Ca, -C <channel_adaptor> specify different CA to check\n\
   --Port, -P <port> specify different port to check\n\
\n"

die () {
    echo -e "$prog: $@" >&2
    exit 1
}

declare -r long_opts="Ca:,Port:,print,help"
declare -r short_opts="C:P:ph"
declare -r getopt="/usr/bin/getopt -u -q -a"

GETOPT=`$getopt -o $short_opts -l $long_opts -n $prog -- $@`
if [ $? != 0 ] ; then
    die "$usage"
fi

CA=""
Port=""
print="no"

eval set -- "$GETOPT"
while true; do
    case "$1" in
        -C|--Ca)	CA=$2 ; shift 2 ;;
        -P|--Port)	Port=$2 ; shift 2 ;;
        -p|--print)	print="yes" ; shift 1 ;;
        -h|--help)	die "$usage"     ;;
	--)		shift ; break    ;;
	*)		die "$usage"     ;;
    esac
done

if [ "$CA" == "" ] || [ "$Port" == "" ] ; then
    die "$usage"
fi

if [ "$print" == "yes" ]; then
   guid_list=`pdsh -g router /usr/sbin/ibstat $CA $Port | grep "Port GUID" | awk '{print $4}'`
   echo -n "any, target-port-guid "
   first="yes"
   for guid in $guid_list; do
      if [ "$first" == "yes" ]; then
         echo -n "$guid"
         first="no"
      else
         echo -n ", $guid"
      fi
   done
   echo " :1 # Lustre on SL 1"
   exit 0
fi

rc=0
node_list=`pdsh -g router /usr/sbin/ibstat $CA $Port | grep "Port GUID" | awk '{print $1 $4}'`
for node in $node_list; do
   guid=`echo $node | sed -e 's/:/ /' | awk '{print $2}'`
   name=`echo $node | sed -e 's/:/ /' | awk '{print $1}'`
   grep -l $guid /etc/opensm/qos-policy.conf > /dev/null
   if [ "$?" == "1" ]; then
      echo "ERROR: GUID: $guid ($name) is not configured in QoS policy file"
      rc=1
   fi
done

exit $rc

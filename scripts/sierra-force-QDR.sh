#!/bin/bash
# BEGIN_ICS_COPYRIGHT8 ****************************************
#                              NOTICE
#
#                 COPYRIGHT 2008 QLOGIC CORPORATION
#                       ALL RIGHTS RESERVED
#
#  This computer program is CONFIDENTIAL and contains TRADE SECRETS of
#  QLOGIC CORPORATION.  The receipt or possession of this program does
#  not convey any rights to reproduce or disclose its contents, or to
#  manufacture, use, or sell anything that it may describe, in whole or
#  in part, without the specific written consent of QLOGIC CORPORATION.
#  Any reproduction of this program without the express written consent
#  of QLOGIC CORPORATION is a violation of the copyright laws and may
#  subject you to civil liability and criminal prosecution.
# END_ICS_COPYRIGHT8   ****************************************

# [ICS VERSION STRING: @(#) ./bin/iba_portconfig.sh 6_0_1_3_3 [10/19/10 14:22]

# ibportstate arguments
path="-C qib0 -D 0 1"
# Below will force QDR
ARGS="speed 4"		# additional arguments to use for all HCAs/PORTs
# Other examples: (uncomment exactly 1)
#ARGS="speed 1"		# force SDR
#ARGS="speed 2"		# force DDR
#ARGS="speed 3"		# negotiate SDR or DDR
#ARGS="speed 7"		# negotiate SDR, DDR or QDR

# ibportstate     use ibportstate to force link speed
#
# chkconfig: 35 16 84
# description: Force HCA port speed
# processname:
### BEGIN INIT INFO
# Provides:       ibportstate
# Required-Start: openibd
# Required-Stop:
# Default-Start:  2 3 5
# Default-Stop:	  0 1 2
# Description:    use ibportstate to force link speed
### END INIT INFO

PATH=/bin:/sbin:/usr/bin:/usr/sbin

# just in case no functions script
echo_success() { echo "[  OK  ]"; }
echo_failure() { echo "[FAILED]"; }

my_rc_status_v()
{
	res=$?
	if [ $res -eq 0 ]
	then
		echo_success
	else
		echo_failure
	fi
	echo
	my_rc_status_all=$(($my_rc_status_all || $res))
}

if [ -f /etc/init.d/functions ]; then
    . /etc/init.d/functions
elif [ -f /etc/rc.d/init.d/functions ] ; then
    . /etc/rc.d/init.d/functions
elif [ -s /etc/rc.status ]; then
	. /etc/rc.status
	rc_reset
	my_rc_status_v()
	{
		res=$?
		[ $res -eq 0 ] || false
		rc_status -v
		my_rc_status_all=$(($my_rc_status_all || $res))
	}
fi

my_rc_status_all=0
my_rc_exit()     { exit $my_rc_status_all; }
my_rc_status()   { my_rc_status_all=$(($my_rc_status_all || $?)); }

cmd=$1

Usage()
{
	echo "Usage: $0 [start|stop|restart]" >&2
	echo " start       - configure port" >&2
	echo " stop        - Noop" >&2
	echo " restart     - restart (eg. stop then start) the Port config" >&2
	echo " status      - show present port configuration" >&2
	exit 2
}

start()
{
	local res hca port hcaport

	echo -n "Starting ibportstate: "
	res=0
	echo -n "Setting $path: "
 	/usr/sbin/ibportstate $path $ARGS >/dev/null
	res=$(($res || $?))
	[ $res -eq 0 ] || false
	my_rc_status_v
 	/usr/sbin/ibportstate $path reset >/dev/null
	if [ $res -eq 0 ]
	then
		touch /var/lock/subsys/ibportstate
	fi
	return $res
}

stop()
{
	local res

	echo -n "Stopping ibportstate: "
	# nothing to do
	res=0
	my_rc_status_v
	rm -f /var/lock/subsys/iba_portconfig
	return $res
}

status()
{
	local res hca port hcaport

	echo -n "Checking port state: "
	res=0
	echo -n "HCA $path "
	/usr/sbin/ibportstate $path
	res=$(($res || $?))
	[ $res -eq 0 ] || false
	my_rc_status_v
	return $res
}

case "$cmd" in
	start)
		start;;
	stop)
		stop;;
	restart)
		stop; start;;
	status)
		status;;
	*)
		Usage;;
esac

my_rc_exit

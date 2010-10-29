#!/bin/bash
#
# Renice all the mad threads in the system.
#

nice_val=-10
rc=1

if [ "$1" == "-h" ]; then
	echo "ibmad_renice.sh [-h] [new_prio]"
	echo "   Renice all the mad kernel threads in the system."
	echo "   -h display this help"
	echo "   [new_prio] specify an optional priority (default: $nice_val)"
	echo "   returns 0 if any threads were reniced, 1 if not"
	exit 0
fi

if [ "$1" != "" ]; then
	nice_val=$1
fi

for id in `seq 0 10`; do
	pids=`/sbin/pidof ib_mad$id`
	if [ "$pids" != "" ]; then
		for pid in $pids; do
			renice $nice_val $pid
			rc=0
		done
	fi
done

exit $rc


#!/bin/bash

# Get the list of grove node GUID's for possible use with
# --guid_routing_order_file OpenSM option

pdsh -w egrove[1-768] /usr/sbin/ibstat mlx4_0 | grep "Port GUID" | sed "s/e\(grove[0-9]*\):.*: \([0-9a-fx]*\)/\2  \1/" > grove-guid-list


#!/bin/sh
#
# file: iFI-server.sh
#
# use brute force to gather information from the management node
#

# timestamped directory name
TIME_STAMP="$(date +%G%b%d-%H%M%S)"
MGR_NODE=`hostname`
TGZ_ROOT=$MGR_NODE-$TIME_STAMP
mkdir -pv $TGZ_ROOT
pushd $TGZ_ROOT

#  collect the output from some commands
ibnetdiscover > ibnetdiscover.out
iblinkinfo -l > iblinkinfo.out

#
saquery -N > saqueryNode.out
saquery -p > saqueryPath.out
saquery -g > saqueryGroup.out

#
# get a copy of the log file (could be long!)
cp /var/log/opensm.log .

#
# get copies of the static files, should not change much
cp /etc/opensm/ib-node-name-map .
cp /etc/opensm/prefix-routes.conf .
cp /etc/opensm/opensm.conf .

/var/cache/opensm/opensm.opts > opensm.opts
/var/cache/opensm/guid2lid > guid2lid

#UpHosts=`whatsup -u`
#pdsh -w $UpHosts ps -ef | grep opensm
#
#UpHosts=`whatsup -u`
#pdsh -w $UpHosts /sbin/lspci | grep InfiniBand

opensmskummeequery -R -S > opensmskummeequery.out

popd # $TGZ_ROOT

tar czf $TGZ_ROOT.tgz ./$TGZ_ROOT


#!/bin/bash
#################################################################################
#
#  Copyright (C) 2012 Lawrence Livermore National Security
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Ira Weiny weiny2@llnl.gov
#  UCRL-CODE-235440
#
#  This file is part of pragmatic-infiniband-tools (PIU), useful tools to manage
#  Infiniband Clusters.
#  For details, see http://www.llnl.gov/linux/.
#
#  PIU is free software; you can redistribute it
#  and/or modify it under the terms of the GNU General Public License as
#  published by the Free Software Foundation; either version 2 of the License,
#  or (at your option) any later version.
#
#  PIU is distributed in the hope that it will be
#  useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
#  Public License for more details.
#
#  You should have received a copy of the GNU General Public License along with
#  PIU; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
#
#################################################################################

interval=60

grove_switches=`cat grove_switches`
ion_switches=`cat ion_switches`
spine_switches=`cat spine_switches`
all_switches=`cat all_switches`

basedir=`pwd`
dts=`date +%F_%X`
dir="$basedir/grove-profile-$dts"

mkdir -p $dir

cd ..

echo -n "Scanning Switches: "
./profile-switches.pl -i $interval -L $all_switches -d $dir/all_data -t

echo -n "Sorting Grove Switches: "
./profile-switches.pl -L $grove_switches -r $dir/all_data/rawdata -d $dir/grove_data -t
echo -n "Sorting ION Switches: "
./profile-switches.pl -L $ion_switches -r $dir/all_data/rawdata -d $dir/ion_data -t
echo -n "Sorting Spine Switches: "
./profile-switches.pl -L $spine_switches -r $dir/all_data/rawdata -d $dir/spine_data -t


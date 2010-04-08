#!/bin/bash
#
#  Copyright (C) 2008 Lawrence Livermore National Security
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Ira Weiny weiny2@llnl.gov
#  UCRL-CODE-235440
#  
#  This file is part of pragmatic-infiniband-tools (PIU), usefull tools to manage
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

tidy_cmd="perltidy -pt=2 -sbt=2 -bt=2 -nsfs -b -t -nola -ce -sbl -nbbc"

argv0=`basename $0`
scripts_dir=`dirname $0`/scripts

if [ "$1" == "-h" ]; then
   echo "$argv0 [-h]"
   echo "   Run perltidy on all perl scripts and modules in the scripts directory"
   exit 1
fi

cd $scripts_dir

for file in *.pl ; do
   echo "tidy : $scripts_dir/$file"
   $tidy_cmd $file
done

for file in *.pm ; do
   echo "tidy : $scripts_dir/$file"
   $tidy_cmd $file
done

exit 0

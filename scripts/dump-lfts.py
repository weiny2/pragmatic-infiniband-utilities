#!/usr/bin/python
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

from threading import Thread
from threading import Lock
import subprocess
import sys
import re

nthreads = 32
threads = []
final_output = {}
output_lock = Lock()

def ibroute(lid_list):
	global final_output
	global output_lock
	for lid in lid_list:
		p = subprocess.Popen(["ibroute", str(lid)],
					stdout=subprocess.PIPE)
		out, err = p.communicate()
		sys.stderr.write (str(lid) + ", ")
		output_lock.acquire()
		final_output[lid] = out
		output_lock.release()


# main

lids = []

# get the list of lids from ibswitches
p = subprocess.Popen(["ibswitches",], stdout=subprocess.PIPE)
ibswout, err = p.communicate()

lines = ibswout.split('\n')
for line in lines:
	m = re.search('^.* lid ([0-9a-f]*) lmc .*$', line)
	if m:
		lids.append(m.group(1))

# testing array
#lids = [220, 55, 429, 551, 554, 561, 1165, 1168]
#lids = [220, 55]

if len(lids) > nthreads:
	chunk_size = (len(lids)/nthreads)+1
else:
	chunk_size = 1

sys.stderr.write ("lids length " +str(len(lids))+"\n")
sys.stderr.write ("chunk_size " +str(chunk_size)+"\n")
sys.stderr.write ("dump routes for switch lid: ")
for i in list(xrange(0, len(lids), chunk_size)):
	t = Thread(target=ibroute, args=(lids[i:i+chunk_size],))
	t.start()
	threads.append(t)

while len(threads):
	t = threads.pop()
	t.join()

# output these in the same order dump_lfts.sh would for consistency with 
# the original tool
for lid in lids:
	sys.stdout.write(final_output[lid])


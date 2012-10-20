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
import getopt

final_output = {}
output_lock = Lock()

def usage():
	print ("Usage: "+sys.argv[0]+" [h] -c <cluster> -l <lidlist>")

def ibroute(lid_list, cluster):
	global final_output
	global output_lock
	for lid in lid_list:
		# run ibroute
		p = subprocess.Popen(["ibroute", str(lid)],
					stdout=subprocess.PIPE)
		out, err = p.communicate()

		# find balance for lids with cluster name
		portcnt = {}
		for line in out.split('\n'):
			m = re.search("^.*\s([0-9a-fA-Fx]*)\s:\s.*'("+cluster+").*",
					line)
			if m:
				port = m.group(1)
				if port in portcnt:
					portcnt[port] += 1
				else:
					portcnt[port] = 1

		# store output
		output_lock.acquire()
		final_output[lid] = portcnt
		output_lock.release()

		# for prgress reporting
		sys.stderr.write (str(lid) + ", ")



def main():
	cluster = ""
	swlids = []
	try:
		optlist, ibccq_args =	getopt.getopt(sys.argv[1:], "hc:l:")
	except getopt.GetoptError as err:
		print (err)
		usage()
		sys.exit(1)

	for a, o in optlist:
		if a == "-c":
			cluster = o
		if a == "-l":
			swlids = str.split(o,",")
		if a == "-h":
			usage()
			sys.exit(0)

	# Parameter checking
	if cluster == "":
		sys.stderr.write("Must specify cluster\n")
		usage()
		sys.exit(1)

	if len(swlids) == 0:
		print "Must specify a lid list"
		usage()
		sys.exit(1)

	# get ibroute data
	nthreads = 32 # default
	threads = []

	try:
		import multiprocessing
		nthreads = multiprocessing.cpu_count()
	except (ImportError, NotImplementedError):
		nthreads = 32

	# testing arrays
	#lids = [220, 55, 429, 551, 554, 561, 1165, 1168]
	#lids = [220, 55]

	# crank up thread engine on lids
	if len(swlids) > nthreads:
		chunk_size = (len(swlids)/nthreads)+1
	else:
		chunk_size = 1

	sys.stderr.write ("nthreads " +str(nthreads)+"\n")
	sys.stderr.write ("lids length " +str(len(swlids))+"\n")
	sys.stderr.write ("chunk_size " +str(chunk_size)+"\n")
	sys.stderr.write ("dump routes for switch lid: ")
	for i in list(xrange(0, len(swlids), chunk_size)):
		t = Thread(target=ibroute, args=(swlids[i:i+chunk_size],cluster))
		t.start()
		threads.append(t)
	while len(threads):
		t = threads.pop()
		t.join()
	sys.stderr.write ("\n")

	for lid in swlids:
		sys.stdout.write ("SW "+lid+": ")
		print final_output[lid]

if __name__ == "__main__":
	sys.exit(main())


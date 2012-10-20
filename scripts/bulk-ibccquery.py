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
	print ("Usage: "+sys.argv[0]+" [h] -l <lidlist> <ibccquery options>")

def ibccconfig(lid_list, option_list):
	global final_output
	global output_lock
	for lid in lid_list:
		sys.stderr.write (str(lid) + ", ")
		# build up the command for Popen
		cmd_list = ["ibccquery"]
		for o in option_list:
			cmd_list.append(o)
		cmd_list.append(str(lid))
		#print "   command: "+str(cmd_list)
		p = subprocess.Popen(cmd_list, stdout=subprocess.PIPE)
		out, err = p.communicate()
		output_lock.acquire()
		final_output[lid] = out
		output_lock.release()


def main():
	lids = []

	try:
		optlist, ibccq_args =	getopt.getopt(sys.argv[1:], "hl:")
	except getopt.GetoptError as err:
		print (err)
		usage()
		sys.exit(1)

	for a, o in optlist:
		if a == "-l":
			lids = str.split(o,",")
		if a == "-h":
			usage()
			sys.exit(0)

	if len(lids) == 0:
		print "Must specify a lid list"
		usage()
		sys.exit(1)

	nthreads = 32 # default
	threads = []

	try:
		import multiprocessing
		nthreads = multiprocessing.cpu_count()
	except (ImportError, NotImplementedError):
		nthreads = 32

	print nthreads

	if len(lids) > nthreads:
		chunk_size = (len(lids)/nthreads)+1
	else:
		chunk_size = 1

	sys.stderr.write ("nthreads " +str(nthreads)+"\n")
	sys.stderr.write ("lids length " +str(len(lids))+"\n")
	sys.stderr.write ("chunk_size " +str(chunk_size)+"\n")
	sys.stderr.write ("Getting CC attribute for lid: ")
	for i in list(xrange(0, len(lids), chunk_size)):
		t = Thread(target=ibccconfig,
				args=(lids[i:i+chunk_size], ibccq_args))
		t.start()
		threads.append(t)
	while len(threads):
		t = threads.pop()
		t.join()
	sys.stderr.write ("\n")

	# Output these sequentially for the user.
	for lid in lids:
		sys.stdout.write(final_output[lid])

	sys.exit(0)

if __name__ == "__main__":
	main()


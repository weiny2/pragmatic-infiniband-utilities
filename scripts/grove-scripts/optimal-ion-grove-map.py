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

class Switch:
	def __init__(self, lid):
		self.lid = lid
		self.uplinkcnt = {}
		self.groveports = {}
		self.ionports = {}
		self.uplink_port = {}

	def __str__(self):
		rc = ""
		for port in sorted(self.groveports.keys()):
			rc += "   UP "+str(port)+": "
			for node in self.groveports[port]:
				rc += node
				rc += ","
			rc += "\n"
		for port in sorted(self.ionports.keys()):
			rc += "   Down "+str(port)+": "
			for node in self.ionports[port]:
				rc += node
				rc += ","
			rc += "\n"
		return self.uplinkcnt.__str__()+"\n"+rc

	def add_uplink(self, port, node):
		if port in self.uplinkcnt:
			self.uplinkcnt[port] += 1
		else:
			self.uplinkcnt[port] = 1
		if port in self.groveports:
			self.groveports[port].append(node)
		else:
			self.groveports[port] = []
			self.groveports[port].append(node)
		self.uplink_port[node] = port

	def get_uplink_port(self, node):
		return self.uplink_port[node]

	def add_downlink(self, port, node):
		if port in self.ionports:
			self.ionports[port].append(node)
		else:
			self.ionports[port] = []
			self.ionports[port].append(node)

	def is_edge_for_ION(self, ion):
		for port in self.ionports:
			if ion in self.ionports[port]:
				return 1
		return 0


def generate_optimal_match(switch_map):

	# with 768 streams to 385 grove nodes we have to allow 2 streams to each grove node.
	used_grove_nodes = {}

	for i in xrange(1, 385):
		used_grove_nodes["grove"+str(i)] = 0

	# mark the upper half of the grove nodes as used so we don't route to them.
	for i in xrange(385, 769):
		used_grove_nodes["grove"+str(i)] = 2

	#print "ION to grove map"
	# for each switch
	for lid in switch_map.keys():
		cnt = 1
		balance_cnt = 0

		print "# SW "+str(lid)
		port_cnt = {}
		for port in switch_map[lid].groveports.keys():
			port_cnt[port] = 0

		# for each ion
		for ionport in switch_map[lid].ionports.keys():
			found = 0
			# find a grove node based on "free" uplinks
			for port in sorted(switch_map[lid].groveports.keys()):
				if port_cnt[port] <= balance_cnt:
					for grove in switch_map[lid].groveports[port]:
						if used_grove_nodes[grove] < 2:
							print str(switch_map[lid].ionports[ionport][0])+", "+grove
							used_grove_nodes[grove] += 1
							found = 1
							port_cnt[port] += 1
							break;
				if found:
					break;
			if found == 0:
				print "ERROR: Failed to find uplink for " + str(switch_map[lid].ionports[ionport][0])

			balance_cnt = int(cnt/6)
			cnt += 1

		sys.stdout.write("# ")
		print port_cnt


switch_map = {}
output_lock = Lock()

def ibroute(lid_list, cluster, cluster2):
	global switch_map
	global output_lock
	for lid in lid_list:
		sw = Switch(lid)
		# run ibroute
		p = subprocess.Popen(["ibroute", str(lid)],
					stdout=subprocess.PIPE)
		out, err = p.communicate()

		# find balance for lids with cluster name
		for line in out.split('\n'):
			# first grab the node name
			m = re.search("^.*\s([0-9a-fA-Fx]*)\s:\s.*'(.*)'.*",
					line)
			if m:
				port = m.group(1)
				node = m.group(2)
				m = re.search("("+cluster+"[0-9]+).*", node)
				if m:
					sw.add_uplink(port, m.group(1))

				if cluster2 != "":
					# HACK for downlinks
					m = re.search("("+cluster2+"[0-9]+)\-ib0.*", node)
					if m and int(port) <= 24:
						sw.add_downlink(port, m.group(1))

		# store output
		output_lock.acquire()
		switch_map[lid] = sw
		output_lock.release()

		# for prgress reporting
		sys.stderr.write (str(lid) + ", ")

def usage():
	print ("Usage: "+sys.argv[0]+" [hp] [-C <cluster2>] -c <cluster> -l <lidlist>")
	print ("   -C <cluster2> cluster string to check for \"downlinks\" (seqio)")
	print ("   -c <cluster> cluster string to check balancing of (grove)")
	print ("   -l <lidlist> list of lids for the switches to check")

def main():
	global num_files
	print_only = 0
	cluster = ""
	cluster2 = ""
	swlids = []
	try:
		optlist, ibccq_args =	getopt.getopt(sys.argv[1:], "hpC:c:l:")
	except getopt.GetoptError as err:
		print (err)
		usage()
		sys.exit(1)

	for a, o in optlist:
		if a == "-C":
			cluster2 = o
		if a == "-c":
			cluster = o
		if a == "-p":
			print_only = 1
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
		t = Thread(target=ibroute, args=(swlids[i:i+chunk_size],cluster, cluster2))
		t.start()
		threads.append(t)
	while len(threads):
		t = threads.pop()
		t.join()
	sys.stderr.write ("\n")

	if print_only == 1:
		for lid in swlids:
			sys.stdout.write ("SW "+lid+": ")
			print switch_map[lid]
	else:
		generate_optimal_match(switch_map)

if __name__ == "__main__":
	sys.exit(main())


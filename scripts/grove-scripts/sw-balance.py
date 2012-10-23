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

# why does this not work now???  I don't know...
#import sequoia_san_simulation

import random
num_ion = 768
num_edge_switch = 32
num_uplinks_per_edge = 12
num_oss = 768
num_files = num_ion

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


# Assume round-robin for now, should be fine for immediate purposes
# even though not true.
#ion_mapping = [x%num_edge_switch for x in range(num_ion)]

def ion_to_edge(switch_map, ion_index):
	for lid in switch_map.keys():
		if switch_map[lid].is_edge_for_ION("seqio"+str(ion_index+1)):
			return lid
	return 0

    #return ion_mapping[ion_index]

# assume same port mapping on each switch for now, round-robin
# also not true, but shouldn't change distribution at full-scale
#port_mapping = [x%num_uplinks_per_edge for x in range(num_oss)]
def oss_to_port(switch_map, edge_index, oss_index):
	return switch_map[edge_index].get_uplink_port("grove"+str(oss_index+1))

def edge_port(switch_map, ion_index, oss_index):
    """Given two integers representing the ion and oss
    that are accessing and storing a file, respectively, this
    function will return a tuple representing the edge switch
    and port on the edge switch used to access the file."""

    # Look up the edge switch in use
    edge = ion_to_edge(switch_map, ion_index)

    # Look up the port used on the edge switch
    port = oss_to_port(switch_map, edge, oss_index)

    return (edge, port)


def mean_stddev(array):
    import math
    mean = float(sum(array))/len(array)
    tmp = [(mean-x)**2 for x in array]
    stddev = math.sqrt(sum(tmp)/len(array))

    return mean, stddev

def run_simulation(switch_map):
    global num_files
    # each file is just represented by an integer from 0 to num_files

    # first assign files evenly to servers
    file_to_oss = [x%num_oss for x in range(num_files)]

    # next randomly assign files to IONs
    file_to_ion = [x%num_ion for x in range(num_files)]
    random.shuffle(file_to_ion)

    sys.stdout.write("processing... ")
    # now map files to edge switch uplink ports
    histogram = {}
    for x in range(num_edge_switch):
        for y in range(num_uplinks_per_edge):
            histogram[(x, y)] = 0
    for file_index in range(num_files):
        ion_index = file_to_ion[file_index]
        oss_index = file_to_oss[file_index]
        port_tuple = edge_port(switch_map, ion_index, oss_index)
        if port_tuple not in histogram:
            histogram[port_tuple] = 0
        histogram[port_tuple] += 1

    sys.stdout.write("done\n")
    #pprint(histogram)

    mean, stddev = mean_stddev(histogram.values())
    minval = min(histogram.values())
    maxval = max(histogram.values())

    print "Number of IONs :", num_ion
    print "Number of OSSs :", num_oss
    print "Number of uplinks per edge switch :", num_uplinks_per_edge
    print "Number of files:", num_files
    print
    print "Files per uplink"
    print "   Mean    =", mean
    print "   Minimum =", minval
    print "   Maximum =", maxval, "(%.2f%% over mean)" % ((maxval-mean)/mean*100)
    print "   Std Dev = %.2f" % (stddev,), "(%.2f%%)" % ((stddev/mean)*100)
    print
    print "Observed Performance: %.2f%% (or worse)" % (mean/maxval*100)


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
	print ("Usage: "+sys.argv[0]+" [hs] [-i <iter>] [-n <num_files>] [-C <cluster2>] -c <cluster> -l <lidlist>")
	print ("   -s suppress balance output and run lustre simulation")
	print ("   -i <iter> number of iterations to run simulation")
	print ("   -n <num_files> number lustre files to simulate")
	print ("   -c <cluster> cluster string to check balancing of (grove)")
	print ("   -C <cluster2> cluster string to check for \"downlinks\" (seqio)")
	print ("   -l <lidlist> list of lids for the switches to check")

def main():
	global num_files
	suppress_output = 0
	iter = 1
	cluster = ""
	cluster2 = ""
	swlids = []
	try:
		optlist, ibccq_args =	getopt.getopt(sys.argv[1:], "hsi:n:C:c:l:")
	except getopt.GetoptError as err:
		print (err)
		usage()
		sys.exit(1)

	for a, o in optlist:
		if a == "-s":
			suppress_output = 1
		if a == "-i":
			iter = int(o)
		if a == "-n":
			num_files = int(o)
		if a == "-c":
			cluster = o
		if a == "-C":
			cluster2 = o
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
		t = Thread(target=ibroute, args=(swlids[i:i+chunk_size],cluster, cluster2))
		t.start()
		threads.append(t)
	while len(threads):
		t = threads.pop()
		t.join()
	sys.stderr.write ("\n")

	if suppress_output == 1:
		while iter > 0:
			run_simulation(switch_map)
			iter -= 1
	else:
		for lid in swlids:
			sys.stdout.write ("SW "+lid+": ")
			print switch_map[lid]
			if switch_map[lid].is_edge_for_ION("seqio2"):
				print "YES, seqio2 is on this edge"
			if not switch_map[lid].is_edge_for_ION("seqio200"):
				print "NO, seqio200 is not on this edge"

if __name__ == "__main__":
	sys.exit(main())


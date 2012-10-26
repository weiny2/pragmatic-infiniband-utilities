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

import sys
import re
import getopt



def usage():
	print ("Usage: "+sys.argv[0]+" [h] -i <ion_set> -r <dump_lft_file> -o optimal_map -l <swlid> -p <port>")

def main():
	try:
		optlist, ibccq_args =	getopt.getopt(sys.argv[1:], "hi:r:o:l:p:")
	except getopt.GetoptError as err:
		print (err)
		usage()
		sys.exit(1)

	for a, o in optlist:
		if a == "-i":
			ion_set = o
		if a == "-r":
			dump_file = o
		if a == "-o":
			optimal_map = o
		if a == "-l":
			swlid = o
		if a == "-p":
			port = o
		if a == "-h":
			usage()
			sys.exit(0)

	# Parameter checking
	if dump_file == "" or optimal_map == "" or swlid == "" or port == "" or ion_set == "":
		sys.stderr.write("Invalid input\n")
		usage()
		sys.exit(1)

	used_ions = []
	file = open(ion_set, "r")
	for line in file:
		used_ions.append(line.strip())
	file.close()

	grove2seq = {}
	file = open(optimal_map, "r")
	for line in file:
		m = re.search("^(.*), (.*)", line)
		if m:
			grove2seq[m.group(2)] = m.group(1)

	file.close()

	file = open(dump_file, "r")
	found = 0
	for line in file:
		m = re.search("^Unicast lids .* Lid ([0-9]*) guid.*", line)
		if m and m.group(1) == swlid:
			found = 1

		if found == 1:
			m = re.search("^[x0-9a-f]* ([0-9].*) : .*: '(grove[0-9].*) .*'", line)
			if m and int(m.group(1)) == int(port):
				seq = "<no match>"
				try:
					seq = str(grove2seq[m.group(2)])
				except:
					pass
				sys.stdout.write(seq+", "+str(m.group(2)))
				if seq in used_ions:
					sys.stdout.write(" ***\n")
				else:
					sys.stdout.write("\n")
			m = re.search("^Unicast lids .* Lid ([0-9]*) guid.*", line)
			if m and m.group(1) != swlid:
				found = 0
				break
	file.close()

if __name__ == "__main__":
	sys.exit(main())


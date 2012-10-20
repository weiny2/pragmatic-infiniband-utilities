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

import subprocess
import sys
import re
import getopt


def usage():
	print ("Usage: "+sys.argv[0]+" [h] -c <cluster>")

def get_all_lids(cluster):
	rc = []
	# build up the command for Popen
	cmd_list = ["saquery", "NR"]
	p = subprocess.Popen(cmd_list, stdout=subprocess.PIPE)
	out, err = p.communicate()
	# process saquery output
	lid = 0
	for line in out.split('\n'):
		m = re.search('\slid\.*([0-9a-fA-Fx]*)$', line)
		if m:
			lid = m.group(1)
		m = re.search('\sNodeDescription\.*('+cluster+').*', line)
		if m:
			rc.append(int(lid, 0))
	return rc

def main():
	cluster = ""
	try:
		optlist, ibccq_args =	getopt.getopt(sys.argv[1:], "hc:")
	except getopt.GetoptError as err:
		print (err)
		usage()
		sys.exit(1)

	for a, o in optlist:
		if a == "-c":
			cluster = o
		if a == "-h":
			usage()
			sys.exit(0)

	if cluster == "":
		sys.stderr.write("Must specify cluster\n")
		usage()
		sys.exit(1)

	lids = get_all_lids(cluster)

	for lid in lids:
		print lid

if __name__ == "__main__":
	sys.exit(main())


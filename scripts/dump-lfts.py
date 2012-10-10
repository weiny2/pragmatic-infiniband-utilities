#!/usr/bin/python

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


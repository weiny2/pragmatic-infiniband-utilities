#!/usr/bin/perl
#################################################################################
#
#  Copyright (C) 2007 The Regents of the University of California.
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
#
# Written by: Albert Chu, chu11@llnl.gov
#
# Loops a specified number of seconds, regularly monitoring the performance
# counters of a specified switch, indicating the data rate at which data is
# moving through the switch.
#
# This script always assumes the use of extended port counters.
#
#################################################################################

use strict;

use Getopt::Std;

use IO::Handle;

my $perfquery = "/usr/sbin/perfquery";
my $perfout;

my $lid;
my $ports;
my $interval = 5;

my $lastxmitdata = 0;
my $lastrcvdata  = 0;

my $tab            = 0;
my $gnuplot        = 0;
my $gnuplotscratch = "/tmp/perfmon.tmp.$$";

my $counter = 0;

sub usage
{
	my $prog = `basename $0`;

	chomp($prog);
	print "Usage: $prog -l <lid> -p <port(s)> [-i <interval>] [-v]\n";
	print "  -l lid of switch to monitor\n";
	print "  -p port(s) of switch to monitor\n";
	print "     separate ports by comma for set of ports (i.e. 1,2,3,4)\n";
	print "  -i interval seconds between poll (default 5)\n";
	print "  -T tab delimited output\n";
	print "  -G gnuplot data\n";
	exit 2;
}

sub perfget
{
	my $perfdata;
	my $tmpxmitdata;
	my $tmprcvdata;

	$perfdata = `$perfquery -a -x -L $lid $ports`;

	if ($perfdata =~ /PortXmitData:....................(.+)/) {
		$tmpxmitdata = $1;
	} else {
		$tmpxmitdata = 0;
	}
	if ($perfdata =~ /PortRcvData:.....................(.+)/) {
		$tmprcvdata = $1;
	} else {
		$tmprcvdata = 0;
	}
	return ($tmpxmitdata, $tmprcvdata);
}

if (!getopts("hl:p:i:TG")) {
	usage();
}

if (defined($main::opt_h)) {
	usage();
}

if (defined($main::opt_l)) {
	$lid = $main::opt_l;
} else {
	print "lid must be specified\n";
	exit 1;
}

if (defined($main::opt_p)) {
	$ports = $main::opt_p;
} else {
	print "port(s) must be specified\n";
	exit 1;
}

if (defined($main::opt_i)) {
	$interval = $main::opt_i;
}

if (defined($main::opt_T)) {
	$tab = 1;
}

if (defined($main::opt_G)) {
	$gnuplot = 1;
}

if ($gnuplot) {
	unlink($gnuplotscratch);
	open(GNUPLOT, "|gnuplot") || die "could not find gnuplot: $!";
	open(GNUPLOTSCRATCH, ">$gnuplotscratch")
	  || die "could not open $gnuplotscratch: $!";
}

while (1) {
	my $currentxmitdata;
	my $currentrcvdata;
	my $diffxmitdatabytes;
	my $diffrcvdatabytes;
	my $xmitrate;
	my $rcvrate;

	($currentxmitdata, $currentrcvdata) = perfget();
	if (   $lastxmitdata != 0
		&& $lastrcvdata != 0
		&& $currentxmitdata >= $lastxmitdata
		&& $currentrcvdata >= $lastrcvdata)
	{
		# data is in quad bytes
		$diffxmitdatabytes = 4 * ($currentxmitdata - $lastxmitdata);
		$diffrcvdatabytes  = 4 * ($currentrcvdata - $lastrcvdata);

		$xmitrate = $diffxmitdatabytes / $interval;
		$rcvrate  = $diffrcvdatabytes / $interval;

		$xmitrate /= 1073741824;
		$rcvrate  /= 1073741824;

		if ($tab) {
			print "$counter\t$xmitrate\t$rcvrate\n";
			$counter++;
		} elsif ($gnuplot) {
			print GNUPLOTSCRATCH "$counter\t$xmitrate\t$rcvrate\n";
			$counter++;

			print GNUPLOT
"plot '$gnuplotscratch' using 1:2 title 'Xmit' with linespoints, '$gnuplotscratch' using 1:3 title 'Rcv' with linespoints\n";
		} else {
			print "-----------------------------------------------\n";
			print "Xmit = $xmitrate gigabytes/sec\n";
			print "Rcv = $rcvrate gigabytes/sec\n";
			print "-----------------------------------------------\n";
		}
	}
	$lastxmitdata = $currentxmitdata;
	$lastrcvdata  = $currentrcvdata;
	sleep($interval);
}

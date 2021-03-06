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

my $interval = 5;

my $tab            = 0;
my $gnuplot        = 0;
my $gnuplotscratch = "/tmp/perfmon.tmp.$$";

my $aggregate = 0;

my $xmitoutput = 1;
my $rcvoutput  = 1;

my $counter = 0;
my $maxcounts = -1;

my $minGBs = 0;
my $maxGBs = 10;

my %nodes = ();

my %glastxmitdata    = ();
my %gcurrentxmitdata = ();

my %glastrcvdata    = ();
my %gcurrentrcvdata = ();

my @portlist = ();
my $ports;
my @nodelist = ();

my $suppress = 0;

sub usage
{
	my $prog = `basename $0`;

	chomp($prog);
	print "Usage: $prog -l <lid> -p <port(s)> [-i <interval>] [-s <file>] [-TGaxr]\n";
	print "  -l lid of switch to monitor\n";
	print "  -L <LIDs> comma separated list of nodes to monitor\n";
	print "  -p port(s) of switch to monitor\n";
	print "     input a single port (i.e. 1)\n";
	print "     or separate ports by comma for set of ports (i.e. 1,2,3,4)\n";
	print "     or separate ports by - for single range ports (i.e. 1-4)\n";
	print "  -i interval seconds between poll (default 5)\n";
	print "  -n <num> run for \"num\" iterations only (default -1 \"forever\")\n";
	print "  -T tab delimited output\n";
	print "  -G gnuplot data\n";
	print "  -s specify gnuplot scratch file\n";
	print "  -a aggregate data\n";
	print "  -x output xmit data only\n";
	print "  -r output rcv data only\n";
	print "  -m <min GB/s> output only rates >= min GB/s (default: $minGBs GB/s)\n";
	print "  -M <max GB/s> output only rates <= max GB/s (default: $maxGBs GB/s)\n";
	print "  -S <rate> suppress rate lower than <rate>\n";
	exit 2;
}

sub perfget
{
	my $lid = $_[0];
	my $perfdata;
	my @perfdatalines;
	my $perfline;
	my $currentport     = -1;    # b/c 0 is valid port
	my $currentxmitdata = 0;
	my $currentrcvdata  = 0;

	if ($aggregate) {
		$perfdata = `$perfquery -a -x -L $lid $ports`;
	} else {
		$perfdata = `$perfquery -l -x -L $lid $ports`;
	}

	@perfdatalines = split("\n", $perfdata);

	foreach $perfline (@perfdatalines) {
		if ($perfline =~ /PortSelect:......................(.+)/) {

			if (   $currentport != -1
				&& $currentxmitdata != 0
				&& $currentrcvdata != 0)
			{
				$gcurrentxmitdata{"$lid-$currentport"} = $currentxmitdata;
				$gcurrentrcvdata{"$lid-$currentport"}  = $currentrcvdata;
			}

			$currentport     = $1;
			$currentxmitdata = 0;
			$currentrcvdata  = 0;
			next;
		}

		if ($perfline =~ /PortXmitData:....................(.+)/) {
			$currentxmitdata = $1;
			next;
		}

		if ($perfline =~ /PortRcvData:.....................(.+)/) {
			$currentrcvdata = $1;
			next;
		}
	}

	if ($currentport != -1) {
		$gcurrentxmitdata{"$lid-$currentport"} = $currentxmitdata;
		$gcurrentrcvdata{"$lid-$currentport"}  = $currentrcvdata;
	}
}

if (!getopts("hl:L:p:i:n:TGs:axrm:M:S:")) {
	usage();
}

if (defined($main::opt_h)) {
	usage();
}

if (defined($main::opt_l)) {
	@nodelist = ( $main::opt_l );
} else {
	if (defined($main::opt_L)) {
		@nodelist = split(",", $main::opt_L);
	} else {
		print "lid or node list must be specified\n";
		exit 1;
	}
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

if (defined($main::opt_n)) {
	$maxcounts = $main::opt_n;
}

if (defined($main::opt_T)) {
	$tab = 1;
}

if (defined($main::opt_G)) {
	$gnuplot = 1;
}

if (defined($main::opt_s)) {
    $gnuplotscratch = $main::opt_s;
}

if (defined($main::opt_S)) {
    $suppress = $main::opt_S;
}

if (defined($main::opt_a)) {
	$aggregate = 1;
}

if (defined($main::opt_x)) {
	$xmitoutput = 1;
	$rcvoutput  = 0;
}

if (defined($main::opt_r)) {
	$xmitoutput = 0;
	$rcvoutput  = 1;
}

if (defined($main::opt_m)) {
	$minGBs = $main::opt_m;
}

if (defined($main::opt_M)) {
	$maxGBs = $main::opt_M;
}


if ($gnuplot) {
	unlink($gnuplotscratch);
	open(GNUPLOT, "|gnuplot") || die "could not find gnuplot: $!";
	open(GNUPLOTSCRATCH, ">$gnuplotscratch")
	  || die "could not open $gnuplotscratch: $!";
}

if ($ports =~ /(.+)-(.+)/) {
	my $tmpport1 = $1;
	my $tmpport2 = $2;
	if ($tmpport2 <= $tmpport1) {
		print "invalid port range specified\n";
		exit 1;
	}
	@portlist = ($tmpport1 .. $tmpport2);
} elsif ($ports =~ /,/) {
	@portlist = split(",", $ports);
} else {
	@portlist = ($ports);
}

if ($aggregate) {
	@portlist = ("255");
}

while (1) {
	my $diffdatabytes;
	my $xmitrate;
	my $xmitGB;
	my $rcvrate;
	my $rcvGB;
	my $port;
	my $gnuplot_datastr;
	my $gnuplot_plotstr;
	my $gnuplot_commastr;
	my $loopcount = 0;
	my $xmitcol;
	my $rcvcol;

	foreach my $node (@nodelist) {
		perfget($node);

		if (keys(%glastxmitdata)) {

			if ($gnuplot) {
				$gnuplot_datastr  = "$counter";
				$gnuplot_plotstr  = "plot ";
				$gnuplot_commastr = "";
			} elsif ($tab) {
				print "$counter";
			}

			$loopcount = 0;
			foreach $port (@portlist) {
				# data is in quad bytes
				# we check for defines if by chance missed/errored on poll
				if ($xmitoutput) {
					if ( defined($glastxmitdata{"$node-$port"})
						&& defined($gcurrentxmitdata{"$node-$port"}))
					{
						$diffdatabytes =
						  4 * ($gcurrentxmitdata{"$node-$port"} - $glastxmitdata{"$node-$port"});
					} else {
						$diffdatabytes = 0;
					}
					$xmitrate = $diffdatabytes / $interval;
					$xmitrate /= 1073741824;
					$xmitGB = $diffdatabytes /= 1073741824;
					if ($xmitrate < $suppress) {
						$xmitrate = 0;
						$xmitGB = 0;
					}
				}

				if ($rcvoutput) {
					if ( defined($glastrcvdata{"$node-$port"})
						&& defined($gcurrentrcvdata{"$node-$port"}))
					{
						$diffdatabytes =
						  4 * ($gcurrentrcvdata{"$node-$port"} - $glastrcvdata{"$node-$port"});
					} else {
						$diffdatabytes = 0;
					}
					$rcvrate = $diffdatabytes / $interval;
					$rcvrate /= 1073741824;
					$rcvGB = $diffdatabytes /= 1073741824;
					if ($rcvrate < $suppress) {
						$rcvrate = 0;
						$rcvGB = 0;
					}
				}

				if ($gnuplot) {
					if ($xmitoutput && $rcvoutput) {
						$gnuplot_datastr .= "\t$node\t$port\t$xmitrate\t$rcvrate";
						$xmitcol = 3 + $loopcount * 3;
						$rcvcol  = 4 + $loopcount * 3;
					} elsif ($xmitoutput) {
						$gnuplot_datastr .= "\t$node\t$port\t$xmitrate";
						$xmitcol = 3 + $loopcount * 2;
					} else {
						$gnuplot_datastr .= "\t$node\t$port\t$rcvrate";
						$rcvcol = 3 + $loopcount * 2;
					}

					if ($xmitoutput) {
						$gnuplot_plotstr .= "$gnuplot_commastr'$gnuplotscratch' using 1:$xmitcol title 'Xmit-$node-$port' with linespoints";
						$gnuplot_commastr = ", ";
					}

					if ($rcvoutput) {
						$gnuplot_plotstr .= "$gnuplot_commastr'$gnuplotscratch' using 1:$rcvcol title 'Rcv-$node-$port' with linespoints";
						$gnuplot_commastr = ", ";
					}
				} elsif ($tab) {
					if ($xmitoutput && $rcvoutput) {
						print "\t$node\n$port\t$xmitrate\t$rcvrate";
					} elsif ($xmitoutput) {
						print "\t$node\n$port\t$xmitrate";
					} else {
						print "\t$node\n$port\t$rcvrate";
					}
				} else {
					if ($xmitoutput) {
						if ($minGBs <= $xmitrate && $xmitrate <= $maxGBs) {
							print "$counter: Xmit node=$node port=$port rate=$xmitrate gigabytes/sec ($xmitGB GB / $interval sec)\n";
						}
					}
					if ($rcvoutput) {
						if ($minGBs <= $rcvrate && $rcvrate <= $maxGBs) {
							print "$counter: Rcv node=$node port=$port rate=$rcvrate gigabytes/sec ($rcvGB GB / $interval sec)\n";
						}
					}
				}
				$loopcount++;
			}
		}
		if ($gnuplot) {
			print GNUPLOTSCRATCH "$gnuplot_datastr\n";
		}
	}

	if ($gnuplot) {
		$gnuplot_plotstr .= "\n";
		print GNUPLOT $gnuplot_plotstr;
	} elsif ($tab) {
		print "\n";
	}

	print "   *** End Sweep $counter ***\n";
	$counter++;
	if ($maxcounts > 0 && $counter > $maxcounts) {
		exit (0);
	}
	%glastxmitdata    = %gcurrentxmitdata;
	%glastrcvdata     = %gcurrentrcvdata;
	%gcurrentxmitdata = ();
	%gcurrentrcvdata  = ();
	sleep($interval);
}

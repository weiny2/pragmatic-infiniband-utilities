#!/usr/bin/perl
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


use strict;

use Getopt::Std;

sub usage
{
	my $prog = `basename $0`;

	chomp($prog);
	print "Usage: $prog\n";
	exit 2;
}

my $grove_switches = "grove_switches";
my $ion_switches = "ion_switches";
my $spine_switches = "spine_switches";
my $all_switches = "all_switches";

if (!getopts("h")) {
	usage();
}

if (defined($main::opt_h)) {
	usage();
}

# process all switches
my %lids = ();
my $ibswitches = `ibswitches -t 50`;
my @datalines = split("\n", $ibswitches);
foreach my $line (@datalines) {
	chomp $line;
	if ($line =~ /.* lid (.+) lmc/) {
		$lids{$1} = $line;
	}
}
open(OUTPUT, ">$all_switches") || die "could not open: $all_switches";
foreach my $key (sort {$a<=>$b} keys %lids) {
	print OUTPUT "$key,";
}
print OUTPUT "\n";
close OUTPUT;

# process grove edge switches
my %lids = ();
my $data = `pdsh -f 128 -A "smpquery -D portinfo 0,1 0 | egrep '^Lid:'"`;
my @datalines = split("\n", $data);
foreach my $line (@datalines) {
	chomp $line;
	if ($line =~ /egrove.* Lid:\.*(.+)/) {
		$lids{$1} = $line;
	}
}
open(OUTPUT, ">$grove_switches") || die "could not open: $grove_switches";
foreach my $key (sort {$a<=>$b} keys %lids) {
	print OUTPUT "$key,";
}
print OUTPUT "\n";
close OUTPUT;

# process spine switches
my %lids = ();
#my $data = `ibswitches | egrep "IS5600/S"`;
my $data = $ibswitches;
my @datalines = split("\n", $data);
foreach my $line (@datalines) {
	chomp $line;
	if ($line =~ /.*IS5600\/S.*/) {
		if ($line =~ /.* lid (.+) lmc/) {
			$lids{$1} = $line;
		}
	}
}
open(OUTPUT, ">$spine_switches") || die "could not open: $spine_switches";
foreach my $key (sort {$a<=>$b} keys %lids) {
	print OUTPUT "$key,";
}
print OUTPUT "\n";
close OUTPUT;

# process ION switches
my %lids = ();
my %guids = ();
#my $data = `ibswitches | egrep "SNR"`;
my $data = $ibswitches;
my @datalines = split("\n", $data);
foreach my $line (@datalines) {
	chomp $line;
	if ($line =~ /.*SNR.*/) {
		if ($line =~ /Switch\s: ([0-9a-fx]*) .* lid (.+) lmc/) {
			$guids{$1} = $line;
			$lids{$2} = $line;
		}
	}
}
open(OUTPUT, ">$ion_switches") || die "could not open: $ion_switches";
foreach my $key (sort {$a<=>$b} keys %lids) {
	print OUTPUT "$key,";
}
print OUTPUT "\n";
close OUTPUT;
$ion_switches = "$ion_switches-guids";
open(OUTPUT, ">$ion_switches") || die "could not open: $ion_switches";
foreach my $key (sort {$a<=>$b} keys %guids) {
	print OUTPUT "$key,";
}
print OUTPUT "\n";
close OUTPUT;

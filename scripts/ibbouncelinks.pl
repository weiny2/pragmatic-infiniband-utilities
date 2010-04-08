#!/usr/bin/perl
#
#  Copyright (C) 2007 The Regents of the University of California.
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Ira Weiny weiny2@llnl.gov
#  UCRL-CODE-235440
#
#  This file is part of pragmatic-infiniband-tools (PIU), usefull tools to manage
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

use strict;
use Getopt::Std;
use IBswcountlimits;

my $timeout = 10;
my $ignore_hosts    = undef;
my @ignore_hosts    = undef;
my $going_to_sdr    = undef;
my $going_to_ddr    = undef;
my $going_to_qdr    = undef;

#
# get_state(switch, port)
#
my $speed = "";

sub get_state
{
	my $switch        = $_[0];
	my $port          = $_[1];
	my $portinfo_data = `smpquery -G portinfo $switch $port`;
	my $state         = "";
	my @lines         = split("\n", $portinfo_data);
	foreach my $line (@lines) {
		if ($line =~ /^LinkState:\.+(.*)/)       { $state = $1; }
		if ($line =~ /^LinkSpeedActive:\.+(.*)/) { $speed = $1; }
	}
	return ($state);
}

sub usage_and_exit
{
	my $prog = $_[0];
	print
"Usage: $prog [-h -t <timeout> -i <host1,host2,...> -R -S <guid> -O <guid:port>]\n";
	print
"   Bounce all the links on the network but the one connected to this HCA\n";
	print "   -h This help message\n";
	print
"   -R Recalculate ibnetdiscover information (Default is to reuse ibnetdiscover output)\n";
	print "   -S only the switch specified by guid\n";
	print
"   -t <timeout> Change the timeout (Default: $timeout)\n";
	print
	  "   -i <host1,host2,...> Ignore links connected to hosts specified.\n";
	print "   -s Specify we are going TO SDR mode (skip ports at SDR)\n";
	print "   -d Specify we are going TO DDR mode (skip ports at DDR))\n";
	print "   -q Specify we are going TO QDR mode (skip ports at QDR))\n";
	print "   -O <guid:port> bounce a single link specified by guid and port\n";
	exit 0;
}

my $argv0          = `basename $0`;
my $single_switch  = undef;
my $single_link    = undef;
my $regenerate_map = undef;
chomp $argv0;
if (!getopts("hsdqi:t:RS:O:"))    { usage_and_exit $argv0; }
if (defined $Getopt::Std::opt_h) { usage_and_exit $argv0; }
if (defined $Getopt::Std::opt_S) { $single_switch   = $Getopt::Std::opt_S; }
if (defined $Getopt::Std::opt_R) { $regenerate_map  = $Getopt::Std::opt_R; }
if (defined $Getopt::Std::opt_t) { $timeout = $Getopt::Std::opt_t; }
if (defined $Getopt::Std::opt_i) { $ignore_hosts    = $Getopt::Std::opt_i; }
if (defined $Getopt::Std::opt_s) { $going_to_sdr    = $Getopt::Std::opt_s; }
if (defined $Getopt::Std::opt_d) { $going_to_ddr    = $Getopt::Std::opt_d; }
if (defined $Getopt::Std::opt_q) {
	$going_to_qdr    = $Getopt::Std::opt_q;
	$timeout = 25; # going to QDR takes time...  :-(
}
if (defined $Getopt::Std::opt_O) { $single_link     = $Getopt::Std::opt_O; }

my $hostname = `hostname`;
chomp $hostname;
my @ignore_hosts = split(",", $ignore_hosts);
push(@ignore_hosts, $hostname);
print "@ignore_hosts\n";

sub bounce_one
{
	my $switch = $_[0];
	my $port   = $_[1];

	if (get_state($switch, $port) eq "Down") {
		printf("   Port is down.\n");
		return;
	}
	if ($going_to_sdr && $speed eq "2.5 Gbps") {
		printf("   Port is already at SDR.\n");
		return;
	}
	if ($going_to_ddr && $speed eq "5.0 Gbps") {
		printf("   Port is already at DDR.\n");
		return;
	}
	if ($going_to_qdr && $speed eq "10.0 Gbps") {
		printf("   Port is already at QDR.\n");
		return;
	}

	printf("   Bouncing... ");

	`ibportstate -G $switch $port reset`;
	my $time = $timeout;
	printf("Wait for \"Active\".");
	while (get_state($switch, $port) ne "Active" && $time--) {
		sleep 1;
		printf (".");
	}
	if ($time <= 0) {
		printf("   ($timeout sec) ERROR: Port failed to activate.\n");
		return;
	}

	printf(" Active\n");
}

sub do_single_port
{
	my $switch = $_[0];
	my $port   = $_[1];

	if (!check_self_link($switch, $port)) {
		bounce_one($switch, $port);
	}
}

sub check_self_link
{
	my $switch = $_[0];
	my $port   = $_[1];

	my $hr = $IBswcountlimits::link_ends{$switch}{$port};
	printf("%18s \"%s\" %4s[%2s]  ==>  %18s %4s[%2s] \"%s\"\n",
		$switch, $hr->{loc_desc}, $port, $hr->{loc_ext_port}, $hr->{rem_guid},
		$hr->{rem_port}, $hr->{rem_ext_port}, $hr->{rem_desc});
	foreach my $host (@ignore_hosts) {
		if ($host eq $hr->{rem_desc}) {
			printf("   Skipping\n");
			return (1);
		}
	}
	return (0);
}

sub main
{
	$IBswcountlimits::auth_check;
	if ($regenerate_map) { generate_ibnetdiscover_topology; }
	get_link_ends;
	if ($single_link) {
		(my $switch, my $port) = split(":", $single_link);
		do_single_port($switch, $port);
		exit;
	}
	foreach my $switch (sort (keys(%IBswcountlimits::link_ends))) {
		if ($single_switch && $switch ne $single_switch) {
			next;
		}
		my $num_ports = get_num_ports($switch);
		PORT: foreach my $port (1 .. $num_ports) {
			if (check_self_link($switch, $port)) {
				next PORT;
			}
			bounce_one($switch, $port);
		}
	}
}
main;


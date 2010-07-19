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
# Outputs the guids that should be routed by OpenSM.
#
#################################################################################

use strict;

use Getopt::Std;
use IBswcountlimits;
use Genders;

my $file               = undef;
my $old_routing_file   = undef;
my %old_routing_hash   = ();
my $ibnetdiscover_file = undef;
my %ibnetdiscover_hash = ();
my $preserve           = 0;
my $verbose            = 0;
my $genders            = undef;

my @nodes = ();
my $node;
my $port_guid;
my $tmp;

my @all_nodes = ();

sub usage
{
	my $prog = $0;
	print
"Usage: $prog -f <genders_database> -o <old_routing_file> -i <ibnetdiscover_file> [-pv]\n";
	print "  -f specify alternate genders database\n";
	print "  -o old routing file\n";
	print "  -i ibnetdiscover output file\n";
	print "  -p preserve order\n";
	print "  -v verbose output\n";
	exit 0;
}

sub remove_duplicates
{
	my @tmp_nodes = ();
	my $count     = 0;
	foreach $node (@_) {
		@tmp_nodes = grep(/\b$node\b/, @all_nodes);
		if (@tmp_nodes) {
			splice(@nodes, $count, 1);
		} else {
			$count++;
		}
	}
}

sub output_nodes
{
	my @tmp_nodes = @{$_[0]};
	my $str       = $_[1];
	foreach $node (@tmp_nodes) {
		$tmp = `/usr/sbin/saquery $node | grep port_guid`;
		if ($? != 0) {
			print STDERR "Error retrieving port guid for '$node'\n";
			if ($old_routing_file && $old_routing_hash{$node}) {
				print STDERR "Using old entry in old routing file\n";
				$port_guid = $old_routing_hash{$node};
			} elsif ($ibnetdiscover_file && $ibnetdiscover_hash{$node}) {
				print STDERR "Using entry in ibnetdiscover file\n";
				$port_guid = $ibnetdiscover_hash{$node};
			} else {
				next;
			}
		} else {
			$tmp =~ /port_guid\.\.\.\.\.\.\.\.\.\.\.\.\.\.\.(.+)/;
			$port_guid = $1;
		}
		if ($verbose) {
			print "$port_guid  # $node $str\n";
		} else {
			print "$port_guid\n";
		}
	}
}

$IBswcountlimits::auth_check;

if (!getopts("hf:o:i:pv")) {
	usage();
}

if (defined($main::opt_h)) {
	usage();
}

if (defined($main::opt_f)) {
	$file = $main::opt_f;
}

if (defined($main::opt_o)) {
	$old_routing_file = $main::opt_o;
}

if (defined($main::opt_i)) {
	$ibnetdiscover_file = $main::opt_i;
}

if (defined($main::opt_p)) {
	$preserve = 1;
}

if (defined($main::opt_v)) {
	$verbose = 1;
}

if ($old_routing_file) {
	my @lines = ();
	my $line;

	if (!open(FH, "< $old_routing_file")) {
		print STDERR (
			"Couldn't open old routing file: $old_routing_file: $!\n");
	} else {
		@lines = <FH>;

		# if we did not output w/ verbose, no way to take advantage of
		# old routing files.
		foreach $line (@lines) {
			if ($line =~ /(.+)  \# (.+) .+/) {
				$old_routing_hash{$2} = $1;
			}
		}
		close(FH);
	}
}

if ($ibnetdiscover_file) {
	my @lines = ();
	my $line;
	my $nodename;
	my $port_guid;
	my $next_line_has_port_guid = 0;
	my $len;
	my $i;

	if (!open(FH, "< $ibnetdiscover_file")) {
		print STDERR (
			"Couldn't open ibnetdiscover file: $ibnetdiscover_file: $!\n");
	} else {
		@lines = <FH>;

		foreach $line (@lines) {
			if ($next_line_has_port_guid) {
				$line =~ /\[.\]\((.+)\).+\#.+/;
				$port_guid = $1;

				# extend port guid as necessary
				$len = length($port_guid);
				for ($i = 0; $i < (16 - $len); $i++) {
					$port_guid = "0" . $port_guid;
				}

				# add 0x in front of guid too
				$port_guid = "0x" . $port_guid;

				$ibnetdiscover_hash{$nodename} = $port_guid;

				$next_line_has_port_guid = 0;
				next;
			}

			if ($line =~ /Ca\s+.\s+\".+\"\s+\# \"(.+)\"/) {
				$nodename                = $1;
				$next_line_has_port_guid = 1;
			} else {
				$next_line_has_port_guid = 0;
			}
		}
		close(FH);
	}
}

$genders = Genders->new($file);
if (!$genders) {
	print STDERR "Error opening genders database\n";
	exit 1;
}

if ($preserve) {
	@nodes = $genders->getnodes();
	remove_duplicates(@nodes);
	output_nodes(\@nodes, "");
	@all_nodes = @nodes;
} else {
	# First get node guids of all compute nodes, we care 1st most about
	# these guys getting routed.
	@nodes = $genders->getnodes("compute");
	remove_duplicates(@nodes);
	output_nodes(\@nodes, "compute");
	@all_nodes = @nodes;

	# Then we do gateway nodes

	@nodes = $genders->getnodes("gw");
	remove_duplicates(@nodes);
	output_nodes(\@nodes, "gw");
	push(@all_nodes, @nodes);

	# Then we do router nodes

	@nodes = $genders->getnodes("router");
	remove_duplicates(@nodes);
	output_nodes(\@nodes, "router");
	push(@all_nodes, @nodes);

	# Then we do login nodes

	@nodes = $genders->getnodes("login");
	remove_duplicates(@nodes);
	output_nodes(\@nodes, "login");
	push(@all_nodes, @nodes);

	# Then we do mgmt nodes

	@nodes = $genders->getnodes("mgmt");
	remove_duplicates(@nodes);
	output_nodes(\@nodes, "mgmt");
	push(@all_nodes, @nodes);

	# Then we do any nodes that are left

	@nodes = $genders->getnodes();
	remove_duplicates(@nodes);
	output_nodes(\@nodes, "unknown");
	push(@all_nodes, @nodes);
}


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
# Reads in a lfts output file (from dump_lfts or ibroute) and outputs
# a sorted output of the lfts or some other sorted output based on
# options passed in.  Useful for analyzing dlfts output for efficacy
# of routing changes.
#
#################################################################################

use strict;

use Getopt::Std;

my $preserve      = 0;
my $table         = 0;
my $verbose       = 0;
my $dump_lft_file = undef;

my $switch_lid   = undef;
my $switch_guid  = undef;
my $switch_name  = undef;
my $port_routed  = undef;
my $routing_line = undef;

my @ca_routing     = ();
my @switch_routing = ();
my @router_routing = ();

my %switch_ports = ();

my $host = undef;

my $switch_header_text  = undef;
my $switch_trailer_text = undef;

my @lft_lines = ();
my $lft_line;

sub usage
{
	my $prog = `basename $0`;

	chomp($prog);
	print "Usage: $prog [-pt] [-v] -o dump_lfts_file\n";
	print "  -p preserve format\n";
	print "  -t table format\n";
	print "  -v verbose output\n";
	exit 2;
}

sub store_routing_line
{
	my $str = $_[0];
	my @fields;
	my $type;

	if ($preserve) {
		@fields = split(/,/, $str);
		$type = $fields[0];

		# get rid of type
		shift(@fields);
		$str = join(",", @fields);

		if ($type eq "Channel") {
			push(@ca_routing, $str);
		} elsif ($type eq "Router") {
			push(@router_routing, $str);
		} elsif ($type eq "Switch") {
			push(@switch_routing, $str);
		}
	} else {
		push(@ca_routing, $routing_line);
	}
}

sub cmp_host_num
{
	my $numa;
	my $numb;
	$a =~ /([a-zA-Z]+)([0-9]+)/;
	$numa = $2;
	$b =~ /([a-zA-Z]+)([0-9]+)/;
	$numb = $2;
	if ($numa < $numb) {
		return -1;
	} elsif ($numa == $numb) {
		return 0;
	} elsif ($numa > $numb) {
		return 1;
	}
}

sub cmp_host_field
{
	my $numa;
	my $numb;
	$a =~ /([a-zA-Z]+)([0-9]+),(.+)/;
	$numa = $2;
	$b =~ /([a-zA-Z]+)([0-9]+),(.+)/;
	$numb = $2;
	if ($numa < $numb) {
		return -1;
	} elsif ($numa == $numb) {
		return 0;
	} elsif ($numa > $numb) {
		return 1;
	}
}

sub output_switch_routing
{
	my @fields;
	my $i;
	my $j;
	my $tmp;

	if ($preserve) {
		if ($switch_header_text) {
			print "$switch_header_text\n";
		}

		if (@ca_routing) {
			@ca_routing = sort cmp_host_field @ca_routing;
			for $i (@ca_routing) {
				@fields = split(/,/, $i);
				# get rid of sort_info
				shift(@fields);
				for $j (@fields) {
					print "$j\n";
				}
			}
		}

		if (@router_routing) {
			# sort via name
			@router_routing = sort @router_routing;
			for $i (@router_routing) {
				@fields = split(/,/, $i);
				# get rid of sort_info
				shift(@fields);
				for $j (@fields) {
					print "$j\n";
				}
			}
		}

		if (@switch_routing) {
			# sort via name
			@switch_routing = sort @switch_routing;
			for $i (@switch_routing) {
				@fields = split(/,/, $i);
				# get rid of sort_info
				shift(@fields);
				for $j (@fields) {
					print "$j\n";
				}
			}
		}

		if ($switch_trailer_text) {
			print "$switch_trailer_text\n";
		}
	} elsif ($table) {
		my @ports = (
			"001", "002", "003", "004", "005", "006", "007", "008",
			"009", "010", "011", "012", "013", "014", "015", "016",
			"017", "018", "019", "020", "021", "022", "023", "024"
		);
		my $flag = 0;

		# first sort all the hosts in each switch_ports entry
		for $i (@ports) {
			if ($switch_ports{$i}) {
				@fields           = split(/,/, $switch_ports{$i});
				@fields           = sort cmp_host_num @fields;
				$tmp              = join(",", @fields);
				$switch_ports{$i} = $tmp;
				$flag++;
			}
		}

		# no nodes
		if ($flag == 0) {
			return;
		}

		print "Switch Lid $switch_lid guid $switch_guid ($switch_name)\n";
		print
"------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n";
		for $i (@ports) {
			printf("%12s", $i);
		}
		printf("\n");
		print
"------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n";
		while (1) {
			$flag = 0;
			for $i (@ports) {
				# achu: This isn't efficient, but I suck at perl.  I
				# can never remember how to allocate lists on the
				# stack, etc.
				if ($switch_ports{$i}) {
					@fields = split(/,/, $switch_ports{$i});
					$tmp = shift(@fields);
					$flag++;
					printf("%12s", $tmp);
					if (@fields) {
						$tmp = join(",", @fields);
						$switch_ports{$i} = $tmp;
					} else {
						$switch_ports{$i} = undef;
					}
				} else {
					printf("%12s", "");
				}
			}
			printf("\n");
			if ($flag == 0) {
				last;
			}
		}
	} else {
		if (@ca_routing) {
			@ca_routing = sort cmp_host_field @ca_routing;
			print "Switch Lid $switch_lid guid $switch_guid ($switch_name)\n";
			for $i (@ca_routing) {
				if ($i =~ /(.+),(.+),(.+),(.+),(.+)/) {
					print "$1: $2 $3 $4 $5\n";
				} elsif ($i =~ /(.+),(.+),(.+)/) {
					print "$1: $2 $3\n";
				} elsif ($i =~ /(.+),(.+)/) {
					print "$1: $2\n";
				}
			}
			print "\n";
		}
	}
}

if (!getopts("hpto:v")) {
	usage();
}

if (defined($main::opt_p)) {
	$preserve = 1;
}

if (defined($main::opt_t)) {
	$table = 1;
}

if (defined($main::opt_v)) {
	$verbose = 1;
}

if (defined($main::opt_o)) {
	$dump_lft_file = $main::opt_o;
} else {
	usage();
}

if (!open(FH, "< $dump_lft_file")) {
	print STDERR ("Couldn't open dump_lfts file: $dump_lft_file: $!\n");
	exit 1;
}

@lft_lines = <FH>;

foreach $lft_line (@lft_lines) {
	chomp($lft_line);
	if ($lft_line =~ /Unicast/) {
		output_switch_routing();
		$lft_line =~ /Unicast lids .+ of switch Lid (.+) guid (.+) \((.+)\)/;

		$switch_lid  = $1;
		$switch_guid = $2;
		$switch_name = $3;

		@ca_routing     = ();
		@router_routing = ();
		@switch_routing = ();

		%switch_ports = ();

		$host         = undef;
		$routing_line = undef;

		if ($preserve) {
			$switch_header_text  = $lft_line;
			$switch_trailer_text = undef;
		}
	} elsif ($lft_line =~ /Lid  Out/) {
		if ($preserve) {
			$switch_header_text = "$switch_header_text\n$lft_line";
		}
	} elsif ($lft_line =~ /Port     Info/) {
		if ($preserve) {
			$switch_header_text = "$switch_header_text\n$lft_line";
		}
	} elsif ($lft_line =~ /Channel/) {
		if ($routing_line) {
			store_routing_line($routing_line);
		}
		$routing_line = undef;

		$lft_line =~ /.+ (.+) : \(.+ portguid .+: '(.+)'\)/;

		if ($preserve) {
			# data = type,sort_info,output
			$routing_line = "Channel,$2,$lft_line";
		} elsif ($table) {
			if ($switch_ports{$1}) {
				$switch_ports{$1} = "$switch_ports{$1},$2";
			} else {
				$switch_ports{$1} = "$2";
			}
			$host = $2;
		} else {
			$port_routed  = $1;
			$host         = $2;
			$routing_line = "$host,$port_routed";
		}
	} elsif ($lft_line =~ /Router/) {
		if ($routing_line) {
			store_routing_line($routing_line);
		}
		$routing_line = undef;

		$lft_line =~ /.+ (.+) : \(.+ portguid .+: '(.+)'\)/;

		if ($preserve) {
			# data = type,sort_info,output
			$routing_line = "Router,$2,$lft_line";
		} elsif ($table) {
			if ($switch_ports{$1}) {
				$switch_ports{$1} = "$switch_ports{$1},$2";
			} else {
				$switch_ports{$1} = "$2";
			}
			$host = $2;
		} else {
			$port_routed  = $1;
			$host         = $2;
			$routing_line = "$host,$port_routed";
		}
	} elsif ($lft_line =~ /Switch portguid/) {
		if ($routing_line) {
			store_routing_line($routing_line);
		}
		$routing_line = undef;

		if ($preserve) {
			$lft_line =~ /Switch portguid 0x.+\: \'(.+)\'/;
			# data = type,sort_info,output
			$routing_line = "Switch,$1,$lft_line";
		}
	} elsif ($lft_line =~ /path/) {
		$lft_line =~ /.+ (.+) : \(path #. out of .: portguid .+\)/;
		if ($preserve) {
			if ($routing_line) {
				$routing_line = "$routing_line,$lft_line";
			}
		} elsif ($table) {
			if ($host) {
				if ($switch_ports{$1}) {
					$switch_ports{$1} = "$switch_ports{$1},$2";
				} else {
					$switch_ports{$1} = "$2";
				}
				$host = $2;
			}
		} else {
			if ($host) {
				$routing_line = "$routing_line,$1";
			}
		}
	} elsif ($lft_line =~ /valid lids dumped/) {
		if ($routing_line) {
			store_routing_line($routing_line);
		}
		$routing_line = undef;

		if ($preserve) {
			$switch_trailer_text = $lft_line;
		}
	} else {
		if ($routing_line) {
			store_routing_line($routing_line);
		}
		$routing_line = undef;
		next;
	}
}

output_switch_routing();

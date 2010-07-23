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
# Reads in a lfts output file (from dump_lfts or ibroute) and using
# a bunch of analysis based on our expectations of what wiring should look like,
# determine and output if wiring is sane.
#
# This script is also a giant script of heuristics and hacks.  Do not
# take this code to be reasonable in any way.
#
#################################################################################

use strict;

use Getopt::Std;
use IBswcountlimits;

my $ibnetdiscover_cache = "";
my $dump_lft_file       = "";

my $switch_lid   = undef;
my $switch_guid  = undef;
my $switch_name  = undef;
my $port_routed  = undef;
my $routing_line = undef;

my %switch_ports = ();

my $host = undef;

my $switch_header_text  = undef;
my $switch_trailer_text = undef;

my @lft_lines = ();
my $lft_line;

my $iblinkinfo_output;
my @iblinkinfo_lines = ();
my $iblinkinfo_line;
my %switch_guid_type = ();
my $spine_count      = 0;
my $line_board_count = 0;
my $leaf_count       = 0;

my $switches_analyzed = 0;

my @switch_port_possibilities = (
	"001", "002", "003", "004", "005", "006", "007", "008", "009", "010",
	"011", "012", "013", "014", "015", "016", "017", "018", "019", "020",
	"021", "022", "023", "024", "025", "026", "027", "028", "029", "030",
	"031", "032", "033", "034", "035", "036"
);

sub usage
{
	my $prog = `basename $0`;

	chomp($prog);
	print "Usage: $prog -o lft-output -c ibnetdiscover_cache\n";
	exit 2;
}

sub cmp_num
{
	if ($a < $b) {
		return -1;
	} elsif ($a == $b) {
		return 0;
	} elsif ($a > $b) {
		return 1;
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

sub analyze_line_board
{
	my $port_num;
	my $switch_desc;
	my $switch_num;
	my %switch_nums     = ();
	my $leaf_switch_min = 99999;
	my $leaf_switch_max = -1;

	my $last_port_num   = 0;
	my $last_switch_num = 0;

	# achu: Check that each line board is connected to at most 1 leaf switch

	$iblinkinfo_output =
	  `/usr/sbin/iblinkinfo --load-cache $ibnetdiscover_cache -S $switch_guid`;

	@iblinkinfo_lines = split("\n", $iblinkinfo_output);
	foreach $iblinkinfo_line (@iblinkinfo_lines) {

		$iblinkinfo_line =~
/[\s]+[\d]+[\s]+([\d]+)\[.+\] \=\=.+\=\=>[\s]+[\d]+[\s]+[\d]+\[.+\] \"(.+)\".+/;
		$port_num    = $1;
		$switch_desc = $2;

		# switch desc may be in format
		# ibswX for a leaf switch
		# or
		# ibcoreX SXXXX for a spine board

		if ($switch_desc =~ /ibsw([\d]+)/) {
			$switch_num = $1;

			if ($switch_num < $leaf_switch_min) {
				$leaf_switch_min = $switch_num;
			}
			if ($switch_num > $leaf_switch_max) {
				$leaf_switch_max = $switch_num;
			}

			if ($last_port_num > 0) {
				if ($switch_num !=
					($last_switch_num + ($port_num - $last_port_num)))
				{
					print "Switch guid $switch_guid ($switch_name)\n";
					print
"  Switch ibsw$switch_num on port $port_num improperly ordered "
					  . "after ibsw$last_switch_num on port $last_port_num\n";
				}
				$last_port_num   = $port_num;
				$last_switch_num = $switch_num;
			} else {
				$last_port_num   = $port_num;
				$last_switch_num = $switch_num;
			}

			if ($switch_nums{$switch_num}) {
				print "Switch guid $switch_guid ($switch_name)\n";
				print "  Connected to leaf switch "
				  . "$switch_desc multiple times\n";
				$switch_nums{$switch_num} = $switch_nums{$switch_num} + 1;
			} else {
				$switch_nums{$switch_num} = 1;
			}
		}
	}

	if (($leaf_switch_max - $leaf_switch_min) > 17) {
		print "Switch guid $switch_guid ($switch_name)\n";
		print "  Invalid leaf switch range connected to line board\n";
		print
		  "  Leaf switch range ibsw$leaf_switch_min to ibsw$leaf_switch_max\n";
	}
}

sub analyze_leaf_switch
{
	my @fields;
	my $fields_len;
	my $i;
	my $min = 999999;
	my $max = -1;
	my $min_host;
	my $max_host;
	my $hostnum;
	my %iblinkinfo_line_board_connections = ();
	my $ibcorenum;
	my $last_ibcorenum                  = -1;
	my $last_ibcorenum_count            = 0;
	my $ibcorenum_min_count             = 999999;
	my $ibcorenum_min_count_ibcorenum   = 0;
	my $ibcorenum_max_count             = -1;
	my $ibcorenum_max_count_ibcorenum   = 0;
	my $line_board_min_count            = 999999;
	my $line_board_min_count_line_board = 0;
	my $line_board_max_count            = -1;
	my $line_board_max_count_line_board = 0;
	my $switch_desc;
	my $line_board_desc;
	my $line_board_num;
	my $last_line_board_num = -1;
	my $is_new_ibcore       = 0;
	my $host_count          = 0;
	my $output_flag         = 0;

	# achu: first check if the hosts connected to the leaf switch
	# make logical sense.  i.e. we're not connecting
	# foo1,foo27,foo2,..
	#
	# We determine it's connected to a host b/c there is only one
	# destination for the particular port.
	for $i (@switch_port_possibilities) {
		# achu: This isn't efficient, but I suck at perl.  I
		# can never remember how to allocate lists on the
		# stack, etc.
		if ($switch_ports{$i}) {
			@fields = split(/,/, $switch_ports{$i});
			$fields_len = $#fields + 1;
			if ($fields_len == 1) {
				$fields[0] =~ /([a-zA-Z]+)([0-9]+)/;
				$hostnum = $2;
				if ($hostnum < $min) {
					$min_host = $fields[0];
					$min      = $hostnum;
					$host_count++;
				}
				if ($hostnum > $max) {
					$max_host = $fields[0];
					$max      = $hostnum;
					$host_count++;
				}
			}
		}
	}

	if ($host_count
		&& ($max - $min) > 17)
	{
		print "Switch guid $switch_guid ($switch_name)\n";
		print "  Invalid host range on leaf switch\n";
		print "  Hosts range from $min_host to $max_host\n";
		$output_flag++;
	}

	# achu: checks for the ordering of the links
	if ($output_flag == 0 && $host_count) {
		my $hostnum;
		my $last_host;
		my $last_hostnum = -1;
		my $last_hostport;
		my $decport;

		for $i (@switch_port_possibilities) {
			# achu: This isn't efficient, but I suck at perl.  I
			# can never remember how to allocate lists on the
			# stack, etc.
			if ($switch_ports{$i}) {
				@fields = split(/,/, $switch_ports{$i});
				$fields_len = $#fields + 1;
				if ($fields_len == 1) {
					$fields[0] =~ /([a-zA-Z]+)([0-9]+)/;
					$hostnum = $2;
					$i =~ /0+(.+)/;
					$decport = $1;
					if ($last_hostnum >= 0) {
						if ($hostnum !=
							($last_hostnum + ($decport - $last_hostport)))
						{
							print "Switch guid $switch_guid ($switch_name)\n";
							print "  Host $fields[0] on port $decport "
							  . "improperly ordered after $last_host "
							  . "on port $last_hostport\n";
							$output_flag++;
						}
						$last_host     = $fields[0];
						$last_hostnum  = $hostnum;
						$last_hostport = $decport;
					} else {
						$last_host     = $fields[0];
						$last_hostnum  = $hostnum;
						$last_hostport = $decport;
					}
				}
			}
		}
	}

	# new type of check
	$output_flag = 0;

	# achu: check that we are connected to lineboards in a common fashion

	$iblinkinfo_output =
	  `/usr/sbin/iblinkinfo --load-cache $ibnetdiscover_cache -S $switch_guid`;

	@iblinkinfo_lines = split("\n", $iblinkinfo_output);
	foreach $iblinkinfo_line (@iblinkinfo_lines) {

		$is_new_ibcore = 0;

		$iblinkinfo_line =~
/[\s]+[\d]+[\s]+[\d]+\[.+\] \=\=.+\=\=>[\s]+[\d]+[\s]+[\d]+\[.+\] \"(.+)\".+/;
		$switch_desc = $1;

		# IB spine switch number could be prefixed with "ibcore"
		if ($switch_desc =~ /ibcore/) {
			$switch_desc =~ /ibcore([\d]+) (.+)/;
			# achu: need scope, don't move out of if block
			$ibcorenum       = $1;
			$line_board_desc = $2;

			if ($last_ibcorenum >= 0) {
				if ($last_ibcorenum == $ibcorenum) {
					$last_ibcorenum_count++;
				} else {
					if ($last_ibcorenum_count < $ibcorenum_min_count) {
						$ibcorenum_min_count           = $last_ibcorenum_count;
						$ibcorenum_min_count_ibcorenum = $last_ibcorenum;
					}
					if ($last_ibcorenum_count > $ibcorenum_max_count) {
						$ibcorenum_max_count           = $last_ibcorenum_count;
						$ibcorenum_max_count_ibcorenum = $last_ibcorenum;
					}

					if ($last_ibcorenum_count != $ibcorenum_min_count) {
						print "Switch guid $switch_guid ($switch_name)\n";
						print
"  Connected to ibcore$last_ibcorenum different number of times than ibcore$ibcorenum_min_count_ibcorenum\n";
						$output_flag++;
					}
					if ($last_ibcorenum_count != $ibcorenum_max_count) {
						print "Switch guid $switch_guid ($switch_name)\n";
						print
"  Connected to ibcore$last_ibcorenum different number of times than ibcore$ibcorenum_max_count_ibcorenum\n";
						$output_flag++;
					}
					$last_ibcorenum       = $ibcorenum;
					$last_ibcorenum_count = 1;
					$is_new_ibcore++;
				}
			} else {
				$last_ibcorenum       = $ibcorenum;
				$last_ibcorenum_count = 1;
			}

			# line_board_desc in format
			# LXXX
			$line_board_desc =~ /L([\d]+)/;
			$line_board_num = $1;

			if ($iblinkinfo_line_board_connections{$line_board_num}) {
				$iblinkinfo_line_board_connections{$line_board_num}++;
			} else {
				$iblinkinfo_line_board_connections{$line_board_num} = 1;
			}

			if ($last_line_board_num > 0) {
				# XXX
				#
				# Special case Hacker: Due to number of Line Boards on
				# Sierra, We actually have Line Board L121 and L122
				# following L226 and L219 follows L208 and L119
				# follows L110. We do some hackery to work around it
				# here.
				if (
					$is_new_ibcore == 0
					&& (
						($last_line_board_num == 226 && $line_board_num == 121)
						|| (   $last_line_board_num == 208
							&& $line_board_num == 219)
						|| (   $last_line_board_num == 110
							&& $line_board_num == 119)
					)
				  )
				{
					# Do nothing in this case
				} elsif ($is_new_ibcore == 0
					&& $line_board_num != ($last_line_board_num + 1))
				{
					print "Switch guid $switch_guid ($switch_name)\n";
					print "  ibcore$ibcorenum connects to line board "
					  . "$line_board_num out of order\n";
				}
				$last_line_board_num = $line_board_num;
			} else {
				$last_line_board_num = $line_board_num;
			}
		}
	}

	foreach $line_board_num (keys(%iblinkinfo_line_board_connections)) {

		if ($iblinkinfo_line_board_connections{$line_board_num} <
			$line_board_min_count)
		{
			$line_board_min_count =
			  $iblinkinfo_line_board_connections{$line_board_num};
			$line_board_min_count_line_board = $line_board_num;
		}
		if ($iblinkinfo_line_board_connections{$line_board_num} >
			$line_board_max_count)
		{
			$line_board_max_count =
			  $iblinkinfo_line_board_connections{$line_board_num};
			$line_board_max_count_line_board = $line_board_num;
		}

		if ($iblinkinfo_line_board_connections{$line_board_num} !=
			$line_board_min_count)
		{
			print "Switch guid $switch_guid ($switch_name)\n";
			print "  Connected to Line Board $line_board_num "
			  . "different number of times than Line Board "
			  . "$line_board_min_count_line_board\n";
			$output_flag++;
		}
		if ($iblinkinfo_line_board_connections{$line_board_num} !=
			$line_board_max_count)
		{
			print "Switch guid $switch_guid ($switch_name)\n";
			print "  Connected to Line Board $line_board_num "
			  . "different number of times than Line Board "
			  . "$line_board_max_count_line_board\n";
			$output_flag++;
		}
	}
}

sub analyze_switch_routing
{
	my @fields;
	my $fields_len;
	my $i;
	my $tmp;
	my $flag = 0;

	# first sort all the hosts in each switch_ports entry
	for $i (@switch_port_possibilities) {
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

	if ($switch_guid_type{$switch_guid} eq "SPINE") {
		# nothing to analyze for spine switches
	} elsif ($switch_guid_type{$switch_guid} eq "LINE") {
		analyze_line_board();
	} elsif ($switch_guid_type{$switch_guid} eq "LEAF") {
		analyze_leaf_switch();
	} else {
		print "Switch guid $switch_guid ($switch_name)\n";
		print "  Switch not found in iblinkinfo output\n";
	}

	$switches_analyzed++;
	print "Finished analyzing ($switches_analyzed): $switch_name\n";
}

if (!getopts("ho:c:")) {
	usage();
}

if (defined($main::opt_h)) {
	usage();
}

if (defined($main::opt_o)) {
	$dump_lft_file = $main::opt_o;
} else {
	print STDERR ("Must specify dump lfts file\n");
	usage();
	exit 1;
}

if (defined($main::opt_c)) {
	$ibnetdiscover_cache = $main::opt_c;
} else {
	print STDERR ("Must specify ibnetdiscover cache file\n");
	usage();
	exit 1;
}

$iblinkinfo_output = `/usr/sbin/iblinkinfo --load-cache $ibnetdiscover_cache`;

@iblinkinfo_lines = split("\n", $iblinkinfo_output);

foreach $iblinkinfo_line (@iblinkinfo_lines) {
	chomp($iblinkinfo_line);

	if ($iblinkinfo_line !~ /Switch/) {
		next;
	}

	$iblinkinfo_line =~ /Switch (\w+)\s+(.+)/;
	$switch_guid = lc $1;
	$switch_name = $2;

	# IB leaf switch labeled ibswX
	# IB line board labeled ibcoreX LXXX
	# IB spine board labeled ibcoreX SXXXX
	if ($switch_name =~ /ibcore/) {
		if ($switch_name =~ /S/) {
			$switch_guid_type{$switch_guid} = "SPINE";
			$spine_count++;
		} else {
			$switch_guid_type{$switch_guid} = "LINE";
			$line_board_count++;
		}
	} elsif ($switch_name =~ /ibsw/) {
		$switch_guid_type{$switch_guid} = "LEAF";
		$leaf_count++;
	}
}

if ($spine_count == 0) {
	print STDERR "Zero spine switches found\n";
	exit 1;
}

if ($line_board_count == 0) {
	print STDERR "Zero line boards found\n";
	exit 1;
}

if ($leaf_count == 0) {
	print STDERR "Zero leaf switches found\n";
	exit 1;
}

if (!open(FH, "< $dump_lft_file")) {
	print STDERR ("Couldn't open dump_lfts file: $dump_lft_file: $!\n");
	exit 1;
}

@lft_lines = <FH>;

foreach $lft_line (@lft_lines) {
	chomp($lft_line);
	if ($lft_line =~ /Unicast/) {
		analyze_switch_routing();
		$lft_line =~ /Unicast lids .+ of switch Lid (.+) guid (.+) \((.+)\)/;
		$switch_lid   = $1;
		$switch_guid  = $2;
		$switch_name  = $3;
		%switch_ports = ();
		$host         = undef;
	} elsif ($lft_line =~ /Lid  Out/) {
	} elsif ($lft_line =~ /Port     Info/) {
	} elsif ($lft_line =~ /valid lids dumped/) {
	} elsif ($lft_line =~ /Channel/) {
		# Channel Adapters
		$lft_line =~ /.+ (.+) : \(.+ portguid .+: '(.+)'\)/;

		if ($switch_ports{$1}) {
			$switch_ports{$1} = "$switch_ports{$1},$2";
		} else {
			$switch_ports{$1} = "$2";
		}
		$host = $2;
	} elsif ($lft_line =~ /Router/) {
		# Routers
		$lft_line =~ /.+ (.+) : \(.+ portguid .+: '(.+)'\)/;

		if ($switch_ports{$1}) {
			$switch_ports{$1} = "$switch_ports{$1},$2";
		} else {
			$switch_ports{$1} = "$2";
		}
		$host = $2;
	} elsif ($lft_line =~ /Switch portguid/) {
		# Switches
	} elsif ($lft_line =~ /path/) {
		# LMC > 0 paths
		$lft_line =~ /.+ (.+) : \(path #. out of .: portguid .+\)/;
		if ($host) {
			if ($switch_ports{$1}) {
				$switch_ports{$1} = "$switch_ports{$1},$2";
			} else {
				$switch_ports{$1} = "$2";
			}
		}
	} else {
		next;
	}
}

analyze_switch_routing();

print "Number of Switches Analyzed: $switches_analyzed\n";

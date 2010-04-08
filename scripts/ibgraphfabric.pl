#!/usr/bin/perl
#
# Copyright (c) 2008 Lawrence Livermore National Security
#
# Produced at Lawrence Livermore National Laboratory.
# Written by Ira Weiny <weiny2@llnl.gov>.
# UCRL-CODE-235440
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the
# OpenIB.org BSD license below:
#
#     Redistribution and use in source and binary forms, with or
#     without modification, are permitted provided that the following
#     conditions are met:
#
#      - Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      - Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials
#        provided with the distribution.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

use strict;

use Getopt::Std;
use IBswcountlimits;
use Hostlist;

sub usage_and_exit
{
	my $prog = $_[0];
	print
"Usage: $prog [-hRe -S <guid> -C <ca_name> -P <ca_port> -r <string[,string]> ]\n";
	print
"   Report link speed and connection for each port of each switch which is active\n";
	print "   -h This help message\n";
	print
"   -R Recalculate cached information (Default is to reuse cached output)\n";
	print
"   -S <guid> generate for the switch specified by guid and its immediate links\n";
	print "   -C <ca_name> use selected Channel Adaptor name for queries\n";
	print "   -P <ca_port> use selected channel adaptor port for queries\n";
	print "   -r <string[,string]> regex's to search for root switch's\n";
	print
"   -e combine edges between nodes into one edge with an associated weight\n";
	print "   -M <node[,node]> plot routes from node to node in red\n";
	print "   -i <string[,string]> ignore nodes which match regex's given\n";
	print "   -g <cluster_name> DON'T print GUID's on nodes.\n";
	exit 0;
}

my $argv0             = `basename $0`;
my $regenerate_cache  = undef;
my $single_switch     = undef;
my $combine_edges     = undef;
my $ca_name           = "";
my $ca_port           = "";
my $dot_mode          = "1";
my @root_sw_regex     = ();
my @ignore_node_regex = ();
my $plot_nodes        = undef;
my @hosts             = ();
my $print_guids       = "yes";
chomp $argv0;

if (!getopts("hRS:C:P:r:eM:i:g:")) { usage_and_exit $argv0; }
if (defined $Getopt::Std::opt_h)   { usage_and_exit $argv0; }
if (defined $Getopt::Std::opt_R) { $regenerate_cache = $Getopt::Std::opt_R; }
if (defined $Getopt::Std::opt_S) { $single_switch    = $Getopt::Std::opt_S; }
if (defined $Getopt::Std::opt_C) { $ca_name          = $Getopt::Std::opt_C; }
if (defined $Getopt::Std::opt_P) { $ca_port          = $Getopt::Std::opt_P; }
if (defined $Getopt::Std::opt_r) {
	@root_sw_regex = split(",", $Getopt::Std::opt_r);
}
if (defined $Getopt::Std::opt_i) {
	@ignore_node_regex = split(",", $Getopt::Std::opt_i);
}
if (defined $Getopt::Std::opt_g) {
	$print_guids = $Getopt::Std::opt_g;
}
if (defined $Getopt::Std::opt_e) { $combine_edges = $Getopt::Std::opt_e; }
if (defined $Getopt::Std::opt_M) { $plot_nodes    = $Getopt::Std::opt_M; }

my $extra_smpquery_params = get_ca_name_port_param_string($ca_name, $ca_port);

# given a swguid and lid [$routing{$swguid}{$lid}] value is the port to route out
my %routing = undef;
# given a node name, map the valid lids for that node.
my %name2lids = undef;
my @lids      = undef;
if (defined $plot_nodes) {
	read_routing_file($regenerate_cache, $ca_name, $ca_port);
	@hosts = Hostlist::expand($plot_nodes);
	foreach my $host (@hosts) {
		#		print("$host -> @lids\n");
		push(@lids, $name2lids{$host});
	}
	#	print ("@lids\n");
	#	foreach my $t (keys %routing) {
	#		foreach my $t2 (keys %{$routing{$t}}) {
	#			print ("$t,$t2 -> $routing{$t}{$t2}\n");
	#		}
	#	}
}

sub main
{
	my %edges =
	  {}; # double hash which is defined by "node:port","node:port" and has a value of "weight"
	my %nodes = undef;    # nodes with desc and guid stored.
	my @roots = ();
	my @leafs = ();
	get_link_ends($regenerate_cache, $ca_name, $ca_port);
	foreach my $switch (sort (keys(%IBswcountlimits::link_ends))) {
		if ($single_switch && $switch ne $single_switch) {
			next;
		}
		my $num_ports = get_num_ports($switch, $ca_name, $ca_port);
		if ($num_ports == 0) {
			printf("ERROR: switch $switch has 0 ports???\n");
		}
		foreach my $port (1 .. $num_ports) {
			my $hr       = $IBswcountlimits::link_ends{$switch}{$port};
			my $psw      = $switch;
			my $rpsw     = $hr->{rem_guid};
			my $node_rec = undef;
			# skip nodes which match the regex's given
			my $ignore = "no";
			foreach my $regex (@ignore_node_regex) {
				if ($hr->{loc_desc} =~ /.*$regex.*/) { $ignore = "yes"; }
				if ($hr->{rem_desc} =~ /.*$regex.*/) { $ignore = "yes"; }
			}
			if ($ignore eq "yes") { next; }
			$psw =~ s/^0(x.*)/$1/;
			if ($rpsw ne "") {
				$rpsw =~ s/^0(x.*)/$1/;

				# store the nodes.
				$node_rec = {
					desc => $hr->{loc_desc},
					guid => $switch,
					lid  => $hr->{loc_sw_lid}
				};
				$nodes{$psw} = $node_rec;
				$node_rec = {
					desc => $hr->{rem_desc},
					guid => $hr->{rem_guid},
					lid  => $hr->{rem_lid}
				};
				$nodes{$rpsw} = $node_rec;

				# define the edges
				my $p  = $port;
				my $rp = $hr->{rem_port};

				# optionally Combine similar links with a weight
				# Do this by making dot think all links are
				# to "port 0"
				if (defined $combine_edges) {
					$p  = 0;
					$rp = 0;
				}
				my $pswnp  = join(":", $psw,  $p);
				my $rpswnp = join(":", $rpsw, $rp);
				if (exists $edges{$pswnp}{$rpswnp}) {
					$edges{$pswnp}{$rpswnp}++;
				} elsif (exists $edges{$rpswnp}{$pswnp}) {
					# do nothing
				} else {
					$edges{$pswnp}{$rpswnp} = 1;
				}
			}
			foreach my $regex (@root_sw_regex) {
				if ($hr->{loc_desc} =~ /.*$regex.*/) { push(@roots, $psw); }
				if ($hr->{rem_desc} =~ /.*$regex.*/) { push(@roots, $rpsw); }
			}
		}
	}

	# print headers
	printf("digraph G {\n");
	printf("   node [shape=record, fontsize=9];\n");
	my $ranksep = 1 + ((keys %nodes) * 0.5 / 10);
	printf("   graph [outputorder=nodesfirst, ranksep=\"%d equally\"];\n",
		$ranksep);

	# sort the nodes according to their number
	my @sorted_keys = sort {
		my $aa = 0;
		my $bb = 0;
		if ($nodes{$a}->{desc} =~ /\D*(\d*)/) {
			$aa = $1;
		}
		if ($nodes{$b}->{desc} =~ /\D*(\d*)/) {
			$bb = $1;
		}
		$aa <=> $bb;
	} keys %nodes;

	# print nodes
	foreach my $key (@sorted_keys) {
		if ("$key" ne "") {
			my $attr = "";
			if ($print_guids ne "yes"
				&& ($nodes{$key}->{desc} =~ /$print_guids.*/))
			{
				$attr = sprintf("label=\"%s\"", $nodes{$key}->{desc});
			} else {
				$attr = sprintf(
					"label=\"%s\\nG: %s\"",
					$nodes{$key}->{desc},
					$nodes{$key}->{guid}
				);
			}
			my @tmp = ($nodes{$key}->{desc});
			if (Hostlist::within(\@tmp, \@hosts)) {
				$attr = "$attr, color=\"red\"";
			}
			printf("   %18s [%s];\n", $key, $attr);
		}
	}

	# print edges
	foreach my $key1 (keys %edges) {
		foreach my $key2 (keys %{$edges{$key1}}) {
			my ($nd,  $p)  = split(":", $key1);
			my ($rnd, $rp) = split(":", $key2);
			my $attr = "";
			if ($p != 0 || $rp != 0) {
				# if the user did not combine the links print the ports
				# for the edge at the end of the link
				$attr = sprintf("taillabel=\"%d\", headlabel=\"%d\"", $p, $rp);
			} else {
				# combined links just print the "weight"
				$attr = sprintf("label=\"%d\"", $edges{$key1}{$key2});
			}
			# color combined edges blue
			if ($edges{$key1}{$key2} > 1) {
				$attr = "$attr, color=\"blue\"";
			} elsif (defined $plot_nodes) {
				# color requested routes red
				foreach my $lid (@lids) {
					#printf ("0$nd,$lid => %d ?= $p\n", $routing{"0$nd"}{$lid});
					if ($routing{"0$nd"}{$lid} == $p) {
						$attr = "$attr, color=\"red\"";
					} elsif ($routing{"0$rnd"}{$lid} == $rp) {
						$attr = "$attr, color=\"red\"";
					}
				}
			}
			printf("   %18s -> %18s [%s, arrowhead=\"none\"];\n",
				$nd, $rnd, $attr);
		}
	}

	# define roots if there were any
	if (scalar @roots) {
		printf("{rank=min; ");
		foreach my $root (@roots) {
			printf("$root; \n");
		}
		printf("}\n");
	}

	# close dot file
	printf("}\n");
}
main;

####################################
##  dump_lfts data  ################
####################################
#Unicast lids [0x0-0xe5c] of switch Lid 11 guid 0x0008f10400412824 (ISR9024D Voltaire):
#  Lid  Out   Destination
#       Port     Info
#0x0002 007 : (Switch portguid 0x0008f10400412806: 'ISR9024D Voltaire')
#0x0003 003 : (Switch portguid 0x0008f10400411a0e: 'ISR9024D Voltaire')
#0x0004 003 : (Switch portguid 0x0008f10400411c10: 'ISR9024D Voltaire')
# ...
#0x000d 004 : (Switch portguid 0x0008f10400401641: 'ISR2012 Voltaire sFB-2012')
#0x000e 004 : (Switch portguid 0x0008f10400401642: 'ISR2012 Voltaire sFB-2012')
#0x000f 004 : (Switch portguid 0x0008f10400401643: 'ISR2012 Voltaire sFB-2012')
#0x0011 004 : (Switch portguid 0x0008f10400411d48: 'ISR9024D Voltaire')
#0x0012 004 : (Switch portguid 0x0008f104003f1e54: 'ISR2012/ISR2004 Voltaire sLB-2024')
#0x0013 004 : (Switch portguid 0x0008f104003f1e55: 'ISR2012/ISR2004 Voltaire sLB-2024')
# ...
#0x0088 007 : (Channel Adapter portguid 0x0002c9020024d26d: 'minos514')
#0x008c 005 : (Channel Adapter portguid 0x0002c9020024c961: 'minos311')
#0x0090 012 : (Channel Adapter portguid 0x0002c90200246a89: 'minos362')
# ...
#0x0404 001 : (Channel Adapter portguid 0x0002c9020024d3e1: 'minos703')
#0x0405 002 : (path #2 out of 4: portguid 0x0002c9020024d3e1)
#0x0406 003 : (path #3 out of 4: portguid 0x0002c9020024d3e1)
#0x0407 004 : (path #4 out of 4: portguid 0x0002c9020024d3e1)
#969 valid lids dumped
sub read_routing_file
{
	my $regenerate_cache = $_[0];
	my $ca_name          = $_[1];
	my $ca_port          = $_[2];

	my $cache_file = get_routing_cache_file($ca_name, $ca_port);

	if ($regenerate_cache || !(-f "$cache_file")) {
		generate_routing_file($ca_name, $ca_port);
	}

	my %guid2name = undef;

	my ($swlid, $swguid);
	my $line = "";
	open(IN, $cache_file);
	while ($line = <IN>) {
		chomp $line;
		if (not $line) { next; }    # skip blank lines
		if ($line =~ /^Unicast lids/)    # starting table for new switch
		{
			($swlid, $swguid) = ($line =~ /Lid (\w+) guid (0x\w+) /);
			next;
		}
		if ($line =~ /^(0x\w+) (\d+) : .* portguid (0x\w+): '(\w+)'/) {
			my ($lid, $port, $guid, $name) = ($1, $2, $3, $4);
			$routing{$swguid}{$lid} = $port;
			$guid2name{$guid} = $name;
			$name2lids{$name} = ($name2lids{$name}, $lid);
		} elsif ($line =~ /^(0x\w+) (\d+) : .* portguid (0x\w+)/) {
			my ($lid, $port, $guid) = ($1, $2, $3);
			my $name = $guid2name{$guid};
			$routing{$swguid}{$lid} = $port;
			$name2lids{$name} = ($name2lids{$name}, $lid);
		}
	}
	close(IN);
}

# Get routing file cached
sub get_routing_cache_file
{
	my $ca_name = $_[0];
	my $ca_port = $_[1];
	ensure_cache_dir;
	return ("$IBswcountlimits::cache_dir/lft_dump-$ca_name-$ca_port");
}

# generate the routing file
sub generate_routing_file
{
	my $ca_name      = $_[0];
	my $ca_port      = $_[1];
	my $cache_file   = get_routing_cache_file($ca_name, $ca_port);
	my $extra_params = get_ca_name_port_param_string($ca_name, $ca_port);

	`dump_lfts.sh $extra_params > $cache_file`;
	if ($? != 0) {
		die "Execution of ibnetdiscover failed with errors\n";
	}
}

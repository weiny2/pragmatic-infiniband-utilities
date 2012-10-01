#!/usr/bin/perl

use strict;

use Getopt::Std;

my $switches = "";
my $ports = "1-36";
my $interval = 5;
my $dir = "profile-switches";

my @mins=(0,     0.125, 0.5,1,2,3,5);
my @maxs=(0.125, 0.500,   1,2,3,5,10);
my $range_cnt = 7;

my $rawdata = "";

sub usage
{
	my $prog = `basename $0`;

	chomp($prog);
	print "Usage: $prog -L <LIDs> -d <dir> [-p <port(s)>] [-i <interval>]\n";
	print "  -L <LIDs> comma separated list of nodes to monitor\n";
	print "  -d <dir> directory to place output in (default $dir)\n";
	print "           NOTE: will append Date/Time stamp\n";
	print "  -p port(s) of switch to monitor (default $ports)\n";
	print "     input a single port (i.e. 1)\n";
	print "     or separate ports by comma for set of ports (i.e. 1,2,3,4)\n";
	print "     or separate ports by - for single range ports (i.e. 1-4)\n";
	print "  -i interval seconds between poll (default $interval)\n";
	print "  -t Don't put date time stamp on output directory\n";
	print "     WARNING: this option will cause the script to remove the directory specified\n";
	print "  -r <rawdata> use rawdata file rather than running perfmon again\n";
	exit 2;
}

if (!getopts("hL:p:d:i:tr:")) {
	usage();
}

if (defined($main::opt_h)) {
	usage();
}

# Required Options
if (defined($main::opt_L)) {
	$switches = $main::opt_L;
} else {
	print "LID list must be specified\n";
	exit 1;
}
if (defined($main::opt_d)) {
	$dir = $main::opt_d;
} else {
	print "dir must be specified\n";
	exit 1;
}

if (defined($main::opt_p)) {
	$ports = $main::opt_p;
}

if (defined($main::opt_i)) {
	$interval = $main::opt_i;
}

if (defined($main::opt_t)) {
	rmdir $dir
} else {
	my $dts=`date +%F_%X`;
	chomp $dts;
	$dir="$dir-$dts";
}

if (defined($main::opt_r)) {
	$rawdata = $main::opt_r;
}

`mkdir -p $dir`;

my @datalines = ();

if ($rawdata eq "") {
	my $file="$dir/rawdata";
	open(RAW, ">$file") || die "could not open: $file";
	my $cmd = "perfmon.pl -L $switches -p $ports -i $interval -n 1";
	print RAW "$cmd\n\n";
	my $data = `$cmd`;
	print RAW "$data";
	@datalines = split("\n", $data);
} else {
	open(INPUT, "<$rawdata") || die "could not open: $rawdata";

	my @nodelist = split(",", $switches);
	my %nodehash = {};
	foreach my $node (@nodelist) {
	   	$nodehash{$node} = $node;
	}
	my @data = <INPUT>;
	foreach my $line (@data) {
		if ($line =~ /.* node=(.+) port.*/) {
		   	if ($nodehash{$1} == $1) {
				chomp $line;
				push (@datalines, $line);
			}
		}
	}
	close INPUT;
}

my $gminrate = 10; # start high
my $gmaxrate = 0; # start low

my $i = 0;
for($i = 0; $i < $range_cnt; $i++) {
	my $lminrate = 10;
	my $lmaxrate = 0;
	my $file="$dir/data-min-$mins[$i]-max-$maxs[$i]";
	open(OUT, ">$file") || die "could not open: $file";
	foreach my $line (@datalines) {
		if ($line =~ /1: .* rate=(.+) gigabytes.*/) {
			if ($mins[$i] <= $1 && $1 < $maxs[$i]) {
				print OUT "$line\n";
				if ($1 < $lminrate) {
					$lminrate = $1;
				}
				if ($1 > $lmaxrate) {
					$lmaxrate = $1;
				}
			}
			if ($1 < $gminrate) {
				$gminrate = $1;
			}
			if ($1 > $gmaxrate) {
				$gmaxrate = $1;
			}
		}
	}
	print OUT "Max rate: $lmaxrate; Min rate: $lminrate\n";
	close OUT;
}

if ($rawdata == "") {
	print RAW "Max rate: $gmaxrate; Min rate: $gminrate\n";
	close RAW;
}
print "Max rate: $gmaxrate; Min rate: $gminrate\n";


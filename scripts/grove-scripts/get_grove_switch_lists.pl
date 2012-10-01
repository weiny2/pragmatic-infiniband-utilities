#!/usr/bin/perl

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
#my $data = `ibswitches | egrep "SNR"`;
my $data = $ibswitches;
my @datalines = split("\n", $data);
foreach my $line (@datalines) {
	chomp $line;
	if ($line =~ /.*SNR.*/) {
		if ($line =~ /.* lid (.+) lmc/) {
			$lids{$1} = $line;
		}
	}
}
open(OUTPUT, ">$ion_switches") || die "could not open: $ion_switches";
foreach my $key (sort {$a<=>$b} keys %lids) {
	print OUTPUT "$key,";
}
print OUTPUT "\n";
close OUTPUT;

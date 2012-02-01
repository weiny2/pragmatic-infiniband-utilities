#!/usr/bin/perl

use strict;
use warnings;

# Jeff Ogden (jbogden@sandia.gov)
#
# Rough script to verify the link width and speed of the PCI Express slot used for
# the Qlogic IB HCA on TLCC2 compute nodes, i.e. nodes with just the Qlogic
# HCA.
#
# Exits with EXIT_SUCCESS either way, but upon error will output text to
# STDOUT explaining what's wrong.

# for lspci output
my $PCI_DEV_STRING = "02:00.0";
my $EXPECTED_SPEED = "5GT/s";
my $EXPECTED_WIDTH = "x8";
my $EXPECTED_CONFIG_BYTE_72 = "82";  # hex

sub check_lspci()
{
    my $dev = "";
    my @problems;
    my @lspci_output = `/sbin/lspci -vv -s $PCI_DEV_STRING`;
    my $foundlink = 0;

    for my $line (@lspci_output) {
        chomp($line);
        if($line =~ m/^$PCI_DEV_STRING/) {
            $dev = $line;
        }
        if($line =~ m/LnkCap:\s+Port #\S+, Speed/) {
            $foundlink = 1;
            my ($speed, $width) = $line =~ m/LnkCap:\s+Port \S+, Speed (\S+), Width (\S+),/;
            if($speed ne $EXPECTED_SPEED || $width ne $EXPECTED_WIDTH) {
                $line =~ s/^\s+//;
                push(@problems, {'dev' => $dev, 'link' => $line});
            }
        }
    }

    (!$foundlink) and print "check_lspci() Problem finding Link information from lspci\n";

    for my $problem (@problems) {
        print "check_lspci() Problem with '$problem->{dev}'\n" .
              "   Should be: LnkCap:\tPort #0, Speed $EXPECTED_SPEED, Width $EXPECTED_WIDTH\n" .
              "   But is:    $problem->{'link'}\n";
    }
}


check_lspci();

exit 0;


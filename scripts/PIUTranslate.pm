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

#
# By Albert Chu
#
# Routines for translating between hostname types
#
# e.g. node1 -> node1 HCA-1
#
# Input file format
#
# str,str
#

use strict;

package PIUTranslate;

our $VERSION = "0.01";

require Exporter;
our @ISA       = qw(Exporter);
our @EXPORT_OK = qw(translate_load, translate_to, translate_from);

if (!$PIUTranslate::included) {
    $PIUTranslate::included = 1;

    my %ltor = ();
    my %rtol = ();

    # Load a translate file
    sub translate_load
    {
	my ($filename) = @_;
	my $l;
	my $r;

	if (open(FILE, "< $filename")) {
	    while (<FILE>) {
		s/\#.*//;    # strip comments
		s/^\s*//;    # strip leading spaces
		s/\s*$//;    # strip trailing spaces

		next if (/^\s*$/);    # skip blank lines

		($l, $r) = split(/,/);

		$l =~ s/\s*$//;       # strip trailing spaces
		$r =~ s/^\s*//;       # strip leading spaces

		next if ($l =~ /^\s*$/);
		next if ($r =~ /^\s*$/);

		$ltor{$l} = $r;
		$rtol{$r} = $l;
	    }
	    close(FILE);
	}
    }

    sub translate_to
    {
	my ($node) = @_;

	if ($ltor{$node}) {
	    return $ltor{$node};
	}
	else {
	    return $node;
	}
    }

    sub translate_from
    {
	my ($node) = @_;

	if ($rtol{$node}) {
	    return $rtol{$node};
	}
	else {
	    return $node;
	}
    }

}    # PIUTranslate::included

1;   # return a true value...

#############################################################################
#  PIUHostlist.pm,v 1.21 2008/07/09 22:53:41 chu11 Exp
#############################################################################
#  Copyright (C) 2007 Lawrence Livermore National Security, LLC.
#  Copyright (C) 2001-2007 The Regents of the University of California.
#  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
#  Written by Jim Garlick <garlick@llnl.gov> and Albert Chu <chu11@llnl.gov>.
#  UCRL-CODE-2003-004.
#
#  This file is part of Gendersllnl, a cluster configuration database
#  and rdist preprocessor for LLNL site specific needs.  This package
#  was originally a part of the Genders package, but has now been
#  split off into a separate package.  For details, see
#  <http://www.llnl.gov/linux/genders/>.
#
#  Genders is free software; you can redistribute it and/or modify it under
#  the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 2 of the License, or (at your option)
#  any later version.
#
#  Genders is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
#  details.
#
#  You should have received a copy of the GNU General Public License along
#  with Genders.  If not, see <http://www.gnu.org/licenses/>.
#############################################################################

#
# Routines for reading, normalizing, and displaying lists of hosts.
#

package PIUHostlist;

use strict;
use Carp;

use Gendersllnl;

our $VERSION = "0.01";

require Exporter;
our @ISA       = qw(Exporter);
our @EXPORT_OK = qw(mk_file mk_gend mk_cmdline to_initial to_reliable
  detect_metachar expand compress intersect union diff
  xor within same);

our %EXPORT_TAGS = (
	all => [
		qw(mk_file mk_gend mk_cmdline to_initial to_reliable
		  detect_metachar expand compress intersect union diff
		  xor within same)
	],
	mk      => [qw(mk_file mk_gend mk_cmdline)],
	to      => [qw(to_initial to_reliable)],
	convert => [qw(expand compress)],
	set     => [qw(intersect union diff xor within same)],
	misc    => [qw(detect_metachar)],
);

if (!$PIUHostlist::included) {
	$PIUHostlist::included = 1;

	# compress will generate a quadrics-style range if this is > 0
	$PIUHostlist::quadrics_ranges = 1;

	# Construct node list from hostlist file
	#   $fileName (IN)      hostlist filename
	#   RETURN              list of nodes
	sub mk_file
	{
		my ($fileName) = @_;
		my (@targetNodes);

		if (open(FILE, "< $fileName")) {
			while (<FILE>) {
				chomp;
				s/\*.*$//;     # strip comments (*)
				s/\!.*$//;     # strip comments (!)
				s/^\s+.*//;    # strip leading spaces
				s/.*\s+$//;    # strip trailing spaces
				next if (/^\s*$/);    # skip blank lines
				push(@targetNodes, $_);
			}
			close(FILE);
		}
		return @targetNodes;
	}

	# Construct node list from genders attribute name
	#   $attrName (IN)      attribute name
	#   RETURN              list of nodes
	sub mk_gend
	{
		my ($attrName) = @_;
		my $obj;

		$obj = Gendersllnl->new();
		return $obj->getnodes($attrName);
	}

	# Construct node list from command line
	#   $cmdLine (IN)       comma-separated list of nodes
	#   RETURN              list of nodes
	sub mk_cmdline
	{
		my ($cmdLine) = @_;
		my (@targetNodes);

		@targetNodes = split(/,/, $cmdLine);
		return @targetNodes;
	}

	# Convert list of hostnames from reliable_hostname to initial_hostname.
	# OK if already initial_hostname.
	#   @inList (IN)        list of reliable_hostnames
	#   RETURN              list of initial_hostnames
	sub to_initial
	{
		my (@inList) = @_;
		my (@outList, $node, $iname);
		my $obj;

		$obj = Gendersllnl->new();
		foreach $node (@inList) {
			($node) = split(/\./, $node);    # shorten name
			$iname = $obj->to_gendname_preserve($node);
			push(@outList, $iname);
		}

		return @outList;
	}

	# Convert list of hostnames from initial_hostname to reliable_hostname.
	# OK if already reliable_hostname.
	#   @inList (IN)        list of initial_hostnames
	#   RETURN              list of reliable_hostnames
	sub to_reliable
	{
		my (@inList) = @_;
		my (@outList, $node, $rname);
		my $obj;

		$obj = Gendersllnl->new();
		foreach $node (@inList) {
			($node) = split(/\./, $node);    # shorten name
			$rname = $obj->to_altname_preserve($node);
			push(@outList, $rname);
		}

		return @outList;
	}

	# Detect shell metacharacters in a hostlist entry.
	#   $line (IN)          hostlist line
	#   RETURN              true if metachars found, false otherwise
	sub detect_metachar
	{
		my ($line) = @_;

		return ($line =~ /(\;|\||\&)/);
	}

	# expand()
	# turn a hostname range into a list of hostnames. Try to autodetect whether
	# a quadrics-style range or a normal hostname range was passed in.
	#
	sub expand
	{
		my ($list) = @_;

		if ($list =~ /\[/ && $list !~ /[^[]*\[.+\]/) {
			# Handle case of no closing bracket - just return
			return ($list);
		}

		# matching "[" "]" pair with stuff inside will be considered a quadrics
		# range:
		if ($list =~ /[^[]*\[.+\]/) {
			# quadrics ranges are separated by whitespace in RMS -
			# try to support that here
			$list =~ s/\s+/,/g;

			#
			# Replace ',' chars internal to "[]" with ':"
			#
			while ($list =~ s/(\[[^\]]*),([^\[]*\])/$1:$2/) { }

			return map { expand_quadrics_range($_) } split /,/, $list;

		} else {
			return map {
				     s/(\w*?)(\d+)-(\1|)(\d+)/"$2".."$4"/
				  || s/(.+)/""/;
				map { "$1$_" } eval;
			} split /,/, $list;
		}
	}

	# expand_quadrics_range
	#
	# expand nodelist in quadrics form
	#
	sub expand_quadrics_range
	{
		my ($list) = @_;
		my ($pfx, $ranges, $suffix) = split(/[\[\]]/, $list, 3);

		return $list if (!defined $ranges);

		return map { "$pfx$_$suffix" }
		  map { s/^(\d+)-(\d+)$/"$1".."$2"/ ? eval : $_ }
		  split(/,|:/, $ranges);
	}

	# compress_to_quadrics
	#
	# compress a list of nodes to into a quadrics-style list of ranges
	#
	sub compress_to_quadrics
	{
		my (@list) = @_;
		local $PIUHostlist::quadrics_ranges = 1;
		return compress(@list) if @list;
	}

	# Turn long lists of nodes with numeric suffixes into ranges where possible
	# optionally return a Quadrics-style range if $quadrics_ranges is nonzero.
	#
	#   @nodes (IN)         flat list of nodes
	#   RETURN              list of nodes possibly containing ranges
	#
	sub compress
	{
		my %rng  = comp2(@_);
		my @list = ();

		local $" = ",";

		if (!$PIUHostlist::quadrics_ranges) {
			foreach my $k (keys %rng) {
				@{$rng{$k}} = map { "$k$_" } @{$rng{$k}};
			}
			@list = map { @{$rng{$_}} } sort keys %rng;

		} else {
			@list = map {
				$_
				  . (
					@{$rng{$_}} > 1 || ${$rng{$_}}[0] =~ /-/
					? "[@{$rng{$_}}]"
					: "@{$rng{$_}}"
				  )
			} sort keys %rng;
		}

		return wantarray ? @list : "@list";
	}

   # comp2():
   #
   # takes a list of names and returns a hash of arrays, indexed by name prefix,
   # each containing a list of numerical ranges describing the initial list.
   #
   # e.g.: %hash = comp2(lx01,lx02,lx03,lx05,dev0,dev1,dev21)
   #       will return:
   #       $hash{"lx"}  = ["01-03", "05"]
   #       $hash{"dev"} = ["0-1", "21"]
   #
	sub comp2
	{
		my (%i) = ();
		my (%s) = ();

		# turn off warnings here to avoid perl complaints about
		# uninitialized values for members of %i and %s
		local ($^W) = 0;
		push(
			@{
				$s{$$_[0]}[
				  (
					  $s{$$_[0]}[$i{$$_[0]}][$#{$s{$$_[0]}[$i{$$_[0]}]}] ==
					    ($$_[1] - 1)
				  ) ? $i{$$_[0]} : ++$i{$$_[0]}
				]
			  },
			($$_[1])
		) for map { [/(.*?)(\d*)$/] } sortn(@_);

		for my $key (keys %s) {
			@{$s{$key}} =
			  map { $#$_ > 0 ? "$$_[0]-$$_[$#$_]" : @{$_} } @{$s{$key}};
		}

		return %s;
	}

	# uniq: remove duplicates from a hostlist
	#
	sub uniq
	{
		my %seen = ();
		grep { !$seen{$_}++ } @_;
	}

	# intersect(\@a, \@b): return the intersection of two lists,
	#                      i.e. those hosts in both @a and @b.
	# IN : two array refs \@a , \@b
	# OUT: flat list of hosts in =both= @a and @b
	sub intersect
	{
		my ($a, $b) = @_;
		(ref $a && ref $b)
		  or croak "Error: arguments to intersect must be references";
		my @result = ();

		for my $hn (@$a) {
			push(@result, grep { $_ eq $hn } @$b);
		}

		return @result;
	}

	# union(\@a, \@b): return the union of two lists
	#                  i.e. list of hosts from both @a and @b
	# IN : two array refs \@a, \@b
	# OUT: flat list of hosts from @a and @b
	sub union
	{
		my ($a, $b) = @_;
		(ref $a && ref $b)
		  or croak "Error: arguments to union must be references";
		return uniq(@$a, @$b);
	}

	# diff(\@a, \@b): return the list of hosts in @a that are not in @b
	#                 i.e. hosts in @a - hosts in @b
	# IN : two array refs \@a, \@b
	# OUT: flat list of hosts in @a that are not in @b
	sub diff
	{
		my ($a, $b) = @_;
		(ref $a && ref $b)
		  or croak "Error: arguments to diff must be references";
		my @result = ();

		for my $hn (@$a) {
			push(@result, $hn) if (!grep { $_ eq $hn } @$b);
		}

		return @result;
	}

	# xor(\@a, \@b): exclusive OR hosts in @a and @b
	#                i.e. those hosts in @a and @b but not in both
	# IN : two array refs \@a, \@b
	# OUT: flat list of hosts in @a and @b but not in both
	sub xor
	{
		my ($a, $b) = @_;
		(ref $a && ref $b)
		  or croak "Error: arguments to xor must be references";
		return (diff($a, $b), diff($b, $a));
	}

	# within(\@a, \@b): true if all hosts in @a are in @b
	#                   i.e. is @a a subset of @b?
	# IN : two array refs \@a, \@b
	# OUT: true if @a is a subset of @b, false otherwise.
	sub within
	{
		my ($a, $b) = @_;
		(ref $a && ref $b)
		  or croak "Error: arguments to within must be references";
		return (intersect($a, $b) == @$a);
	}

	# same(\@a, \@b) : true if @a and @b contain the exact same hosts
	#                  i.e. are @a and @b the same list?
	# IN : two array refs \@a, \@b
	# OUT: true if @a is same as @b, false otherwise
	sub same
	{
		my ($a, $b) = @_;
		(ref $a && ref $b)
		  or croak "arguments to same must be references";
		return (within($a, $b) && within($b, $a));
	}

	# sortn:
	#
	# sort a group of alphanumeric strings by the last group of digits on
	# those strings, if such exists (good for numerically suffixed host lists)
	#
	sub sortn
	{
		map { $$_[0] }
		  sort { ($$a[1] || 0) <=> ($$b[1] || 0) } map { [$_, /(\d*)$/] } @_;
	}

}    # PIUHostlist::included

1;   # return a true value...

__END__

=head1 NAME

PIUHostlist - Routines for operating on lists of hosts.

=head1 SYNOPSIS

 use PIUHostlist;

 PIUHostlist::mk_file($fileName)
 PIUHostlist::mk_gend($attrName) 
 PIUHostlist::mk_cmdline($cmdLine)

 PIUHostlist::to_initial(@reliableNames)
 PIUHostlist::to_reliable(@initialNames)

 PIUHostlist::detect_metachar($entry)
 PIUHostlist::expand($hostList)
 PIUHostlist::compress(@inList)

 PIUHostlist::intersect(\@a, \@b)
 PIUHostlist::union(\@a, \@b)
 PIUHostlist::diff(\@a, \@b)
 PIUHostlist::xor(\@a, \@b)
 PIUHostlist::within(\@a, \@b)
 PIUHostlist::same(\@a, \@b)

 $PIUHostlist::quadrics_ranges

=head1 DESCRIPTION

This package provides routines for reading, converting, operating, and
displaying lists of hosts.

=over 4

=item B<PIUHostlist::mk_file($fileName)>

Returns a list of the hostnames listed in the specified file.  

=item B<PIUHostlist::mk_gend($attrName)> 

Returns a list of hostnames with the specified genders attribute.

=item B<PIUHostlist::mk_cmdline($cmdLine)>

Returns a list of hostnames originally separated by commas on the
command line.

=item B<PIUHostlist::to_initial(@reliableNames)>

Returns a list of initial hostnames, converted from the specified list
of reliable hostnames.  Hostnames are preserved if the conversion
fails.

=item B<PIUHostlist::to_reliable(@initialNames)>

Returns a list of reliable hostnames, converted from the specified
list of initial hostnames.  Hostnames are preserved if the conversion
fails.

=item B<PIUHostlist::detect_metachar($entry)>

Returns true if shell metacharacters are detected in the specified
hostlist entry.

=item B<PIUHostlist::expand($hostList)>

Return a list of hostnames based on the specified hostrange.

=item B<PIUHostlist::compress(@inList)>

Return a hostrange based on a list of hostnames with identical
prefixes.

=item B<PIUHostlist::intersect(\@a, \@b)>

Returns the intersection of two lists.

=item B<PIUHostlist::union(\@a, \@b)>

Returns the union of two lists.  

=item B<PIUHostlist::diff(\@a, \@b)>

Returns the list of hosts @a that are not in @b.

=item B<PIUHostlist::xor(\@a, \@b)>

Returns the exclusive OR of hosts in @a and @b.

=item B<PIUHostlist::within(\@a, \@b)>

Returns true if all hosts in @a are in @b.

=item B<PIUHostlist::same(\@a, \@b)>

Returns true if @a and @b contain the exact same hosts.

=item B<$PIUHostlist::quadrics_ranges>

If set to 0, quadrics style host ranges will not be used.

=back

=head1 EXPORT SYMBOLS

When the PIUHostlist module is loaded, the following symbols can be
exported.

=over 4

=item mk_file

=item mk_gend

=item mk_cmdline

=item to_initial

=item to_reliable

=item detect_metachar

=item expand

=item compress

=item intersect

=item union

=item diff

=item xor

=item within

=item same

=back

=head1 EXPORT TAGS

When the PIUHostlist module is loaded, the following tags can be used to
export the following sets of symbols.

=over 10

=item B<all>

mk_file, mk_gend, mk_cmdline, to_initial, to_reliable,
detect_metachar, expand, compress, intersect, union, diff, xor,
within, same

=item B<mk>

mk_file, mk_gend, mk_cmdline

=item B<to>

to_initial, to_reliable

=item B<convert>

expand, compress

=item B<set>

intersect, union, diff, xor, within, same

=item B<misc>

detect_metachar

=back

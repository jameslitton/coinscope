#!/usr/bin/perl

use strict;
use warnings;

use Data::Dumper;

my $commit_hash = `git rev-parse HEAD`;
$commit_hash =~ s/\n//g;

open(my $fp, '<', "shared/includes/autogen.hpp") or die "$!";

open(my $new, '>', "shared/includes/autogen.hpp.new") or die "$!";

while(<$fp>) {
	s/(const char commit_hash\[\] = )"(.*)"/$1"$commit_hash"/;
	print {$new} $_;
}

close($fp);
close($new);

rename("shared/includes/autogen.hpp.new", "shared/includes/autogen.hpp") or die "$!";

#!/usr/bin/env perl

use strict;
use warnings;
use utf8;
use feature qw{ postderef say signatures state };
no warnings qw{ experimental::postderef experimental::signatures };

foreach(@ARGV) {
    my $inputfile = $_;

    if($inputfile =~ /^(.*)_(\d+).log$/) {
	my $formula = $1;
	my $assumption_id = $2;

	my $formula_datafile = "$formula.data";

	open my $data, '>>', $formula_datafile or die "Could not open $inputfile: $!";

	# Parse the file for the relevant QuAPI line
	open my $info, $inputfile or die "Could not open $inputfile: $!";

	while(my $line = <$info>)  {
	    if($line =~ /^(\d+) (([0-9]*[.])?[0-9]+) (\w+) (\d+) (.*)$/) {
		print $data $line;
	    }
	}

	close $info;

	close $data;
    }
}

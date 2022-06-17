#!/usr/bin/env perl

use strict;
use warnings;
use utf8;
use feature qw{ postderef say signatures state };
no warnings qw{ experimental::postderef experimental::signatures };
use DBI;

sub ParseAndInsertFile {
    use File::Basename;
    my $stmt = $_[0];
    my $datafile_path = $_[1];

    my $bname = basename($datafile_path);

    # Irfansha's file naming scheme together with encoding variant.
    if($bname =~ /^(\D+)_(.*)_(\d+)_(\d+)\.data$/) {
	my $encoding = $1;
	my $problem = $2;
	my $intsplit = $3;
	my $states = $4;

	open my $fh, $datafile_path or die "Could not open $datafile_path: $!";

	while(my $line = <$fh>)  {
	    if($line =~ /^(\d+) (([0-9]*[.])?[0-9]+) (\w+) (\d+) (.*)$/) {
		my $timens = $1;
		my $times = $2;
		my $result = $4;
		my $assumption_id = $5;
		my $assumption = $6;

		$stmt->execute($datafile_path, $encoding, $problem, $intsplit, $states, $result, $assumption_id, $assumption, $times);
	    }
	}

	close $fh;
    }
}

sub ProcessFolder {
    my $folder = $_[0];
    my $dbh = $_[1];

    my $preprocessor = "";
    my $solver = "";
    my $layers = "";
    my $topped = "0";

    if($folder =~ m/^(\w+)_(\w+)_(\d+)_(\w+)$/) {
	$preprocessor = $1;
	$solver = $2;
	$layers = $3;
	if($4 eq "topped") {
	    $topped = 1;
	}
    } else {
	say "Folder $folder does not follow naming constraints!";
	return
    }

    say "Processing folder \"$folder\" into table \"data\"...";

    my $stmt_insert = "INSERT INTO data VALUES (\"$preprocessor\", \"$solver\", $layers, $topped, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    say $stmt_insert;
    my $stmt_insert_prepared = $dbh->prepare($stmt_insert);

    my @datafiles = glob "$folder/*.data";
    foreach my $l (@datafiles) {
        ParseAndInsertFile($stmt_insert_prepared, $l);
    }

    $dbh->do("COMMIT;");
}

my $dbpath = $ARGV[$#ARGV];

# Connect to database.
my $dbh = DBI->connect("DBI:SQLite:dbname=$dbpath", "", "", {
        RaiseError => 0,
        AutoCommit => 0 })
    or die $DBI::errstr;

my $stmt_create_table = qq(CREATE TABLE IF NOT EXISTS data (
    preprocessor TEXT NOT NULL,
    solver TEXT NOT NULL,
    layers INTEGER NOT NULL,
    topped BOOLEAN NOT NULL,
    file TEXT NOT NULL,
    encoding TEXT NOT NULL,
    problem TEXT NOT NULL,
    intsplit INTEGER NOT NULL,
    states INTEGER NOT NULL,
    result TEXT NOT NULL,
    assumption_id INTEGER NOT NULL,
    assumption TEXT NOT NULL,
    time REAL NOT NULL);
);
my $rv = $dbh->do($stmt_create_table);
if($rv < 0) {
    say $DBI::errstr;
} else {
    say "Table data created successfully.";
}
$dbh->do("COMMIT;");

$dbh->do("PRAGMA synchronous = OFF;");

foreach(my $i = 0; $i < $#ARGV; $i++) {
    my $inputfolder = $ARGV[$i];
    ProcessFolder $inputfolder, $dbh;
}

$dbh->disconnect();

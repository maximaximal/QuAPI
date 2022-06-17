#!/usr/bin/env perl

use strict;
use warnings;
use utf8;
use feature qw{ postderef say signatures state };
no warnings qw{ experimental::postderef experimental::signatures };
use Digest::SHA qw(sha256_hex);

use Data::Dumper;
use Cwd;
use File::Basename;

if($@) {
    use Getopt::Long 'HelpMessage';
}

my $norunlim = 0;
my $top_out_intsplits = 0;

GetOptions(
    'help|h'             => sub { HelpMessage(0) },
    'executable|e=s'     => \   my $executable,
    'solver|s=s'         => \   my $solver,
    'glob|g=s{1,}'       => \   my @problemglobs,
    'name|n=s'           => \ ( my $name = "quapify" ),
    'cpus_per_task=n'    => \ ( my $cpus_per_task = 2 ),
    'intsplit-layers=n'  => \ ( my $intsplit_layers = 1 ),
    'runlim=s'           => \ ( my $runlim = "/usr/local/bin/runlim" ),
    'norunlim!'          => \$norunlim,
    'top-out-intsplits!' => \$top_out_intsplits,
    'time=n'             => \ ( my $time = 10000 ),
    'space=n'            => \ ( my $space = 8000 ),
    'nice=n'             => \ ( my $nice = 0 ),
) or HelpMessage(1);

sub mylog($msg) {
    say STDERR $msg;
}

my $additional_args = join(" ", @ARGV);
mylog "Giving arguments to $executable: $additional_args";

if(not $norunlim and -e $runlim) {
    $runlim = "$runlim --real-time-limit=\"$time\" --space-limit=\"$space\" -p";
    mylog "Runlim active! Command: $runlim";
} else {
    # Deactivate runlim
    $runlim = "";
    mylog "Runlim inactive!";
}

die "Require an executable to run!" unless $executable;
die "Require a name!" unless $name;
die "Require a solver!" unless $solver;

# Generate jobs array
my @problems = [];

if(@problemglobs) {
    mylog "Problem globs specified! Resolving $#problemglobs globs.";
    foreach(@problemglobs) {
        push(@problems, glob "$_");
    }
}

if($#problems == 0) {
    mylog "No problems found! Aborting batch call.";
    exit(-1);
}

my $task_problems_map = "";
my $task_assumption_map = "";
my $task_intsplits_map = "";

my $problem_i = 0;
my $global_i = 0;
foreach(@problems) {
    my $p = $_;
    if( -e "$p") {
	my $intsplit = 0;
	my $maxlayers = 0;
	if($p =~ /_(\d+)_(\d+)$/) {
	    $intsplit = $1;
	    $maxlayers = $2;
	}

	if($intsplit == 0) {
	    mylog "No intsplit formatting on problem file \"$p\"! Require _INTSPLIT_MAXLAYERS as file ending!";
	    exit(-1);
	}

	if($top_out_intsplits) {
	    my $power = 1;
	    while($power < $intsplit) {
		$power *= 2;
	    }
	    $intsplit = $power;
	}

	my $intsplit_assumptions = $intsplit ** $intsplit_layers;
	my $intsplit_args = "";

	for(my $i = 0; $i < $intsplit_layers; $i++) {
	    $intsplit_args .= " -i $intsplit";
	}

	for(my $i = 0; $i < $intsplit_assumptions; $i++) {
	    my $local_i = $global_i + $i;
	    $task_problems_map .= "problems_map[$local_i]=\"$p\"\n";
	    $task_assumption_map .= "assumption_map[$local_i]=$i\n";
	    $task_intsplits_map .= "intsplits_map[$local_i]=\"$intsplit_args\"\n";
	}

	$problem_i = $problem_i + 1;
	$global_i = $global_i + $intsplit_assumptions;
    } else {
	main::mylog "Cannot queue problem $p as file does not exist!";
    }
}
$global_i = $global_i - 1;

my $task = << "END";
#!/usr/bin/bash

# Array to store all declared problems to be solved later.
declare -A problem_map;
declare -A assumption_map;
declare -A intsplits_map;

$task_problems_map
$task_assumption_map
$task_intsplits_map

problem=\${problems_map[\$SLURM_ARRAY_TASK_ID]}
assumption_id=\${assumption_map[\$SLURM_ARRAY_TASK_ID]}
intsplits=\${intsplits_map[\$SLURM_ARRAY_TASK_ID]}

export ASAN_OPTIONS=print_stacktrace=1
export UBSAN_OPTIONS=print_stacktrace=1
export QUAPI_PRELOAD_PATH=\$(dirname $executable)/libquapi_preload.so

problemname=`basename "\$problem"`_\$assumption_id;

logfile="`pwd`/\$problemname.log";

echo "c submit.pl: name:  $name" > "\$logfile";
echo "c submit.pl: task:  \$SLURM_ARRAY_TASK_ID" >> "\$logfile";
echo "c submit.pl: host:  `cat /etc/hostname`" >> "\$logfile";
echo "c submit.pl: start: `date`" >> "\$logfile";
echo "c submit.pl: problem: \$problem" > "\$logfile";
echo "c submit.pl: assumption_id:  \$assumption" > "\$logfile";
echo "c submit.pl: problem_id: \$SLURM_ARRAY_TASK_ID" > "\$logfile";
echo "c submit.pl: additional args: $additional_args" >> "\$logfile";
echo "c submit.pl: executable: $executable" >> "\$logfile";
echo "c submit.pl: solver: $solver" >> "\$logfile";
echo "c Running job $name with problem \$problem of name \$problemname and assumption id \$assumption_id !" >> "\$logfile";
$runlim \"$executable\" \"\$problem\" -I \$assumption_id \$intsplits -pS $additional_args -- $solver &>> "\$logfile";
echo "c submit.pl: exit status: \$exitstatus" >> "\$logfile";
END


my $tmpfile = "./task.sh";

open(TMPFH, '>', $tmpfile) or die $!;
print TMPFH $task;
close(TMPFH);

mylog "Wrote $tmpfile with full runner information (not distributed by slurm, but by NFS)";

my $slurm_task = << "END";
#!/usr/bin/env bash
source $tmpfile
END

main::mylog "Giving the following to slurm:";
main::mylog "$slurm_task";

my $slurmjob = "./slurmjob.txt";

open(TMPFH, '>', $slurmjob) or die $!;
print TMPFH $slurm_task;
close(TMPFH);

my $cmd = "cat $slurmjob | sbatch --nice=$nice --parsable -J $name -c $cpus_per_task --array=0-$global_i --output=/dev/null --error=/dev/null";
main::mylog "sbatch command: $cmd";
my $client_jobnum = `$cmd`;
chop $client_jobnum;

main::mylog "Queued job $client_jobnum";

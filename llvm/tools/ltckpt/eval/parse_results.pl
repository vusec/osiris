#!/usr/bin/perl -t -T -w

#############################################################################
#
# This script converts Unix Bench log files generated with run_bc.sh in .INI
# format.
#
#############################################################################

# Convert Unix Bench logs to INI, for the llvm-apps test framework.
use strict;

# folder containing the results of the test run to convert, from command line.
die "USAGE: $0 <path>\n\tpath: path to the Unixbench log files" unless $ARGV[0];

my $UB_RESULT_PATH = $ARGV[0];
chomp($UB_RESULT_PATH);

#############################################################################
# Baseline data
#############################################################################
# BASELINE, Baseline values, labels associated to prognames
my %BASELINE = (
# Scores from "George", a SPARCstation 20-61.
	"dhry2reg" => {
		"logmsg" => "Dhrystone 2 using register variables",
		"units" => "lps",
		"baseline" => "116700",
	},
	"whetstone-double" => {
		"logmsg" => "Double-Precision Whetstone",
		"units" => "MWIPS",
		"baseline" => "55.0",
	},
	"execl" => {
		"logmsg" => "Execl Throughput",
		"units" => "lps",
		"baseline" => "43.0",
	},
	"fstime" => {
		"logmsg" => "File Copy 1024 bufsize 2000 maxblocks",
		"units" => "KBps",
		"baseline" => "3960",
	},
	"fsbuffer" => {
		"logmsg" => "File Copy 256 bufsize 500 maxblocks",
		"units" => "KBps",
		"baseline" => "1655",
	},
	"fsdisk" => {
		"logmsg" => "File Copy 4096 bufsize 8000 maxblocks",
		"units" => "KBps",
		"baseline" => "5800",
	},
	"pipe" => {
		"logmsg" => "Pipe Throughput",
		"units" => "lps",
		"baseline" => "12440",
	},
	"context1" => {
		"logmsg" => "Pipe-based Context Switching",
		"units" => "lps",
		"baseline" => "4000",
	},
	"spawn" => {
		"logmsg" => "Process Creation",
		"units" => "lps",
		"baseline" => "126",
	},
	"shell8" => {
		"logmsg" => "Shell Scripts (8 concurrent)",
		"units" => "lpm",
		"baseline" => "6",
	},
	"syscall" => {
		"logmsg" => "System Call Overhead",
		"units" => "lps",
		"baseline" => "15000",
	},
	"shell1" => {
		"logmsg" => "Shell Scripts (1 concurrent)",
		"units" => "lpm",
		"baseline" => "42.4",
	},
);

# Stores test reports
my %REPORTS;

# Stores computed results
my %RESULTS;

#############################################################################
# Load data
#############################################################################

opendir(UB_RESULT_DIR, $UB_RESULT_PATH) || die "Can't read directory $UB_RESULT_PATH\n";

while(readdir(UB_RESULT_DIR)) {
    chomp $_;
    next if $_ =~ m/^\.{1,2}$/;

    if($_ =~ m/(\d{4})\.(\d{2})\.(\d{2})-(\d+)-(.+?)-(\d+)\.log/) {
	my (      $year,  $month,   $day, $seq,$name,$indice) =
	   (         $1,      $2,     $3,   $4,   $5,     $6);

	unless(open(LOG, "<$UB_RESULT_PATH/$_")) {
	    print(STDERR "Failed to read $UB_RESULT_PATH/$_, skipping\n");
	    next;
	}

	# Hackish, if number of tests is changed, it has to be updated.
	$name = "shell1" if $name =~ m"looper" && $indice == 10;
	$name = "shell8" if $name =~ m"looper" && $indice == 11;

	$name = "fstime" if $name =~ m"fstime" && $indice == 3;
	$name = "fsbuffer" if $name =~ m"fstime" && $indice == 4;
	$name = "fsdisk" if $name =~ m"fstime" && $indice == 5;

	$REPORTS{"$name"}{"key"} = "ub.$seq.$indice.$name";

	my $sample = 0;
	while(<LOG>) {
	    my $line = $_;
	    chomp($line);

	    if($line =~ m/^COUNT\|([0-9.]+)\|(\d+)\|(.+)$/) {
		my $count = $1;
		my $time = $2;
		my $unit = $3;
		$REPORTS{"$name"}{"samples"}[$sample]{"unit"} = $unit;
		$REPORTS{"$name"}{"samples"}[$sample]{"count"} = $count;
		$REPORTS{"$name"}{"samples"}[$sample]{"timebase"} = $time;
	    }

	    if($line =~ m/^ELAPSED\|([0-9.]+)\|s$/) {
		my $time = $1;

		# ignore global time if the test provides its own tiem accounting.
		$REPORTS{"$name"}{"samples"}[$sample]{"time"} = $time
			unless $REPORTS{"$name"}{"samples"}[$sample]{"time"};

		$REPORTS{"$name"}{"samples"}[$sample]{"timebase"} = 0
			unless $REPORTS{"$name"}{"samples"}[$sample]{"timebase"};

		$sample++;
	    }

	    if($line =~ m/^TIME\|([0-9.]+)$/) {
		my $time = $1;
		$REPORTS{"$name"}{"samples"}[$sample]{"time"} = $time;
	    }

	    if($name =~ m/fs/) {
		if($line =~ m/Copy done: (\d+) in ([0-9.]+), score (\d+)/) {
		    my (                  $val,        $secs,      $score) =
		       (                    $1,           $2,          $3);

		    $REPORTS{"$name"}{"samples"}[$sample]{"time"} = $secs;
		    $REPORTS{"$name"}{"samples"}[$sample]{"score"} = $val;
		    $REPORTS{"$name"}{"samples"}[$sample]{"count"} = $score;
		}
	    }

#	    if($name =~ m/whetstone-double/) {
#		if($line =~ m/^N(\d+) (.+?)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)$/) {
#		    my (         $nr, $label,     $res,     $val,      $secs) =
#		       (          $1,     $2,       $3,       $4,         $5);
#		    my $unit = "mflops";

#		    $unit = "mops" if (($nr > 2 && $nr < 6) || ($nr > 6 && $nr < 8));

#		    chomp($label);
#		    $label =~ s/[, ]/_/g;
#		    $label =~ s/\.//g;

#		    $REPORTS{"$name"}{"samples"}[$sample]{"${label}_${unit}"} = $val;
#		    $REPORTS{"$name"}{"samples"}[$sample]{"${label}_secs"} = $secs;
#		}
#	    }
	}

	close(LOG);
    } else {
	print(STDERR "Skipping file, failed to parse Unix Bench log filename:\n");
	print(STDERR "  '$_'\n");
    }
}

closedir(UB_RESULT_DIR);

#############################################################################
# Generate score results
#
# This follows the computing method used in the original Run script.
#############################################################################
foreach my $name (sort keys %REPORTS) {
    my $baseline =  $BASELINE{"$name"}{"baseline"};
    my $k = $REPORTS{"$name"}{"key"};

    my $totalTime = 0;
    my $average = 0;
    my $iter = 0;
    my $product = 0;
    my @samples = ();

    @samples = @{$REPORTS{"$name"}{"samples"}};

    # Dump a third of the values, the worst ones.
    my $dump = int(@samples / 3);
    @samples = sort { ($a->{"count"} / $a->{"time"}) <=> ($b->{"count"} / $b->{"time"}) } @samples;

    foreach my $sample (@samples) {
	if($dump >= 1) {
	$dump--;
	    push(@{$RESULTS{$k}{"dump"}}, $sample);
	    next;
	}

	$iter++;
	push(@{$RESULTS{$k}{"keep"}}, $sample);
    }

    @samples = @{$RESULTS{$k}{"keep"}};

#    if( $name =~ "spawn" || $name =~ "shell" ){
#    print "$name: [$k] ".scalar(@samples)."\n";
#    foreach my $h (@samples) {
#	foreach my $k2 (sort keys %$h) {
#		print "$k2 => ".$h->{$k2}.", ";
#	}
#	print "\n";
#    }
#    print "\n";
#}

    # The score is defined as the average of the samples left
    foreach my $sample (@samples) {
	my $timebase = $sample->{"timebase"};
	my $time = $sample->{"time"};

	$totalTime += $time;

	if ($timebase > 0) {
	    $average += $sample->{"count"} / ($time / $timebase);
	    $product += log($sample->{"count"}) - log($time / $timebase);
	} else {
	    $average += $sample->{"count"};
	    $product += log($sample->{"count"});
	}
    }

    #my $score = $average / $iter;
    my $score = exp($product / $iter);

    my $index = ($score * 10.0 / $baseline);

    $RESULTS{$k}{"name"} = $name;
    $RESULTS{$k}{"baseline"} = $baseline;
    $RESULTS{$k}{"score"} = $score;
    $RESULTS{$k}{"index"} = $index;
    $RESULTS{$k}{"iterations"} = $iter;
    $RESULTS{$k}{"time"} = $totalTime / $iter;
}

#############################################################################
# Print report
#############################################################################
foreach my $k (sort keys %RESULTS) {
    my $name = $RESULTS{$k}{"name"};
    print("[$k]\n");
    #print("description = '".$BASELINE{"$name"}{"logmsg"}."'\n");
    #print("units = '".$BASELINE{"$name"}{"units"}."'\n");
    print("base = ".$RESULTS{$k}{"baseline"}."\n");
    print("score = ".$RESULTS{$k}{"score"}."\n");
    print("time = ".$RESULTS{$k}{"time"}."\n");
    print("iterations = ".$RESULTS{$k}{"iterations"}."\n");
    print("index = ".$RESULTS{$k}{"index"}."\n");
    print("\n");
}


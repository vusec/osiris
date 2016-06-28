#!/usr/bin/perl -w -W -t -T

$ENV{"PATH"} = "/bin:/usr/bin";

open(LOOKUP, "test_lookup.csv") || die "Can't read lookup table";
open(IN, "test.csv") || die "Can't read input file";

my %db;

while(<LOOKUP>) {
	if ($_ =~ m/^(.+?);(.+?);$/) {
		$db{"$1"} = "$2";
	}
}
close(LOOKUP);

while(<IN>) {
	if ($_ =~ m/^(.+?);(.+)/) {
		print $db{"$1"}.";$2\n" if $db{"$1"};
		print "$1;$2\n" unless $db{"$1"};
	}
}
close(IN);

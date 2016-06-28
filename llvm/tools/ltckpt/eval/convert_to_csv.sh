#!/bin/sh

echo "" > report.csv
for f in *
do                                                                                  (echo -n "$f;";cat $f | perl -ne 'if($_ =~ m/(PRE|POST):(opt|dfa|pes) (.+?):([0-9.]+)/){ print "$1;$3;$4;"}';echo) >> report.csv
	(
		echo -n "$f;";
		cat $f | perl -ne 'if($_ =~ m/(PRE|POST):(opt|dfa|pes) (.+?):([0-9.]+)/){ print "$1;$3;$4;"}';
		echo
	) >> report.csv
done

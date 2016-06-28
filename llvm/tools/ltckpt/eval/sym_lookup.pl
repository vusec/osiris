#!/usr/bin/perl -w -W -t -T

# Where are located the report files.
my $REPORTDIR = "results.coverage";
# For which services it was run
my @servers = qw(mod09_vm mod05_vfs mod03_pm mod02_rs ipc);

# Secure our default path.
$ENV{"PATH"} = "/bin:/usr/bin";

my %SYMS;
my $ADDR2LINE = "../../../../apps/minix/obj.i386/tooldir.Linux-3.18.6-1-ARCH-x86_64/bin/i586-elf32-minix-addr2line";
my $BINDIR = "../../../../apps/minix/obj.i386/destdir.i386/boot/minix/.temp";

# Small lookup between each service file name and official names.
my %s2servers;
foreach my $s (@servers) {
	my $t = $s;
	if ($s =~ m/mod\d\d_(.+)/) {
		$t = $1;
	}
	$s2servers{$t} = $s;
}

# Replace all addresses found with the names of the functions containing it.
opendir($DIR, $REPORTDIR) || die "Can't find $REPORTDIR\n";

while(readdir($DIR)) {
	# Skip current and parent directory entries.
	next if $_ =~ m/^\.{1,2}/;

	# Skip server global stats.
	next if $_ =~ m/^.{2,3}\.txt$/;

	if ($_ =~ m/(.+)\.(.+?),(.+?),(.+?),(.+)/) {
		my $server = $s2servers{$1};
		print "$_;$1.";

		@vals = readpipe("$ADDR2LINE -e $BINDIR/$server -f $2");
		$tmp = $vals[0]; chomp $tmp; print "$tmp,";

		@vals = readpipe("$ADDR2LINE -e $BINDIR/$server -f $3");
		$tmp = $vals[0]; chomp $tmp; print "$tmp,";

		@vals = readpipe("$ADDR2LINE -e $BINDIR/$server -f $4");
		$tmp = $vals[0]; chomp $tmp; print "$tmp,";

		print "$5;\n";
	}
}

closedir($DIR);

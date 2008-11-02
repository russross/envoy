#!/usr/bin/perl -w

use strict;

my ($host, $prot, $test, $cache, $num);

while (my $line = <>) {
    if ($line =~ m/^== (.*) : (.*) (.*) (.*) (\d+) ==$/) {
        ($host, $prot, $test, $cache, $num) = ($1, $2, $3, $4, $5);
        if ($num == 0 && $test ne 'bonnie') {
            print "$host,$prot,$test,$cache";
        }
    } elsif ($line =~ m/ (\d+):(\d+.\d+)elapsed/) {
        my $time = $1 * 60 + $2;
        print ",$time";
        print "\n" if $num == 9;
    }
}

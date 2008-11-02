#!/usr/bin/perl -w

use strict;

my $count = 0;
my $next = 0;

my $last;

my ($sum, $size) = (0,0);

while (<>) {
    $last = $_;
    m/(\d+)/ or die;
    $sum += $1;
    $size++;
    if ($count++ >= int($next)) {
        print $sum / $size, "\n";
        $sum = $size = 0;
        $next += 20.22822822;
    }
}
print $last;

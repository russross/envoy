#!/usr/bin/perl -w

use strict;

while (<>) {
    chomp;
    m/^(\d+):(\d+)\.(\d+)$/ or die;
    print $1 * 60 + $2 + $3 / 100 . "\n";
}

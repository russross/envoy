#!/usr/bin/perl -w

use strict;

my @stack = ('/local/scratch/rgr22/linux-2.6.18');

my ($largest, $smallest, $totalfiles, $totaldirs, $mostfiles, $mostdirs) =
    (0, 1000000, 0, 1, 0, 0);

print "name\tdirs\tfiles\tmeansize\tstdev\n";

my @smallfiles = ();
my @largefiles = ();

while (scalar(@stack)) {
    my $dir = pop @stack;

    my @ls = `ls -al $dir`;
    shift @ls;
    shift @ls;
    shift @ls;
    my ($files, $dirs) = (0, 0);
    my $total = 0;
    my @sizes = ();
    foreach (@ls) {
        chomp;
        m/^(.)\S+\s+\d+\s+\S+\s+\S+\s+(\d+)\s+\S+\s+\S+\s+\S+\s+(.*)$/ or die;
        my ($type, $size, $name) = ($1, $2, $3);
        if ($type eq 'd') {
            push @stack, "$dir/$name";
            $dirs++;
            $totaldirs++;
        } elsif ($type eq '-') {
            push @sizes, $size;
            if ($size > 32768) {
                push @largefiles, $size;
            } else {
                push @smallfiles, $size;
            }
            $files++;
            $totalfiles++;
            $total += $size;
            $largest = $size if $size > $largest;
            $smallest = $size if $size < $smallest;
        } else {
            die "unknown file type [$type]: $_\n";
        }
    }

    my ($mean, $stdev) = (0, 0);
    if ($files > 0) {
        $mean += $_ foreach @sizes;
        $mean /= $files;
        foreach (@sizes) {
            my $diff = $_ - $mean;
            $stdev += $diff * $diff;
        }
        $stdev /= $files;
        $stdev = sqrt($stdev);
    }
    print "$dir\t$dirs\t$files\t$mean\t$stdev\n";
    $mostfiles = $files if $files > $mostfiles;
    $mostdirs = $dirs if $dirs > $mostdirs;
}

print "\n\nTotals:\n";
print "files\t$totalfiles\n";
print "dirs\t$totaldirs\n";
print "smallest\t$smallest\n";
print "largest\t$largest\n";
print "mostfiles\t$mostfiles\n";
print "mostdirs\t$mostdirs\n";

my ($smallmean, $smallstdev) = (0, 0);
$smallmean += $_ foreach @smallfiles;
$smallmean /= scalar(@smallfiles);
foreach (@smallfiles) {
    my $diff = $_ - $smallmean;
    $smallstdev += $diff * $diff;
}
$smallstdev /= scalar(@smallfiles);
$smallstdev = sqrt($smallstdev);

my ($largemean, $largestdev) = (0, 0);
$largemean += $_ foreach @largefiles;
$largemean /= scalar(@largefiles);
foreach (@largefiles) {
    my $diff = $_ - $largemean;
    $largestdev += $diff * $diff;
}
$largestdev /= scalar(@largefiles);
$largestdev = sqrt($largestdev);

my ($mean, $stdev) = (0, 0);
$mean += $_ foreach @largefiles;
$mean += $_ foreach @smallfiles;
$mean /= scalar(@smallfiles) + scalar(@largefiles);
foreach (@smallfiles, @largefiles) {
    my $diff = $_ - $mean;
    $stdev += $diff * $diff;
}
$stdev /= scalar(@smallfiles) + scalar(@largefiles);
$stdev = sqrt($stdev);

print "meansize\t$mean\n";
print "stdev\t$stdev\n";
print "largecount\t".scalar(@largefiles)."\n";
print "largemean\t$largemean\n";
print "largestdev\t$largestdev\n";
print "smallcount\t".scalar(@smallfiles)."\n";
print "smallmean\t$smallmean\n";
print "smallstdev\t$smallstdev\n";

print "\n\nFile sizes:\n";
print "$_\n" foreach sort {$a <=> $b} @smallfiles;
print "$_\n" foreach sort {$a <=> $b} @largefiles;

#!/usr/bin/perl -w

use strict;

@ARGV > 0 or die "Usage: $0 includes.dynamic *.c *.h\n";

my $filelist;
my $t;

my $datafile = shift @ARGV;

{
    open FP, "<$datafile" or die "Unable to open $datafile: $!\n";
    local $/ = undef;
    my $in = <FP>;
    close FP;
    my $data = eval $in;
    die $@ if $@;
    ($filelist, $t) = @$data;
}

foreach my $file (@ARGV) {
    my $f;
    {
        open FP, "<$file" or die "Unable to open $file: $!\n";
        local $/ = undef;
        $f = <FP>;
        close FP;
    }
    $f =~ s/(^|\n+)(?:\n*#include [^\n]+)+(\n)/$1#### PLACEHOLDER ####$2/s
        or next;

    my @includes = ();
    foreach my $inc (@$filelist) {
        next if $inc eq "\"$file\"";
        foreach my $key (keys %{$t->{$inc}}) {
            if ($f =~ m/\b$key\b/s) {
                push @includes, $inc;
                last;
            }
        }
    }
    my $res = join "\n", map { "#include $_" } @includes;
    $f =~ s/#### PLACEHOLDER ####/$res/s;
    open FP, ">$file" or die "Unable to open $file for writing: $!\n";
    print FP $f;
    close FP;
}

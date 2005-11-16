#!/usr/bin/perl -w

use strict;

my $t;
my $filelist;

@ARGV > 0 or die "Usage: $0 includes.static *.h\n";

my $static = shift @ARGV;
{
    open FP, "<$static"
        or die "Unable to open the file '$static': $!\n";
    local $/ = undef;
    my $in = <FP>;
    close FP;
    my $data = eval $in;
    die $@ if $@;
    ($filelist, $t) = @$data;
}

foreach my $file (@ARGV) {
    open FP, "<$file" or die "Unable to open $file: $!\n";

    my $s = {};
    $s = $t->{"\"$file\""} if exists $t->{"\"$file\""};
    $s = $t->{"\"9p.h\""} if $file eq "9pstatic.h" and exists $t->{"\"9p.h\""};
    $s->{(uc $1).$2} = 1 if $file =~ m/^([a-z])(.*)\.h/;

    my $mode = 'default';
    while (my $line = <FP>) {
        if ($mode eq 'default') {
            next if $line =~ m/^#define +_\w+_/;
            $s->{$1} = 1 if $line =~ m/^#define +(\w+)/;
            $s->{$1} = 1 if $line =~ m/^typedef .* (\w+);/;
            $s->{$1} = 1 if $line =~ m/^\w+ [\w \*]*\b(\w+)\(.*\)/;
            $s->{$1} = 1 if $line =~ m/^extern \w+ [\w \*]*\b(\w+);/;
            $s->{"struct $1"} = 1, $mode = 'struct'
                if $line =~ m/^struct +(\w+) *{/;
            $s->{"enum $1"} = 1, $mode = 'enum'
                if $line =~ m/^enum +(\w+) *{/;
        } elsif ($mode eq 'enum') {
            $s->{$1} = 1 if $line =~ m/ +(\w+)/;
            $mode = 'default' if $line =~ m/^};/;
        } elsif ($mode eq 'struct') {
            $mode = 'default' if $line =~ m/^};/;
        } else {
            die;
        }
    }
    close FP;
    push @$filelist, "\"$file\"" unless $file eq "9pstatic.h";
    $t->{"\"9p.h\""} = $s if $file eq "9pstatic.h";
    $t->{"\"$file\""} = $s unless $file eq "9pstatic.h";
}

print "[\n    [\n";
print "        '$_',\n" foreach @$filelist;
print "    ],\n\n    {\n";
foreach my $file (@$filelist) {
    print "        '$file' => {\n";
    foreach my $elt (sort keys %{$t->{$file}}) {
        print "            '$elt' => 1,\n";
    }
    print "        },\n";
}
print "    },\n]\n";

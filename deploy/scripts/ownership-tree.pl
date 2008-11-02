#!/usr/bin/perl -w

use strict;

my $prefix = ',linux,current';

my @servers = ('druid', 'skiing', 'moonraider');
my @ip_in = ('128.232.39.40', '128.232.39.100', '128.232.38.190');
my %ips;
for (my $i = 0; $i < scalar(@servers); $i++) {
    $ips{$servers[$i]} = $ip_in[$i];
}
my %counters;
my %totals;
$counters{$_} = 0 foreach @servers;
my $tree = { '_owner' => $servers[0] };

sub next_owner {
    my ($parent, $level) = @_;
    my $key = "$parent,$level";
    $counters{$key} = $level unless exists $counters{$key};
    $counters{$key} = ($counters{$key} + 1) % scalar(@servers);
    $counters{$key} = ($counters{$key} + 1) % scalar(@servers)
        if $servers[$counters{$key}] eq $parent;
    $totals{$servers[$counters{$key}]}++;
    return $servers[$counters{$key}];
}

my $host = `hostname`;
chomp $host;
my $depth = $ENV{'DEPTH'} || 1000;
$host =~ s/^([a-z]+).*/$1/;
print "migrating leases for $host at depth $depth\n";

while (my $dir = <>) {
    chomp $dir;
    my @path = split /\//, $dir;
    my $node = $tree;
    my $level = -1;
    foreach my $name (@path) {
        $level++;
        $node->{$name} = { '_owner'  => next_owner($node->{'_owner'}, $level),
                           '_parent' => $node }
            unless (exists $node->{$name});
        $node = $node->{$name};
    }
    print "$dir => " . $node->{'_owner'} . "\n";
    if (scalar(@path) == $depth && $node->{'_parent'}->{'_owner'} eq $host) {
        print "***** migrate *****\n";
        my $p = join ',', @path;
        my $cmd = "::lease::$prefix,$p" . "::" . $ips{$node->{'_owner'}} . ":9922";
        print "command: $cmd\n";
        open FP, ">$cmd" and die;
        close FP;
    }
}

#foreach my $s (@servers) {
#    print "count for $s: " . $totals{$s} . "\n";
#}

# histogram bars: solid = color intensity
set style fill solid border -1
set style data histograms
set boxwidth 0.9 relative

# lw = errorbar thickness, gap = gap between clusters
set style histogram errorbars lw 1 gap 1
# errorbar width
set bars 2

# move the key out of the way
set key left Left reverse

# start the y axis at zero
set yrange [0:*]
set ylabel "time in seconds"
# lines across the graph
set grid ytics

# keep labels on the clusters, but no tick at the top
set xtics nomirror

set terminal postscript eps color

set output "envoy-tar.eps"
plot 'envoy-tar.dat' using 9:10 ti col, '' using 6:7 ti col, '' using 3:4:xticlabels(1) ti col

set output "envoy-rsync.eps"
plot 'envoy-rsync.dat' using 9:10 ti col, '' using 6:7 ti col, '' using 3:4:xticlabels(1) ti col

set output "envoy-nocache.eps"
plot 'envoy-nocache.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9:xticlabels(1) ti col

set key default
set key left Left reverse

set style histogram rowstacked
set xtics ("druid-0" 0, "druid-1" 1, "skiing-0" 2, "skiing-1" 3, "druid-0" 5, "druid-1" 6, "skiing-0" 7, "skiing-1" 8)
set output "envoy-cold-vs-nocache.eps"
plot newhistogram "tar", 'envoy-cold-vs-nocache-tar.dat' using 3 ti col, '' using 2 ti col, \
     newhistogram "untar", 'envoy-cold-vs-nocache-untar.dat' using 2 ti col, '' using 3 ti col

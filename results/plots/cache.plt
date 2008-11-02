# cache graphs

# histogram bars: solid = color intensity
set style fill solid border -1
set style data histograms
set boxwidth 0.9 relative

# lw = errorbar thickness, gap = gap between clusters
set style histogram errorbars lw 1 gap 1
# errorbar width
set bars 2

# move the key out of the way
#set key below

# start the y axis at zero
set yrange [0:*]
# lines across the graph
set grid ytics
set ylabel "time in seconds"

# keep labels on the clusters, but no tick at the top
set xtics nomirror
set xtics ("druid-0" 0, "druid-1" 1, "skiing-0" 2, "skiing-1" 3)

set terminal postscript eps color

set key left Left reverse

set output 'cache-tar-rsync.eps'
plot 'cache-tar-rsync.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col, '' using 10:11 ti col, '' using 12:13 ti col

set xtics ("druid-1" 0, "skiing-1" 1)

set output 'cache-rsync.eps'
plot 'cache-rsync.dat' using 6:7 ti col, '' using 4:5 ti col, '' using 2:3 ti col, '' using 8:9 ti col

set output 'cache-tar.eps'
plot 'cache-tar.dat' using 6:7 ti col, '' using 4:5 ti col, '' using 2:3 ti col, '' using 8:9 ti col

set xtics ("cold" 0, "warm" 1, "hot" 2)

set output 'cache-tar-alt.eps'
plot 'cache-tar-alt.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

set style histogram rowstacked
set xtics ("hot" 0, "warm" 1, "cold" 2)

set output 'cache-tar-stacked.eps'
plot 'cache-tar-stacked.dat' using 2 ti col, '' using 3 ti col

# architecture graphs

# histogram bars: solid = color intensity
set style fill solid border -1
set style data histograms
set boxwidth 0.9 relative

# lw = errorbar thickness, gap = gap between clusters
set style histogram errorbars lw 1 gap 1
# errorbar width
set bars 2

# display color bar to left of key
set key Left reverse

# start the y axis at zero
set yrange [0:*]
# lines across the graph
set grid ytics
set ylabel "time in seconds"

# keep labels on the clusters, but no tick at the top
set xtics nomirror
set xtics ("druid-0" 0, "druid-1" 1, "skiing-0" 2, "skiing-1" 3)

set terminal postscript eps color

set output 'arch-tar-hot.eps'
plot 'arch-tar-hot.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

set output 'arch-tar-warm.eps'
plot 'arch-tar-warm.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col

set output 'arch-rsync-hot.eps'
plot 'arch-rsync-hot.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

set key left

set output 'arch-rsync-tar-hot.eps'
plot 'arch-rsync-tar-hot.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

set output 'arch-rsync-tar-warm.eps'
plot 'arch-rsync-tar-warm.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

set output 'arch-untar.eps'
plot 'arch-untar.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col

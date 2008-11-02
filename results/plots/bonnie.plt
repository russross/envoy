# bonnie graphs

# histogram bars: solid = color intensity
set style fill solid border -1
set style data histograms
set boxwidth 0.9 relative

# lw = errorbar thickness, gap = gap between clusters
set style histogram errorbars lw 1 gap 1
# errorbar width
set bars 2

# move the key out of the way
set key below

# start the y axis at zero
set yrange [0:*]
# lines across the graph
set grid ytics
set ylabel "kilobytes per second"

# keep labels on the clusters, but no tick at the top
set xtics nomirror
set xtics ("druid-0" 0, "druid-1" 1, "skiing-0" 2, "skiing-1" 3)

set terminal postscript eps color

set output "bonnie-write.eps"
plot 'bonnie-write.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

set output "bonnie-rewrite.eps"
plot 'bonnie-rewrite.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

set output "bonnie-read.eps"
plot 'bonnie-read.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

set ylabel "seeks per second"

set output "bonnie-seek.eps"
plot 'bonnie-seek.dat' using 2:3 ti col, '' using 4:5 ti col, '' using 6:7 ti col, '' using 8:9 ti col

# druid-1 results

set ylabel "kilobytes per second"
set xtics
unset key

set output 'bonnie-druid1-read.eps'
plot 'bonnie-druid1-read.dat' using 2:3:xticlabel(1) ti col

set output 'bonnie-druid1-write.eps'
plot 'bonnie-druid1-write.dat' using 2:3:xticlabel(1) ti col

set output 'bonnie-druid1-rewrite.eps'
plot 'bonnie-druid1-rewrite.dat' using 2:3:xticlabel(1) ti col

set ylabel "seeks per second"

set output 'bonnie-druid1-seek.eps'
plot 'bonnie-druid1-seek.dat' using 2:3:xticlabel(1) ti col

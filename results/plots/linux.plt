set logscale y
set ytics (16, 32, 64, 128, 256, 512, \
           "1k" 1024, "2k" 2048, "4k" 4096, "8k" 8192, "16k" 16384, \
           "32k" 32768, "64k"65536, "128k" 131072, "256k" 262144, "512k" 524288)
unset key
set grid ytics
set xrange [0:1011]
set xtics ("10%%" 100, "20%%" 200, "30%%" 300, "40%%" 400, "50%%" 500, \
        "60%%" 600, "70%%" 700, "80%%" 800, "90%%" 900, "100%%" 1000)
set ylabel "file size in bytes"
set xlabel "files smaller than given size"

set terminal postscript eps
set output 'linux-sizes.eps'

plot "linux-sizes-small.csv"

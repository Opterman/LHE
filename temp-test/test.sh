#!/bin/sh

touch aa.lhe
touch dec_aa.bmp

rm *.lhe
rm dec_*.bmp

ffmpeg -i 1.bmp -basic_lhe true 1.lhe 2>e_1.csv
ffmpeg -i 1.lhe -basic_lhe true dec_1.bmp 2>d_1.csv

ffmpeg -i 2.bmp -basic_lhe true 2.lhe 2>e_2.csv
ffmpeg -i 2.lhe -basic_lhe true dec_2.bmp 2>d_2.csv

rm *.lhe

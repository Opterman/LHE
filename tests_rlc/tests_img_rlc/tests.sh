#!/bin/sh

rm *.lhe
rm dec_*.bmp

#ffmpeg -i orig_rub.bmp -basic_lhe true rub.lhe
#ffmpeg -i rub.lhe -basic_lhe true dec_rub.bmp

for f in *.bmp
do
	ffmpeg -i "$f" -basic_lhe true "$f.lhe" 2>"$f.csv"
	ffmpeg -i "$f.lhe" -basic_lhe true "dec_$f" 2>>"$f.csv"
done

#!/bin/sh

if [[ $(arch) == 'i386' ]]; then
  	echo Intel Mac
	IDIR="/usr/local/include"
	LDIR="/usr/local/lib"
fi

gcc -Wall -o metronome metronome.c paUtils.c \
	-I$IDIR -L$LDIR -lsndfile -lportaudio


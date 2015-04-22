#!/bin/sh

gcc -o mp4grab movie.c mp4grab.c -L /usr/local/lib -I/usr/local/include -lavcodec -lavformat -lavutil -lavfilter -lswscale -lswresample -lopencv_core -lopencv_imgproc -lopencv_highgui -lm -lz -lpthread

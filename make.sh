#!/bin/sh

gcc -o mp4grab movie.c mp4grab.c -L /usr/local/lib -lavcodec -lavformat -lavutil -lswscale -lswresample -lopencv_core -lopencv_imgproc -lopencv_highgui -lm

#!/bin/sh
./configure --enable-shared \
       	--enable-small \
       	--prefix=compiled/x86 \
       	--target-os=linux \
		--enable-version3 \
	    --disable-doc \
		--disable-debug \
	       --disable-x86asm  \
	--enable-gpl --enable-nonfree --enable-libmp3lame --enable-libx264 --enable-encoder=aac --enable-encoder=libx264  --enable-libfdk-aac \
		--enable-protocol=file \
		--extra-cflags="-fPIC" \
		--extra-cflags=-I/home/wy/build/x264/include \
		--extra-ldflags=-L/home/wy/build/x264/lib\
		--extra-cflags=-I/home/wy/build/libfdk_aac/include \
		--extra-ldflags=-L/home/wy/build/libfdk_aac/lib \
		--extra-cflags=-I/home/wy/build/lame/include \
		--extra-ldflags=-L/home/wy/build/lame/lib
#configure 最后两项指明libx264路径
make -j4
make install 	

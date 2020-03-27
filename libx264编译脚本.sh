#bin/bash


export PREBUILT=/opt/gcc-linaro-arm-linux-gnueabihf-4.8-2014.04_linux/ 
export CC=$PREBUILT/bin/arm-linux-gnueabihf-gcc
export LD=$PREBUILT/bin/arm-linux-gnueabihf-ld
export AR=$PREBUILT/bin/arm-linux-gnueabihf-ar
export PREFIX=./compiled/
./configure --prefix=$PREFIX \
--cross-prefix=$PREBUILT/bin/arm-linux-gnueabihf- \
--enable-static \
--enable-shared \
--enable-pic \
--enable-strip \
--host=arm-linux \
--disable-asm

make -j4
make install

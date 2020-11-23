#!/bin/bash

DIR="uWebSockets"
if [ -d $DIR ]; then
    rm -rf $DIR
fi

git clone https://github.com/markccchiang/uWebSockets.git

cd uWebsockets

git submodule init && git submodule update

WITH_LIBUV=1 make

## install the uWebSockets
sudo make install

## install the uSockets
sudo cp uSockets/src/libusockets.h /usr/local/include
sudo cp uSockets/uSockets.a /usr/local/lib/libuSockets.a


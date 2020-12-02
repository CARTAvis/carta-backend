#!/bin/bash

DIR="uWebSockets"
if [ -d $DIR ]; then
    rm -rf $DIR
fi

git clone https://github.com/uNetworking/uWebSockets.git

cd $DIR
git checkout 72e9951
git submodule init && git submodule update

## remove the -flto flag while compiling the uSockets
cd uSockets
sed 's/-flto//g' Makefile > Makefile.new && mv Makefile.new Makefile
cd ..

make

## install the uWebSockets
sudo make install

## install the uSockets
sudo cp uSockets/src/libusockets.h /usr/local/include
sudo cp uSockets/uSockets.a /usr/local/lib/libuSockets.a


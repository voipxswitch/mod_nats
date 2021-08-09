#!/bin/bash

rm -rf ~/mod_nats
cp -rfp /tmp/mod_nats ~/
pushd ~/mod_nats
cmake .
make
make install
popd

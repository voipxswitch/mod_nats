#!/bin/bash

if [ ! -d build ] ; then
  mkdir build
fi

if [ ! -d build/nats.c ] ; then
  git clone https://github.com/nats-io/nats.c.git build/nats.c
fi
pushd build/nats.c
if [ ! -d build ] ; then
  mkdir build
fi

cd build
cmake .. -DNATS_BUILD_TLS_USE_OPENSSL_1_1_API=ON
make ; make install
popd

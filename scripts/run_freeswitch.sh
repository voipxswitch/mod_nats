#!/bin/bash

rm -rf /etc/freeswitch
mkdir -p /etc/freeswitch
cp -rfp /tmp/scripts/freeswitch.xml /etc/freeswitch/freeswitch.xml
freeswitch -nonat -nc

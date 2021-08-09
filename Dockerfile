FROM debian:buster

RUN apt-get update && apt-get upgrade -y && apt-get install -y apt-transport-https ca-certificates gnupg2 wget cmake build-essential git vim curl devscripts pkg-config flex bison git libuv1-dev libcurl4-openssl-dev libjson-c-dev git cmake libprotobuf-c-dev libwebsockets-dev gdb
RUN /usr/bin/wget -O - https://files.freeswitch.org/repo/deb/freeswitch-1.8/fsstretch-archive-keyring.asc | apt-key add -
RUN /bin/echo "deb http://files.freeswitch.org/repo/deb/freeswitch-1.8/ buster main" > /etc/apt/sources.list.d/freeswitch.list
RUN /bin/echo "deb-src http://files.freeswitch.org/repo/deb/freeswitch-1.8/ buster main" >> /etc/apt/sources.list.d/freeswitch.list
RUN apt-get update && apt-get install -y freeswitch freeswitch-mod-console freeswitch-mod-sofia freeswitch-mod-commands freeswitch-mod-conference freeswitch-mod-db freeswitch-mod-dptools freeswitch-mod-hash freeswitch-mod-dialplan-xml freeswitch-mod-sndfile freeswitch-mod-native-file freeswitch-mod-tone-stream freeswitch-mod-say-en freeswitch-mod-event-socket libfreeswitch-dev
RUN apt-get update && apt-get install -y freeswitch-dbg freeswitch-mod-console-dbg freeswitch-mod-sofia-dbg freeswitch-mod-commands-dbg freeswitch-mod-conference-dbg freeswitch-mod-db-dbg freeswitch-mod-dptools-dbg freeswitch-mod-hash-dbg freeswitch-mod-dialplan-xml-dbg freeswitch-mod-sndfile-dbg freeswitch-mod-native-file-dbg freeswitch-mod-tone-stream-dbg freeswitch-mod-say-en-dbg freeswitch-mod-event-socket-dbg libfreeswitch-dev

ADD scripts/build_nats.c.sh /tmp
RUN /tmp/build_nats.c.sh
RUN ldconfig

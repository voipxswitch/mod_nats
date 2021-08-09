# mod_nats
FreeSWITCH NATS event publisher


### build freeswitch container with nats.c library

```sh
docker build . -t freeswitch-mod_nats:latest
```

### run container & open bash

```sh
docker run -i -t --rm -v $(pwd):/tmp -e LANG=C.UTF-8 freeswitch-mod_nats:latest /bin/bash
```

### build mod_nats

```sh
/tmp/scripts/build_mod_nats.sh
```

### run freeswitch using scripts/freeswitch.xml

```sh
/tmp/scripts/run_freeswitch.sh
```

### load nats module

```
fs_cli -x 'load mod_nats'
```

include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_nats

if HAVE_NATS

mod_LTLIBRARIES = mod_nats.la
mod_nats_la_SOURCES  = mod_nats_utils.c mod_nats_connection.c mod_nats_publisher.c mod_nats.c
mod_nats_la_CFLAGS   = $(AM_CFLAGS) $(NATS_CFLAGS)
mod_nats_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_nats_la_LDFLAGS  = -avoid-version -module -no-undefined -shared $(NATS_LIBS) $(SWITCH_AM_LDFLAGS)

else
install: error
all: error
error:
	$(error You must install libnats-dev to build this module)
endif
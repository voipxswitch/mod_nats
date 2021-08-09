cmake_minimum_required(VERSION 3.3)

project(mod_nats C)

set(FREESWITCH_INCLUDE_DIR "/usr/include/freeswitch" CACHE PATH "Location of FreeSWITCH headers")
set(INSTALL_MOD_DIR "/usr/lib/freeswitch/mod" CACHE PATH "Location install library")

add_library(mod_nats SHARED mod_nats_utils.c mod_nats_connection.c mod_nats_publisher.c mod_nats.c)
target_include_directories(mod_nats PRIVATE  ${FREESWITCH_INCLUDE_DIR} )

target_link_libraries(mod_nats PRIVATE -lnats)

set_target_properties(mod_nats PROPERTIES PREFIX "")
set_target_properties(mod_nats PROPERTIES OUTPUT_NAME "mod_nats")

install(TARGETS mod_nats DESTINATION ${INSTALL_MOD_DIR})

/*
* FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
* Copyright (C) 2005-2012, Anthony Minessale II <anthm@freeswitch.org>
*
* Version: MPL 1.1
*
* The contents of this file are subject to the Mozilla Public License Version
* 1.1 (the "License"); you may not use this file except in compliance with
* the License. You may obtain a copy of the License at
* http://www.mozilla.org/MPL/
*
* Software distributed under the License is distributed on an "AS IS" basis,
* WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
* for the specific language governing rights and limitations under the
* License.
*
* The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
*
* The Initial Developer of the Original Code is
* Anthony Minessale II <anthm@freeswitch.org>
* Portions created by the Initial Developer are Copyright (C)
* the Initial Developer. All Rights Reserved.
*
* Based on mod_skel by
* Anthony Minessale II <anthm@freeswitch.org>
*
* Contributor(s):
*
* Daniel Bryars <danb@aeriandi.com>
* Tim Brown <tim.brown@aeriandi.com>
* Anthony Minessale II <anthm@freeswitch.org>
* William King <william.king@quentustech.com>
* Mike Jerris <mike@jerris.com>
* Emmanuel Schmidbauer <eschmidbauer@gmail.com>
*
* mod_nats -- Sends FreeSWITCH events to NATS queues
*
*/

#ifndef MOD_NATS_H
#define MOD_NATS_H

#include <switch.h>
#include <nats/nats.h>
#include <strings.h>

#define NATS_MAX_SERVERS 10

typedef struct
{
  char *pjson;
} mod_nats_message_t;

typedef struct mod_nats_connection_s
{
  char *name;
  natsConnection *connection;
  char *nats_servers[NATS_MAX_SERVERS];
  struct mod_nats_connection_s *next;
} mod_nats_connection_t;

typedef struct
{
  char *name;
  char *subject;
  /* Array to store the possible event subscriptions */
  int event_subscriptions;
  switch_event_node_t *event_nodes[SWITCH_EVENT_ALL];
  switch_event_types_t event_ids[SWITCH_EVENT_ALL];

  /* Because only the 'running' thread will be reading or writing to the two connection pointers
   * this does not 'yet' need a read/write lock. Before these structures can be destroyed,
   * the running thread must be joined first.
   */
  mod_nats_connection_t *conn_root;
  mod_nats_connection_t *conn_active;
  switch_thread_t *publisher_thread;
  switch_queue_t *send_queue;
  unsigned int send_queue_size;

  int reconnect_interval_ms;
  int circuit_breaker_ms;
  switch_time_t circuit_breaker_reset_time;

  switch_bool_t running;
  switch_memory_pool_t *pool;
} mod_nats_publisher_profile_t;

typedef struct mod_nats_globals_s
{
  switch_memory_pool_t *pool;
  switch_hash_t *publisher_hash;
} mod_nats_globals_t;

extern mod_nats_globals_t mod_nats_globals;

/* utils */
switch_status_t mod_nats_do_config(switch_bool_t reload);
void mod_nats_util_msg_destroy(mod_nats_message_t **msg);

/* connection */
switch_status_t mod_nats_connection_create(mod_nats_connection_t **conn, switch_xml_t cfg, switch_memory_pool_t *pool);
void mod_nats_connection_destroy(mod_nats_connection_t **conn);
void mod_nats_connection_close(mod_nats_connection_t *connection);
switch_status_t mod_nats_connection_open(mod_nats_connection_t *connections, mod_nats_connection_t **active, char *profile_name);

/* publisher */
void mod_nats_publisher_event_handler(switch_event_t *evt);
switch_status_t mod_nats_publisher_destroy(mod_nats_publisher_profile_t **profile);
switch_status_t mod_nats_publisher_create(char *name, switch_xml_t cfg);
void *SWITCH_THREAD_FUNC mod_nats_publisher_thread(switch_thread_t *thread, void *data);

#endif /* MOD_NATS_H */

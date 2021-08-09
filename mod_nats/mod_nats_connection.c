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

#include "mod_nats.h"

void mod_nats_connection_close(mod_nats_connection_t *connection)
{
	natsConnection *old_state = connection->connection;
	connection->connection = NULL;
	if (old_state != NULL)
	{
		natsConnection_Destroy(old_state);
	}
}

switch_status_t mod_nats_connection_open(mod_nats_connection_t *connections, mod_nats_connection_t **active, char *profile_name)
{
	natsConnection *newConnection = NULL;
	natsConnection *oldConnection = NULL;
	natsOptions *opts;
	mod_nats_connection_t *connection_attempt = NULL;
	natsStatus nats_status;
	natsConnStatus nats_connection_status;

	// set NATS options
	nats_status = natsOptions_Create(&opts);
	if (nats_status != NATS_OK)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "could not create NATS Options\n");
		return SWITCH_STATUS_GENERR;
	}
	natsOptions_SetName(opts, profile_name);
	// use these defaults
	natsOptions_SetAllowReconnect(opts, true);
	natsOptions_SetSecure(opts, false);
	natsOptions_SetMaxReconnect(opts, 10000);
	natsOptions_SetReconnectWait(opts, 2 * 1000);	  // 2s
	natsOptions_SetPingInterval(opts, 2 * 60 * 1000); // 2m
	natsOptions_SetMaxPingsOut(opts, 2);
	natsOptions_SetIOBufSize(opts, 32 * 1024); // 32 KB
	natsOptions_SetMaxPendingMsgs(opts, 65536);
	natsOptions_SetTimeout(opts, 2 * 1000);					// 2s
	natsOptions_SetReconnectBufSize(opts, 8 * 1024 * 1024); // 8 MB;
	natsOptions_SetReconnectJitter(opts, 100, 1000);		// 100ms, 1s;

	if (active && *active)
	{
		oldConnection = (*active)->connection;
	}

	connection_attempt = connections;

	while (connection_attempt && nats_connection_status != NATS_CONN_STATUS_CONNECTED)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "trying to connect to profile[%s]\n",
						  profile_name);
		nats_status = natsOptions_SetServers(opts, (const char **)connection_attempt->nats_servers, 1);
		if (nats_status != NATS_OK)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "could not set NATS Servers\n");
			continue;
		}
		nats_status = natsConnection_Connect(&connection_attempt->connection, opts);
		if (nats_status != NATS_OK)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "could not connect to profile[%s] %s\n",
							  profile_name, natsStatus_GetText(nats_status));
			connection_attempt = connection_attempt->next;
			continue;
		}
		nats_connection_status = natsConnection_Status(connection_attempt->connection);
	}

	*active = connection_attempt;
	if (!connection_attempt || nats_status != NATS_OK)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Profile[%s] could not connect to any NATS URLS\n", profile_name);
		goto err;
	}
	newConnection = connection_attempt->connection;
	(*active)->connection = newConnection;
	if (oldConnection)
	{
		natsConnection_Destroy(oldConnection);
	}
	if (opts)
	{
		natsOptions_Destroy(opts);
	}

	return SWITCH_STATUS_SUCCESS;

err:
	if (newConnection)
	{
		natsConnection_Destroy(newConnection);
	}
	if (opts)
	{
		natsOptions_Destroy(opts);
	}
	return SWITCH_STATUS_GENERR;
}

switch_status_t mod_nats_connection_create(mod_nats_connection_t **conn, switch_xml_t cfg, switch_memory_pool_t *pool)
{
	mod_nats_connection_t *new_con = switch_core_alloc(pool, sizeof(mod_nats_connection_t));
	switch_xml_t param;
	char *name = (char *)switch_xml_attr_soft(cfg, "name");
	char *nats_url = NULL;

	if (zstr(name))
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "connection missing name attribute\n");
		return SWITCH_STATUS_GENERR;
	}

	new_con->name = switch_core_strdup(pool, name);
	new_con->connection = NULL;
	new_con->next = NULL;

	for (param = switch_xml_child(cfg, "param"); param; param = param->next)
	{
		char *var = (char *)switch_xml_attr_soft(param, "name");
		char *val = (char *)switch_xml_attr_soft(param, "value");

		if (!var)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "NATS connection[%s] param missing 'name' attribute\n", name);
			continue;
		}

		if (!val)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "NATS connection[%s] param[%s] missing 'value' attribute\n", name, var);
			continue;
		}

		if (!strncmp(var, "url", 3))
		{
			nats_url = switch_core_strdup(pool, val);
		}
	}
	new_con->nats_servers[0] = nats_url ? nats_url : NATS_DEFAULT_URL;
	*conn = new_con;
	return SWITCH_STATUS_SUCCESS;
}

void mod_nats_connection_destroy(mod_nats_connection_t **conn)
{
	if (conn && *conn)
	{
		mod_nats_connection_close(*conn);
		*conn = NULL;
	}
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4
 */

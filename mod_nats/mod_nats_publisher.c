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
* Emmanuel Schmidbauer <eschmidbauer@gmail.com>
*
* mod_nats -- Sends FreeSWITCH events to NATS queues
*
*/

#include "mod_nats.h"

void mod_nats_publisher_event_handler(switch_event_t *evt)
{
	mod_nats_message_t *message;
	mod_nats_publisher_profile_t *profile = (mod_nats_publisher_profile_t *)evt->bind_user_data;
	switch_time_t now = switch_time_now();
	switch_time_t reset_time;

	if (!profile)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Event without a profile %p %p\n", (void *)evt, (void *)evt->event_user_data);
		return;
	}

	/* If the mod is disabled ignore the event */
	if (!profile->running)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "profile [%s] not running\n", profile->name);
		return;
	}

	/* If the circuit breaker is active, ignore the event */
	reset_time = profile->circuit_breaker_reset_time;
	if (now < reset_time)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "profile [%s] circuit breaker hit [%d] (%d)\n", profile->name, (int)now, (int)reset_time);
		return;
	}

	switch_malloc(message, sizeof(mod_nats_message_t));
	message->evname = strdup(switch_event_name(evt->event_id));
	switch_event_serialize_json(evt, &message->pjson);

	/* Queue the message to be sent by the worker thread, errors are reported only once per circuit breaker interval */
	if (switch_queue_trypush(profile->send_queue, message) != SWITCH_STATUS_SUCCESS)
	{
		unsigned int queue_size = switch_queue_size(profile->send_queue);
		/* Trip the circuit breaker for a short period to stop recurring error messages (time is measured in uS) */
		profile->circuit_breaker_reset_time = now + profile->circuit_breaker_ms * 1000;
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "NATS message queue full. Messages will be dropped for %.1fs! (Queue capacity %d)",
						  profile->circuit_breaker_ms / 1000.0, queue_size);
		mod_nats_util_msg_destroy(&message);
	}
}

switch_status_t mod_nats_publisher_destroy(mod_nats_publisher_profile_t **prof)
{
	mod_nats_message_t *msg = NULL;
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	mod_nats_connection_t *conn = NULL, *conn_next = NULL;
	switch_memory_pool_t *pool;
	mod_nats_publisher_profile_t *profile;

	if (!prof || !*prof)
	{
		return SWITCH_STATUS_SUCCESS;
	}
	profile = *prof;
	pool = profile->pool;
	if (profile->name)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "shutting down profile [%s]\n", profile->name);
		switch_core_hash_delete(mod_nats_globals.publisher_hash, profile->name);
	}
	profile->running = 0;
	if (profile->publisher_thread)
	{
		switch_thread_join(&status, profile->publisher_thread);
	}
	if (profile->js)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "destroyed NATS stream in profile [%s]\n", profile->name);
		jsCtx_Destroy(profile->js);
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "closing NATS connection in profile [%s]\n", profile->name);
	for (conn = profile->conn_root; conn; conn = conn_next)
	{
		conn_next = conn->next;
		mod_nats_connection_destroy(&conn);
	}
	profile->conn_active = NULL;
	profile->conn_root = NULL;
	while (profile->send_queue && switch_queue_trypop(profile->send_queue, (void **)&msg) == SWITCH_STATUS_SUCCESS)
	{
		mod_nats_util_msg_destroy(&msg);
	}
	if (pool)
	{
		switch_core_destroy_memory_pool(&pool);
	}
	*prof = NULL;
	return SWITCH_STATUS_SUCCESS;
}

switch_status_t mod_nats_publisher_create(char *name, switch_xml_t cfg)
{
	mod_nats_publisher_profile_t *profile = NULL;
	int arg = 0, i = 0;
	char *argv[SWITCH_EVENT_ALL];
	switch_xml_t params, param, connections, connection;
	switch_threadattr_t *thd_attr = NULL;
	char *subject = NULL;
	char *jetstream_name = NULL;
	switch_bool_t jetstream_enabled = SWITCH_FALSE;
	char *jetstream_subject = NULL;
	switch_memory_pool_t *pool;

	if (switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS)
	{
		goto err;
	}

	profile = switch_core_alloc(pool, sizeof(mod_nats_publisher_profile_t));
	profile->pool = pool;
	profile->name = switch_core_strdup(profile->pool, name);
	profile->running = 1;
	profile->event_ids[0] = SWITCH_EVENT_ALL;
	profile->conn_root = NULL;
	profile->conn_active = NULL;
	/* Set reasonable defaults which may change if more reasonable defaults are found */
	/* Handle defaults of non string types */
	profile->circuit_breaker_ms = 10000;
	profile->reconnect_interval_ms = 1000;
	profile->send_queue_size = 5000;

	if ((params = switch_xml_child(cfg, "params")) != NULL)
	{
		for (param = switch_xml_child(params, "param"); param; param = param->next)
		{
			char *var = (char *)switch_xml_attr_soft(param, "name");
			char *val = (char *)switch_xml_attr_soft(param, "value");

			if (!var)
			{
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Profile[%s] param missing 'name' attribute\n", profile->name);
				continue;
			}

			if (!val)
			{
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Profile[%s] param[%s] missing 'value' attribute\n", profile->name, var);
				continue;
			}

			if (!strncmp(var, "reconnect_interval_ms", 21))
			{
				int interval = atoi(val);
				if (interval && interval > 0)
				{
					profile->reconnect_interval_ms = interval;
				}
			}
			else if (!strncmp(var, "circuit_breaker_ms", 18))
			{
				int interval = atoi(val);
				if (interval && interval > 0)
				{
					profile->circuit_breaker_ms = interval;
				}
			}
			else if (!strncmp(var, "send_queue_size", 15))
			{
				int interval = atoi(val);
				if (interval && interval > 0)
				{
					profile->send_queue_size = interval;
				}
			}
			else if (!strncmp(var, "subject", 7))
			{
				subject = switch_core_strdup(profile->pool, val);
			}
			else if (!strncmp(var, "jetstream_enabled", 17))
			{
				jetstream_enabled = switch_true(val);
			}
			else if (!strncmp(var, "jetstream_name", 14))
			{
				jetstream_name = switch_core_strdup(profile->pool, val);
			}
			else if (!strncmp(var, "event_filter", 12))
			{
				char *tmp = switch_core_strdup(profile->pool, val);
				/* Parse new events */
				profile->event_subscriptions = switch_separate_string(tmp, ',', argv, (sizeof(argv) / sizeof(argv[0])));

				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Found %d event subscriptions\n", profile->event_subscriptions);

				for (arg = 0; arg < profile->event_subscriptions; arg++)
				{
					if (switch_name_event(argv[arg], &(profile->event_ids[arg])) != SWITCH_STATUS_SUCCESS && !switch_strstr(argv[arg], "SWITCH_EVENT_CUSTOM::"))
					{
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "The switch event %s was not recognised.\n", argv[arg]);
					}
				}
			}
		} /* params for loop */
	}

	profile->subject = subject ? subject : switch_core_strdup(profile->pool, profile->name);
	profile->jetstream_name = jetstream_name ? jetstream_name : switch_core_strdup(profile->pool, profile->name);
	profile->jetstream_enabled = jetstream_enabled;
	if (jetstream_enabled == SWITCH_TRUE)
	{
		jetstream_subject = strdup(profile->subject);
		if (jetstream_subject != NULL)
		{
			size_t size = strlen(jetstream_subject);
			if (size >= 2 &&
				jetstream_subject[size - 2] == '.' &&
				jetstream_subject[size - 1] == '*')
			{
				char jetstream_subject_trimmed[1024];
				switch_snprintf(jetstream_subject_trimmed, sizeof(jetstream_subject) - 2, "%s", jetstream_subject);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "profile [%s] trimmed subject [%s]\n", profile->name, jetstream_subject_trimmed);
				profile->jetstream_subject = switch_core_strdup(profile->pool, jetstream_subject_trimmed);
			}
			else
			{
				profile->jetstream_subject = switch_core_strdup(profile->pool, jetstream_subject);
			}
		}
		else
		{
			profile->jetstream_subject = switch_core_strdup(profile->pool, jetstream_subject);
		}
		switch_safe_free(jetstream_subject);
	}

	if ((connections = switch_xml_child(cfg, "connections")) != NULL)
	{
		for (connection = switch_xml_child(connections, "connection"); connection; connection = connection->next)
		{
			if (!profile->conn_root)
			{ /* Handle first root node */
				if (mod_nats_connection_create(&(profile->conn_root), connection, profile->pool) != SWITCH_STATUS_SUCCESS)
				{
					/* Handle connection create failure */
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "profile [%s] failed to create connection\n", profile->name);
					continue;
				}
				profile->conn_active = profile->conn_root;
			}
			else
			{
				if (mod_nats_connection_create(&(profile->conn_active->next), connection, profile->pool) != SWITCH_STATUS_SUCCESS)
				{
					/* Handle connection create failure */
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Profile[%s] failed to create connection\n", profile->name);
					continue;
				}
				profile->conn_active = profile->conn_active->next;
			}
		}
	}
	profile->conn_active = NULL;
	/* We are not going to open the publisher queue connection on create, but instead wait for the running thread to open it */

	/* Create a bounded FIFO queue for sending messages */
	if (switch_queue_create(&(profile->send_queue), profile->send_queue_size, profile->pool) != SWITCH_STATUS_SUCCESS)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot create send queue of size %d!\n",
						  profile->send_queue_size);
		goto err;
	}

	/* Start the event send thread. This will set up the initial connection */
	switch_threadattr_create(&thd_attr, profile->pool);
	switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
	if (switch_thread_create(&profile->publisher_thread, thd_attr, mod_nats_publisher_thread, profile, profile->pool))
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot create 'nats event sender' thread!\n");
		goto err;
	}

	/* Subscribe events */
	for (i = 0; i < profile->event_subscriptions; i++)
	{
		if (switch_strstr(argv[i], "SWITCH_EVENT_CUSTOM::"))
		{
			if (switch_event_bind_removable("NATS",
											SWITCH_EVENT_CUSTOM,
											argv[i] + strlen("SWITCH_EVENT_CUSTOM::"),
											mod_nats_publisher_event_handler,
											profile,
											&(profile->event_nodes[i])) != SWITCH_STATUS_SUCCESS)
			{

				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot bind to event handler %d!\n", (int)profile->event_ids[i]);
				goto err;
			}
		}
		else
		{
			if (switch_event_bind_removable("NATS",
											profile->event_ids[i],
											SWITCH_EVENT_SUBCLASS_ANY,
											mod_nats_publisher_event_handler,
											profile,
											&(profile->event_nodes[i])) != SWITCH_STATUS_SUCCESS)
			{

				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot bind to event handler %d!\n", (int)profile->event_ids[i]);
				goto err;
			}
		}
	}

	if (switch_core_hash_insert(mod_nats_globals.publisher_hash, name, (void *)profile) != SWITCH_STATUS_SUCCESS)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to insert new profile [%s] into mod_nats profile hash\n", name);
		goto err;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Profile[%s] Successfully started\n", profile->name);
	return SWITCH_STATUS_SUCCESS;

err:
	/* Cleanup */
	mod_nats_publisher_destroy(&profile);
	return SWITCH_STATUS_GENERR;
}

/* This should only be called in a single threaded context from the publisher profile send thread */
switch_status_t mod_nats_publisher_send(mod_nats_publisher_profile_t *profile, mod_nats_message_t *msg)
{
	natsMsg *message = NULL;
	natsStatus s;

	if (!profile->conn_active)
	{
		/* No connection, so we can not send the message. */
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "profile [%s] not active\n", profile->name);
		return SWITCH_STATUS_NOT_INITALIZED;
	}

	if (profile->jetstream_connected == SWITCH_TRUE)
	{
		char subj[1024];
		switch_snprintf(subj, sizeof(subj), "%s.%s", profile->jetstream_subject, msg->evname);
		s = natsMsg_Create(&message, subj, NULL, msg->pjson, strlen(msg->pjson));
		if (s != NATS_OK)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "could not create message using subject [%s] in profile [%s] not active\n", profile->jetstream_subject, profile->name);
			return SWITCH_STATUS_SOCKERR;
		}
		s = js_PublishMsgAsync(profile->js, &message, NULL);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "sending event [%s] in subject [%s]\n", msg->evname, subj);
		natsMsg_Destroy(message);
	}
	else
	{
		s = natsMsg_Create(&message, profile->subject, NULL, msg->pjson, strlen(msg->pjson));
		if (s != NATS_OK)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "could not create message using subject [%s] in profile [%s] not active\n", profile->subject, profile->name);
			return SWITCH_STATUS_SOCKERR;
		}
		s = natsConnection_PublishMsg(profile->conn_active->connection, message);
		natsMsg_Destroy(message);
	}

	if (s != NATS_OK)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Profile[%s] failed to send event on connection[%s] with subject [%s] payload [%s] %s\n",
						  profile->name, profile->conn_active->name, profile->subject, msg->pjson, natsStatus_GetText(s));
		mod_nats_connection_close(profile->conn_active);
		profile->conn_active = NULL;
		return SWITCH_STATUS_SOCKERR;
	}

	return SWITCH_STATUS_SUCCESS;
}

void *SWITCH_THREAD_FUNC mod_nats_publisher_thread(switch_thread_t *thread, void *data)
{
	mod_nats_message_t *msg = NULL;
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	mod_nats_publisher_profile_t *profile = (mod_nats_publisher_profile_t *)data;

	while (profile->running)
	{
		if (!profile->conn_active)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "no connection - reconnecting...\n");
			status = mod_nats_connection_open(profile->conn_root, &(profile->conn_active), profile->name);
			if (status == SWITCH_STATUS_SUCCESS)
			{
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "connected to profile [%s]\n", profile->name);
				if (profile->jetstream_enabled == SWITCH_TRUE)
				{
					profile->jetstream_connected = SWITCH_FALSE;
					profile->jerr = 0;
					jsOptions jsOpts;
					natsStatus s;
					s = jsOptions_Init(&jsOpts);
					if (s == NATS_OK)
					{
						char subj[1024];
						switch_snprintf(subj, sizeof(subj), "%s.*", profile->jetstream_subject);
						s = natsConnection_JetStream(&profile->js, profile->conn_active->connection, &jsOpts);
						if (s == NATS_OK)
						{
							jsStreamInfo *si = NULL;
							s = js_GetStreamInfo(&si, profile->js, profile->jetstream_name, NULL, &profile->jerr);
							if (s == NATS_OK)
							{
								switch_bool_t subject_exists = SWITCH_FALSE;
								int i = 0;
								jsStreamConfig cfg = *si->Config;
								for (i; i < cfg.SubjectsLen; i++)
								{
									if (!strncmp(cfg.Subjects[i], subj, strlen(subj)))
									{
										subject_exists = SWITCH_TRUE;
										switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "subject found [%s]\n",
														  cfg.Subjects[i]);
									}
								}
								if (subject_exists == SWITCH_FALSE)
								{
									cfg.Subjects[i] = subj;
									cfg.SubjectsLen = i + 1;
									switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "added subject [%s] to stream [%s]\n",
													  subj, profile->jetstream_name);
								}
								s = js_UpdateStream(&si, profile->js, &cfg, NULL, &profile->jerr);
								if (s != NATS_OK)
								{
									switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "could not update NATS stream [%s] on profile [%s] %s\n",
													  profile->jetstream_name, profile->name, natsStatus_GetText(s));
								}
								else
								{
									profile->jetstream_connected = SWITCH_TRUE;
								}
							}
							else if (s == NATS_NOT_FOUND)
							{
								jsStreamConfig cfg;
								jsStreamConfig_Init(&cfg);
								cfg.Name = profile->jetstream_name;
								cfg.Subjects = (const char *[1]){subj};
								cfg.SubjectsLen = 1;
								cfg.Storage = js_MemoryStorage;
								cfg.Retention = js_WorkQueuePolicy;
								switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "profile [%s] NATS stream [%s] not found\n",
												  profile->name, profile->jetstream_name);
								s = js_AddStream(&si, profile->js, &cfg, NULL, &profile->jerr);
								if (s != NATS_OK)
								{
									switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "could not add NATS stream [%s] on profile [%s] with subject [%s] %s\n",
													  profile->jetstream_name, profile->name, subj, natsStatus_GetText(s));
								}
								else
								{
									profile->jetstream_connected = SWITCH_TRUE;
								}
							}
							else
							{
								switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "could not get NATS stream [%s] info in profile [%s] %s\n", profile->jetstream_name, profile->name, natsStatus_GetText(s));
							}
							if (si)
							{
								jsStreamInfo_Destroy(si);
							}
						}
						if (profile->jetstream_connected == SWITCH_TRUE)
						{
							switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "stream [%s] connected on profile [%s]\n", profile->jetstream_name, profile->name);
						}
					}
				}
				continue;
			}
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "profile [%s] failed to connect with code(%d), sleeping for %dms\n",
							  profile->name, status, profile->reconnect_interval_ms);
			switch_sleep(profile->reconnect_interval_ms * 1000);
			continue;
		}
		if (!msg && switch_queue_pop_timeout(profile->send_queue, (void **)&msg, 1000000) != SWITCH_STATUS_SUCCESS)
		{
			continue;
		}

		if (msg)
		{
			switch (mod_nats_publisher_send(profile, msg))
			{
			case SWITCH_STATUS_SUCCESS:
				mod_nats_util_msg_destroy(&msg);
				break;

			case SWITCH_STATUS_NOT_INITALIZED:
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Send failed with 'not initialized'\n");
				break;

			case SWITCH_STATUS_SOCKERR:
			{
				mod_nats_message_t *msg_dup = NULL;
				switch_malloc(msg_dup, sizeof(mod_nats_message_t));
				*msg_dup = *msg;
				msg = NULL;
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Send failed with 'socket error'\n");
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "message %s\n", msg_dup->pjson);

				if (switch_queue_trypush(profile->send_queue, msg_dup) != SWITCH_STATUS_SUCCESS)
				{
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
									  "error placing the event in the listeners queue\n");
					mod_nats_util_msg_destroy(&msg_dup);
				}
				break;
			}

			default:
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Send failed with a generic error\n");
				break;
			}
		}
	}

	/* Abort the current message */
	mod_nats_util_msg_destroy(&msg);

	// Terminate the thread
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Event sender thread stopped\n");
	switch_thread_exit(thread, SWITCH_STATUS_SUCCESS);
	return NULL;
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

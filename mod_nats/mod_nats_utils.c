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

switch_status_t mod_nats_do_config(switch_bool_t reload)
{
	switch_xml_t cfg = NULL, xml = NULL, profiles = NULL, profile = NULL;
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, reload ? "reloading Config\n" : "loading Config\n");

	if (!(xml = switch_xml_open_cfg("nats.conf", &cfg, NULL)))
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "open of nats.conf.xml failed\n");
		return SWITCH_STATUS_FALSE;
	}

	if ((profiles = switch_xml_child(cfg, "publishers")))
	{
		if ((profile = switch_xml_child(profiles, "profile")))
		{
			for (; profile; profile = profile->next)
			{
				char *name = (char *)switch_xml_attr_soft(profile, "name");

				if (zstr(name))
				{
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to load mod_nats profile. Check configs missing name attr\n");
					continue;
				}

				if (mod_nats_publisher_create(name, profile) != SWITCH_STATUS_SUCCESS)
				{
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to load mod_nats profile [%s]. Check configs\n", name);
				}
				else
				{
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Loaded mod_nats profile [%s] successfully\n", name);
				}
			}
		}
		else
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Unable to locate a profile for mod_nats\n");
		}
	}
	else
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Unable to locate publishers section for mod_nats\n");
	}

	return SWITCH_STATUS_SUCCESS;
}

void mod_nats_util_msg_destroy(mod_nats_message_t **msg)
{
	if (!msg || !*msg)
		return;
	switch_safe_free((*msg)->pjson);
	switch_safe_free(*msg);
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

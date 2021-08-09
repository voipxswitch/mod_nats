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

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_nats_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_nats_load);
SWITCH_MODULE_DEFINITION(mod_nats, mod_nats_load, mod_nats_shutdown, NULL);

mod_nats_globals_t mod_nats_globals;

/* ------------------------------
   Startup
   ------------------------------
*/
SWITCH_MODULE_LOAD_FUNCTION(mod_nats_load)
{

	memset(&mod_nats_globals, 0, sizeof(mod_nats_globals_t));
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	mod_nats_globals.pool = pool;
	switch_core_hash_init(&(mod_nats_globals.publisher_hash));

	/* Create publisher profiles */
	if (mod_nats_do_config(SWITCH_FALSE) != SWITCH_STATUS_SUCCESS)
	{
		return SWITCH_STATUS_GENERR;
	}

	return SWITCH_STATUS_SUCCESS;
}

/* ------------------------------
   Shutdown
   ------------------------------
*/
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_nats_shutdown)
{
	switch_hash_index_t *hi = NULL;
	mod_nats_publisher_profile_t *publisher;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Mod starting shutting down\n");
	switch_event_unbind_callback(mod_nats_publisher_event_handler);

	while ((hi = switch_core_hash_first_iter(mod_nats_globals.publisher_hash, hi)))
	{
		switch_core_hash_this(hi, NULL, NULL, (void **)&publisher);
		mod_nats_publisher_destroy(&publisher);
	}

	switch_core_hash_destroy(&(mod_nats_globals.publisher_hash));

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Mod finished shutting down\n");
	return SWITCH_STATUS_SUCCESS;
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

#include "blade.h"
#include "tap.h"

#define CONSOLE_INPUT_MAX 512

ks_bool_t g_shutdown = KS_FALSE;

void loop(blade_handle_t *bh);
void process_console_input(blade_handle_t *bh, char *line);

typedef void (*command_callback)(blade_handle_t *bh, char *args);

struct command_def_s {
	const char *cmd;
	command_callback callback;
};

void command_quit(blade_handle_t *bh, char *args);
void command_execute(blade_handle_t *bh, char *args);
void command_subscribe(blade_handle_t *bh, char *args);

static const struct command_def_s command_defs[] = {
	{ "quit", command_quit },
	{ "execute", command_execute },
	{ "subscribe", command_subscribe },

	{ NULL, NULL }
};

ks_bool_t test_echo_response_handler(blade_rpc_response_t *brpcres, void *data)
{
	blade_handle_t *bh = NULL;
	blade_session_t *bs = NULL;
	cJSON *result = NULL;
	const char *text = NULL;

	ks_assert(brpcres);

	bh = blade_rpc_response_handle_get(brpcres);
	ks_assert(bh);

	bs = blade_sessionmgr_session_lookup(blade_handle_sessionmgr_get(bh), blade_rpc_response_sessionid_get(brpcres));
	ks_assert(bs);

	result = blade_rpcexecute_response_result_get(brpcres);
	ks_assert(result);

	text = cJSON_GetObjectCstr(result, "text");
	ks_assert(text);

	ks_log(KS_LOG_DEBUG, "Session (%s) test.echo response processing\n", blade_session_id_get(bs));

	blade_session_read_unlock(bs);

	ks_log(KS_LOG_DEBUG, "Session (%s) test.echo: %s\n", blade_session_id_get(bs), text);

	return KS_FALSE;
}

ks_bool_t blade_locate_response_handler(blade_rpc_response_t *brpcres, void *data)
{
	blade_handle_t *bh = NULL;
	blade_session_t *bs = NULL;
	const char *nodeid = NULL;
	cJSON *res = NULL;
	cJSON *res_result = NULL;
	cJSON *res_result_controllers = NULL;
	const char *res_result_protocol = NULL;
	const char *res_result_realm = NULL;
	cJSON *params = NULL;

	ks_assert(brpcres);

	bh = blade_rpc_response_handle_get(brpcres);
	ks_assert(bh);

	bs = blade_sessionmgr_session_lookup(blade_handle_sessionmgr_get(bh), blade_rpc_response_sessionid_get(brpcres));
	ks_assert(bs);

	res = blade_rpc_response_message_get(brpcres);
	ks_assert(res);

	res_result = cJSON_GetObjectItem(res, "result");
	ks_assert(res_result);

	res_result_protocol = cJSON_GetObjectCstr(res_result, "protocol");
	ks_assert(res_result_protocol);
	
	res_result_realm = cJSON_GetObjectCstr(res_result, "realm");
	ks_assert(res_result_realm);

	res_result_controllers = cJSON_GetObjectItem(res_result, "controllers");
	ks_assert(res_result_controllers);

	ks_log(KS_LOG_DEBUG, "Session (%s) blade.locate response processing\n", blade_session_id_get(bs));

	for (int index = 0; index < cJSON_GetArraySize(res_result_controllers); ++index) {
		cJSON *elem = cJSON_GetArrayItem(res_result_controllers, index);
		if (elem->type == cJSON_String) {
			ks_log(KS_LOG_DEBUG, "Session (%s) blade.locate (%s@%s) provider (%s)\n", blade_session_id_get(bs), res_result_protocol, res_result_realm, elem->valuestring);
			nodeid = elem->valuestring;
		}
	}

	blade_session_read_unlock(bs);

	params = cJSON_CreateObject();
	cJSON_AddStringToObject(params, "text", "hello world!");
	blade_handle_rpcexecute(bh, nodeid, "test.echo", res_result_protocol, res_result_realm, params, test_echo_response_handler, NULL);

	return KS_FALSE;
}

ks_bool_t blade_subscribe_response_handler(blade_rpc_response_t *brpcres, void *data)
{
	blade_handle_t *bh = NULL;
	blade_session_t *bs = NULL;

	ks_assert(brpcres);

	bh = blade_rpc_response_handle_get(brpcres);
	ks_assert(bh);

	bs = blade_sessionmgr_session_lookup(blade_handle_sessionmgr_get(bh), blade_rpc_response_sessionid_get(brpcres));
	ks_assert(bs);

	ks_log(KS_LOG_DEBUG, "Session (%s) blade.subscribe response processing\n", blade_session_id_get(bs));

	blade_session_read_unlock(bs);

	return KS_FALSE;
}

ks_bool_t test_event_request_handler(blade_rpc_request_t *brpcreq, void *data)
{
	blade_handle_t *bh = NULL;
	blade_session_t *bs = NULL;

	ks_assert(brpcreq);

	bh = blade_rpc_request_handle_get(brpcreq);
	ks_assert(bh);

	bs = blade_sessionmgr_session_lookup(blade_handle_sessionmgr_get(bh), blade_rpc_request_sessionid_get(brpcreq));
	ks_assert(bs);

	ks_log(KS_LOG_DEBUG, "Session (%s) test.event request processing\n", blade_session_id_get(bs));

	blade_session_read_unlock(bs);

	return KS_FALSE;
}

int main(int argc, char **argv)
{
	blade_handle_t *bh = NULL;
	config_t config;
	config_setting_t *config_blade = NULL;
	const char *cfgpath = "bladec.cfg";
	//const char *session_state_callback_id = NULL;
	const char *autoconnect = NULL;

	ks_global_set_default_logger(KS_LOG_LEVEL_DEBUG);

	blade_init();

	blade_handle_create(&bh);

	//if (argc > 1) cfgpath = argv[1];
	if (argc > 1) autoconnect = argv[1];

	config_init(&config);
	if (!config_read_file(&config, cfgpath)) {
		ks_log(KS_LOG_ERROR, "%s:%d - %s\n", config_error_file(&config), config_error_line(&config), config_error_text(&config));
		config_destroy(&config);
		return EXIT_FAILURE;
	}
	config_blade = config_lookup(&config, "blade");
	if (!config_blade) {
		ks_log(KS_LOG_ERROR, "Missing 'blade' config group\n");
		config_destroy(&config);
		return EXIT_FAILURE;
	}
	if (config_setting_type(config_blade) != CONFIG_TYPE_GROUP) {
		ks_log(KS_LOG_ERROR, "The 'blade' config setting is not a group\n");
		return EXIT_FAILURE;
	}

	if (blade_handle_startup(bh, config_blade) != KS_STATUS_SUCCESS) {
		ks_log(KS_LOG_ERROR, "Blade startup failed\n");
		return EXIT_FAILURE;
	}

	if (autoconnect) {
		blade_connection_t *bc = NULL;
		blade_identity_t *target = NULL;

		blade_identity_create(&target, ks_pool_get(bh));

		if (blade_identity_parse(target, autoconnect) == KS_STATUS_SUCCESS) blade_handle_connect(bh, &bc, target, NULL);

		blade_identity_destroy(&target);

		ks_sleep_ms(3000);
	}
	
	loop(bh);

	blade_handle_destroy(&bh);

	config_destroy(&config);

	blade_shutdown();

	return 0;
}

void loop(blade_handle_t *bh)
{
	char buf[CONSOLE_INPUT_MAX];
	while (!g_shutdown) {
		if (!fgets(buf, CONSOLE_INPUT_MAX, stdin)) break;

		for (int index = 0; buf[index]; ++index) {
			if (buf[index] == '\r' || buf[index] == '\n') {
				buf[index] = '\0';
				break;
			}
		}
		process_console_input(bh, buf);
	}
}

void parse_argument(char **input, char **arg, char terminator)
{
	char *tmp;

	ks_assert(input);
	ks_assert(*input);
	ks_assert(arg);

	tmp = *input;
	*arg = tmp;

	while (*tmp && *tmp != terminator) ++tmp;
	if (*tmp == terminator) {
		*tmp = '\0';
		++tmp;
	}
	*input = tmp;
}

void process_console_input(blade_handle_t *bh, char *line)
{
	char *args = line;
	char *cmd = NULL;
	ks_bool_t found = KS_FALSE;

	ks_log(KS_LOG_DEBUG, "Output: %s\n", line);

	parse_argument(&args, &cmd, ' ');

	ks_log(KS_LOG_DEBUG, "Command: %s, Args: %s\n", cmd, args);

	for (int32_t index = 0; command_defs[index].cmd; ++index) {
		if (!strcmp(command_defs[index].cmd, cmd)) {
			found = KS_TRUE;
			command_defs[index].callback(bh, args);
		}
	}
	if (!found) ks_log(KS_LOG_INFO, "Command '%s' unknown.\n", cmd);
}

void command_quit(blade_handle_t *bh, char *args)
{
	//ks_assert(bh);
	ks_assert(args);

	ks_log(KS_LOG_DEBUG, "Shutting down\n");
	g_shutdown = KS_TRUE;
}

void command_execute(blade_handle_t *bh, char *args)
{
	ks_assert(bh);
	ks_assert(args);

	blade_handle_rpclocate(bh, "test", "mydomain.com", blade_locate_response_handler, NULL);
}

void command_subscribe(blade_handle_t *bh, char *args)
{
	cJSON *channels = NULL;

	ks_assert(bh);
	ks_assert(args);

	channels = cJSON_CreateArray();
	cJSON_AddItemToArray(channels, cJSON_CreateString("test"));
	blade_handle_rpcsubscribe(bh, "test", "mydomain.com", channels, NULL, blade_subscribe_response_handler, NULL, test_event_request_handler, NULL);
	cJSON_Delete(channels);
}

/* For Emacs:
* Local Variables:
* mode:c
* indent-tabs-mode:t
* tab-width:4
* c-basic-offset:4
* End:
* For VIM:
* vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
*/

/*
 * "git alias" builtin command
 *
 * Copyright (C) 2020 Phillip Wood
 */
#include "builtin.h"
#include "config.h"
#include "parse-options.h"

enum cmd {
	CMD_GET,
	CMD_SET,
};

static const char * const alias_usage[] = {
	N_("git alias <name> [<command> [args ...]]"),
	NULL
};

static char *user_config_file(void)
{
	char *file = NULL, *user_config, *xdg_config;

	git_global_config(&user_config, &xdg_config);
	if (!user_config) {
		/*
		 * It is unknown if HOME/.gitconfig exists, so we do
		 * not know if we should write to XDG location; error
		 * out even if XDG_CONFIG_HOME is set and points at a
		 * sane location.
		 */
		error(_("$HOME not set"));
	} else if (access_or_warn(user_config, R_OK, 0) &&
	    xdg_config && !access_or_warn(xdg_config, R_OK, 0)) {
		file = xdg_config;
		free(user_config);
	} else {
		file = user_config;
		free(xdg_config);
	}

	return file;
}

static int git_cmd_exists(const char *cmd) {
	struct string_list sl = STRING_LIST_INIT_DUP;
	unsigned int i;
	int res;

	if (is_builtin(cmd))
		return 1;

	string_list_split(&sl, getenv("PATH"), PATH_SEP, -1);
	for (i = 0; i < sl.nr; i++) {
		if (!access(mkpath("%s/git-%s", sl.items[i].string, cmd),
			    R_OK | X_OK))
			break;
	}
	res = i < sl.nr;
	string_list_clear(&sl, 0);

	return res;
}

static int check_alias_name(const char *alias)
{
	const char *c = alias;
	while (*c) {
		if (!isalnum(*c) && *c != '-')
			return error(_("invalid name '%s' - alias names can "
				       "only contain letters, numbers and '-'"),
				     alias);
		c++;
	}
	if (git_cmd_exists(alias))
		return error(_("'%s' is a git command"), alias);

	return 0;
}

static char* concatanate_argv(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;
	const char *c;

	for (c = argv[0]; *c; c++) {
		if (!isspace(*c))
			break;
	}
	if (!*c) {
		error(_("alias definition is empty"));
		return NULL;
	}
	if (*c == '!') {
		if (argc > 1) {
			error(_("too many arguments for shell alias"));
			return NULL;
		}
		return xstrdup(c);
	}
	argv[0] = c;
	for (int i = 0; i < argc; i++) {
		int quote = !!argv[i][strcspn(argv[i], " \t\r\n")];

		if (i)
			strbuf_addch(&buf, ' ');
		if (quote)
			strbuf_addch(&buf, '"');
		for (c = argv[i]; *c; c++) {
			if (*c == '\\' || *c == '"')
				strbuf_addch(&buf, '\\');
			strbuf_addch(&buf, *c);
		}
		if (quote)
			strbuf_addch(&buf, '"');
	}
	return strbuf_detach(&buf, NULL);
}

struct alias_data {
	const char *alias;
	char *command;
	const char *origin;
	struct strbuf file;
};

static int collect_alias(const char *key, const char *value, void *d)
{
	struct alias_data *data = d;
	const char *p;

	if (!skip_prefix(key, "alias.", &p))
		return 0;

	if (strcasecmp(p, data->alias))
		return 0;

	free(data->command);
	if (git_config_string((const char**) &data->command, key, value))
		return 1;

	strbuf_reset(&data->file);
	data->origin = current_config_origin_type();
	if (!strcmp(data->origin, "file"))
		strbuf_addstr(&data->file, current_config_name());

	return 0;
}

static int find_alias_definition(const char *alias,
				 char **definition,
				 char **file,
				 const char **origin)

{
	struct alias_data data = { .alias = alias, .file = STRBUF_INIT };
	struct config_options opts = { .respect_includes = 1 };
	int res;

	if (definition)
		*definition = NULL;
	if (file)
		*file = NULL;
	if (origin)
		*origin = NULL;
	res = check_alias_name(alias);
	if (res)
		goto out;
	if (startup_info->have_repository) {
		opts.commondir = get_git_common_dir();
		opts.git_dir = get_git_dir();
	}

	res = config_with_options(collect_alias, &data, NULL, &opts);
	if (res)
		goto out;
	if (data.command) {
		if (definition)
			*definition = data.command;
		else
			free(data.command);
		if (origin)
			*origin = data.origin;
		if (file) {
			if (*data.file.buf)
				*file = strbuf_detach(&data.file, NULL);
			else
				*file = NULL;
		}
		res = 0;
	} else {
		res = 1;
	}

out:
	strbuf_release(&data.file);
	return res;
}

static int update_alias(int argc, const char **argv, enum cmd cmd)
{
	int res;
	const char *alias = argv[0];
	const char *origin;
	char *file;
	char *old_definition, *new_definition = NULL;
	struct strbuf key = STRBUF_INIT;

	argc--;
	argv++;
	res = find_alias_definition(alias, &old_definition, &file, &origin);
	if (res < 0) {
		goto out;
	} else if (res) {
		file = user_config_file();
		if (!file)
			goto out;
	} else if (strcmp(origin, "file")) {
		res = error(_("cannot change alias set in %s"), origin);
		goto out;
	}

	switch(cmd) {
	case CMD_SET:
		new_definition = concatanate_argv(argc, argv);
		if (!new_definition) {
			res = -1;
			goto out;
		}
		break;

	default:
		BUG("unknown command");
	}

	strbuf_addf(&key, "alias.%s", alias);
	res = git_config_set_in_file_gently(file, key.buf, new_definition);
	strbuf_release(&key);
	if (res) {
		if (old_definition)
			error(_("could not update alias"));
		else
			error(_("could not create alias"));
	} else {
		if (old_definition)
			printf(_("updated alias '%s'\n"), alias);
		else
			printf(_("created alias '%s'\n"), alias);
	}
out:
	free(old_definition);
	free(new_definition);
	free(file);

	return res;
}

static int get_alias(const char *alias)
{
	char *definition;
	int res;

	res = find_alias_definition(alias, &definition, NULL, NULL);
	if (res < 0)
		goto out;
	else if (res)
		error(_("alias '%s' does not exist"), alias);
	else if (!*definition)
		warning(_("alias '%s' is empty"), alias);
	else
		puts(definition);
out:
	free(definition);
	return res;
}

int cmd_alias(int argc, const char **argv, const char *prefix)
{
	enum cmd cmd = CMD_GET;

	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, alias_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc == 1) {
		return !!get_alias(argv[0]);
	} else if (argc > 1) {
		cmd = CMD_SET;
		return !!update_alias(argc, argv, cmd);
	}
	usage_with_options(alias_usage, options);
}

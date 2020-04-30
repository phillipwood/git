/*
 * "git alias" builtin command
 *
 * Copyright (C) 2020 Phillip Wood
 */
#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"

static const char * const alias_usage[] = {
	N_("git alias <name> [<command> [args ...]]"),
	NULL
};

static int git_cmd_exists(const char *cmd) {
	struct string_list sl = STRING_LIST_INIT_DUP;
	unsigned int i;
	int ret;

	if (is_builtin(cmd))
		return 1;

	string_list_split(&sl, getenv("PATH"), PATH_SEP, -1);
	for (i = 0; i < sl.nr; i++) {
		struct stat st;
		fprintf(stdout, "checking %s\n",
			mkpath("%s/git-%s", sl.items[i].string, cmd));

		if (!stat(mkpath("%s/git-%s", sl.items[i].string, cmd), &st)
#ifndef NO_TRUSTABLE_FILEMODE
			    && (st.st_mode & S_IXUSR)
#endif
		   )
			break;
	}

	ret = i < sl.nr;
	string_list_clear(&sl, 0);

	return ret;
}

static int check_alias(const char *alias)
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
	int shell_alias, i;

	for (c = argv[0]; *c; c++) {
		if (!isspace(*c))
			break;
	}
	if (!*c)
		return NULL;
	shell_alias = *c == '!';
	argv[0] = c;
	for (i = 0; i < argc; i++) {
		for (c = argv[i]; *c; c++) {
			if (!shell_alias &&
			    (isspace(*c) || *c == '\\' || *c == '\"'))
				strbuf_addch(&buf, '\\');
			strbuf_addch(&buf, *c);
		}
		if (i < argc - 1)
			strbuf_addch(&buf, ' ');
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
	struct config_options opts = { 0 };
	int ret;

	if (check_alias(alias))
		return -1;
	opts.respect_includes = 1;
	if (startup_info->have_repository) {
		opts.commondir = get_git_common_dir();
		opts.git_dir = get_git_dir();
	}

	if (config_with_options(collect_alias, &data, NULL, &opts))
		return -1;
	if (data.command) {
		if (definition)
			*definition = data.command;
		if (origin)
			*origin = data.origin;
		if (file) {
			if (*data.file.buf)
				*file = strbuf_detach(&data.file, NULL);
			else
				*file = NULL;
		}
		ret = 0;
	} else {
		if (definition)
			*definition = NULL;
		if (file)
			*file = NULL;
		if (origin)
			*origin = NULL;
		ret = 1;
	}

	strbuf_release(&data.file);
	return ret;
}

static int set_alias(int argc, const char **argv)
{
	const char *alias = argv[0];
	char *definition = concatanate_argv(--argc, ++argv);
	const char *origin = NULL;
	char *file = NULL;
	struct strbuf key = STRBUF_INIT;
	int res;

	if (!definition)
		error(_("alias command is empty"));
	res = find_alias_definition(alias, NULL, &file, &origin);
	if (res < 0)
		return res;
	else if (res)
		file = user_config_path();
	else if (strcmp(origin, "file"))
		return error(_("cannot change alias set in %s"), origin);
	if (!file)
		return error(_("cannot get default config file"));
	strbuf_addf(&key, "alias.%s", alias);
	res = git_config_set_in_file_gently(file, key.buf, definition);
	strbuf_release(&key);
	free(file);

	return res;
}

static int get_alias(const char *alias)
{
	char *definition;
	int res;

	res = find_alias_definition(alias, &definition, NULL, NULL);
	if (res < 0)
		return res;
	else if (res)
		error(_("alias '%s' does not exist"), alias);
	else if (!*definition)
		warning(_("alias '%s' is empty"), alias);
	else
		puts(definition);

	free(definition);
	return !!res;
}

int cmd_alias(int argc, const char **argv, const char *prefix)
{
	struct option options[] = { OPT_END() };
	int i;

	argc = parse_options(argc, argv, prefix, options, alias_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	for (i = 0; i < argc; i++)
		fprintf(stdout, "argv[%d]=|%s|\n", i, argv[i]);
	if (argc == 1)
		return get_alias(argv[0]);
	else if (argc > 1)
		return !!set_alias(argc, argv);
	usage_with_options(alias_usage, options);
}

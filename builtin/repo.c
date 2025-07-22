#include "builtin.h"
#include "parse-options.h"
#include "refs.h"

typedef const char *get_value_fn(struct repository *repo);

struct field {
	const char *key;
	get_value_fn *add_field_callback;
};

static const char *get_references_format(struct repository *repo)
{
	return ref_storage_format_to_name(repo->ref_storage_format);
}

/* repo_info_fields keys should be in lexicographical order */
static const struct field repo_info_fields[] = {
	{ "references.format", get_references_format },
};

static int repo_info_fields_cmp(const void *va, const void *vb)
{
	const struct field *a = va;
	const struct field *b = vb;

	return strcmp(a->key, b->key);
}

static get_value_fn *get_value_callback(const char *key)
{
	const struct field search_key = { key, NULL };
	const struct field *found = bsearch(&search_key, repo_info_fields,
					    ARRAY_SIZE(repo_info_fields),
					    sizeof(struct field),
					    repo_info_fields_cmp);
	return found ? found->add_field_callback : NULL;
}

static int qsort_strcmp(const void *va, const void *vb)
{
	const char *a = *(const char **)va;
	const char *b = *(const char **)vb;

	return strcmp(a, b);
}

static int print_fields(int argc, const char **argv, struct repository *repo)
{
	const char *last = "";

	QSORT(argv, argc, qsort_strcmp);

	for (int i = 0; i < argc; i++) {
		get_value_fn *callback;
		const char *key = argv[i];
		const char *value;

		if (!strcmp(key, last))
			continue;

		callback = get_value_callback(key);

		if (!callback)
			return error("key %s not found", key);

		value = callback(repo);
		printf("%s=%s\n", key, value);
		last = key;
	}

	return 0;
}

static int repo_info(int argc, const char **argv, const char *prefix UNUSED,
		     struct repository *repo)
{
	return print_fields(argc - 1, argv + 1, repo);
}

int cmd_repo(int argc, const char **argv, const char *prefix,
	     struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	const char *const repo_usage[] = {
		"git repo info [<key>...]",
		NULL
	};
	struct option options[] = {
		OPT_SUBCOMMAND("info", &fn, repo_info),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, repo_usage, 0);

	return fn(argc, argv, prefix, repo);
}

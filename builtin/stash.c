#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "builtin.h"
#include "config.h"
#include "parse-options.h"
#include "refs.h"
#include "lockfile.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "merge-recursive.h"
#include "argv-array.h"
#include "run-command.h"
#include "dir.h"
#include "rerere.h"
#include "revision.h"
#include "log-tree.h"
#include "diffcore.h"
#include "exec-cmd.h"
#include "quote.h"

#define INCLUDE_ALL_FILES 2

static const char * const git_stash_usage[] = {
	N_("git stash list [<options>]"),
	N_("git stash show [<options>] [<stash>]"),
	N_("git stash drop [-q|--quiet] [<stash>]"),
	N_("git stash ( pop | apply ) [--index] [-q|--quiet] [<stash>]"),
	N_("git stash branch <branchname> [<stash>]"),
	N_("git stash clear"),
	N_("git stash [push [-p|--patch] [-k|--[no-]keep-index] [-q|--quiet]\n"
	   "          [-u|--include-untracked] [-a|--all] [-m|--message <message>]\n"
	   "          [--pathspec-from-file=<file> [--pathspec-file-nul]]\n"
	   "          [--] [<pathspec>...]]"),
	N_("git stash save [-p|--patch] [-k|--[no-]keep-index] [-q|--quiet]\n"
	   "          [-u|--include-untracked] [-a|--all] [<message>]"),
	NULL
};

static const char * const git_stash_list_usage[] = {
	N_("git stash list [<options>]"),
	NULL
};

static const char * const git_stash_show_usage[] = {
	N_("git stash show [<options>] [<stash>]"),
	NULL
};

static const char * const git_stash_drop_usage[] = {
	N_("git stash drop [-q|--quiet] [<stash>]"),
	NULL
};

static const char * const git_stash_pop_usage[] = {
	N_("git stash pop [--index] [-q|--quiet] [<stash>]"),
	NULL
};

static const char * const git_stash_apply_usage[] = {
	N_("git stash apply [--index] [-q|--quiet] [<stash>]"),
	NULL
};

static const char * const git_stash_branch_usage[] = {
	N_("git stash branch <branchname> [<stash>]"),
	NULL
};

static const char * const git_stash_clear_usage[] = {
	N_("git stash clear"),
	NULL
};

static const char * const git_stash_store_usage[] = {
	N_("git stash store [-m|--message <message>] [-q|--quiet] <commit>"),
	NULL
};

static const char * const git_stash_push_usage[] = {
	N_("git stash [push [-p|--patch] [-k|--[no-]keep-index] [-q|--quiet]\n"
	   "          [-u|--include-untracked] [-a|--all] [-m|--message <message>]\n"
	   "          [--] [<pathspec>...]]"),
	NULL
};

static const char * const git_stash_save_usage[] = {
	N_("git stash save [-p|--patch] [-k|--[no-]keep-index] [-q|--quiet]\n"
	   "          [-u|--include-untracked] [-a|--all] [<message>]"),
	NULL
};

static const char *ref_stash = "refs/stash";
static struct strbuf stash_index_path = STRBUF_INIT;

/*
 * w_commit is set to the commit containing the working tree
 * b_commit is set to the base commit
 * i_commit is set to the commit containing the index tree
 * u_commit is set to the commit containing the untracked files tree
 * w_tree is set to the working tree
 * b_tree is set to the base tree
 * i_tree is set to the index tree
 * u_tree is set to the untracked files tree
 */
struct stash_info {
	struct object_id w_commit;
	struct object_id b_commit;
	struct object_id i_commit;
	struct object_id u_commit;
	struct object_id w_tree;
	struct object_id b_tree;
	struct object_id i_tree;
	struct object_id u_tree;
	struct strbuf revision;
	int is_stash_ref;
	int has_u;
};

static void free_stash_info(struct stash_info *info)
{
	strbuf_release(&info->revision);
}

static void assert_stash_like(struct stash_info *info, const char *revision)
{
	if (get_oidf(&info->b_commit, "%s^1", revision) ||
	    get_oidf(&info->w_tree, "%s:", revision) ||
	    get_oidf(&info->b_tree, "%s^1:", revision) ||
	    get_oidf(&info->i_tree, "%s^2:", revision))
		die(_("'%s' is not a stash-like commit"), revision);
}

static int get_stash_info(struct stash_info *info, int argc, const char **argv)
{
	int ret;
	char *end_of_rev;
	char *expanded_ref;
	const char *revision;
	const char *commit = NULL;
	struct object_id dummy;
	struct strbuf symbolic = STRBUF_INIT;

	if (argc > 1) {
		int i;
		struct strbuf refs_msg = STRBUF_INIT;

		for (i = 0; i < argc; i++)
			strbuf_addf(&refs_msg, " '%s'", argv[i]);

		fprintf_ln(stderr, _("Too many revisions specified:%s"),
			   refs_msg.buf);
		strbuf_release(&refs_msg);

		return -1;
	}

	if (argc == 1)
		commit = argv[0];

	strbuf_init(&info->revision, 0);
	if (!commit) {
		if (!ref_exists(ref_stash)) {
			free_stash_info(info);
			fprintf_ln(stderr, _("No stash entries found."));
			return -1;
		}

		strbuf_addf(&info->revision, "%s@{0}", ref_stash);
	} else if (strspn(commit, "0123456789") == strlen(commit)) {
		strbuf_addf(&info->revision, "%s@{%s}", ref_stash, commit);
	} else {
		strbuf_addstr(&info->revision, commit);
	}

	revision = info->revision.buf;

	if (get_oid(revision, &info->w_commit)) {
		error(_("%s is not a valid reference"), revision);
		free_stash_info(info);
		return -1;
	}

	assert_stash_like(info, revision);

	info->has_u = !get_oidf(&info->u_tree, "%s^3:", revision);

	end_of_rev = strchrnul(revision, '@');
	strbuf_add(&symbolic, revision, end_of_rev - revision);

	ret = dwim_ref(symbolic.buf, symbolic.len, &dummy, &expanded_ref);
	strbuf_release(&symbolic);
	switch (ret) {
	case 0: /* Not found, but valid ref */
		info->is_stash_ref = 0;
		break;
	case 1:
		info->is_stash_ref = !strcmp(expanded_ref, ref_stash);
		break;
	default: /* Invalid or ambiguous */
		free_stash_info(info);
	}

	free(expanded_ref);
	return !(ret == 0 || ret == 1);
}

static int do_clear_stash(void)
{
	struct object_id obj;
	if (get_oid(ref_stash, &obj))
		return 0;

	return delete_ref(NULL, ref_stash, &obj, 0);
}

static int clear_stash(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_clear_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc)
		return error(_("git stash clear with parameters is "
			       "unimplemented"));

	return do_clear_stash();
}

static int reset_tree(struct object_id *i_tree, int update, int reset)
{
	int nr_trees = 1;
	struct unpack_trees_options opts;
	struct tree_desc t[MAX_UNPACK_TREES];
	struct tree *tree;
	struct lock_file lock_file = LOCK_INIT;

	read_cache_preload(NULL);
	if (refresh_cache(REFRESH_QUIET))
		return -1;

	hold_locked_index(&lock_file, LOCK_DIE_ON_ERROR);

	memset(&opts, 0, sizeof(opts));

	tree = parse_tree_indirect(i_tree);
	if (parse_tree(tree))
		return -1;

	init_tree_desc(t, tree->buffer, tree->size);

	opts.head_idx = 1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	opts.merge = 1;
	opts.reset = reset;
	opts.update = update;
	opts.fn = oneway_merge;

	if (unpack_trees(nr_trees, t, &opts))
		return -1;

	if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
		return error(_("unable to write new index file"));

	return 0;
}

static int diff_tree_binary(struct strbuf *out, struct object_id *w_commit)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	const char *w_commit_hex = oid_to_hex(w_commit);

	/*
	 * Diff-tree would not be very hard to replace with a native function,
	 * however it should be done together with apply_cached.
	 */
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "diff-tree", "--binary", NULL);
	argv_array_pushf(&cp.args, "%s^2^..%s^2", w_commit_hex, w_commit_hex);

	return pipe_command(&cp, NULL, 0, out, 0, NULL, 0);
}

static int apply_cached(struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * Apply currently only reads either from stdin or a file, thus
	 * apply_all_patches would have to be updated to optionally take a
	 * buffer.
	 */
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "apply", "--cached", NULL);
	return pipe_command(&cp, out->buf, out->len, NULL, 0, NULL, 0);
}

static int reset_head(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * Reset is overall quite simple, however there is no current public
	 * API for resetting.
	 */
	cp.git_cmd = 1;
	argv_array_push(&cp.args, "reset");

	return run_command(&cp);
}

static void add_diff_to_buf(struct diff_queue_struct *q,
			    struct diff_options *options,
			    void *data)
{
	int i;

	for (i = 0; i < q->nr; i++) {
		strbuf_addstr(data, q->queue[i]->one->path);

		/* NUL-terminate: will be fed to update-index -z */
		strbuf_addch(data, '\0');
	}
}

static int get_newly_staged(struct strbuf *out, struct object_id *c_tree)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	const char *c_tree_hex = oid_to_hex(c_tree);

	/*
	 * diff-index is very similar to diff-tree above, and should be
	 * converted together with update_index.
	 */
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "diff-index", "--cached", "--name-only",
			 "--diff-filter=A", NULL);
	argv_array_push(&cp.args, c_tree_hex);
	return pipe_command(&cp, NULL, 0, out, 0, NULL, 0);
}

static int update_index(struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * Update-index is very complicated and may need to have a public
	 * function exposed in order to remove this forking.
	 */
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "update-index", "--add", "--stdin", NULL);
	return pipe_command(&cp, out->buf, out->len, NULL, 0, NULL, 0);
}

static int restore_untracked(struct object_id *u_tree)
{
	int res;
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * We need to run restore files from a given index, but without
	 * affecting the current index, so we use GIT_INDEX_FILE with
	 * run_command to fork processes that will not interfere.
	 */
	cp.git_cmd = 1;
	argv_array_push(&cp.args, "read-tree");
	argv_array_push(&cp.args, oid_to_hex(u_tree));
	argv_array_pushf(&cp.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);
	if (run_command(&cp)) {
		remove_path(stash_index_path.buf);
		return -1;
	}

	child_process_init(&cp);
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "checkout-index", "--all", NULL);
	argv_array_pushf(&cp.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);

	res = run_command(&cp);
	remove_path(stash_index_path.buf);
	return res;
}

static int do_apply_stash(const char *prefix, struct stash_info *info,
			  int index, int quiet)
{
	int ret;
	int has_index = index;
	struct merge_options o;
	struct object_id c_tree;
	struct object_id index_tree;
	struct commit *result;
	const struct object_id *bases[1];

	read_cache_preload(NULL);
	if (refresh_and_write_cache(REFRESH_QUIET, 0, 0))
		return -1;

	if (write_cache_as_tree(&c_tree, 0, NULL))
		return error(_("cannot apply a stash in the middle of a merge"));

	if (index) {
		if (oideq(&info->b_tree, &info->i_tree) ||
		    oideq(&c_tree, &info->i_tree)) {
			has_index = 0;
		} else {
			struct strbuf out = STRBUF_INIT;

			if (diff_tree_binary(&out, &info->w_commit)) {
				strbuf_release(&out);
				return error(_("could not generate diff %s^!."),
					     oid_to_hex(&info->w_commit));
			}

			ret = apply_cached(&out);
			strbuf_release(&out);
			if (ret)
				return error(_("conflicts in index."
					       "Try without --index."));

			discard_cache();
			read_cache();
			if (write_cache_as_tree(&index_tree, 0, NULL))
				return error(_("could not save index tree"));

			reset_head();
			discard_cache();
			read_cache();
		}
	}

	if (info->has_u && restore_untracked(&info->u_tree))
		return error(_("could not restore untracked files from stash"));

	init_merge_options(&o, the_repository);

	o.branch1 = "Updated upstream";
	o.branch2 = "Stashed changes";

	if (oideq(&info->b_tree, &c_tree))
		o.branch1 = "Version stash was based on";

	if (quiet)
		o.verbosity = 0;

	if (o.verbosity >= 3)
		printf_ln(_("Merging %s with %s"), o.branch1, o.branch2);

	bases[0] = &info->b_tree;

	ret = merge_recursive_generic(&o, &c_tree, &info->w_tree, 1, bases,
				      &result);
	if (ret) {
		rerere(0);

		if (index)
			fprintf_ln(stderr, _("Index was not unstashed."));

		return ret;
	}

	if (has_index) {
		if (reset_tree(&index_tree, 0, 0))
			return -1;
	} else {
		struct strbuf out = STRBUF_INIT;

		if (get_newly_staged(&out, &c_tree)) {
			strbuf_release(&out);
			return -1;
		}

		if (reset_tree(&c_tree, 0, 1)) {
			strbuf_release(&out);
			return -1;
		}

		ret = update_index(&out);
		strbuf_release(&out);
		if (ret)
			return -1;

		/* read back the result of update_index() back from the disk */
		discard_cache();
		read_cache();
	}

	if (!quiet) {
		struct child_process cp = CHILD_PROCESS_INIT;

		/*
		 * Status is quite simple and could be replaced with calls to
		 * wt_status in the future, but it adds complexities which may
		 * require more tests.
		 */
		cp.git_cmd = 1;
		cp.dir = prefix;
		argv_array_pushf(&cp.env_array, GIT_WORK_TREE_ENVIRONMENT"=%s",
				 absolute_path(get_git_work_tree()));
		argv_array_pushf(&cp.env_array, GIT_DIR_ENVIRONMENT"=%s",
				 absolute_path(get_git_dir()));
		argv_array_push(&cp.args, "status");
		run_command(&cp);
	}

	return 0;
}

static int apply_stash(int argc, const char **argv, const char *prefix)
{
	int ret;
	int quiet = 0;
	int index = 0;
	struct stash_info info;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_BOOL(0, "index", &index,
			 N_("attempt to recreate the index")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_apply_usage, 0);

	if (get_stash_info(&info, argc, argv))
		return -1;

	ret = do_apply_stash(prefix, &info, index, quiet);
	free_stash_info(&info);
	return ret;
}

static int do_drop_stash(struct stash_info *info, int quiet)
{
	int ret;
	struct child_process cp_reflog = CHILD_PROCESS_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * reflog does not provide a simple function for deleting refs. One will
	 * need to be added to avoid implementing too much reflog code here
	 */

	cp_reflog.git_cmd = 1;
	argv_array_pushl(&cp_reflog.args, "reflog", "delete", "--updateref",
			 "--rewrite", NULL);
	argv_array_push(&cp_reflog.args, info->revision.buf);
	ret = run_command(&cp_reflog);
	if (!ret) {
		if (!quiet)
			printf_ln(_("Dropped %s (%s)"), info->revision.buf,
				  oid_to_hex(&info->w_commit));
	} else {
		return error(_("%s: Could not drop stash entry"),
			     info->revision.buf);
	}

	/*
	 * This could easily be replaced by get_oid, but currently it will throw
	 * a fatal error when a reflog is empty, which we can not recover from.
	 */
	cp.git_cmd = 1;
	/* Even though --quiet is specified, rev-parse still outputs the hash */
	cp.no_stdout = 1;
	argv_array_pushl(&cp.args, "rev-parse", "--verify", "--quiet", NULL);
	argv_array_pushf(&cp.args, "%s@{0}", ref_stash);
	ret = run_command(&cp);

	/* do_clear_stash if we just dropped the last stash entry */
	if (ret)
		do_clear_stash();

	return 0;
}

static void assert_stash_ref(struct stash_info *info)
{
	if (!info->is_stash_ref) {
		error(_("'%s' is not a stash reference"), info->revision.buf);
		free_stash_info(info);
		exit(1);
	}
}

static int drop_stash(int argc, const char **argv, const char *prefix)
{
	int ret;
	int quiet = 0;
	struct stash_info info;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_drop_usage, 0);

	if (get_stash_info(&info, argc, argv))
		return -1;

	assert_stash_ref(&info);

	ret = do_drop_stash(&info, quiet);
	free_stash_info(&info);
	return ret;
}

static int pop_stash(int argc, const char **argv, const char *prefix)
{
	int ret;
	int index = 0;
	int quiet = 0;
	struct stash_info info;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_BOOL(0, "index", &index,
			 N_("attempt to recreate the index")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_pop_usage, 0);

	if (get_stash_info(&info, argc, argv))
		return -1;

	assert_stash_ref(&info);
	if ((ret = do_apply_stash(prefix, &info, index, quiet)))
		printf_ln(_("The stash entry is kept in case "
			    "you need it again."));
	else
		ret = do_drop_stash(&info, quiet);

	free_stash_info(&info);
	return ret;
}

static int branch_stash(int argc, const char **argv, const char *prefix)
{
	int ret;
	const char *branch = NULL;
	struct stash_info info;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_branch_usage, 0);

	if (!argc) {
		fprintf_ln(stderr, _("No branch name specified"));
		return -1;
	}

	branch = argv[0];

	if (get_stash_info(&info, argc - 1, argv + 1))
		return -1;

	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "checkout", "-b", NULL);
	argv_array_push(&cp.args, branch);
	argv_array_push(&cp.args, oid_to_hex(&info.b_commit));
	ret = run_command(&cp);
	if (!ret)
		ret = do_apply_stash(prefix, &info, 1, 0);
	if (!ret && info.is_stash_ref)
		ret = do_drop_stash(&info, 0);

	free_stash_info(&info);

	return ret;
}

static int list_stash(int argc, const char **argv, const char *prefix)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_list_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (!ref_exists(ref_stash))
		return 0;

	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "log", "--format=%gd: %gs", "-g",
			 "--first-parent", "-m", NULL);
	argv_array_pushv(&cp.args, argv);
	argv_array_push(&cp.args, ref_stash);
	argv_array_push(&cp.args, "--");
	return run_command(&cp);
}

static int show_stat = 1;
static int show_patch;
static int use_legacy_stash;

static int git_stash_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "stash.showstat")) {
		show_stat = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "stash.showpatch")) {
		show_patch = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "stash.usebuiltin")) {
		use_legacy_stash = !git_config_bool(var, value);
		return 0;
	}
	return git_diff_basic_config(var, value, cb);
}

static int show_stash(int argc, const char **argv, const char *prefix)
{
	int i;
	int ret = 0;
	struct stash_info info;
	struct rev_info rev;
	struct argv_array stash_args = ARGV_ARRAY_INIT;
	struct argv_array revision_args = ARGV_ARRAY_INIT;
	struct option options[] = {
		OPT_END()
	};

	init_diff_ui_defaults();
	git_config(git_diff_ui_config, NULL);
	init_revisions(&rev, prefix);

	argv_array_push(&revision_args, argv[0]);
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			argv_array_push(&stash_args, argv[i]);
		else
			argv_array_push(&revision_args, argv[i]);
	}

	ret = get_stash_info(&info, stash_args.argc, stash_args.argv);
	argv_array_clear(&stash_args);
	if (ret)
		return -1;

	/*
	 * The config settings are applied only if there are not passed
	 * any options.
	 */
	if (revision_args.argc == 1) {
		if (show_stat)
			rev.diffopt.output_format = DIFF_FORMAT_DIFFSTAT;

		if (show_patch)
			rev.diffopt.output_format |= DIFF_FORMAT_PATCH;

		if (!show_stat && !show_patch) {
			free_stash_info(&info);
			return 0;
		}
	}

	argc = setup_revisions(revision_args.argc, revision_args.argv, &rev, NULL);
	if (argc > 1) {
		free_stash_info(&info);
		usage_with_options(git_stash_show_usage, options);
	}
	if (!rev.diffopt.output_format) {
		rev.diffopt.output_format = DIFF_FORMAT_PATCH;
		diff_setup_done(&rev.diffopt);
	}

	rev.diffopt.flags.recursive = 1;
	setup_diff_pager(&rev.diffopt);
	diff_tree_oid(&info.b_commit, &info.w_commit, "", &rev.diffopt);
	log_tree_diff_flush(&rev);

	free_stash_info(&info);
	return diff_result_code(&rev.diffopt, 0);
}

static int do_store_stash(const struct object_id *w_commit, const char *stash_msg,
			  int quiet)
{
	if (!stash_msg)
		stash_msg = "Created via \"git stash store\".";

	if (update_ref(stash_msg, ref_stash, w_commit, NULL,
		       REF_FORCE_CREATE_REFLOG,
		       quiet ? UPDATE_REFS_QUIET_ON_ERR :
		       UPDATE_REFS_MSG_ON_ERR)) {
		if (!quiet) {
			fprintf_ln(stderr, _("Cannot update %s with %s"),
				   ref_stash, oid_to_hex(w_commit));
		}
		return -1;
	}

	return 0;
}

static int store_stash(int argc, const char **argv, const char *prefix)
{
	int quiet = 0;
	const char *stash_msg = NULL;
	struct object_id obj;
	struct object_context dummy;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet")),
		OPT_STRING('m', "message", &stash_msg, "message",
			   N_("stash message")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_store_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (argc != 1) {
		if (!quiet)
			fprintf_ln(stderr, _("\"git stash store\" requires one "
					     "<commit> argument"));
		return -1;
	}

	if (get_oid_with_context(the_repository,
				 argv[0], quiet ? GET_OID_QUIETLY : 0, &obj,
				 &dummy)) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot update %s with %s"),
					     ref_stash, argv[0]);
		return -1;
	}

	return do_store_stash(&obj, stash_msg, quiet);
}

static void add_pathspecs(struct argv_array *args,
			  const struct pathspec *ps) {
	int i;

	for (i = 0; i < ps->nr; i++)
		argv_array_push(args, ps->items[i].original);
}

/*
 * `untracked_files` will be filled with the names of untracked files.
 * The return value is:
 *
 * = 0 if there are not any untracked files
 * > 0 if there are untracked files
 */
static int get_untracked_files(const struct pathspec *ps, int include_untracked,
			       struct strbuf *untracked_files)
{
	int i;
	int found = 0;
	struct dir_struct dir;

	memset(&dir, 0, sizeof(dir));
	if (include_untracked != INCLUDE_ALL_FILES)
		setup_standard_excludes(&dir);

	fill_directory(&dir, the_repository->index, ps);
	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		found++;
		strbuf_addstr(untracked_files, ent->name);
		/* NUL-terminate: will be fed to update-index -z */
		strbuf_addch(untracked_files, '\0');
		free(ent);
	}

	free(dir.entries);
	free(dir.ignored);
	clear_directory(&dir);
	return found;
}

/*
 * The return value of `check_changes_tracked_files()` can be:
 *
 * < 0 if there was an error
 * = 0 if there are no changes.
 * > 0 if there are changes.
 */
static int check_changes_tracked_files(const struct pathspec *ps)
{
	int result;
	struct rev_info rev;
	struct object_id dummy;
	int ret = 0;

	/* No initial commit. */
	if (get_oid("HEAD", &dummy))
		return -1;

	if (read_cache() < 0)
		return -1;

	init_revisions(&rev, NULL);
	copy_pathspec(&rev.prune_data, ps);

	rev.diffopt.flags.quick = 1;
	rev.diffopt.flags.ignore_submodules = 1;
	rev.abbrev = 0;

	add_head_to_pending(&rev);
	diff_setup_done(&rev.diffopt);

	result = run_diff_index(&rev, 1);
	if (diff_result_code(&rev.diffopt, result)) {
		ret = 1;
		goto done;
	}

	object_array_clear(&rev.pending);
	result = run_diff_files(&rev, 0);
	if (diff_result_code(&rev.diffopt, result)) {
		ret = 1;
		goto done;
	}

done:
	clear_pathspec(&rev.prune_data);
	return ret;
}

/*
 * The function will fill `untracked_files` with the names of untracked files
 * It will return 1 if there were any changes and 0 if there were not.
 */
static int check_changes(const struct pathspec *ps, int include_untracked,
			 struct strbuf *untracked_files)
{
	int ret = 0;
	if (check_changes_tracked_files(ps))
		ret = 1;

	if (include_untracked && get_untracked_files(ps, include_untracked,
						     untracked_files))
		ret = 1;

	return ret;
}

static int save_untracked_files(struct stash_info *info, struct strbuf *msg,
				struct strbuf files)
{
	int ret = 0;
	struct strbuf untracked_msg = STRBUF_INIT;
	struct child_process cp_upd_index = CHILD_PROCESS_INIT;
	struct index_state istate = { NULL };

	cp_upd_index.git_cmd = 1;
	argv_array_pushl(&cp_upd_index.args, "update-index", "-z", "--add",
			 "--remove", "--stdin", NULL);
	argv_array_pushf(&cp_upd_index.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);

	strbuf_addf(&untracked_msg, "untracked files on %s\n", msg->buf);
	if (pipe_command(&cp_upd_index, files.buf, files.len, NULL, 0,
			 NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (write_index_as_tree(&info->u_tree, &istate, stash_index_path.buf, 0,
				NULL)) {
		ret = -1;
		goto done;
	}

	if (commit_tree(untracked_msg.buf, untracked_msg.len,
			&info->u_tree, NULL, &info->u_commit, NULL, NULL)) {
		ret = -1;
		goto done;
	}

done:
	discard_index(&istate);
	strbuf_release(&untracked_msg);
	remove_path(stash_index_path.buf);
	return ret;
}

static int get_uncommitted_changes(struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "diff-index", "-p", "-U0",
			 "--inter-hunk-context=0", "-O/dev/null", "--no-prefix",
			 "HEAD", "--", NULL);

	return pipe_command(&cp, NULL, 0, out, 0, NULL, 0);
}

enum diff_parser_state {
	DIFF_STATE_DIFF_HEADER,
	DIFF_STATE_HUNK_HEADER,
	DIFF_STATE_IN_HUNK,
	DIFF_STATE_EOF
};

struct diff_parser {
	struct strbuf old_path, new_path;
	const char *old_mode, *new_mode;
	size_t old_mode_len, new_mode_len;
	const char *s;
	size_t len, rem;
	size_t del_pos, add_pos;
	unsigned long del_off, add_off, del_len, add_len;
	ssize_t skip_delta;
	enum diff_parser_state state;
};

struct patch_line {
	const char *text;
	size_t len;
	char sign;
};

struct patch_hunk {
	unsigned long del_off, del_len, add_off, add_len;
	ssize_t delta;
	size_t line_alloc, line_nr;
	struct patch_line *line;
};

struct patch_file {
	char *old_path, *new_path, *old_mode, *new_mode;
	size_t hunk_alloc, hunk_nr;
	struct patch_hunk *hunk;
};

struct patch {
	size_t file_alloc, file_nr;
	struct patch_file *file;
	unsigned push_incomplete;
};

#define PATCH_INIT { 0 }

static void patch_release(struct patch *patch)
{
	size_t i, j;

	for (i = 0; i < patch->file_nr; i++) {
		struct patch_file *file = &patch->file[i];

		for (j = 0; j < file->hunk_nr; j++)
			free(file->hunk[j].line);

		free(file->hunk);
		free(file->old_path);
		free(file->new_path);
		free(file->old_mode);
		free(file->new_mode);
	}
	free(patch->file);
}

static int render_patch_hunk_range(unsigned long off, unsigned long len,
				   FILE *out)
{
	if (!len)
		off--;
	fprintf(out, "%lu", off);
	if (len != 1)
		fprintf(out, ",%lu", len);
	return 0;
}

static int render_patch_hunk(struct patch_hunk *hunk, FILE *out)
{
	size_t i;

	fputs("@@ -", out);
	render_patch_hunk_range(hunk->del_off - hunk->delta, hunk->del_len,
				out);
	fputs(" +", out);
	render_patch_hunk_range(hunk->add_off, hunk->add_len, out);
	fputs(" @@\n", out);
	for (i = 0; i < hunk->line_nr; i++)
		fprintf(out, "%c%.*s", hunk->line[i].sign,
			(int)hunk->line[i].len, hunk->line[i].text);
	return 0;
}

static int render_patch_file(struct patch_file *file, FILE *out)
{
	size_t i;
	struct strbuf path_a = STRBUF_INIT;
	struct strbuf path_b = STRBUF_INIT;
	if (strcmp(file->old_path, "/dev/null"))
		quote_two_c_style(&path_a, "a/", file->old_path, 1);
	else
		quote_two_c_style(&path_a, "a/", file->new_path, 1);
	if (strcmp(file->new_path, "/dev/null"))
		quote_two_c_style(&path_b, "b/", file->new_path, 1);
	else
		quote_two_c_style(&path_b, "b/", file->old_path, 1);
	fprintf(out, "diff --git %s %s\n", path_a.buf, path_b.buf);
	if (file->old_mode) {
		if (file->new_mode) {
			fprintf(out, "old mode %s\n", file->old_mode);
			fprintf(out, "new mode %s\n", file->new_mode);
		} else {
			fprintf(out, "deleted file mode %s\n", file->old_mode);
		}
	}
	fprintf(out, "--- %s\n",
		strcmp(file->old_path, "/dev/null") ? path_a.buf : "/dev/null");
	fprintf(out, "+++ %s\n",
		strcmp(file->new_path, "/dev/null") ? path_b.buf : "/dev/null");

	for (i = 0; i < file->hunk_nr; i++)
		render_patch_hunk(&file->hunk[i], out);

	return 0;
}

static int render_patch(struct patch *patch, FILE *out)
{
	size_t i;

	for (i = 0; i < patch->file_nr; i++)
		render_patch_file(&patch->file[i], out);

	return 0;
}

static void patch_push_file(struct patch *patch, struct diff_parser *parser)
{
	struct patch_file *file;

	ALLOC_GROW_BY(patch->file, patch->file_nr, 1, patch->file_alloc);
	file = &patch->file[patch->file_nr - 1];
	file->old_path = strbuf_detach(&parser->old_path, NULL);
	file->new_path = strbuf_detach(&parser->new_path, NULL);
	if (parser->old_mode)
		file->old_mode =
			xmemdupz(parser->old_mode, parser->old_mode_len);
	if (parser->new_mode)
		file->new_mode =
			xmemdupz(parser->new_mode, parser->new_mode_len);
}

static void patch_push_hunk(struct patch *patch, struct diff_parser *parser)
{
	struct patch_file *file = &patch->file[patch->file_nr - 1];
	struct patch_hunk *hunk;

	ALLOC_GROW_BY(file->hunk, file->hunk_nr, 1, file->hunk_alloc);
	hunk = &file->hunk[file->hunk_nr - 1];
	hunk->del_off = parser->del_off;
	hunk->add_off = parser->add_off;
	hunk->delta = parser->skip_delta + (ssize_t)parser->del_len -
		      (ssize_t)parser->add_len;
}

static void patch_push_line(struct patch *patch, char sign, const char *text,
			    size_t len)
{
	struct patch_file *file = &patch->file[patch->file_nr - 1];
	struct patch_hunk *hunk = &file->hunk[file->hunk_nr - 1];
	struct patch_line *line;

	ALLOC_GROW_BY(hunk->line, hunk->line_nr, 1, hunk->line_alloc);
	line = &hunk->line[hunk->line_nr - 1];
	line->sign = sign;
	line->text = text;
	line->len = len;
	if (sign == '+') {
		hunk->add_len++;
	} else if (sign == '-') {
		hunk->del_len++;
	} else if (sign == ' ') {
		hunk->del_len++;
		hunk->add_len++;
	}
}

struct merge_line {
	const char *text;
	size_t len;
};

static void patch_push_stashed_line(struct patch *patch,
				    struct merge_line *line)
{
	const char *text = line->text;

	if (text[0] == '+' || text[0] == '-' || text[0] == '\\')
		patch_push_line(patch, text[0], text + 1, line->len - 1);
	else
		BUG("bad line '%.*s'", (int)line->len, text);
}

static void patch_push_unstashed_line(struct patch *patch,
				      struct merge_line *line)
{
	const char *text = line->text;

	if (text[0] == '-') {
		patch->push_incomplete = 0;
	} else if (text[0] == '+') {
		patch_push_line(patch, ' ', text + 1, line->len - 1);
		patch->push_incomplete = 1;
	} else if (text[0] != '\\') {
		BUG("bad line '%.*s'", (int)line->len, text);
	} else if (patch->push_incomplete) {
		patch_push_line(patch, text[0], text + 1, line->len - 1);
		patch->push_incomplete = 0;
	}
}

static void diff_parser_release(struct diff_parser *parser)
{
	strbuf_release(&parser->old_path);
	strbuf_release(&parser->new_path);
}

static int diff_parser_advance_internal(const char **s, size_t *rem,
					size_t *len)
{
	const char *eol;

	*s += *len;
	*rem -= *len;
	if (!*rem)
		return 0;
	eol = memchr(*s, '\n', *rem);
	if (!eol)
		BUG("diff did not end with new line '%s'", *s);
	*len = eol - *s + 1;

	return 1;
}

static void diff_parser_parse_path(const char *path, size_t len,
				   struct strbuf *out)
{
	const char *end;

	if (path[0] == '"') {
		if (unquote_c_style(out, path, &end) || end - path != len)
			BUG("unable to unquote path '%.*s'", (int)len, path);
	} else {
		strbuf_add(out, path, len);
	}
}

static void diff_parser_parse_diff_header(struct diff_parser *p)
{
	const char *s = p->s, *path;
	size_t len = p->len;
	size_t rem = p->rem;

	strbuf_reset(&p->old_path);
	strbuf_reset(&p->new_path);
	p->old_mode = p->new_mode = NULL;
	p->old_mode_len = p->new_mode_len = 0;
	p->skip_delta = 0;
	p->del_pos = 0;

	while (diff_parser_advance_internal(&s, &rem, &len) &&
	       !starts_with(s, "@@ ") && !starts_with(s, "diff ")) {
		p->len += len;
		if (skip_prefix(s, "--- ", &path))
			diff_parser_parse_path(path, len - (path - s) - 1,
					       &p->old_path);
		else if (skip_prefix(s, "+++ ", &path))
			diff_parser_parse_path(path, len - (path - s) - 1,
					       &p->new_path);
		else if (skip_prefix(s, "old mode ", &p->old_mode))
			p->old_mode_len = len - (p->old_mode - s) - 1;
		else if (skip_prefix(s, "new mode ", &p->new_mode))
			p->new_mode_len = len - (p->new_mode - s) - 1;
		else if (skip_prefix(s, "deleted file mode ", &p->old_mode))
			p->old_mode_len = len - (p->old_mode - s) - 1;
	}

	if (!p->old_path.len || !p->new_path.len)
		BUG("unexpected end of diff");
	if (p->new_mode && !p->old_mode)
		BUG("new mode without old mode");
	if (p->old_mode && !p->new_mode && strcmp(p->new_path.buf, "/dev/null"))
		BUG("old mode without new mode");

	p->state = DIFF_STATE_DIFF_HEADER;
}

static int parse_range(const char **p, unsigned long *offset,
		       unsigned long *count)
{
	char *pend;

	*offset = strtoul(*p, &pend, 10);
	if (pend == *p)
		return -1;
	if (*pend != ',') {
		*count = 1;
		*p = pend;
		return 0;
	}
	*count = strtoul(pend + 1, (char **)p, 10);
	return *p == pend + 1 ? -1 : 0;
}

static void diff_parser_parse_hunk_header(struct diff_parser *p)
{
	const char *s = p->s;

	if (!skip_prefix(s, "@@ -", &s) ||
	    parse_range(&s, &p->del_off, &p->del_len) < 0 ||
	    !skip_prefix(s, " +", &s) ||
	    parse_range(&s, &p->add_off, &p->add_len) < 0 ||
	    !skip_prefix(s, " @@", &s))
		BUG("could not parse hunk header '%.*s'", (int)p->len, p->s);
	if (!p->del_len)
		p->del_off++;
	if (!p->add_len)
		p->add_off++;

	p->del_pos = p->del_off;
	p->state = DIFF_STATE_HUNK_HEADER;
}

static enum diff_parser_state diff_parser_advance(struct diff_parser *p)
{
	if (!diff_parser_advance_internal(&p->s, &p->rem, &p->len)) {
		p->state = DIFF_STATE_EOF;
	} else if (starts_with(p->s, "diff ")) {
		diff_parser_parse_diff_header(p);
	} else if (starts_with(p->s, "@@ ")) {
		diff_parser_parse_hunk_header(p);
	} else {
		switch (p->s[0]) {
		case '-':
			p->del_pos++;
			/* fallthrough */
		case '+':
		case '\\':
			p->state = DIFF_STATE_IN_HUNK;
			break;
		default:
			BUG("invalid diff line '%.*s'", (int)p->len, p->s);
		}
	}
	return p->state;
}

#define diff_parser_assert_state(parser, expected_state) \
	if ((parser)->state != (expected_state))         \
		BUG("unexpected parser state");


static void diff_parser_skip_hunk(struct diff_parser *parser)
{
	diff_parser_assert_state(parser, DIFF_STATE_HUNK_HEADER);
	parser->skip_delta +=
		(ssize_t)parser->del_len - (ssize_t)parser->add_len;
	do {
		diff_parser_advance(parser);
	} while (parser->state == DIFF_STATE_IN_HUNK);
}

static void diff_parser_skip_file(struct diff_parser *parser)
{
	diff_parser_assert_state(parser, DIFF_STATE_DIFF_HEADER);
	do {
		diff_parser_advance(parser);
	} while (parser->state != DIFF_STATE_DIFF_HEADER &&
		 parser->state != DIFF_STATE_EOF);
}

static void diff_parser_init(struct diff_parser *p, struct strbuf *buf)
{
	memset(p, 0, sizeof(*p));
	p->s = buf->buf;
	p->rem = buf->len;
	strbuf_init(&p->old_path, 0);
	strbuf_init(&p->new_path, 0);
	diff_parser_advance(p);
}

struct merge_line_array {
	struct merge_line *line;
	size_t alloc, nr, i;
};

struct merge_hunk {
	struct merge_line_array add, del;
	unsigned long del_off, del_len, add_off, add_len;
};

struct merge_hunk_array {
	struct merge_hunk *hunk;
	size_t alloc, nr;
};

static void merge_hunk_array_release(struct merge_hunk_array *hunks)
{
	size_t i;

	for (i = 0; i < hunks->nr; i++) {
		struct merge_hunk *hunk = &hunks->hunk[i];

		free(hunk->add.line);
		free(hunk->del.line);
	}

	free(hunks->hunk);
}

static void merge_line_array_init(struct merge_line_array *lines,
				  unsigned long len)
{
	size_t alloc = len + 1;
	ALLOC_ARRAY(lines->line, alloc);
	lines->alloc = alloc;
	lines->nr = lines->i = 0;
}

static struct merge_hunk *merge_hunk_push(struct merge_hunk_array *hunks,
					  struct diff_parser *parser)
{
	struct merge_hunk *hunk;
	struct merge_line_array *side, *last = NULL;

	ALLOC_GROW(hunks->hunk, hunks->nr + 1, hunks->alloc);
	hunk = &hunks->hunk[hunks->nr++];

	hunk->del_off = parser->del_off;
	hunk->del_len = parser->del_len;
	hunk->add_off = parser->add_off;
	hunk->add_len = parser->add_len;
	merge_line_array_init(&hunk->add, hunk->add_len);
	merge_line_array_init(&hunk->del, hunk->del_len);

	diff_parser_assert_state(parser, DIFF_STATE_HUNK_HEADER);
	while (diff_parser_advance(parser) == DIFF_STATE_IN_HUNK) {
		if (parser->s[0] == '+') {
			last = side = &hunk->add;
		} else if (parser->s[0] == '-') {
			last = side = &hunk->del;
		} else if (parser->s[0] == '\\') {
			if (!last)
				BUG("first line of hunk starts with '\'");
			side = last;
			last = NULL;
		} else {
			BUG("bad hunk line '%.*s'", (int)parser->len,
			    parser->s);
		}
		if (side->nr == side->alloc)
			BUG("bad hunk header");
		side->line[side->nr].text = parser->s;
		side->line[side->nr++].len = parser->len;
	}
	if (hunk->add_len > 0 && hunk->add.nr < hunk->add_len - 1)
		BUG("bad hunk: too few additions");
	if (hunk->del.nr > 0 && hunk->del.nr < hunk->del_len - 1)
		BUG("bad hunk: too few deletions");

	return hunk;
}

static int merge_hunk_pair(struct merge_hunk *s, struct merge_hunk *u,
			   struct patch *patch)
{
	while (s->del_off + s->del.i < u->del_off + u->del.i) {
		/*
		 * When the hunk was edited a context line was converted to a
		 * deletion - ignore as reversing it would give us back the
		 * context line that we already have.
		 */
		s->del.i++;
	}
	while (u->del_off + u->del.i < s->del_off + s->del.i) {
		/* Unstashed deletion */
		patch_push_unstashed_line(patch, &u->del.line[u->del.i++]);
	}
	if (u->del_off + u->del.i == s->del_off + s->del.i) {
		for (; s->del.i < s->del.nr && u->del.i < u->del.nr;
		     s->del.i++, u->del.i++) {
			struct merge_line *sl = &s->del.line[s->del.i];
			struct merge_line *ul = &u->del.line[u->del.i];

			if (sl->len == ul->len &&
			    !memcmp(sl->text, ul->text, sl->len))
				patch_push_stashed_line(patch, sl);
			else
				BUG("preimages do not match\n  %.*s  %.*s",
				    (int)sl->len, sl->text, (int)ul->len,
				    ul->text);
		}
		while (s->add.i < s->add.nr && u->add.i < u->add.nr) {
			struct merge_line *sl = &s->add.line[s->add.i];
			struct merge_line *ul = &u->add.line[u->add.i];

			if (sl->len == ul->len &&
			    !memcmp(sl->text, ul->text, sl->len)) {
				patch_push_stashed_line(patch, sl);
				s->add.i++;
				u->add.i++;
			} else {
				patch_push_unstashed_line(patch, ul);
				u->add.i++;
			}
		}
	}
	/* Check that all the stashed lines matched the working tree */
	if (s->del.i == s->del.nr && s->add.i < s->add.nr)
		return error(_("stashed line\n%.*sdoes not match working tree"),
			     (int)s->add.line[s->add.i].len,
			     s->add.line[s->add.i].text);
	/* If we've used up all the deletions in the working tree hunk
	 * then push any remaining additions. However if the last
	 * stashed hunk contained only deletions then it is not clear
	 * where the insertions from the stashed deletions should come
	 * in relation to the context lines from the insertions in the
	 * worktree.
	 */
	if (u->del.i == u->del.nr) {
		if (!s->add.nr)
			return error(_("ambiguous"));
		else
			for (; u->add.i < u->add.nr; u->add.i++)
				patch_push_unstashed_line(patch,
							  &u->add.line[u->add.i]);
	}
	if ((u->del.i < u->del.nr || u->add.i < u->add.nr) &&
	    (s->del.i < s->del.nr || s->add.i < s->add.nr))
		BUG("neither hunk has been exhausted");

	return 0;
}

static int merge_overlapping_hunks(struct diff_parser *s, struct diff_parser *u,
				   struct patch *patch)
{
	struct merge_hunk_array hss = { 0 }, hus = { 0 };
	struct merge_hunk *hs, *hu;
	int res = 0;

	patch_push_hunk(patch, u);
	hs = merge_hunk_push(&hss, s);
	hu = merge_hunk_push(&hus, u);
	while (1) {
		if (merge_hunk_pair(hs, hu, patch)) {
			res = -1;
			goto out;
		}
		if (u->state != DIFF_STATE_HUNK_HEADER ||
		    s->state != DIFF_STATE_HUNK_HEADER ||
		    s->del_off > u->del_off + u->del_len)
			break;

		if (hs->del.i == hs->del.nr && hs->add.i == hs->add.nr)
			hs = merge_hunk_push(&hss, s);
		if (hu->del.i == hu->del.nr && hu->add.i == hu->add.nr)
			hu = merge_hunk_push(&hus, u);
	}

	/* Check that all the stashed lines matched the working tree */
	if (hs->add.i < hs->add.nr) {
		error(_("stashed line\n%.*sdoes not match working tree"),
		      (int)hs->add.line[hs->add.i].len,
		      hs->add.line[hs->add.i].text);
		goto out;
	}
	/*
	 * If the stashed hunk contains only deletions and there are
	 * additions in the worktree that we have not processed yet
	 * then it is not clear where the insertions from the stashed
	 * deletions should come in relation to the context lines from
	 * the insertions in the worktree. We only check the last hunk
	 * as the backwards loop will pick up any other ambiguities.
	 */
	if (!hs->add.nr && hu->add.i < hu->add.nr) {
		error(_("don't know how to revert stashed deletions"));
		goto out;
	}
	/*
	 * We don't really need to push these unstashed lines but it
	 * makes checking that the patch is the same when we loop
	 * backwards over the hunks easier
	 */
	 for (; hu->add.i < hu->add.nr; hu->add.i++)
		 patch_push_unstashed_line(patch, &hu->add.line[hu->add.i]);

	 /* TODO check we get the same patch when we iterate backwards
	  * over hss and hus
	  */
out:
	merge_hunk_array_release(&hss);
	merge_hunk_array_release(&hus);

	return res;
}

static int merge_file(struct diff_parser *s, struct diff_parser *u,
		      struct patch *patch)
{
	patch_push_file(patch, u);
	diff_parser_advance(u);
	diff_parser_advance(s);
	while (u->state == DIFF_STATE_HUNK_HEADER &&
	       s->state == DIFF_STATE_HUNK_HEADER) {
		if (s->del_off + s->del_len < u->del_off) {
			warning("stashed changes, but no changes in working tree");
			diff_parser_skip_hunk(s);
		} else if (s->del_off > u->del_off + u->del_len) {
			/* unstashed hunk */
			diff_parser_skip_hunk(u);
		} else if (merge_overlapping_hunks(s, u, patch)) {
			return -1;
		}
	}
	while (u->state == DIFF_STATE_HUNK_HEADER)
		diff_parser_skip_hunk(u);
	if (s->state == DIFF_STATE_HUNK_HEADER)
		warning("stashed changes, but no changes in working tree");
	while (s->state == DIFF_STATE_HUNK_HEADER)
		diff_parser_skip_hunk(s);

	return 0;
}

static int stash_patch_remove(struct strbuf *stashed_changes)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf uncommitted_changes = STRBUF_INIT;
	struct diff_parser s;
	struct diff_parser u;
	struct patch patch = PATCH_INIT;
	int cmp, res = 0;
	FILE *out;

	diff_parser_init(&s, stashed_changes);
	if (get_uncommitted_changes(&uncommitted_changes))
		return error(_("cannot get uncommitted changes"));
	diff_parser_init(&u, &uncommitted_changes);

	diff_parser_assert_state(&s, DIFF_STATE_DIFF_HEADER);
	diff_parser_assert_state(&u, DIFF_STATE_DIFF_HEADER);

	do {
		cmp = strcmp(s.old_path.buf, "/dev/null") ?
			      strcmp(s.old_path.buf, u.old_path.buf) :
			      strcmp(s.new_path.buf, u.new_path.buf);
		if (cmp < 0) {
			warning("stashed changes in '%s' but not in working tree",
				s.old_path.buf);
			diff_parser_skip_file(&s);
		} else if (cmp) {
			diff_parser_skip_file(&u);
		} else if (merge_file(&s, &u, &patch)) {
			res = -1;
			goto out;
		}
	} while (s.state == DIFF_STATE_DIFF_HEADER &&
		 u.state == DIFF_STATE_DIFF_HEADER);

	if (s.state != DIFF_STATE_EOF && u.state != DIFF_STATE_EOF)
		BUG("neither diff has been exhausted");

	cp.in = -1;
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "apply", "-R", "--unidiff-zero", NULL);
	if (start_command(&cp))
		die_errno("apply failed to start");
	out = xfdopen(cp.in, "w");
	render_patch(&patch, out);
	if (fflush(out) | fclose(out))
		die_errno("cannot write to git apply");
	if (finish_command(&cp))
		die_errno("apply failed");
out:
	patch_release(&patch);
	diff_parser_release(&s);
	diff_parser_release(&u);
	strbuf_release(&uncommitted_changes);

	return res;
}

static int stash_patch(struct stash_info *info, const struct pathspec *ps,
		       struct strbuf *out_patch, int quiet)
{
	int ret = 0;
	struct child_process cp_read_tree = CHILD_PROCESS_INIT;
	struct child_process cp_diff_tree = CHILD_PROCESS_INIT;
	struct index_state istate = { NULL };
	char *old_index_env = NULL, *old_repo_index_file;

	remove_path(stash_index_path.buf);

	cp_read_tree.git_cmd = 1;
	argv_array_pushl(&cp_read_tree.args, "read-tree", "HEAD", NULL);
	argv_array_pushf(&cp_read_tree.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);
	if (run_command(&cp_read_tree)) {
		ret = -1;
		goto done;
	}

	/* Find out what the user wants. */
	old_repo_index_file = the_repository->index_file;
	the_repository->index_file = stash_index_path.buf;
	old_index_env = xstrdup_or_null(getenv(INDEX_ENVIRONMENT));
	setenv(INDEX_ENVIRONMENT, the_repository->index_file, 1);

	ret = run_add_interactive(NULL, "--patch=stash", ps);

	the_repository->index_file = old_repo_index_file;
	if (old_index_env && *old_index_env)
		setenv(INDEX_ENVIRONMENT, old_index_env, 1);
	else
		unsetenv(INDEX_ENVIRONMENT);
	FREE_AND_NULL(old_index_env);

	/* State of the working tree. */
	if (write_index_as_tree(&info->w_tree, &istate, stash_index_path.buf, 0,
				NULL)) {
		ret = -1;
		goto done;
	}

	cp_diff_tree.git_cmd = 1;
	argv_array_pushl(&cp_diff_tree.args, "diff-tree", "-p", "-U0",
			 "--inter-hunk-context=0", "-O/dev/null",
			 "--no-prefix", "HEAD", oid_to_hex(&info->w_tree), "--",
			 NULL);
	if (pipe_command(&cp_diff_tree, NULL, 0, out_patch, 0, NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (!out_patch->len) {
		if (!quiet)
			fprintf_ln(stderr, _("No changes selected"));
		ret = 1;
	}

done:
	discard_index(&istate);
	remove_path(stash_index_path.buf);
	return ret;
}

static int stash_working_tree(struct stash_info *info, const struct pathspec *ps)
{
	int ret = 0;
	struct rev_info rev;
	struct child_process cp_upd_index = CHILD_PROCESS_INIT;
	struct strbuf diff_output = STRBUF_INIT;
	struct index_state istate = { NULL };

	init_revisions(&rev, NULL);
	copy_pathspec(&rev.prune_data, ps);

	set_alternate_index_output(stash_index_path.buf);
	if (reset_tree(&info->i_tree, 0, 0)) {
		ret = -1;
		goto done;
	}
	set_alternate_index_output(NULL);

	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = add_diff_to_buf;
	rev.diffopt.format_callback_data = &diff_output;

	if (read_cache_preload(&rev.diffopt.pathspec) < 0) {
		ret = -1;
		goto done;
	}

	add_pending_object(&rev, parse_object(the_repository, &info->b_commit),
			   "");
	if (run_diff_index(&rev, 0)) {
		ret = -1;
		goto done;
	}

	cp_upd_index.git_cmd = 1;
	argv_array_pushl(&cp_upd_index.args, "update-index",
			 "--ignore-skip-worktree-entries",
			 "-z", "--add", "--remove", "--stdin", NULL);
	argv_array_pushf(&cp_upd_index.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);

	if (pipe_command(&cp_upd_index, diff_output.buf, diff_output.len,
			 NULL, 0, NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (write_index_as_tree(&info->w_tree, &istate, stash_index_path.buf, 0,
				NULL)) {
		ret = -1;
		goto done;
	}

done:
	discard_index(&istate);
	UNLEAK(rev);
	object_array_clear(&rev.pending);
	clear_pathspec(&rev.prune_data);
	strbuf_release(&diff_output);
	remove_path(stash_index_path.buf);
	return ret;
}

static int do_create_stash(const struct pathspec *ps, struct strbuf *stash_msg_buf,
			   int include_untracked, int patch_mode,
			   struct stash_info *info, struct strbuf *patch,
			   int quiet)
{
	int ret = 0;
	int flags = 0;
	int untracked_commit_option = 0;
	const char *head_short_sha1 = NULL;
	const char *branch_ref = NULL;
	const char *branch_name = "(no branch)";
	struct commit *head_commit = NULL;
	struct commit_list *parents = NULL;
	struct strbuf msg = STRBUF_INIT;
	struct strbuf commit_tree_label = STRBUF_INIT;
	struct strbuf untracked_files = STRBUF_INIT;

	prepare_fallback_ident("git stash", "git@stash");

	read_cache_preload(NULL);
	if (refresh_and_write_cache(REFRESH_QUIET, 0, 0) < 0) {
		ret = -1;
		goto done;
	}

	if (get_oid("HEAD", &info->b_commit)) {
		if (!quiet)
			fprintf_ln(stderr, _("You do not have "
					     "the initial commit yet"));
		ret = -1;
		goto done;
	} else {
		head_commit = lookup_commit(the_repository, &info->b_commit);
	}

	if (!check_changes(ps, include_untracked, &untracked_files)) {
		ret = 1;
		goto done;
	}

	branch_ref = resolve_ref_unsafe("HEAD", 0, NULL, &flags);
	if (flags & REF_ISSYMREF)
		branch_name = strrchr(branch_ref, '/') + 1;
	head_short_sha1 = find_unique_abbrev(&head_commit->object.oid,
					     DEFAULT_ABBREV);
	strbuf_addf(&msg, "%s: %s ", branch_name, head_short_sha1);
	pp_commit_easy(CMIT_FMT_ONELINE, head_commit, &msg);

	strbuf_addf(&commit_tree_label, "index on %s\n", msg.buf);
	commit_list_insert(head_commit, &parents);
	if (write_cache_as_tree(&info->i_tree, 0, NULL) ||
	    commit_tree(commit_tree_label.buf, commit_tree_label.len,
			&info->i_tree, parents, &info->i_commit, NULL, NULL)) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot save the current "
					     "index state"));
		ret = -1;
		goto done;
	}

	if (include_untracked) {
		if (save_untracked_files(info, &msg, untracked_files)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save "
						     "the untracked files"));
			ret = -1;
			goto done;
		}
		untracked_commit_option = 1;
	}
	if (patch_mode) {
		ret = stash_patch(info, ps, patch, quiet);
		if (ret < 0) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save the current "
						     "worktree state"));
			goto done;
		} else if (ret > 0) {
			goto done;
		}
	} else {
		if (stash_working_tree(info, ps)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save the current "
						     "worktree state"));
			ret = -1;
			goto done;
		}
	}

	if (!stash_msg_buf->len)
		strbuf_addf(stash_msg_buf, "WIP on %s", msg.buf);
	else
		strbuf_insertf(stash_msg_buf, 0, "On %s: ", branch_name);

	/*
	 * `parents` will be empty after calling `commit_tree()`, so there is
	 * no need to call `free_commit_list()`
	 */
	parents = NULL;
	if (untracked_commit_option)
		commit_list_insert(lookup_commit(the_repository,
						 &info->u_commit),
				   &parents);
	commit_list_insert(lookup_commit(the_repository, &info->i_commit),
			   &parents);
	commit_list_insert(head_commit, &parents);

	if (commit_tree(stash_msg_buf->buf, stash_msg_buf->len, &info->w_tree,
			parents, &info->w_commit, NULL, NULL)) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot record "
					     "working tree state"));
		ret = -1;
		goto done;
	}

done:
	strbuf_release(&commit_tree_label);
	strbuf_release(&msg);
	strbuf_release(&untracked_files);
	return ret;
}

static int create_stash(int argc, const char **argv, const char *prefix)
{
	int ret = 0;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	struct stash_info info;
	struct pathspec ps;

	/* Starting with argv[1], since argv[0] is "create" */
	strbuf_join_argv(&stash_msg_buf, argc - 1, ++argv, ' ');

	memset(&ps, 0, sizeof(ps));
	if (!check_changes_tracked_files(&ps))
		return 0;

	ret = do_create_stash(&ps, &stash_msg_buf, 0, 0, &info,
			      NULL, 0);
	if (!ret)
		printf_ln("%s", oid_to_hex(&info.w_commit));

	strbuf_release(&stash_msg_buf);
	return ret;
}

static int do_push_stash(const struct pathspec *ps, const char *stash_msg, int quiet,
			 int keep_index, int patch_mode, int include_untracked)
{
	int ret = 0;
	struct stash_info info;
	struct strbuf patch = STRBUF_INIT;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	struct strbuf untracked_files = STRBUF_INIT;

	if (patch_mode && keep_index == -1)
		keep_index = 1;

	if (patch_mode && include_untracked) {
		fprintf_ln(stderr, _("Can't use --patch and --include-untracked"
				     " or --all at the same time"));
		ret = -1;
		goto done;
	}

	read_cache_preload(NULL);
	if (!include_untracked && ps->nr) {
		int i;
		char *ps_matched = xcalloc(ps->nr, 1);

		for (i = 0; i < active_nr; i++)
			ce_path_match(&the_index, active_cache[i], ps,
				      ps_matched);

		if (report_path_error(ps_matched, ps)) {
			fprintf_ln(stderr, _("Did you forget to 'git add'?"));
			ret = -1;
			free(ps_matched);
			goto done;
		}
		free(ps_matched);
	}

	if (refresh_and_write_cache(REFRESH_QUIET, 0, 0)) {
		ret = -1;
		goto done;
	}

	if (!check_changes(ps, include_untracked, &untracked_files)) {
		if (!quiet)
			printf_ln(_("No local changes to save"));
		goto done;
	}

	if (!reflog_exists(ref_stash) && do_clear_stash()) {
		ret = -1;
		if (!quiet)
			fprintf_ln(stderr, _("Cannot initialize stash"));
		goto done;
	}

	if (stash_msg)
		strbuf_addstr(&stash_msg_buf, stash_msg);
	if (do_create_stash(ps, &stash_msg_buf, include_untracked, patch_mode,
			    &info, &patch, quiet)) {
		ret = -1;
		goto done;
	}

	if (do_store_stash(&info.w_commit, stash_msg_buf.buf, 1)) {
		ret = -1;
		if (!quiet)
			fprintf_ln(stderr, _("Cannot save the current status"));
		goto done;
	}

	if (!quiet)
		printf_ln(_("Saved working directory and index state %s"),
			  stash_msg_buf.buf);

	if (!patch_mode) {
		if (include_untracked && !ps->nr) {
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "clean", "--force",
					 "--quiet", "-d", NULL);
			if (include_untracked == INCLUDE_ALL_FILES)
				argv_array_push(&cp.args, "-x");
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}
		discard_cache();
		if (ps->nr) {
			struct child_process cp_add = CHILD_PROCESS_INIT;
			struct child_process cp_diff = CHILD_PROCESS_INIT;
			struct child_process cp_apply = CHILD_PROCESS_INIT;
			struct strbuf out = STRBUF_INIT;

			cp_add.git_cmd = 1;
			argv_array_push(&cp_add.args, "add");
			if (!include_untracked)
				argv_array_push(&cp_add.args, "-u");
			if (include_untracked == INCLUDE_ALL_FILES)
				argv_array_push(&cp_add.args, "--force");
			argv_array_push(&cp_add.args, "--");
			add_pathspecs(&cp_add.args, ps);
			if (run_command(&cp_add)) {
				ret = -1;
				goto done;
			}

			cp_diff.git_cmd = 1;
			argv_array_pushl(&cp_diff.args, "diff-index", "-p",
					 "--cached", "--binary", "HEAD", "--",
					 NULL);
			add_pathspecs(&cp_diff.args, ps);
			if (pipe_command(&cp_diff, NULL, 0, &out, 0, NULL, 0)) {
				ret = -1;
				goto done;
			}

			cp_apply.git_cmd = 1;
			argv_array_pushl(&cp_apply.args, "apply", "--index",
					 "-R", NULL);
			if (pipe_command(&cp_apply, out.buf, out.len, NULL, 0,
					 NULL, 0)) {
				ret = -1;
				goto done;
			}
		} else {
			struct child_process cp = CHILD_PROCESS_INIT;
			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "reset", "--hard", "-q",
					 "--no-recurse-submodules", NULL);
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}

		if (keep_index == 1 && !is_null_oid(&info.i_tree)) {
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "checkout", "--no-overlay",
					 oid_to_hex(&info.i_tree), "--", NULL);
			if (!ps->nr)
				argv_array_push(&cp.args, ":/");
			else
				add_pathspecs(&cp.args, ps);
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}
		goto done;
	} else {
		if (stash_patch_remove(&patch)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot remove "
						     "worktree changes"));
			ret = -1;
			goto done;
		}

		if (keep_index < 1) {
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "reset", "-q", "--", NULL);
			add_pathspecs(&cp.args, ps);
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}
		goto done;
	}

done:
	strbuf_release(&stash_msg_buf);
	strbuf_release(&patch);
	return ret;
}

static int push_stash(int argc, const char **argv, const char *prefix,
		      int push_assumed)
{
	int force_assume = 0;
	int keep_index = -1;
	int patch_mode = 0;
	int include_untracked = 0;
	int quiet = 0;
	int pathspec_file_nul = 0;
	const char *stash_msg = NULL;
	const char *pathspec_from_file = NULL;
	struct pathspec ps;
	struct option options[] = {
		OPT_BOOL('k', "keep-index", &keep_index,
			 N_("keep index")),
		OPT_BOOL('p', "patch", &patch_mode,
			 N_("stash in patch mode")),
		OPT__QUIET(&quiet, N_("quiet mode")),
		OPT_BOOL('u', "include-untracked", &include_untracked,
			 N_("include untracked files in stash")),
		OPT_SET_INT('a', "all", &include_untracked,
			    N_("include ignore files"), 2),
		OPT_STRING('m', "message", &stash_msg, N_("message"),
			   N_("stash message")),
		OPT_PATHSPEC_FROM_FILE(&pathspec_from_file),
		OPT_PATHSPEC_FILE_NUL(&pathspec_file_nul),
		OPT_END()
	};

	if (argc) {
		force_assume = !strcmp(argv[0], "-p");
		argc = parse_options(argc, argv, prefix, options,
				     git_stash_push_usage,
				     PARSE_OPT_KEEP_DASHDASH);
	}

	if (argc) {
		if (!strcmp(argv[0], "--")) {
			argc--;
			argv++;
		} else if (push_assumed && !force_assume) {
			die("subcommand wasn't specified; 'push' can't be assumed due to unexpected token '%s'",
			    argv[0]);
		}
	}

	parse_pathspec(&ps, 0, PATHSPEC_PREFER_FULL | PATHSPEC_PREFIX_ORIGIN,
		       prefix, argv);

	if (pathspec_from_file) {
		if (patch_mode)
			die(_("--pathspec-from-file is incompatible with --patch"));

		if (ps.nr)
			die(_("--pathspec-from-file is incompatible with pathspec arguments"));

		parse_pathspec_file(&ps, 0,
				    PATHSPEC_PREFER_FULL | PATHSPEC_PREFIX_ORIGIN,
				    prefix, pathspec_from_file, pathspec_file_nul);
	} else if (pathspec_file_nul) {
		die(_("--pathspec-file-nul requires --pathspec-from-file"));
	}

	return do_push_stash(&ps, stash_msg, quiet, keep_index, patch_mode,
			     include_untracked);
}

static int save_stash(int argc, const char **argv, const char *prefix)
{
	int keep_index = -1;
	int patch_mode = 0;
	int include_untracked = 0;
	int quiet = 0;
	int ret = 0;
	const char *stash_msg = NULL;
	struct pathspec ps;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	struct option options[] = {
		OPT_BOOL('k', "keep-index", &keep_index,
			 N_("keep index")),
		OPT_BOOL('p', "patch", &patch_mode,
			 N_("stash in patch mode")),
		OPT__QUIET(&quiet, N_("quiet mode")),
		OPT_BOOL('u', "include-untracked", &include_untracked,
			 N_("include untracked files in stash")),
		OPT_SET_INT('a', "all", &include_untracked,
			    N_("include ignore files"), 2),
		OPT_STRING('m', "message", &stash_msg, "message",
			   N_("stash message")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_save_usage,
			     PARSE_OPT_KEEP_DASHDASH);

	if (argc)
		stash_msg = strbuf_join_argv(&stash_msg_buf, argc, argv, ' ');

	memset(&ps, 0, sizeof(ps));
	ret = do_push_stash(&ps, stash_msg, quiet, keep_index,
			    patch_mode, include_untracked);

	strbuf_release(&stash_msg_buf);
	return ret;
}

int cmd_stash(int argc, const char **argv, const char *prefix)
{
	pid_t pid = getpid();
	const char *index_file;
	struct argv_array args = ARGV_ARRAY_INIT;

	struct option options[] = {
		OPT_END()
	};

	git_config(git_stash_config, NULL);

	if (use_legacy_stash ||
	    !git_env_bool("GIT_TEST_STASH_USE_BUILTIN", -1))
		warning(_("the stash.useBuiltin support has been removed!\n"
			  "See its entry in 'git help config' for details."));

	argc = parse_options(argc, argv, prefix, options, git_stash_usage,
			     PARSE_OPT_KEEP_UNKNOWN | PARSE_OPT_KEEP_DASHDASH);

	index_file = get_index_file();
	strbuf_addf(&stash_index_path, "%s.stash.%" PRIuMAX, index_file,
		    (uintmax_t)pid);

	if (!argc)
		return !!push_stash(0, NULL, prefix, 0);
	else if (!strcmp(argv[0], "apply"))
		return !!apply_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "clear"))
		return !!clear_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "drop"))
		return !!drop_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "pop"))
		return !!pop_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "branch"))
		return !!branch_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "list"))
		return !!list_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "show"))
		return !!show_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "store"))
		return !!store_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "create"))
		return !!create_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "push"))
		return !!push_stash(argc, argv, prefix, 0);
	else if (!strcmp(argv[0], "save"))
		return !!save_stash(argc, argv, prefix);
	else if (*argv[0] != '-')
		usage_msg_opt(xstrfmt(_("unknown subcommand: %s"), argv[0]),
			      git_stash_usage, options);

	/* Assume 'stash push' */
	argv_array_push(&args, "push");
	argv_array_pushv(&args, argv);
	return !!push_stash(args.argc, args.argv, prefix, 1);
}

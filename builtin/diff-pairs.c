#include "builtin.h"
#include "commit.h"
#include "config.h"
#include "diff.h"
#include "diffcore.h"
#include "gettext.h"
#include "hex.h"
#include "object.h"
#include "parse-options.h"
#include "revision.h"
#include "strbuf.h"

static unsigned parse_mode_or_die(const char *mode, const char **endp)
{
	uint16_t ret;

	*endp = parse_mode(mode, &ret);
	if (!*endp)
		die(_("unable to parse mode: %s"), mode);
	return ret;
}

static void parse_oid_or_die(const char *p, struct object_id *oid,
			     const char **endp, const struct git_hash_algo *algop)
{
	if (parse_oid_hex_algop(p, oid, endp, algop) || *(*endp)++ != ' ')
		die(_("unable to parse object id: %s"), p);
}

static void flush_diff_queue(struct diff_options *options)
{
	/*
	 * If rename detection is not requested, use rename information from the
	 * raw diff formatted input. Setting found_follow ensures diffcore_std()
	 * does not mess with rename information already present in queued
	 * filepairs.
	 */
	if (!options->detect_rename)
		options->found_follow = 1;
	diffcore_std(options);
	diff_flush(options);
}

int cmd_diff_pairs(int argc, const char **argv, const char *prefix,
		   struct repository *repo)
{
	struct strbuf path_dst = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	struct strbuf meta = STRBUF_INIT;
	struct rev_info revs;
	int ret;

	const char * const usage[] = {
		N_("git diff-pairs -z [<diff-options>]"),
		NULL
	};
	struct option options[] = {
		OPT_END()
	};
	struct option *parseopts = add_diff_options(options, &revs.diffopt);

	show_usage_with_options_if_asked(argc, argv, usage, parseopts);

	repo_init_revisions(repo, &revs, prefix);
	repo_config(repo, git_diff_basic_config, NULL);
	revs.diffopt.no_free = 1;
	revs.disable_stdin = 1;
	revs.abbrev = 0;
	revs.diff = 1;

	if (setup_revisions(argc, argv, &revs, NULL) > 1)
		usage_with_options(usage, parseopts);

	/*
	 * With the -z option, both command input and raw output are
	 * NUL-delimited (this mode does not effect patch output). At present
	 * only NUL-delimited raw diff formatted input is supported.
	 */
	if (revs.diffopt.line_termination) {
		error(_("working without -z is not supported"));
		usage_with_options(usage, parseopts);
	}

	if (revs.prune_data.nr) {
		error(_("pathspec arguments not supported"));
		usage_with_options(usage, parseopts);
	}

	if (revs.pending.nr || revs.max_count != -1 ||
	    revs.min_age != (timestamp_t)-1 ||
	    revs.max_age != (timestamp_t)-1) {
		error(_("revision arguments not allowed"));
		usage_with_options(usage, parseopts);
	}

	if (!revs.diffopt.output_format)
		revs.diffopt.output_format = DIFF_FORMAT_PATCH;

	while (1) {
		struct object_id oid_a, oid_b;
		struct diff_filepair *pair;
		unsigned mode_a, mode_b;
		const char *p;
		char status;

		if (strbuf_getline_nul(&meta, stdin) == EOF)
			break;

		p = meta.buf;
		if (!*p) {
			flush_diff_queue(&revs.diffopt);
			/*
			 * When the diff queue is explicitly flushed, append a
			 * NUL byte to separate batches of diffs.
			 */
			fputc('\0', revs.diffopt.file);
			fflush(revs.diffopt.file);
			continue;
		}

		if (*p != ':')
			die(_("invalid raw diff input"));
		p++;

		mode_a = parse_mode_or_die(p, &p);
		mode_b = parse_mode_or_die(p, &p);

		if (S_ISDIR(mode_a) || S_ISDIR(mode_b))
			die(_("tree objects not supported"));

		parse_oid_or_die(p, &oid_a, &p, repo->hash_algo);
		parse_oid_or_die(p, &oid_b, &p, repo->hash_algo);

		status = *p++;

		if (strbuf_getline_nul(&path, stdin) == EOF)
			die(_("got EOF while reading path"));

		switch (status) {
		case DIFF_STATUS_ADDED:
			pair = diff_queue_addremove(&diff_queued_diff,
						    &revs.diffopt, '+', mode_b,
						    &oid_b, 1, path.buf, 0);
			if (pair)
				pair->status = status;
			break;

		case DIFF_STATUS_DELETED:
			pair = diff_queue_addremove(&diff_queued_diff,
						    &revs.diffopt, '-', mode_a,
						    &oid_a, 1, path.buf, 0);
			if (pair)
				pair->status = status;
			break;

		case DIFF_STATUS_TYPE_CHANGED:
		case DIFF_STATUS_MODIFIED:
			pair = diff_queue_change(&diff_queued_diff, &revs.diffopt,
						 mode_a, mode_b, &oid_a, &oid_b,
						 1, 1, path.buf, 0, 0);
			if (pair)
				pair->status = status;
			break;

		case DIFF_STATUS_RENAMED:
		case DIFF_STATUS_COPIED:
			{
				struct diff_filespec *a, *b;
				unsigned int score;

				if (strbuf_getline_nul(&path_dst, stdin) == EOF)
					die(_("got EOF while reading destination path"));

				a = alloc_filespec(path.buf);
				b = alloc_filespec(path_dst.buf);
				fill_filespec(a, &oid_a, 1, mode_a);
				fill_filespec(b, &oid_b, 1, mode_b);

				pair = diff_queue(&diff_queued_diff, a, b);

				if (strtoul_ui(p, 10, &score))
					die(_("unable to parse rename/copy score: %s"), p);

				pair->score = score * MAX_SCORE / 100;
				pair->status = status;
				pair->renamed_pair = 1;
			}
			break;

		default:
			die(_("unknown diff status: %c"), status);
		}
	}

	revs.diffopt.no_free = 0;
	flush_diff_queue(&revs.diffopt);
	ret = diff_result_code(&revs);

	strbuf_release(&path_dst);
	strbuf_release(&path);
	strbuf_release(&meta);
	release_revisions(&revs);
	FREE_AND_NULL(parseopts);

	return ret;
}

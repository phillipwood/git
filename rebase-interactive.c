#include "cache.h"
#include "commit.h"
#include "sequencer.h"
#include "rebase-interactive.h"
#include "strbuf.h"
#include "commit-slab.h"
#include "config.h"

enum missing_commit_check_level {
	MISSING_COMMIT_CHECK_IGNORE = 0,
	MISSING_COMMIT_CHECK_WARN,
	MISSING_COMMIT_CHECK_ERROR
};

static enum missing_commit_check_level get_missing_commit_check_level(void)
{
	const char *value;

	if (git_config_get_value("rebase.missingcommitscheck", &value) ||
			!strcasecmp("ignore", value))
		return MISSING_COMMIT_CHECK_IGNORE;
	if (!strcasecmp("warn", value))
		return MISSING_COMMIT_CHECK_WARN;
	if (!strcasecmp("error", value))
		return MISSING_COMMIT_CHECK_ERROR;
	warning(_("unrecognized setting %s for option "
		  "rebase.missingCommitsCheck. Ignoring."), value);
	return MISSING_COMMIT_CHECK_IGNORE;
}

void append_todo_help(unsigned edit_todo, unsigned keep_empty,
		      struct strbuf *buf)
{
	const char *msg = _("\nCommands:\n"
"p, pick <commit> = use commit\n"
"r, reword <commit> = use commit, but edit the commit message\n"
"e, edit <commit> = use commit, but stop for amending\n"
"s, squash <commit> = use commit, but meld into previous commit\n"
"f, fixup <commit> = like \"squash\", but discard this commit's log message\n"
"x, exec <command> = run command (the rest of the line) using shell\n"
"d, drop <commit> = remove commit\n"
"l, label <label> = label current HEAD with a name\n"
"t, reset <label> = reset HEAD to a label\n"
"m, merge [-C <commit> | -c <commit>] <label> [# <oneline>]\n"
".       create a merge commit using the original merge commit's\n"
".       message (or the oneline, if no original merge commit was\n"
".       specified). Use -c <commit> to reword the commit message.\n"
"\n"
"These lines can be re-ordered; they are executed from top to bottom.\n");

	strbuf_add_commented_lines(buf, msg, strlen(msg));

	if (get_missing_commit_check_level() == MISSING_COMMIT_CHECK_ERROR)
		msg = _("\nDo not remove any line. Use 'drop' "
			 "explicitly to remove a commit.\n");
	else
		msg = _("\nIf you remove a line here "
			 "THAT COMMIT WILL BE LOST.\n");

	strbuf_add_commented_lines(buf, msg, strlen(msg));

	if (edit_todo)
		msg = _("\nYou are editing the todo file "
			"of an ongoing interactive rebase.\n"
			"To continue rebase after editing, run:\n"
			"    git rebase --continue\n\n");
	else
		msg = _("\nHowever, if you remove everything, "
			"the rebase will be aborted.\n\n");

	strbuf_add_commented_lines(buf, msg, strlen(msg));

	if (!keep_empty) {
		msg = _("Note that empty commits are commented out");
		strbuf_add_commented_lines(buf, msg, strlen(msg));
	}
}

int edit_todo_list(unsigned flags)
{
	struct strbuf buf = STRBUF_INIT;
	const char *todo_file = rebase_path_todo();

	if (strbuf_read_file(&buf, todo_file, 0) < 0)
		return error_errno(_("could not read '%s'."), todo_file);

	strbuf_stripspace(&buf, 1);
	if (write_message(buf.buf, buf.len, todo_file, 0)) {
		strbuf_release(&buf);
		return -1;
	}

	strbuf_release(&buf);

	transform_todo_file(flags | TODO_LIST_SHORTEN_IDS);

	if (strbuf_read_file(&buf, todo_file, 0) < 0)
		return error_errno(_("could not read '%s'."), todo_file);

	append_todo_help(1, 0, &buf);
	if (write_message(buf.buf, buf.len, todo_file, 0)) {
		strbuf_release(&buf);
		return -1;
	}

	strbuf_release(&buf);

	if (launch_sequence_editor(todo_file, NULL, NULL))
		return -1;

	transform_todo_file(flags & ~(TODO_LIST_SHORTEN_IDS));

	return 0;
}

define_commit_slab(commit_seen, unsigned char);
/*
 * Check if the user dropped some commits by mistake
 * Behaviour determined by rebase.missingCommitsCheck.
 * Check if there is an unrecognized command or a
 * bad SHA-1 in a command.
 */
int todo_list_check(struct todo_list *old_todo, struct todo_list *new_todo)
{
	enum missing_commit_check_level check_level = get_missing_commit_check_level();
	struct strbuf missing = STRBUF_INIT;
	int advise_to_edit_todo = 0, res = 0, i;
	struct commit_seen commit_seen;

	init_commit_seen(&commit_seen);

	res = todo_list_parse_insn_buffer(old_todo->buf.buf, old_todo);
	if (!res)
		res = todo_list_parse_insn_buffer(new_todo->buf.buf, new_todo);
	if (res) {
		advise_to_edit_todo = res;
		goto leave_check;
	}

	if (check_level == MISSING_COMMIT_CHECK_IGNORE)
		goto leave_check;

	/* Mark the commits in git-rebase-todo as seen */
	for (i = 0; i < new_todo->nr; i++) {
		struct commit *commit = new_todo->items[i].commit;
		if (commit)
			*commit_seen_at(&commit_seen, commit) = 1;
	}

	/* Find commits in git-rebase-todo.backup yet unseen */
	for (i = old_todo->nr - 1; i >= 0; i--) {
		struct todo_item *item = old_todo->items + i;
		struct commit *commit = item->commit;
		if (commit && !*commit_seen_at(&commit_seen, commit)) {
			strbuf_addf(&missing, " - %s %.*s\n",
				    find_unique_abbrev(&commit->object.oid, DEFAULT_ABBREV),
				    item->arg_len, item->arg);
			*commit_seen_at(&commit_seen, commit) = 1;
		}
	}

	/* Warn about missing commits */
	if (!missing.len)
		goto leave_check;

	if (check_level == MISSING_COMMIT_CHECK_ERROR)
		advise_to_edit_todo = res = 1;

	fprintf(stderr,
		_("Warning: some commits may have been dropped accidentally.\n"
		"Dropped commits (newer to older):\n"));

	/* Make the list user-friendly and display */
	fputs(missing.buf, stderr);
	strbuf_release(&missing);

	fprintf(stderr, _("To avoid this message, use \"drop\" to "
		"explicitly remove a commit.\n\n"
		"Use 'git config rebase.missingCommitsCheck' to change "
		"the level of warnings.\n"
		"The possible behaviours are: ignore, warn, error.\n\n"));

leave_check:
	clear_commit_seen(&commit_seen);

	if (advise_to_edit_todo)
		fprintf(stderr,
			_("You can fix this with 'git rebase --edit-todo' "
			  "and then run 'git rebase --continue'.\n"
			  "Or you can abort the rebase with 'git rebase"
			  " --abort'.\n"));

	return res;
}

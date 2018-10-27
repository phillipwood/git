#ifndef REBASE_INTERACTIVE_H
#define REBASE_INTERACTIVE_H

void append_todo_help(unsigned keep_empty, int command_count,
		      const char *shortrevisions, const char *shortonto,
		      struct strbuf *buf);
int edit_todo_list(unsigned flags);
int todo_list_check(struct todo_list *old_todo, struct todo_list *new_todo);

#endif

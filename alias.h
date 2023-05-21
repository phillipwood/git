#ifndef ALIAS_H
#define ALIAS_H

struct strbuf;
struct string_list;

char *alias_lookup(const char *alias);
/* Quote argv so buf can be parsed by split_cmdline() */
void quote_cmdline(struct strbuf *buf, const char **argv);
/*
 * Quote a single commandline argument so it can be parsed by
 * split_cmdline(). Adds a space before the quoted argument if buf
 * does not end with whitespace.
 */
void quote_cmdline_arg(struct strbuf *buf, const char *arg);
int split_cmdline(char *cmdline, const char ***argv);
/* Takes a negative value returned by split_cmdline */
const char *split_cmdline_strerror(int cmdline_errno);
void list_aliases(struct string_list *list);

#endif

#ifndef TRACE2_H
#define TRACE2_H

struct child_process { int stub; };
struct repository;
struct json_writer { int stub; };

void trace2_region_enter_fl(const char *file, int line, const char *category,
			    const char *label, const struct repository *repo, ...);

#define trace2_region_enter(category, label, repo) \
	trace2_region_enter_fl(__FILE__, __LINE__, (category), (label), (repo))

void trace2_region_leave_fl(const char *file, int line, const char *category,
			    const char *label, const struct repository *repo, ...);

#define trace2_region_leave(category, label, repo) \
	trace2_region_leave_fl(__FILE__, __LINE__, (category), (label), (repo))

void trace2_data_string_fl(const char *file, int line, const char *category,
			   const struct repository *repo, const char *key,
			   const char *value);

#define trace2_data_string(category, repo, key, value)                       \
	trace2_data_string_fl(__FILE__, __LINE__, (category), (repo), (key), \
			      (value))

void trace2_cmd_ancestry_fl(const char *file, int line, const char **parent_names);

#define trace2_cmd_ancestry(v) trace2_cmd_ancestry_fl(__FILE__, __LINE__, (v))

void trace2_cmd_error_va_fl(const char *file, int line, const char *fmt,
			    va_list ap);

#define trace2_cmd_error_va(fmt, ap) \
	trace2_cmd_error_va_fl(__FILE__, __LINE__, (fmt), (ap))


void trace2_cmd_name_fl(const char *file, int line, const char *name);

#define trace2_cmd_name(v) trace2_cmd_name_fl(__FILE__, __LINE__, (v))

void trace2_thread_start_fl(const char *file, int line,
			    const char *thread_base_name);

#define trace2_thread_start(thread_base_name) \
	trace2_thread_start_fl(__FILE__, __LINE__, (thread_base_name))

void trace2_thread_exit_fl(const char *file, int line);

#define trace2_thread_exit() trace2_thread_exit_fl(__FILE__, __LINE__)

void trace2_data_intmax_fl(const char *file, int line, const char *category,
			   const struct repository *repo, const char *key,
			   intmax_t value);

#define trace2_data_intmax(category, repo, key, value)                       \
	trace2_data_intmax_fl(__FILE__, __LINE__, (category), (repo), (key), \
			      (value))

enum trace2_process_info_reason {
	TRACE2_PROCESS_INFO_STARTUP,
	TRACE2_PROCESS_INFO_EXIT,
};
int trace2_is_enabled(void);
void trace2_collect_process_info(enum trace2_process_info_reason reason);

#endif /* TRACE2_H */

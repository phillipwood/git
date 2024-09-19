#include "test-lib.h"
#include "oidset.h"
#include "lib-oid.h"
#include "hex.h"
#include "strbuf.h"

static const char *const hex_input[] = { "00", "11", "22", "33", "aa", "cc" };

static void strbuf_test_data_path(struct strbuf *buf, int hash_algo)
{
	strbuf_getcwd(buf);
	strbuf_strip_suffix(buf, "/unit-tests/bin");
	strbuf_addf(buf, "/unit-tests/t-oidset/%s",
		    hash_algo == GIT_HASH_SHA1 ? "sha1-oids" : "sha256-oids");
}

static void setup(void (*f)(struct oidset *st))
{
	struct oidset st = OIDSET_INIT;
	struct object_id oid;
	int ret = 0;

	if (!check_int(oidset_size(&st), ==, 0)) {
		test_skip_all("OIDSET_INIT is broken");
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(hex_input); i++) {
		if ((ret = get_oid_arbitrary_hex(hex_input[i], &oid)))
			break;
		if (!check_int((ret = oidset_insert(&st, &oid)), ==, 0))
			break;
	}

	if (!ret && check_int(oidset_size(&st), ==, ARRAY_SIZE(hex_input)))
		f(&st);

	oidset_clear(&st);
}

static void t_contains(struct oidset *st)
{
	struct object_id oid;

	for (size_t i = 0; i < ARRAY_SIZE(hex_input); i++) {
		if (!get_oid_arbitrary_hex(hex_input[i], &oid)) {
			if (!check_int(oidset_contains(st, &oid), ==, 1))
				test_msg("oid: %s", oid_to_hex(&oid));
		}
	}

	if (!get_oid_arbitrary_hex("55", &oid))
		check_int(oidset_contains(st, &oid), ==, 0);
}

static void t_insert_dup(struct oidset *st)
{
	struct object_id oid;

	if (!get_oid_arbitrary_hex("11", &oid))
		check_int(oidset_insert(st, &oid), ==, 1);

	if (!get_oid_arbitrary_hex("aa", &oid))
		check_int(oidset_insert(st, &oid), ==, 1);

	check_int(oidset_size(st), ==, ARRAY_SIZE(hex_input));
}

static void t_insert_from_set(struct oidset *st_src)
{
	struct oidset st_dest = OIDSET_INIT;
	struct oidset_iter iter_src, iter_dest;
	struct object_id *oid_src, *oid_dest;
	struct object_id oid;
	size_t count = 0;

	oidset_insert_from_set(&st_dest, st_src);
	check_int(oidset_size(st_src), ==, ARRAY_SIZE(hex_input));
	check_int(oidset_size(&st_dest), ==, oidset_size(st_src));

	oidset_iter_init(st_src, &iter_src);
	oidset_iter_init(&st_dest, &iter_dest);

	/* check that oidset_insert_from_set() makes a copy of the object_ids */
	while ((oid_src = oidset_iter_next(&iter_src)) &&
	       (oid_dest = oidset_iter_next(&iter_dest))) {
		check(oid_src != oid_dest);
		count++;
	}
	check_int(count, ==, ARRAY_SIZE(hex_input));

	for (size_t i = 0; i < ARRAY_SIZE(hex_input); i++) {
		if (!get_oid_arbitrary_hex(hex_input[i], &oid)) {
			if (!check_int(oidset_contains(&st_dest, &oid), ==, 1))
				test_msg("oid: %s", oid_to_hex(&oid));
		}
	}

	if (!get_oid_arbitrary_hex("55", &oid))
		check_int(oidset_contains(&st_dest, &oid), ==, 0);
	oidset_clear(&st_dest);
}

static void t_remove(struct oidset *st)
{
	struct object_id oid;

	if (!get_oid_arbitrary_hex("55", &oid)) {
		check_int(oidset_remove(st, &oid), ==, 0);
		check_int(oidset_size(st), ==, ARRAY_SIZE(hex_input));
	}

	if (!get_oid_arbitrary_hex("22", &oid)) {
		check_int(oidset_remove(st, &oid), ==, 1);
		check_int(oidset_size(st), ==, ARRAY_SIZE(hex_input) - 1);
		check_int(oidset_contains(st, &oid), ==, 0);
	}

	if (!get_oid_arbitrary_hex("cc", &oid)) {
		check_int(oidset_remove(st, &oid), ==, 1);
		check_int(oidset_size(st), ==, ARRAY_SIZE(hex_input) - 2);
		check_int(oidset_contains(st, &oid), ==, 0);
	}

	if (!get_oid_arbitrary_hex("00", &oid))
	{
		/* remove a value inserted more than once */
		check_int(oidset_insert(st, &oid), ==, 1);
		check_int(oidset_remove(st, &oid), ==, 1);
		check_int(oidset_size(st), ==, ARRAY_SIZE(hex_input) - 3);
		check_int(oidset_contains(st, &oid), ==, 0);
	}

	if (!get_oid_arbitrary_hex("22", &oid))
		check_int(oidset_remove(st, &oid), ==, 0);
}

static int input_contains(struct object_id *oid, char *seen)
{
	for (size_t i = 0; i < ARRAY_SIZE(hex_input); i++) {
		struct object_id oid_input;
		if (get_oid_arbitrary_hex(hex_input[i], &oid_input))
			return -1;
		if (oideq(&oid_input, oid)) {
			if (seen[i])
				return 2;
			seen[i] = 1;
			return 0;
		}
	}
	return 1;
}

static void t_iterate(struct oidset *st)
{
	struct oidset_iter iter;
	struct object_id *oid;
	char seen[ARRAY_SIZE(hex_input)] = { 0 };
	int count = 0;

	oidset_iter_init(st, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		int ret;
		if (!check_int((ret = input_contains(oid, seen)), ==, 0)) {
			switch (ret) {
			case -1:
				break; /* handled by get_oid_arbitrary_hex() */
			case 1:
				test_msg("obtained object_id was not given in the input\n"
					 "  object_id: %s", oid_to_hex(oid));
				break;
			case 2:
				test_msg("duplicate object_id detected\n"
					 "  object_id: %s", oid_to_hex(oid));
				break;
			}
		} else {
			count++;
		}
	}
	check_int(count, ==, ARRAY_SIZE(hex_input));
	check_int(oidset_size(st), ==, ARRAY_SIZE(hex_input));
}

static void t_parse_file(void)
{
	struct strbuf path = STRBUF_INIT;
	struct oidset st = OIDSET_INIT;
	struct object_id oid;
	int hash_algo = init_hash_algo();

	if (!check_int(hash_algo, !=, GIT_HASH_UNKNOWN))
		return;

	strbuf_test_data_path(&path, hash_algo);
	oidset_parse_file(&st, path.buf, &hash_algos[hash_algo]);
	check_int(oidset_size(&st), ==, 6);

	if (!get_oid_arbitrary_hex("00", &oid))
		check_int(oidset_contains(&st, &oid), ==, 1);
	if (!get_oid_arbitrary_hex("44", &oid))
		check_int(oidset_contains(&st, &oid), ==, 1);
	if (!get_oid_arbitrary_hex("cc", &oid))
		check_int(oidset_contains(&st, &oid), ==, 1);

	if (!get_oid_arbitrary_hex("11", &oid))
		check_int(oidset_contains(&st, &oid), ==, 0);

	oidset_clear(&st);
	strbuf_release(&path);
}

int cmd_main(int argc UNUSED, const char **argv UNUSED)
{
	TEST(setup(t_contains), "contains works");
	TEST(setup(t_insert_dup), "insert an already inserted value works");
	TEST(setup(t_insert_from_set), "insert from one set to another works");
	TEST(setup(t_remove), "remove works");
	TEST(setup(t_iterate), "iteration works");
	TEST(t_parse_file(), "parsing from file works");
	return test_done();
}

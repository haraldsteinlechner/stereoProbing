#include "redblue_args.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static struct redblue_args
parse_ok(const char *what, int argc, char **argv)
{
	struct redblue_args args;
	char error[256] = "";
	if (!redblue_parse_args(argc, argv, &args, error, sizeof(error))) {
		fprintf(stderr, "FAIL %s: rejected: %s\n", what, error);
		failures++;
	}
	return args;
}

static void
parse_fails(const char *what, int argc, char **argv)
{
	struct redblue_args args;
	char error[256] = "";
	if (redblue_parse_args(argc, argv, &args, error, sizeof(error))) {
		fprintf(stderr, "FAIL %s: accepted\n", what);
		failures++;
	} else if (error[0] == '\0') {
		fprintf(stderr, "FAIL %s: rejected without message\n", what);
		failures++;
	}
}

static void
check(bool ok, const char *what)
{
	if (!ok) {
		fprintf(stderr, "FAIL %s\n", what);
		failures++;
	}
}

int
main(void)
{
	{
		char *argv[] = {"redblue", "--api=gl"};
		struct redblue_args a = parse_ok("defaults", 2, argv);
		check(a.api == REDBLUE_API_GL, "api gl");
		check(!a.list, "list off by default");
		check(!a.target_given, "no target means primary screen");
		check(a.seconds == 10.0, "seconds default to 10");
		check(!a.fullscreen, "windowed by default");
	}
	{
		char *argv[] = {"redblue", "--api=vk", "--adapter=2", "--output=1", "--seconds=2.5",
		                "--fullscreen"};
		struct redblue_args a = parse_ok("all options", 6, argv);
		check(a.api == REDBLUE_API_VK, "api vk");
		check(a.adapter == 2, "adapter 2");
		check(a.output == 1, "output 1");
		check(a.seconds == 2.5, "seconds 2.5");
		check(a.fullscreen, "fullscreen");
		check(a.target_given, "target given");
	}
	{
		char *argv[] = {"redblue", "--api=gl", "--output=1"};
		struct redblue_args a = parse_ok("output only", 3, argv);
		check(a.target_given && a.adapter == 0 && a.output == 1, "output only keeps adapter 0");
	}
	{
		const struct redblue_screen screens[] = {
		    {0, 0, 0, 0},       // primary
		    {0, 1, -2560, -1},  // left of primary
		    {1, 0, 2560, 173},  // other adapter, right of primary
		};
		const struct redblue_screen *p = redblue_find_primary(screens, 3);
		check(p == &screens[0], "primary is the screen at desktop origin");
		const struct redblue_screen other_primary[] = {
		    {0, 0, -2560, 0},
		    {1, 0, 0, 0},
		};
		check(redblue_find_primary(other_primary, 2) == &other_primary[1],
		      "primary on second adapter");
		check(redblue_find_primary(screens + 1, 2) == NULL, "no screen at origin");
		check(redblue_find_primary(NULL, 0) == NULL, "no screens");
	}
	{
		char *argv[] = {"redblue", "--api=dxgi"};
		check(parse_ok("dxgi", 2, argv).api == REDBLUE_API_DXGI, "api dxgi");
	}
	{
		char *argv[] = {"redblue", "--list"};
		struct redblue_args a = parse_ok("list", 2, argv);
		check(a.list, "list");
		check(a.api == REDBLUE_API_NONE, "list needs no api");
	}

	char *no_args[] = {"redblue"};
	parse_fails("no api", 1, no_args);
	char *unknown_api[] = {"redblue", "--api=metal"};
	parse_fails("unknown api", 2, unknown_api);
	char *unknown_flag[] = {"redblue", "--api=gl", "--stereo"};
	parse_fails("unknown flag", 3, unknown_flag);
	char *negative_adapter[] = {"redblue", "--api=gl", "--adapter=-1"};
	parse_fails("negative adapter", 3, negative_adapter);
	char *junk_output[] = {"redblue", "--api=gl", "--output=1x"};
	parse_fails("trailing junk in output", 3, junk_output);
	char *empty_adapter[] = {"redblue", "--api=gl", "--adapter="};
	parse_fails("empty adapter", 3, empty_adapter);
	char *zero_seconds[] = {"redblue", "--api=gl", "--seconds=0"};
	parse_fails("zero seconds", 3, zero_seconds);
	char *junk_seconds[] = {"redblue", "--api=gl", "--seconds=abc"};
	parse_fails("non-numeric seconds", 3, junk_seconds);
	{
		char *argv[] = {"redblue", "--api=dxgi", "--exclusive"};
		struct redblue_args a = parse_ok("dxgi exclusive", 3, argv);
		check(a.exclusive && !a.fullscreen, "exclusive is separate from borderless fullscreen");
	}
	char *exclusive_gl[] = {"redblue", "--api=gl", "--exclusive"};
	parse_fails("exclusive is dxgi only", 3, exclusive_gl);
	char *exclusive_and_borderless[] = {"redblue", "--api=dxgi", "--exclusive", "--fullscreen"};
	parse_fails("exclusive with borderless", 4, exclusive_and_borderless);
	char *list_and_api[] = {"redblue", "--list", "--api=gl"};
	parse_fails("list with api", 3, list_and_api);

	if (failures == 0) {
		printf("redblue_args_test: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}

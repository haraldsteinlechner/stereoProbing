#include "redblue_args.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
parse_unsigned(const char *text, unsigned *out)
{
	if (*text < '0' || *text > '9') {
		return false;
	}
	char *end;
	unsigned long value = strtoul(text, &end, 10);
	if (*end != '\0' || value > 0xFFFFu) {
		return false;
	}
	*out = (unsigned)value;
	return true;
}

static const char *
value_of(const char *arg, const char *key)
{
	size_t n = strlen(key);
	return strncmp(arg, key, n) == 0 ? arg + n : NULL;
}

bool
redblue_parse_args(int argc, char **argv, struct redblue_args *out, char *error, size_t error_size)
{
	*out = (struct redblue_args){.api = REDBLUE_API_NONE, .seconds = 10.0};
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		const char *v;
		if (strcmp(arg, "--list") == 0) {
			out->list = true;
		} else if (strcmp(arg, "--fullscreen") == 0) {
			out->fullscreen = true;
		} else if (strcmp(arg, "--exclusive") == 0) {
			out->exclusive = true;
		} else if ((v = value_of(arg, "--api=")) != NULL) {
			if (strcmp(v, "dxgi") == 0) {
				out->api = REDBLUE_API_DXGI;
			} else if (strcmp(v, "gl") == 0) {
				out->api = REDBLUE_API_GL;
			} else if (strcmp(v, "vk") == 0) {
				out->api = REDBLUE_API_VK;
			} else {
				snprintf(error, error_size, "unknown api \"%s\" (dxgi, gl, vk)", v);
				return false;
			}
		} else if ((v = value_of(arg, "--adapter=")) != NULL) {
			out->target_given = true;
			if (!parse_unsigned(v, &out->adapter)) {
				snprintf(error, error_size, "bad adapter index \"%s\"", v);
				return false;
			}
		} else if ((v = value_of(arg, "--output=")) != NULL) {
			out->target_given = true;
			if (!parse_unsigned(v, &out->output)) {
				snprintf(error, error_size, "bad output index \"%s\"", v);
				return false;
			}
		} else if ((v = value_of(arg, "--seconds=")) != NULL) {
			char *end;
			out->seconds = strtod(v, &end);
			if (end == v || *end != '\0' || !(out->seconds > 0.0)) {
				snprintf(error, error_size, "bad seconds \"%s\" (> 0)", v);
				return false;
			}
		} else {
			snprintf(error, error_size, "unknown argument \"%s\"", arg);
			return false;
		}
	}
	if (out->list == (out->api != REDBLUE_API_NONE)) {
		snprintf(error, error_size, "need exactly one of --list or --api=");
		return false;
	}
	if (out->exclusive && (out->api != REDBLUE_API_DXGI || out->fullscreen)) {
		snprintf(error, error_size, "--exclusive is for --api=dxgi, without --fullscreen");
		return false;
	}
	return true;
}

const struct redblue_screen *
redblue_find_primary(const struct redblue_screen *screens, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (screens[i].left == 0 && screens[i].top == 0) {
			return &screens[i];
		}
	}
	return NULL;
}

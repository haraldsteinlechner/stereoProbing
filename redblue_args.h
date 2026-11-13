#pragma once

#include <stdbool.h>
#include <stddef.h>

enum redblue_api
{
	REDBLUE_API_NONE,
	REDBLUE_API_DXGI,
	REDBLUE_API_GL,
	REDBLUE_API_VK,
};

struct redblue_args
{
	enum redblue_api api;
	bool list;
	// false: use the screen with the Windows primary desktop.
	bool target_given;
	// DXGI adapter index (as printed by --list); Vulkan matches it by LUID.
	unsigned adapter;
	// Output index of that adapter; the window is placed there.
	unsigned output;
	double seconds;
	// Borderless window covering the output (all APIs).
	bool fullscreen;
	// DXGI exclusive fullscreen (SetFullscreenState).
	bool exclusive;
};

bool
redblue_parse_args(int argc, char **argv, struct redblue_args *out, char *error, size_t error_size);

struct redblue_screen
{
	unsigned adapter;
	unsigned output;
	long left;
	long top;
};

// The primary desktop starts at desktop coordinate 0,0. NULL when no screen does.
const struct redblue_screen *
redblue_find_primary(const struct redblue_screen *screens, size_t count);

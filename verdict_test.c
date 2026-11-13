#include "verdict.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void
expect(struct stereo_evidence e, enum stereo_verdict expected, const char *what)
{
	enum stereo_verdict actual = stereo_verdict_classify(&e);
	if (actual != expected) {
		fprintf(stderr, "FAIL %s: got %s, expected %s\n", what, stereo_verdict_name(actual),
		        stereo_verdict_name(expected));
		failures++;
	}
}

int
main(void)
{
	expect((struct stereo_evidence){true, 4, 2, 2}, STEREO_VERDICT_VULKAN_STEREO_AVAILABLE,
	       "vulkan two layers with dxgi and opengl stereo");
	expect((struct stereo_evidence){false, 0, 0, 2}, STEREO_VERDICT_VULKAN_STEREO_AVAILABLE,
	       "vulkan two layers without other stereo");
	expect((struct stereo_evidence){true, 4, 2, 1}, STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_OPENGL,
	       "opengl stereo pixel formats but vulkan one layer");
	expect((struct stereo_evidence){false, 0, 2, 1}, STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_OPENGL,
	       "opengl stereo only");
	expect((struct stereo_evidence){true, 4, 0, 1}, STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_DXGI,
	       "dxgi stereo swapchain and stereo modes but vulkan one layer");
	expect((struct stereo_evidence){true, 4, 0, 0}, STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_DXGI,
	       "dxgi stereo but no vulkan surface support");
	expect((struct stereo_evidence){false, 0, 0, 1}, STEREO_VERDICT_NO_STEREO_PATH,
	       "no api offers stereo");
	// D3D11 creates Stereo=TRUE swapchains on adapters with no stereo modes or
	// no outputs. Creation alone is no evidence of stereo output.
	expect((struct stereo_evidence){true, 0, 0, 1}, STEREO_VERDICT_NO_STEREO_PATH,
	       "dxgi swapchain without stereo modes");
	expect((struct stereo_evidence){false, 4, 0, 1}, STEREO_VERDICT_NO_STEREO_PATH,
	       "stereo modes but swapchain creation failed");

	const struct {
		const char *renderer;
		const char *adapter;
		bool expected;
	} matches[] = {
		{"Vendor GPU X", "Vendor GPU X", true},
		{"Vendor GPU X (suffix)", "Vendor GPU X", true},
		{"Vendor GPU X", "Vendor GPU Y", false},
		{"", "Vendor GPU X", false},
		{"Vendor GPU X", "", false},
	};
	for (size_t i = 0; i < sizeof(matches) / sizeof(matches[0]); i++) {
		if (gl_renderer_matches_adapter(matches[i].renderer, matches[i].adapter) !=
		    matches[i].expected) {
			fprintf(stderr, "FAIL renderer \"%s\" vs adapter \"%s\"\n", matches[i].renderer,
			        matches[i].adapter);
			failures++;
		}
	}

	if (strcmp(stereo_verdict_name((enum stereo_verdict)99), "unknown") != 0) {
		fprintf(stderr, "FAIL out-of-range verdict name\n");
		failures++;
	}

	if (failures == 0) {
		printf("verdict_test: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}

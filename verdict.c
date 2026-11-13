#include "verdict.h"

#include <string.h>

enum stereo_verdict
stereo_verdict_classify(const struct stereo_evidence *evidence)
{
	if (evidence->vk_max_image_array_layers >= 2) {
		return STEREO_VERDICT_VULKAN_STEREO_AVAILABLE;
	}
	if (evidence->gl_stereo_pixel_format_count > 0) {
		return STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_OPENGL;
	}
	if (evidence->dxgi_stereo_swapchain_created && evidence->dxgi_stereo_mode_count > 0) {
		return STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_DXGI;
	}
	return STEREO_VERDICT_NO_STEREO_PATH;
}

const char *
stereo_verdict_name(enum stereo_verdict verdict)
{
	switch (verdict) {
	case STEREO_VERDICT_VULKAN_STEREO_AVAILABLE: return "vulkan-stereo-available";
	case STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_OPENGL: return "vulkan-inconsistent-with-opengl";
	case STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_DXGI: return "vulkan-inconsistent-with-dxgi";
	case STEREO_VERDICT_NO_STEREO_PATH: return "no-stereo-path";
	}
	return "unknown";
}

bool
gl_renderer_matches_adapter(const char *gl_renderer, const char *adapter_name)
{
	return gl_renderer[0] != '\0' && adapter_name[0] != '\0' &&
	       strstr(gl_renderer, adapter_name) != NULL;
}

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Stereo evidence for one adapter, gathered from each API independently.
struct stereo_evidence
{
	bool dxgi_stereo_swapchain_created;
	uint32_t dxgi_stereo_mode_count;
	// PFD_STEREO pixel formats on windows placed on the adapter's outputs.
	uint32_t gl_stereo_pixel_format_count;
	uint32_t vk_max_image_array_layers;
};

enum stereo_verdict
{
	STEREO_VERDICT_VULKAN_STEREO_AVAILABLE,
	// The same driver's OpenGL ICD offers stereo pixel formats, Vulkan
	// reports maxImageArrayLayers < 2.
	STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_OPENGL,
	// DXGI enumerates stereo modes and creates a Stereo=TRUE swapchain,
	// Vulkan reports maxImageArrayLayers < 2.
	STEREO_VERDICT_VULKAN_INCONSISTENT_WITH_DXGI,
	STEREO_VERDICT_NO_STEREO_PATH,
};

enum stereo_verdict
stereo_verdict_classify(const struct stereo_evidence *evidence);

const char *
stereo_verdict_name(enum stereo_verdict verdict);

// OpenGL loads one ICD per process, so a window on another adapter's output
// can still report the first ICD's pixel formats. Formats count for an adapter
// only when GL_RENDERER names that adapter.
bool
gl_renderer_matches_adapter(const char *gl_renderer, const char *adapter_name);

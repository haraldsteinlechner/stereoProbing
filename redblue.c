// Presents left eye = blue, right eye = red through DXGI, OpenGL quad buffer
// or a two-layer Vulkan swapchain. Viewed without glasses, a working
// frame-sequential path flickers or mixes to magenta; through the glasses one
// eye sees blue and the other red. Output is key=value lines.
//
// Exit codes: 0 the API accepted two-eye frames (whether the display shows
// them is only visible through the glasses), 1 error, 2 API offers no stereo,
// 64 usage.

#define COBJMACROS
#include "window.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <GL/gl.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "redblue_args.h"
#include "verdict.h"

enum
{
	EXIT_OK = 0,
	EXIT_ERROR = 1,
	EXIT_NO_STEREO = 2,
	EXIT_USAGE = 64,
};

static const float EYE_COLORS[2][4] = {{0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}};

struct target
{
	IDXGIFactory2 *factory;
	IDXGIAdapter1 *adapter;
	DXGI_ADAPTER_DESC1 desc;
	char adapter_name[128];
	// NULL when the adapter has no outputs (e.g. a laptop dGPU).
	IDXGIOutput *output;
	RECT rect;
};

struct stopwatch
{
	LARGE_INTEGER freq;
	LARGE_INTEGER start;
};

static struct stopwatch
stopwatch_start(void)
{
	struct stopwatch s;
	QueryPerformanceFrequency(&s.freq);
	QueryPerformanceCounter(&s.start);
	return s;
}

static double
stopwatch_seconds(const struct stopwatch *s)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (double)(now.QuadPart - s->start.QuadPart) / (double)s->freq.QuadPart;
}

static void
print_rate(const char *api, unsigned presents, const struct stopwatch *s)
{
	double elapsed = stopwatch_seconds(s);
	printf("%s presents=%u elapsed_s=%.3f present_rate_hz=%.2f\n", api, presents, elapsed,
	       elapsed > 0.0 ? presents / elapsed : 0.0);
}

static UINT
count_stereo_modes(IDXGIOutput *output)
{
	IDXGIOutput1 *output1 = NULL;
	if (FAILED(IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput1, (void **)&output1))) {
		return 0;
	}
	UINT count = 0;
	IDXGIOutput1_GetDisplayModeList1(output1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_STEREO,
	                                 &count, NULL);
	DXGI_MODE_DESC1 *modes = calloc(count ? count : 1, sizeof(*modes));
	UINT stereo = 0;
	if (count > 0 && SUCCEEDED(IDXGIOutput1_GetDisplayModeList1(
	                     output1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_STEREO, &count, modes))) {
		for (UINT m = 0; m < count; m++) {
			stereo += modes[m].Stereo ? 1u : 0u;
		}
	}
	free(modes);
	IDXGIOutput1_Release(output1);
	return stereo;
}

static int
list_targets(void)
{
	IDXGIFactory2 *factory = NULL;
	if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory))) {
		printf("dxgi factory failed\n");
		return EXIT_ERROR;
	}
	printf("dxgi windowed_stereo_enabled=%d\n", IDXGIFactory2_IsWindowedStereoEnabled(factory));
	IDXGIAdapter1 *adapter = NULL;
	for (UINT a = 0; IDXGIFactory2_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND; a++) {
		DXGI_ADAPTER_DESC1 desc;
		IDXGIAdapter1_GetDesc1(adapter, &desc);
		printf("adapter=%u name=\"%ls\" software=%d\n", a, desc.Description,
		       (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0);
		IDXGIOutput *output = NULL;
		for (UINT o = 0; IDXGIAdapter1_EnumOutputs(adapter, o, &output) != DXGI_ERROR_NOT_FOUND; o++) {
			DXGI_OUTPUT_DESC od;
			IDXGIOutput_GetDesc(output, &od);
			RECT r = od.DesktopCoordinates;
			printf("  output=%u device=%ls desktop=%ld,%ld %ldx%ld stereo_modes=%u\n", o,
			       od.DeviceName, r.left, r.top, r.right - r.left, r.bottom - r.top,
			       count_stereo_modes(output));
			IDXGIOutput_Release(output);
		}
		IDXGIAdapter1_Release(adapter);
	}
	IDXGIFactory2_Release(factory);
	return EXIT_OK;
}

static bool
open_target(const struct redblue_args *args, struct target *t)
{
	memset(t, 0, sizeof(*t));
	if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&t->factory))) {
		printf("dxgi factory failed\n");
		return false;
	}
	unsigned adapter = args->adapter;
	unsigned output = args->output;
	if (!args->target_given) {
		struct redblue_screen screens[64];
		size_t count = 0;
		IDXGIAdapter1 *a = NULL;
		for (UINT ai = 0; IDXGIFactory2_EnumAdapters1(t->factory, ai, &a) != DXGI_ERROR_NOT_FOUND;
		     ai++) {
			IDXGIOutput *o = NULL;
			for (UINT oi = 0; count < 64 && IDXGIAdapter1_EnumOutputs(a, oi, &o) != DXGI_ERROR_NOT_FOUND;
			     oi++) {
				DXGI_OUTPUT_DESC od;
				IDXGIOutput_GetDesc(o, &od);
				screens[count++] = (struct redblue_screen){ai, oi, od.DesktopCoordinates.left,
				                                           od.DesktopCoordinates.top};
				IDXGIOutput_Release(o);
			}
			IDXGIAdapter1_Release(a);
		}
		const struct redblue_screen *primary = redblue_find_primary(screens, count);
		if (primary == NULL) {
			printf("error=no screen at desktop origin; pass --adapter/--output (see --list)\n");
			return false;
		}
		adapter = primary->adapter;
		output = primary->output;
		printf("target=primary-screen\n");
	}
	if (FAILED(IDXGIFactory2_EnumAdapters1(t->factory, adapter, &t->adapter))) {
		printf("error=no adapter %u (see --list)\n", adapter);
		return false;
	}
	IDXGIAdapter1_GetDesc1(t->adapter, &t->desc);
	snprintf(t->adapter_name, sizeof(t->adapter_name), "%ls", t->desc.Description);
	HRESULT hr = IDXGIAdapter1_EnumOutputs(t->adapter, output, &t->output);
	if (SUCCEEDED(hr)) {
		DXGI_OUTPUT_DESC od;
		IDXGIOutput_GetDesc(t->output, &od);
		t->rect = od.DesktopCoordinates;
		printf("target adapter=%u name=\"%s\" output=%u device=%ls stereo_modes=%u\n",
		       adapter, t->adapter_name, output, od.DeviceName,
		       count_stereo_modes(t->output));
	} else if (hr == DXGI_ERROR_NOT_FOUND && output == 0) {
		t->rect = (RECT){0, 0, 1280, 720};
		printf("target adapter=%u name=\"%s\" output=none (adapter has no outputs)\n",
		       adapter, t->adapter_name);
	} else {
		printf("error=adapter %u has no output %u (see --list)\n", adapter, output);
		return false;
	}
	printf("dxgi windowed_stereo_enabled=%d\n", IDXGIFactory2_IsWindowedStereoEnabled(t->factory));
	return true;
}

static void
close_target(struct target *t)
{
	if (t->output != NULL) {
		IDXGIOutput_Release(t->output);
	}
	if (t->adapter != NULL) {
		IDXGIAdapter1_Release(t->adapter);
	}
	if (t->factory != NULL) {
		IDXGIFactory2_Release(t->factory);
	}
}

// Borderless covers the output; otherwise a 640x360 window on it.
static HWND
target_window(const struct target *t, const wchar_t *title, bool borderless)
{
	RECT r = t->rect;
	HWND hwnd = borderless
	                ? window_create(title, WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top)
	                : window_create(title, WS_OVERLAPPEDWINDOW, r.left + 50, r.top + 50, 640, 360);
	SetForegroundWindow(hwnd);
	window_pump_messages();
	return hwnd;
}

static int
run_dxgi(const struct target *t, const struct redblue_args *args)
{
	ID3D11Device *device = NULL;
	ID3D11DeviceContext *context = NULL;
	HRESULT hr = D3D11CreateDevice((IDXGIAdapter *)t->adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL,
	                               0, D3D11_SDK_VERSION, &device, NULL, &context);
	if (FAILED(hr)) {
		printf("dxgi d3d11_device_hr=0x%08lx\n", (unsigned long)hr);
		return EXIT_ERROR;
	}
	if (args->exclusive && t->output == NULL) {
		printf("error=exclusive fullscreen needs an output of the adapter\n");
		return EXIT_ERROR;
	}
	HWND hwnd = target_window(t, L"redblue DXGI", args->fullscreen);
	DXGI_SWAP_CHAIN_DESC1 scd = {0};
	scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.Stereo = TRUE;
	scd.SampleDesc.Count = 1;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.BufferCount = 2;
	scd.Scaling = DXGI_SCALING_STRETCH;
	scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	IDXGISwapChain1 *swapchain = NULL;
	hr = IDXGIFactory2_CreateSwapChainForHwnd(t->factory, (IUnknown *)device, hwnd, &scd, NULL, NULL,
	                                         &swapchain);
	DXGI_SWAP_CHAIN_DESC1 actual = {0};
	if (SUCCEEDED(hr)) {
		IDXGISwapChain1_GetDesc1(swapchain, &actual);
	}
	printf("dxgi stereo_swapchain_hr=0x%08lx stereo_in_desc=%d window=%s\n", (unsigned long)hr,
	       actual.Stereo,
	       args->exclusive ? "exclusive-fullscreen"
	       : args->fullscreen ? "borderless-fullscreen"
	                          : "windowed");
	if (FAILED(hr) || !actual.Stereo) {
		return EXIT_NO_STEREO;
	}
	if (args->exclusive) {
		hr = IDXGISwapChain1_SetFullscreenState(swapchain, TRUE, t->output);
		printf("dxgi fullscreen=exclusive hr=0x%08lx\n", (unsigned long)hr);
		if (FAILED(hr)) {
			return EXIT_ERROR;
		}
		hr = IDXGISwapChain1_ResizeBuffers(swapchain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
		if (FAILED(hr)) {
			printf("dxgi resize_hr=0x%08lx\n", (unsigned long)hr);
			return EXIT_ERROR;
		}
	}

	ID3D11Texture2D *buffer = NULL;
	hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&buffer);
	if (FAILED(hr)) {
		printf("dxgi get_buffer_hr=0x%08lx\n", (unsigned long)hr);
		return EXIT_ERROR;
	}
	ID3D11RenderTargetView *views[2] = {NULL, NULL};
	for (UINT eye = 0; eye < 2; eye++) {
		D3D11_RENDER_TARGET_VIEW_DESC rtv = {0};
		rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		rtv.Texture2DArray.FirstArraySlice = eye;
		rtv.Texture2DArray.ArraySize = 1;
		hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)buffer, &rtv, &views[eye]);
		if (FAILED(hr)) {
			printf("dxgi rtv_eye=%u hr=0x%08lx\n", eye, (unsigned long)hr);
			return EXIT_ERROR;
		}
	}
	ID3D11Texture2D_Release(buffer);

	struct stopwatch clock = stopwatch_start();
	unsigned presents = 0;
	unsigned occluded = 0;
	HRESULT present_hr = S_OK;
	while (window_pump_messages() && stopwatch_seconds(&clock) < args->seconds) {
		ID3D11DeviceContext_ClearRenderTargetView(context, views[0], EYE_COLORS[0]);
		ID3D11DeviceContext_ClearRenderTargetView(context, views[1], EYE_COLORS[1]);
		present_hr = IDXGISwapChain1_Present(swapchain, 1, 0);
		if (FAILED(present_hr)) {
			break;
		}
		// DXGI_STATUS_OCCLUDED: the frame was not shown (window hidden or
		// exclusive fullscreen lost).
		occluded += present_hr == DXGI_STATUS_OCCLUDED ? 1u : 0u;
		presents++;
	}
	print_rate("dxgi", presents, &clock);
	printf("dxgi last_present_hr=0x%08lx occluded_presents=%u\n", (unsigned long)present_hr,
	       occluded);
	if (occluded > 0) {
		printf("error=frames not shown (DXGI_STATUS_OCCLUDED); keep the test window in front\n");
	}

	if (args->exclusive) {
		IDXGISwapChain1_SetFullscreenState(swapchain, FALSE, NULL);
	}
	ID3D11RenderTargetView_Release(views[0]);
	ID3D11RenderTargetView_Release(views[1]);
	IDXGISwapChain1_Release(swapchain);
	DestroyWindow(hwnd);
	ID3D11DeviceContext_Release(context);
	ID3D11Device_Release(device);
	return FAILED(present_hr) || occluded > 0 ? EXIT_ERROR : EXIT_OK;
}

typedef BOOL(WINAPI *swap_interval_fn)(int interval);

static int
run_gl(const struct target *t, const struct redblue_args *args)
{
	HWND hwnd = target_window(t, L"redblue OpenGL", args->fullscreen);
	HDC hdc = GetDC(hwnd);
	int total = DescribePixelFormat(hdc, 1, sizeof(PIXELFORMATDESCRIPTOR), NULL);
	int chosen = 0;
	unsigned stereo_formats = 0;
	const DWORD required = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER | PFD_STEREO;
	for (int i = 1; i <= total; i++) {
		PIXELFORMATDESCRIPTOR pfd;
		DescribePixelFormat(hdc, i, sizeof(pfd), &pfd);
		if ((pfd.dwFlags & required) != required || (pfd.dwFlags & PFD_GENERIC_FORMAT) ||
		    pfd.iPixelType != PFD_TYPE_RGBA || pfd.cColorBits < 24) {
			continue;
		}
		stereo_formats++;
		if (chosen == 0) {
			chosen = i;
		}
	}
	printf("gl pixel_formats=%d stereo_formats=%u chosen_format=%d\n", total, stereo_formats, chosen);
	if (chosen == 0) {
		ReleaseDC(hwnd, hdc);
		DestroyWindow(hwnd);
		return EXIT_NO_STEREO;
	}

	PIXELFORMATDESCRIPTOR pfd;
	DescribePixelFormat(hdc, chosen, sizeof(pfd), &pfd);
	if (!SetPixelFormat(hdc, chosen, &pfd)) {
		printf("gl set_pixel_format_error=%lu\n", GetLastError());
		return EXIT_ERROR;
	}
	HGLRC context = wglCreateContext(hdc);
	if (context == NULL || !wglMakeCurrent(hdc, context)) {
		printf("gl create_context_error=%lu\n", GetLastError());
		return EXIT_ERROR;
	}
	GLboolean stereo = GL_FALSE;
	glGetBooleanv(GL_STEREO, &stereo);
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	printf("gl renderer=\"%s\" version=\"%s\" renderer_matches_adapter=%d gl_stereo=%d\n",
	       renderer != NULL ? renderer : "", (const char *)glGetString(GL_VERSION),
	       gl_renderer_matches_adapter(renderer != NULL ? renderer : "", t->adapter_name), stereo);
	swap_interval_fn swap_interval = (swap_interval_fn)(void *)wglGetProcAddress("wglSwapIntervalEXT");
	printf("gl swap_interval_1=%d window=%s\n", swap_interval != NULL && swap_interval(1),
	       args->fullscreen ? "borderless-fullscreen" : "windowed");
	if (!stereo) {
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(context);
		ReleaseDC(hwnd, hdc);
		DestroyWindow(hwnd);
		return EXIT_NO_STEREO;
	}

	struct stopwatch clock = stopwatch_start();
	unsigned presents = 0;
	GLenum first_error = GL_NO_ERROR;
	while (window_pump_messages() && stopwatch_seconds(&clock) < args->seconds) {
		const GLenum buffers[2] = {GL_BACK_LEFT, GL_BACK_RIGHT};
		for (int eye = 0; eye < 2; eye++) {
			glDrawBuffer(buffers[eye]);
			glClearColor(EYE_COLORS[eye][0], EYE_COLORS[eye][1], EYE_COLORS[eye][2], 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		if (!SwapBuffers(hdc)) {
			printf("gl swap_buffers_error=%lu\n", GetLastError());
			break;
		}
		if (first_error == GL_NO_ERROR) {
			first_error = glGetError();
		}
		presents++;
	}
	print_rate("gl", presents, &clock);
	printf("gl first_error=0x%04x\n", first_error);

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(context);
	ReleaseDC(hwnd, hdc);
	DestroyWindow(hwnd);
	return first_error == GL_NO_ERROR ? EXIT_OK : EXIT_ERROR;
}

static int
vk_failed(const char *what, VkResult result)
{
	printf("vk %s=%d\n", what, result);
	return EXIT_ERROR;
}

static bool
has_extension(VkPhysicalDevice device, const char *name)
{
	uint32_t count = 0;
	vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
	VkExtensionProperties *props = calloc(count ? count : 1, sizeof(*props));
	vkEnumerateDeviceExtensionProperties(device, NULL, &count, props);
	bool found = false;
	for (uint32_t i = 0; i < count && !found; i++) {
		found = strcmp(props[i].extensionName, name) == 0;
	}
	free(props);
	return found;
}

// Vulkan can list one GPU more than once; the first device
// with the DXGI adapter's LUID is used.
static VkPhysicalDevice
find_physical_device(VkInstance instance, LUID luid)
{
	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance, &count, NULL);
	VkPhysicalDevice *devices = calloc(count ? count : 1, sizeof(*devices));
	vkEnumeratePhysicalDevices(instance, &count, devices);
	VkPhysicalDevice found = VK_NULL_HANDLE;
	uint32_t matches = 0;
	for (uint32_t i = 0; i < count; i++) {
		VkPhysicalDeviceIDProperties id = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
		VkPhysicalDeviceProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		                                     .pNext = &id};
		vkGetPhysicalDeviceProperties2(devices[i], &props);
		LUID vk_luid;
		memcpy(&vk_luid, id.deviceLUID, sizeof(vk_luid));
		if (id.deviceLUIDValid && vk_luid.LowPart == luid.LowPart && vk_luid.HighPart == luid.HighPart) {
			if (matches++ == 0) {
				found = devices[i];
				printf("vk device=%u name=\"%s\" driver_version=0x%08x\n", i,
				       props.properties.deviceName, props.properties.driverVersion);
			}
		}
	}
	printf("vk devices=%u luid_matches=%u\n", count, matches);
	free(devices);
	return found;
}

static int
run_vk(const struct target *t, const struct redblue_args *args)
{
	const char *instance_ext[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
	VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	                         .pApplicationName = "redblue",
	                         .apiVersion = VK_API_VERSION_1_1};
	VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	                            .pApplicationInfo = &app,
	                            .enabledExtensionCount = 2,
	                            .ppEnabledExtensionNames = instance_ext};
	VkInstance instance;
	VkResult res = vkCreateInstance(&ici, NULL, &instance);
	if (res != VK_SUCCESS) {
		return vk_failed("create_instance", res);
	}
	VkPhysicalDevice gpu = find_physical_device(instance, t->desc.AdapterLuid);
	if (gpu == VK_NULL_HANDLE) {
		printf("error=no Vulkan device with the adapter's LUID\n");
		return EXIT_ERROR;
	}
	printf("vk full_screen_exclusive_available=%d\n",
	       has_extension(gpu, VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME));

	HWND hwnd = target_window(t, L"redblue Vulkan", args->fullscreen);
	VkWin32SurfaceCreateInfoKHR sci = {.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
	                                   .hinstance = GetModuleHandleW(NULL),
	                                   .hwnd = hwnd};
	VkSurfaceKHR surface;
	res = vkCreateWin32SurfaceKHR(instance, &sci, NULL, &surface);
	if (res != VK_SUCCESS) {
		return vk_failed("create_surface", res);
	}

	uint32_t family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &family_count, NULL);
	VkQueueFamilyProperties families[16];
	family_count = family_count > 16 ? 16 : family_count;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &family_count, families);
	uint32_t family = UINT32_MAX;
	for (uint32_t f = 0; f < family_count && family == UINT32_MAX; f++) {
		VkBool32 present = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(gpu, f, surface, &present);
		if (present && (families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			family = f;
		}
	}
	VkSurfaceCapabilitiesKHR caps;
	res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
	if (res != VK_SUCCESS) {
		return vk_failed("surface_caps", res);
	}
	printf("vk surface_supported=%d max_image_array_layers=%u transfer_dst=%d window=%s\n",
	       family != UINT32_MAX, caps.maxImageArrayLayers,
	       (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0,
	       args->fullscreen ? "borderless-fullscreen" : "windowed");
	if (family == UINT32_MAX) {
		printf("error=no graphics queue family can present to the window\n");
		return EXIT_ERROR;
	}
	if (caps.maxImageArrayLayers < 2) {
		vkDestroySurfaceKHR(instance, surface, NULL);
		vkDestroyInstance(instance, NULL);
		DestroyWindow(hwnd);
		return EXIT_NO_STEREO;
	}
	if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
		printf("error=swapchain images cannot be cleared (no TRANSFER_DST usage)\n");
		return EXIT_ERROR;
	}

	uint32_t format_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, NULL);
	VkSurfaceFormatKHR formats[64];
	format_count = format_count > 64 ? 64 : format_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats);
	VkSurfaceFormatKHR format = formats[0];
	for (uint32_t i = 0; i < format_count; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
			format = formats[i];
		}
	}

	const float priority = 1.0f;
	VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
	                               .queueFamilyIndex = family,
	                               .queueCount = 1,
	                               .pQueuePriorities = &priority};
	const char *device_ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	                          .queueCreateInfoCount = 1,
	                          .pQueueCreateInfos = &qci,
	                          .enabledExtensionCount = 1,
	                          .ppEnabledExtensionNames = device_ext};
	VkDevice device;
	res = vkCreateDevice(gpu, &dci, NULL, &device);
	if (res != VK_SUCCESS) {
		return vk_failed("create_device", res);
	}
	VkQueue queue;
	vkGetDeviceQueue(device, family, 0, &queue);

	VkExtent2D extent = caps.currentExtent;
	if (extent.width == UINT32_MAX) {
		RECT client;
		GetClientRect(hwnd, &client);
		extent = (VkExtent2D){(uint32_t)client.right, (uint32_t)client.bottom};
	}
	uint32_t image_count = caps.minImageCount < 2 ? 2 : caps.minImageCount;
	if (caps.maxImageCount != 0 && image_count > caps.maxImageCount) {
		image_count = caps.maxImageCount;
	}
	VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	while (!(caps.supportedCompositeAlpha & alpha)) {
		alpha <<= 1;
	}
	VkSwapchainCreateInfoKHR swci = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	                                 .surface = surface,
	                                 .minImageCount = image_count,
	                                 .imageFormat = format.format,
	                                 .imageColorSpace = format.colorSpace,
	                                 .imageExtent = extent,
	                                 .imageArrayLayers = 2,
	                                 .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                                 .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
	                                 .preTransform = caps.currentTransform,
	                                 .compositeAlpha = alpha,
	                                 .presentMode = VK_PRESENT_MODE_FIFO_KHR,
	                                 .clipped = VK_TRUE};
	VkSwapchainKHR swapchain;
	res = vkCreateSwapchainKHR(device, &swci, NULL, &swapchain);
	printf("vk swapchain_result=%d layers=2 extent=%ux%u format=%d present_mode=fifo\n", res,
	       extent.width, extent.height, format.format);
	if (res != VK_SUCCESS) {
		return EXIT_ERROR;
	}

	enum { MAX_IMAGES = 8 };
	VkImage images[MAX_IMAGES];
	uint32_t count = MAX_IMAGES;
	vkGetSwapchainImagesKHR(device, swapchain, &count, images);
	VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	                               .queueFamilyIndex = family};
	VkCommandPool pool;
	vkCreateCommandPool(device, &pci, NULL, &pool);
	VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	                                   .commandPool = pool,
	                                   .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	                                   .commandBufferCount = count};
	VkCommandBuffer cmds[MAX_IMAGES];
	vkAllocateCommandBuffers(device, &cai, cmds);
	VkSemaphore rendered[MAX_IMAGES];
	const VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	for (uint32_t i = 0; i < count; i++) {
		vkCreateSemaphore(device, &sem_info, NULL, &rendered[i]);
		VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		vkBeginCommandBuffer(cmds[i], &begin);
		VkImageSubresourceRange both = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
		VkImageMemoryBarrier to_clear = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		                                 .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		                                 .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		                                 .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                                 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                                 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		                                 .image = images[i],
		                                 .subresourceRange = both};
		vkCmdPipelineBarrier(cmds[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_clear);
		for (uint32_t eye = 0; eye < 2; eye++) {
			VkClearColorValue color;
			memcpy(color.float32, EYE_COLORS[eye], sizeof(color.float32));
			VkImageSubresourceRange layer = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, eye, 1};
			vkCmdClearColorImage(cmds[i], images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
			                     &layer);
		}
		VkImageMemoryBarrier to_present = to_clear;
		to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		to_present.dstAccessMask = 0;
		to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		vkCmdPipelineBarrier(cmds[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
		                     &to_present);
		vkEndCommandBuffer(cmds[i]);
	}
	VkSemaphore acquired;
	vkCreateSemaphore(device, &sem_info, NULL, &acquired);
	VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	                                .flags = VK_FENCE_CREATE_SIGNALED_BIT};
	VkFence done;
	vkCreateFence(device, &fence_info, NULL, &done);

	struct stopwatch clock = stopwatch_start();
	unsigned presents = 0;
	VkResult present_res = VK_SUCCESS;
	while (window_pump_messages() && stopwatch_seconds(&clock) < args->seconds) {
		vkWaitForFences(device, 1, &done, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &done);
		uint32_t index;
		present_res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE,
		                                    &index);
		if (present_res != VK_SUCCESS && present_res != VK_SUBOPTIMAL_KHR) {
			// The fence was reset without a submit; re-signal it for teardown.
			vkQueueSubmit(queue, 0, NULL, done);
			break;
		}
		const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		                       .waitSemaphoreCount = 1,
		                       .pWaitSemaphores = &acquired,
		                       .pWaitDstStageMask = &wait_stage,
		                       .commandBufferCount = 1,
		                       .pCommandBuffers = &cmds[index],
		                       .signalSemaphoreCount = 1,
		                       .pSignalSemaphores = &rendered[index]};
		vkQueueSubmit(queue, 1, &submit, done);
		VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		                            .waitSemaphoreCount = 1,
		                            .pWaitSemaphores = &rendered[index],
		                            .swapchainCount = 1,
		                            .pSwapchains = &swapchain,
		                            .pImageIndices = &index};
		present_res = vkQueuePresentKHR(queue, &present);
		if (present_res != VK_SUCCESS && present_res != VK_SUBOPTIMAL_KHR) {
			break;
		}
		presents++;
	}
	print_rate("vk", presents, &clock);
	printf("vk last_present_result=%d\n", present_res);

	vkDeviceWaitIdle(device);
	vkDestroyFence(device, done, NULL);
	vkDestroySemaphore(device, acquired, NULL);
	for (uint32_t i = 0; i < count; i++) {
		vkDestroySemaphore(device, rendered[i], NULL);
	}
	vkDestroyCommandPool(device, pool, NULL);
	vkDestroySwapchainKHR(device, swapchain, NULL);
	vkDestroyDevice(device, NULL);
	vkDestroySurfaceKHR(instance, surface, NULL);
	vkDestroyInstance(instance, NULL);
	DestroyWindow(hwnd);
	return present_res == VK_SUCCESS || present_res == VK_SUBOPTIMAL_KHR ? EXIT_OK : EXIT_ERROR;
}

static const char USAGE[] =
    "usage: redblue --list\n"
    "       redblue --api=dxgi|gl|vk [--adapter=N] [--output=N] [--seconds=S]\n"
    "               [--fullscreen | --exclusive]\n"
    "  left eye = blue, right eye = red, vsync on; Esc or close ends early\n"
    "  --adapter/--output  indices from --list (default: the Windows main screen)\n"
    "  --seconds=S         run time (default 10)\n"
    "  --fullscreen        borderless window covering the output\n"
    "  --exclusive         dxgi only: exclusive fullscreen (SetFullscreenState)\n"
    "exit: 0 API accepted two-eye frames (check the glasses), 1 error, 2 API offers no stereo, 64 usage\n";

int
main(int argc, char **argv)
{
	struct redblue_args args;
	char error[256];
	if (!redblue_parse_args(argc, argv, &args, error, sizeof(error))) {
		fprintf(stderr, "%s\n%s", error, USAGE);
		return EXIT_USAGE;
	}
	if (args.list) {
		return list_targets();
	}
	window_register_class();
	struct target target;
	int code = EXIT_ERROR;
	if (open_target(&args, &target)) {
		switch (args.api) {
		case REDBLUE_API_DXGI: code = run_dxgi(&target, &args); break;
		case REDBLUE_API_GL: code = run_gl(&target, &args); break;
		case REDBLUE_API_VK: code = run_vk(&target, &args); break;
		case REDBLUE_API_NONE: break;
		}
	}
	close_target(&target);
	printf("result=%s\n", code == EXIT_OK          ? "two-eye-frames-presented"
	                      : code == EXIT_NO_STEREO ? "no-stereo"
	                                               : "error");
	return code;
}

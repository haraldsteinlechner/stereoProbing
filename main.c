// Records, per adapter, whether Windows/DXGI offers stereo and whether the
// Vulkan ICD offers a two-layer swapchain on a Win32 surface of the same
// adapter. Output is plain key=value lines intended for driver bug reports.

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

#include "verdict.h"

#define MAX_ADAPTERS 16

struct dxgi_adapter_result
{
	LUID luid;
	wchar_t name[128];
	struct stereo_evidence evidence;
};

static void
print_os_build(void)
{
	wchar_t build[32] = L"?";
	DWORD size = sizeof(build);
	DWORD ubr = 0;
	DWORD ubr_size = sizeof(ubr);
	const wchar_t *key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
	RegGetValueW(HKEY_LOCAL_MACHINE, key, L"CurrentBuild", RRF_RT_REG_SZ, NULL, build, &size);
	RegGetValueW(HKEY_LOCAL_MACHINE, key, L"UBR", RRF_RT_REG_DWORD, NULL, &ubr, &ubr_size);
	printf("os build=%ls.%lu\n", build, ubr);
}

struct gl_probe
{
	int total_formats;
	int icd_window_formats;
	uint32_t stereo_formats;
	char renderer[256];
};

// Counts PFD_STEREO pixel formats on a window placed on the given output and
// reads GL_RENDERER from a context on that window.
static struct gl_probe
probe_gl(RECT desktop)
{
	struct gl_probe r = {0};
	HWND hwnd = window_create(L"stereo_probe OpenGL", WS_POPUP, desktop.left, desktop.top, 64, 64);
	HDC hdc = GetDC(hwnd);
	int first_icd_format = 0;
	r.total_formats = DescribePixelFormat(hdc, 1, sizeof(PIXELFORMATDESCRIPTOR), NULL);
	for (int i = 1; i <= r.total_formats; i++) {
		PIXELFORMATDESCRIPTOR pfd;
		DescribePixelFormat(hdc, i, sizeof(pfd), &pfd);
		const DWORD required = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
		if ((pfd.dwFlags & required) != required || (pfd.dwFlags & PFD_GENERIC_FORMAT)) {
			continue;
		}
		if (first_icd_format == 0) {
			first_icd_format = i;
		}
		r.icd_window_formats++;
		r.stereo_formats += (pfd.dwFlags & PFD_STEREO) ? 1u : 0u;
	}

	PIXELFORMATDESCRIPTOR pfd;
	if (first_icd_format != 0 && DescribePixelFormat(hdc, first_icd_format, sizeof(pfd), &pfd) != 0 &&
	    SetPixelFormat(hdc, first_icd_format, &pfd)) {
		HGLRC context = wglCreateContext(hdc);
		if (context != NULL && wglMakeCurrent(hdc, context)) {
			const char *renderer = (const char *)glGetString(GL_RENDERER);
			strncpy(r.renderer, renderer != NULL ? renderer : "", sizeof(r.renderer) - 1);
			wglMakeCurrent(NULL, NULL);
		}
		if (context != NULL) {
			wglDeleteContext(context);
		}
	}
	ReleaseDC(hwnd, hdc);
	DestroyWindow(hwnd);
	return r;
}

static void
print_stereo_modes(IDXGIAdapter1 *adapter, const char *adapter_name, struct stereo_evidence *evidence)
{
	IDXGIOutput *output = NULL;
	for (UINT o = 0; IDXGIAdapter1_EnumOutputs(adapter, o, &output) != DXGI_ERROR_NOT_FOUND; o++) {
		DXGI_OUTPUT_DESC odesc;
		IDXGIOutput_GetDesc(output, &odesc);
		IDXGIOutput1 *output1 = NULL;
		HRESULT hr = IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput1, (void **)&output1);
		UINT count = 0;
		if (SUCCEEDED(hr)) {
			hr = IDXGIOutput1_GetDisplayModeList1(output1, DXGI_FORMAT_R8G8B8A8_UNORM,
			                                      DXGI_ENUM_MODES_STEREO, &count, NULL);
		}
		DXGI_MODE_DESC1 *modes = calloc(count ? count : 1, sizeof(*modes));
		if (SUCCEEDED(hr) && count > 0) {
			hr = IDXGIOutput1_GetDisplayModeList1(output1, DXGI_FORMAT_R8G8B8A8_UNORM,
			                                      DXGI_ENUM_MODES_STEREO, &count, modes);
		}
		UINT stereo = 0;
		for (UINT m = 0; SUCCEEDED(hr) && m < count; m++) {
			stereo += modes[m].Stereo ? 1u : 0u;
		}
		evidence->dxgi_stereo_mode_count += stereo;
		struct gl_probe gl = probe_gl(odesc.DesktopCoordinates);
		bool gl_matches = gl_renderer_matches_adapter(gl.renderer, adapter_name);
		if (gl_matches) {
			evidence->gl_stereo_pixel_format_count += gl.stereo_formats;
		}
		printf("dxgi   output=%u device=%ls desktop=%ldx%ld enum_hr=0x%08lx modes=%u stereo_modes=%u\n",
		       o, odesc.DeviceName, odesc.DesktopCoordinates.right - odesc.DesktopCoordinates.left,
		       odesc.DesktopCoordinates.bottom - odesc.DesktopCoordinates.top, (unsigned long)hr,
		       count, stereo);
		printf("gl     output=%u renderer=\"%s\" renderer_matches_adapter=%d pixel_formats=%d "
		       "icd_window_formats=%d stereo_formats=%u\n",
		       o, gl.renderer, gl_matches, gl.total_formats, gl.icd_window_formats, gl.stereo_formats);
		for (UINT m = 0; SUCCEEDED(hr) && m < count; m++) {
			if (modes[m].Stereo) {
				printf("dxgi     stereo_mode=%ux%u@%u/%u\n", modes[m].Width, modes[m].Height,
				       modes[m].RefreshRate.Numerator, modes[m].RefreshRate.Denominator);
			}
		}
		free(modes);
		if (output1 != NULL) {
			IDXGIOutput1_Release(output1);
		}
		IDXGIOutput_Release(output);
	}
}

static uint32_t
probe_dxgi(struct dxgi_adapter_result *results)
{
	IDXGIFactory2 *factory = NULL;
	HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory);
	if (FAILED(hr)) {
		printf("dxgi factory_hr=0x%08lx\n", (unsigned long)hr);
		return 0;
	}
	printf("dxgi windowed_stereo_enabled=%d\n", IDXGIFactory2_IsWindowedStereoEnabled(factory));

	uint32_t n = 0;
	IDXGIAdapter1 *adapter = NULL;
	for (UINT a = 0; n < MAX_ADAPTERS &&
	                 IDXGIFactory2_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND;
	     a++) {
		DXGI_ADAPTER_DESC1 desc;
		IDXGIAdapter1_GetDesc1(adapter, &desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			IDXGIAdapter1_Release(adapter);
			continue;
		}
		struct dxgi_adapter_result *r = &results[n++];
		memset(r, 0, sizeof(*r));
		r->luid = desc.AdapterLuid;
		wcsncpy(r->name, desc.Description, 127);
		printf("dxgi adapter=%u name=\"%ls\" vendor=0x%04x device=0x%04x luid=%08lx:%08lx\n", a,
		       desc.Description, desc.VendorId, desc.DeviceId, (unsigned long)desc.AdapterLuid.HighPart,
		       desc.AdapterLuid.LowPart);
		char adapter_name[128];
		snprintf(adapter_name, sizeof(adapter_name), "%ls", desc.Description);
		print_stereo_modes(adapter, adapter_name, &r->evidence);

		ID3D11Device *device = NULL;
		ID3D11DeviceContext *context = NULL;
		hr = D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0,
		                       D3D11_SDK_VERSION, &device, NULL, &context);
		if (FAILED(hr)) {
			printf("dxgi   d3d11_device_hr=0x%08lx\n", (unsigned long)hr);
			IDXGIAdapter1_Release(adapter);
			continue;
		}

		HWND hwnd = window_create(L"stereo_probe DXGI", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 360);
		DXGI_SWAP_CHAIN_DESC1 scd = {0};
		scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scd.Stereo = TRUE;
		scd.SampleDesc.Count = 1;
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scd.BufferCount = 2;
		scd.Scaling = DXGI_SCALING_STRETCH;
		scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		IDXGISwapChain1 *swapchain = NULL;
		hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)device, hwnd, &scd, NULL,
		                                         NULL, &swapchain);
		BOOL stereo_kept = FALSE;
		if (SUCCEEDED(hr)) {
			DXGI_SWAP_CHAIN_DESC1 actual;
			IDXGISwapChain1_GetDesc1(swapchain, &actual);
			stereo_kept = actual.Stereo;
		}
		r->evidence.dxgi_stereo_swapchain_created = SUCCEEDED(hr) && stereo_kept;
		printf("dxgi   stereo_swapchain_hr=0x%08lx stereo_in_desc=%d\n", (unsigned long)hr,
		       stereo_kept);


		if (swapchain != NULL) {
			IDXGISwapChain1_Release(swapchain);
		}
		DestroyWindow(hwnd);
		ID3D11DeviceContext_Release(context);
		ID3D11Device_Release(device);
		IDXGIAdapter1_Release(adapter);
	}
	IDXGIFactory2_Release(factory);
	return n;
}

static const char *
present_mode_name(VkPresentModeKHR mode)
{
	switch (mode) {
	case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
	case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
	case VK_PRESENT_MODE_FIFO_KHR: return "fifo";
	case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "fifo_relaxed";
	default: return "other";
	}
}

static void
probe_vulkan(const struct dxgi_adapter_result *dxgi, uint32_t dxgi_count)
{
	const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
	VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	                         .pApplicationName = "stereo_probe",
	                         .apiVersion = VK_API_VERSION_1_2};
	VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	                            .pApplicationInfo = &app,
	                            .enabledExtensionCount = 2,
	                            .ppEnabledExtensionNames = extensions};
	VkInstance instance;
	VkResult res = vkCreateInstance(&ici, NULL, &instance);
	if (res != VK_SUCCESS) {
		printf("vk create_instance=%d\n", res);
		return;
	}

	HWND hwnd = window_create(L"stereo_probe Vulkan", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 360);
	VkWin32SurfaceCreateInfoKHR sci = {.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
	                                   .hinstance = GetModuleHandleW(NULL),
	                                   .hwnd = hwnd};
	VkSurfaceKHR surface;
	res = vkCreateWin32SurfaceKHR(instance, &sci, NULL, &surface);
	if (res != VK_SUCCESS) {
		printf("vk create_surface=%d\n", res);
		vkDestroyInstance(instance, NULL);
		DestroyWindow(hwnd);
		return;
	}

	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance, &count, NULL);
	VkPhysicalDevice *devices = calloc(count ? count : 1, sizeof(*devices));
	vkEnumeratePhysicalDevices(instance, &count, devices);

	for (uint32_t i = 0; i < count; i++) {
		VkPhysicalDeviceProperties base;
		vkGetPhysicalDeviceProperties(devices[i], &base);
		VkPhysicalDeviceDriverProperties driver = {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
		VkPhysicalDeviceIDProperties id = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
		bool has_driver_props = base.apiVersion >= VK_API_VERSION_1_2;
		id.pNext = has_driver_props ? &driver : NULL;
		VkPhysicalDeviceProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		                                     .pNext = &id};
		vkGetPhysicalDeviceProperties2(devices[i], &props);

		uint32_t luid_high = 0, luid_low = 0;
		memcpy(&luid_low, id.deviceLUID, 4);
		memcpy(&luid_high, id.deviceLUID + 4, 4);
		printf("vk device=%u name=\"%s\" vendor=0x%04x device_id=0x%04x driver_version=0x%08x "
		       "api=%u.%u.%u luid=%08x:%08x luid_valid=%d\n",
		       i, base.deviceName, base.vendorID, base.deviceID, base.driverVersion,
		       VK_API_VERSION_MAJOR(base.apiVersion), VK_API_VERSION_MINOR(base.apiVersion),
		       VK_API_VERSION_PATCH(base.apiVersion), luid_high, luid_low, id.deviceLUIDValid);
		if (has_driver_props) {
			printf("vk   driver_name=\"%s\" driver_info=\"%s\"\n", driver.driverName,
			       driver.driverInfo);
		}

		uint32_t families = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, NULL);
		VkBool32 supported = VK_FALSE;
		for (uint32_t f = 0; f < families && !supported; f++) {
			vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], f, surface, &supported);
		}

		VkSurfaceCapabilitiesKHR caps = {0};
		res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[i], surface, &caps);
		printf("vk   surface_supported=%u caps_result=%d max_image_array_layers=%u "
		       "image_count=%u..%u current_extent=%ux%u\n",
		       supported, res, caps.maxImageArrayLayers, caps.minImageCount, caps.maxImageCount,
		       caps.currentExtent.width, caps.currentExtent.height);

		uint32_t mode_count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], surface, &mode_count, NULL);
		VkPresentModeKHR modes[16];
		if (mode_count > 16) {
			mode_count = 16;
		}
		vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], surface, &mode_count, modes);
		printf("vk   present_modes=");
		for (uint32_t m = 0; m < mode_count; m++) {
			printf("%s%s", m ? "," : "", present_mode_name(modes[m]));
		}
		printf("\n");

		const struct dxgi_adapter_result *match = NULL;
		for (uint32_t d = 0; id.deviceLUIDValid && d < dxgi_count; d++) {
			if ((uint32_t)dxgi[d].luid.HighPart == luid_high && dxgi[d].luid.LowPart == luid_low) {
				match = &dxgi[d];
			}
		}
		if (match != NULL) {
			struct stereo_evidence e = match->evidence;
			e.vk_max_image_array_layers =
			    (res == VK_SUCCESS && supported) ? caps.maxImageArrayLayers : 0;
			printf("verdict adapter=\"%ls\" dxgi_stereo_swapchain=%d dxgi_stereo_modes=%u "
			       "gl_stereo_formats=%u vk_max_image_array_layers=%u verdict=%s\n",
			       match->name, e.dxgi_stereo_swapchain_created, e.dxgi_stereo_mode_count,
			       e.gl_stereo_pixel_format_count, e.vk_max_image_array_layers,
			       stereo_verdict_name(stereo_verdict_classify(&e)));
		} else {
			printf("verdict vk_device=%u no matching DXGI adapter by LUID\n", i);
		}
	}

	free(devices);
	vkDestroySurfaceKHR(instance, surface, NULL);
	vkDestroyInstance(instance, NULL);
	DestroyWindow(hwnd);
}

int
main(int argc, char **argv)
{
	(void)argv;
	if (argc != 1) {
		fprintf(stderr, "usage: stereo_probe (no arguments; presenting is in redblue.exe)\n");
		return 64;
	}
	window_register_class();
	// DescribePixelFormat reports ICD formats only while opengl32 is loaded.
	(void)wglGetCurrentContext();
	print_os_build();
	struct dxgi_adapter_result dxgi[MAX_ADAPTERS];
	uint32_t dxgi_count = probe_dxgi(dxgi);
	probe_vulkan(dxgi, dxgi_count);
	return 0;
}

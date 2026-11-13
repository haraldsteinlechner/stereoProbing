# stereo-tools

## Quick start

1. Make the 3D display or projector the Windows **main display**
   (Settings → System → Display → "Make this my main display").
2. Turn on 3D/stereo in the GPU driver and Windows, as for any stereo app.
3. Put on the glasses and double-click each of these:
   - `test_directx.cmd`
   - `test_opengl.cmd`
   - `test_vulkan.cmd`

Each one fills the main screen for 10 s. Esc ends it early.

**Working stereo:** the left eye sees **blue** and the right eye sees **red**.
Without glasses the screen looks magenta or flickers.

**No stereo:** both eyes see the same color, usually only blue.

Requirements: Windows 10/11 x64 and the GPU driver. The exes link the C
runtime statically, so the Visual C++ Redistributable (vcredist) is **not**
needed. If Windows still reports a missing `VCRUNTIME140.dll` or `MSVCP140.dll`,
install the x64 Visual C++ Redistributable from
https://aka.ms/vs/17/release/vc_redist.x64.exe.

Write down what each eye saw for each of the three. Send that together with the
`result_*.txt` files the scripts write next to them.

---

Windows tools for checking whether a GPU/driver/display combination outputs
frame-sequential (active) stereo through DXGI, OpenGL and Vulkan.

```
build.cmd        # builds, runs the tests, writes build\dist\stereo-tools.zip
```

The zip contains `stereo_probe.exe`, `redblue.exe`, the `test_*.cmd`
scripts, `run_all.cmd`, `edid\edid_stereo.py`, this README and `LICENSE`. The exes
link the CRT statically. They need only the GPU driver (`vulkan-1.dll` ships
with it).

## On a test machine

```
run_all.cmd [--adapter=N] [--output=N]
```

This runs the probe and `redblue` for dxgi/gl/vk, each windowed
and borderless fullscreen (8 s each, with a key press before each run). Logs go to
`results\`. Watch every run through the glasses and write down what each eye
sees.

## redblue

```
redblue --list
redblue --api=dxgi|gl|vk [--adapter=N] [--output=N] [--seconds=S] [--fullscreen | --exclusive]
```

Left eye is blue, right eye is red, with vsync on. Esc or closing the window
ends the run early.

| api | stereo request |
|---|---|
| `dxgi` | D3D11 flip-model swapchain with `Stereo=TRUE`; each array slice is cleared |
| `gl` | first ICD pixel format with `PFD_STEREO \| PFD_DOUBLEBUFFER`; `GL_BACK_LEFT`/`GL_BACK_RIGHT` are cleared |
| `vk` | Win32 surface swapchain with `imageArrayLayers=2`; layer 0/1 are cleared |

- Without `--adapter`/`--output`, the test runs on the Windows main screen (the
  output whose desktop starts at 0,0).
- `--adapter`/`--output` take the DXGI indices printed by `--list`. Vulkan picks
  the device with the same LUID, because one GPU can show up twice in Vulkan
  (measured: NVIDIA).
- `--fullscreen` opens a borderless window covering the output (all APIs).
  The `test_*.cmd` scripts use it.
- `--exclusive` (dxgi only) uses exclusive fullscreen (`SetFullscreenState`).
  Measured on NVIDIA: when the window loses focus, presents return
  `DXGI_STATUS_OCCLUDED` and later `DXGI_ERROR_INVALID_CALL`.
- OpenGL uses the driver Windows assigns to the process. On hybrid laptops it
  can be the integrated GPU's driver even for a window on another GPU's screen.
  Check `renderer=` in the output.

Exit code `0` means the API accepted two-eye frames. It does **not** mean the
display shows stereo; only the glasses tell. Measured:

- D3D11 accepts stereo swapchains without stereo modes (Intel Arc).
- NVIDIA Vulkan reports 2 layers on an output-less GPU.
- NVIDIA shows only the left eye in a window and alternates only in fullscreen.

Exit `2` means the API offers no stereo (no `PFD_STEREO` format, `GL_STEREO`
false, or `maxImageArrayLayers < 2`). Exit `1` is an error, `64` is a usage
error.

## stereo_probe

```
stereo_probe
```

For each adapter, this prints DXGI stereo modes, `Stereo=TRUE` swapchain
creation, OpenGL `PFD_STEREO` formats and the Vulkan `maxImageArrayLayers` on
a Win32 surface of the same adapter (matched by LUID). Then it prints a verdict:

| verdict | meaning |
|---|---|
| `vulkan-stereo-available` | Vulkan `maxImageArrayLayers >= 2` |
| `vulkan-inconsistent-with-opengl` | the adapter's OpenGL ICD offers `PFD_STEREO` pixel formats; Vulkan reports `< 2` |
| `vulkan-inconsistent-with-dxgi` | DXGI has stereo modes on an output of the adapter and creates a `Stereo=TRUE` swapchain; Vulkan reports `< 2` |
| `no-stereo-path` | neither API offers stereo output |

D3D11 creates `Stereo=TRUE` swapchains on every adapter, with or without
windowed stereo, stereo modes or outputs (measured: AMD W7600, Intel Arc,
NVIDIA laptop dGPU). Swapchain creation alone therefore does not count as
stereo support.

OpenGL formats are counted per output, on a window placed on that output, and
only when `GL_RENDERER` names that output's adapter. Measured: a window on the
Intel output gets the AMD renderer on the W7600 box (GPU preference routes
OpenGL to AMD).

## EDID stereo patcher

`edid/edid_stereo.py` takes a monitor's EDID and writes variants that declare
stereo support. With such an EDID, Windows and the driver may offer stereo
modes and the Windows 3D toggle on a display that has no stereo EDID of its
own. Requires Python via [uv](https://docs.astral.sh/uv/).

Variants:

- `hdmi3d`: in the CTA extension, non-HDMI vendor blocks are replaced by an
  HDMI 1.4 VSDB with `3D_present=1`.
- `dtd-stereo`: the stereo bits of base DTD 1 are set to field sequential.

Example (PowerShell):

```powershell
# 1. Find the monitor's instance id
Get-PnpDevice -Class Monitor -PresentOnly | Select-Object FriendlyName, InstanceId

# 2. Export its EDID
$id = "DISPLAY\ABC1234\1&2345678&0&UID256"   # InstanceId from step 1
[IO.File]::WriteAllBytes("$PWD\monitor.bin",
    (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Enum\$id\Device Parameters").EDID)

# 3. Write the variants and .reg files
uv run --no-project edid\edid_stereo.py monitor.bin out --instance $id
```

This writes `out\monitor_<variant>.bin` plus `_apply.reg` and `_revert.reg`
for each variant. Use them in one of two ways:

- Load the `.bin` into the GPU driver's EDID emulation, if it has one.
- Double-click `_apply.reg` to set the Windows `EDID_OVERRIDE` key. This needs
  admin rights and takes effect after replugging the monitor or rebooting.
  `_revert.reg` removes the override.

A monitor that cannot show the advertised modes may go black in them. Revert
in that case.

Tests: `uv run --no-project --with pytest python -m pytest -q edid`

## Credits and license

This project was made with AI assistance by Claude (Anthropic).

MIT License, see `LICENSE`.

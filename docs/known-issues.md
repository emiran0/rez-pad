# Known issues and limitations

Things that are known but deliberately not fixed yet. Review this list before the first full release.

## Host portability (logged 2026-11-07, after Phase 7)

The host has only been tested on one PC: Ryzen 5 5600X, RTX 3070 Ti, ASUS TUF B450M-PRO GAMING, Windows 11 (English). Nothing in it is specific to that hardware, but these paths are untested or known to be weak:

| # | Issue | Effect | Likely fix |
|---|---|---|---|
| 1 | GPU choice without NVML: `GpuEngines.Open` picks the render adapter with the most engine nodes | On AMD/Intel laptops with integrated + discrete GPUs it may pick the integrated one, so `sys.gpu.load` and the engine loads show the wrong GPU | Prefer the adapter with the most dedicated video memory (D3DKMT segment info or DXGI), let the config pick |
| 2 | Only the first NVIDIA GPU (NVML index 0) | Multi-GPU PCs show GPU 0 only | GPU selection in the config |
| 3 | Network filter only skips adapters named "Virtual", "Hyper-V" or "vEthernet" | VPN adapters (WireGuard, Tailscale, OpenVPN TAP) can count traffic twice | Skip adapters without a physical media type, or let the config pick adapters |
| 4 | CPU counters are opened by their English names | Untested on non-English Windows (falls back to GetSystemTimes, which matches "% Processor Time" rather than Task Manager's "% Processor Utility") | Test on a localized Windows; use PdhAddEnglishCounter if needed |
| 5 | AMD/Intel GPUs use LibreHardwareMonitor for temperature, power, fan | Works, but costs about 33 ms CPU per second (measured on NVIDIA through LibreHardwareMonitor), still under the 1 % target | ADLX for AMD, or refresh those sensors less often |
| 6 | Intel CPU temperature/power, laptop embedded controllers, other SuperIO chips | Untested (sensor names for Intel are in the lookup list) | Test on other machines; `/api/sources` and `/api/hardware` output shows what is found |
| 7 | Hot spot temperature on NVIDIA needs LibreHardwareMonitor | Opening it costs memory and CPU; only done when the layout uses `sys.gpu.hotspot_temp` | None needed unless it becomes popular |
| 8 | Six motherboard temperatures (IT8655E) have no names | Not offered as sources | Board-specific names, or offer them as "Motherboard temperature n" |

Quick check on a new PC: run the host, then save `http://127.0.0.1:47800/api/sources` and `/api/hardware`.

## Display

- **Resolved 2026-11-07: white/grey screen after a cold plug-in.** Seen once with the display on 3V3 (its U1 regulator starved, about 3.0 V out): native USB plugged in first, backlight on but no image while the firmware ran; RST fixed it. After moving display VCC to 5V (`IN-OUT` bridged), 5 of 5 cold plug-ins with the native cable first came up normally. If it ever comes back: delay the panel init after a power-on reset and/or re-run it once (the ILI9488 cannot be read back, MISO is not connected).

## Firmware

- **Full page redraw about 153 ms** (target 120 ms). Measured with `page <n>` on the System and Weather pages at 40 MHz SPI; the page switch still looks instant on the hardware. The flush waits for each strip to go out over SPI before LVGL renders the next one. Fix if it matters: convert RGB565 to RGB666 into a separate DMA buffer and return from the flush right away (see measurements.md, "UI rendering").
- **Animated images cost SPI time**: each frame redraws the image area over the 40 MHz SPI link (about 25 ms for a 234x144 frame). Several large GIFs on one page at 20 fps would keep the link busy; frames are limited to one per 50 ms or slower.
- **Asset transfer about 57 KB/s** (Arduino `USBCDC` byte queue, see below): a 1 MB animation takes about 20 s to reach the device the first time. Faster transfers need direct TinyUSB reads.
- **No light theme**: the four presets are dark; the weather icons are drawn for dark cards.
- **Night dimming and "screen off while the PC sleeps"** are in the config and the editor but not applied by the firmware until Phase 11.
- **Unexplained restart on 2026-11-08 around 01:21**: seen afterwards from the sensor sample count; the host log showed no link drop (the host was being restarted at that time) and the reset reason was not recorded yet. `info` now prints the reset reason; check it if the device restarts again. The AHT20 has had a loose contact before.

## Buttons and macros

- **Device shortcuts are key positions**, like a physical keyboard: on a Turkish Q layout `ctrl+i` presses the key where `i` is on a US keyboard (which is `ı` there). The editor records positions too, so recording a shortcut gives what was pressed. Typed text belongs in a macro (the host types characters, layout-independent).
- **Macros cannot type into the Windows secure desktop** (UAC prompts, the login and lock screen): SendInput does not reach it. Device shortcuts (USB keyboard) do.
- **"Open" steps lose their arguments while the host runs elevated** (started through Explorer so programs do not inherit administrator rights). A shortcut file (`.lnk`) with arguments works.
- **Windows may keep the old USB descriptor** after the keyboard was added; if the shortcuts do nothing, unplug and replug the native port once.
- **The editor cannot record F13 to F24, Win shortcuts Windows grabs (Win+L, Win+D), or Print Screen** in every browser; F13 to F24 have buttons under the recorder.

## USB and development setup

- **30 s COM close / COM4 wedge** behind the Genesys USB 2.0 hub (`05E3:0610`). Plug the native port straight into the PC. Details in `measurements.md`.
- **Art transfer about 80 KB/s** through Arduino's `USBCDC` byte queue. Enough for art and config; replace with direct TinyUSB reads if bigger transfers ever matter.
- **Host log from a Claude desktop session:** the app is MSIX-packaged, so its child processes see a private copy of `%LOCALAPPDATA%`. Read the elevated host's log through `GET /api/log`.

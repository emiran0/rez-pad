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

- **White/grey screen after a cold plug-in (2026-11-07, seen once).** Plugging in the native USB cable first (board unpowered before): the backlight came on but the screen stayed white with no image, while the firmware ran normally (host connected). Pressing RST fixed it. The display was on 3V3 (its U1 regulator starved, about 3.0 V out). Suspects: contact bounce while plugging in drops the display but not the ESP32, or panel init before the display supply is stable. Recheck after moving display VCC to 5 V; if it still happens, delay the panel init after a power-on reset and/or re-run it once (the ILI9488 cannot be read back, MISO is not connected).

## USB and development setup

- **30 s COM close / COM4 wedge** behind the Genesys USB 2.0 hub (`05E3:0610`). Plug the native port straight into the PC. Details in `measurements.md`.
- **Art transfer about 80 KB/s** through Arduino's `USBCDC` byte queue. Enough for art and config; replace with direct TinyUSB reads if bigger transfers ever matter.
- **Host log from a Claude desktop session:** the app is MSIX-packaged, so its child processes see a private copy of `%LOCALAPPDATA%`. Read the elevated host's log through `GET /api/log`.

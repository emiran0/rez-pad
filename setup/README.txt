rez-pad setup guide (Windows 10/11, 64-bit)
===========================================

What is in this zip
-------------------
  rez-pad.exe        The host app (tray app + config page). Self-contained: no .NET install needed.
                     Do not run it from here; Install.cmd copies it to its install folder.
  Install.cmd        Installs or updates the host app. Double-click it.
  Uninstall.cmd      Removes the host app (your settings are kept).
  install.ps1        The script behind both .cmd files (no need to open it).
  firmware\rez-pad-firmware-<version>.bin
                     The device firmware as one full flash image, for esptool or the web flasher.
                     Not needed normally: rez-pad.exe has it built in (Device page).
  README.txt         This guide.
  CHANGELOG.txt      What is new in this version.
  LICENSE.txt, THIRD-PARTY-NOTICES.txt
                     rez-pad is open source (MIT); these list its license and the bundled
                     components' licenses.


The two USB-C ports on the board
--------------------------------
The ESP32-S3 board has two USB-C ports. They do different jobs:

  NATIVE port   The port the rez-pad runs from: power, data to the host app, media keys and
                key shortcuts. This is the only cable the finished device needs.
                Usually labelled "USB" on the board.
  UART port     A USB-to-serial bridge (WCH CH343 chip). Used for flashing firmware and for
                debug logs. Usually labelled "COM" or "UART".

If the labels are missing or unclear, plug one cable in and open Device Manager:
  - "Ports (COM & LPT)" shows "USB-Enhanced-SERIAL CH343 (COMx)"  -> that is the UART port.
  - It shows "USB Serial Device (COMx)" and new "HID" entries appear -> that is the native port
    (once the rez-pad firmware is on the board).
On a board that has never been flashed with rez-pad, the native port shows up as Espressif's
built-in "USB JTAG/serial debug unit" (with its own COM port) instead.

Plug the native port straight into a USB port of the PC, not through a USB 2.0 hub
(a shared hub can make the COM port hang for 30 s on close).


Before you start
----------------
Right-click the zip > Properties > tick "Unblock" (if shown) > OK, then extract it
(right-click > Extract All). Unblocking avoids extra Windows security warnings for each file.
Windows SmartScreen may still say "Windows protected your PC" for rez-pad.exe because it is
not signed: click "More info" > "Run anyway".


Step 1: Install the host app
----------------------------
1. Double-click Install.cmd. A console window opens and asks a few questions (press Enter for
   the default shown in capitals).
2. It copies rez-pad.exe to %LOCALAPPDATA%\Programs\rez-pad, adds a "rez-pad" Start menu
   entry and an entry in Settings > Apps. No administrator rights needed for this.
3. "Start with Windows?" (default yes): creates a task that starts rez-pad at every logon
   with administrator rights. Windows asks for permission once now (UAC), never at logon.
   CPU temperature and motherboard fans need these rights. Without the task, start rez-pad
   from the Start menu yourself; GPU, loads, media and weather still work.
4. If the PawnIO driver is missing, it says so and offers to open https://pawnio.eu/.
   PawnIO is what CPU temperature and motherboard fans are read through (needs Start with
   Windows as well). Install it from there if you want those values; it can be done later.
5. It starts rez-pad and opens the config page in your browser: http://127.0.0.1:47800
6. Press a key to close the console window.

rez-pad now runs in the tray (notification area; it may be under the "^" arrow). The icon
has a blue screen when the device is connected and a grey one when it is not. Double-click
it for the config page; right-click for the menu (status, config page, log, Start with
Windows, Exit).


Step 2: Plug in the board and install the firmware from the config page
-----------------------------------------------------------------------
Plug the board's NATIVE port into the PC (directly, see above). Windows installs it by itself,
no driver needed.

On the config page, open Device (top bar). The Firmware card shows what the board runs and
the firmware built into this rez-pad:
  - A new board (or one running something else) shows as "An ESP32 board without rez-pad".
    Click "Install rez-pad" and confirm. It takes about half a minute.
  - A board that already runs rez-pad shows its version; "Update the board" appears when this
    rez-pad has a newer one.
When it is done the board restarts with the default pages, connects (top bar: "Connected")
and gets your layout. If the board does not show up at all, hold its BOOT button while you
plug the native cable in (that starts the chip's built-in update mode), then try again.

The update never touches the storage area with the board's layout and pictures. If it is cut
off (cable pulled), the board waits in update mode: just start it again.


Other ways to flash the firmware (optional)
-------------------------------------------
The firmware is also in this zip as one full flash image, for the usual ESP32 tools:

1. Plug a USB-C cable into the board's UART port and into the PC. (The native port may stay
   plugged in or not; it does not matter for flashing.)
   Windows 10/11 normally sets up the CH343 port by itself. If no new COM port appears in
   Device Manager, install the CH343 driver from WCH (wch-ic.com).
2. Open the web flasher in Chrome or Edge (Firefox cannot do this):
       https://espressif.github.io/esptool-js/
3. Baud rate 921600, click "Connect" and pick the CH343 COM port.
4. Under "Flash Address" enter 0x0 and choose firmware\rez-pad-firmware-<version>.bin.
   Do NOT tick or run "Erase Flash" unless you want to wipe the device's saved layout and
   pictures.
5. Click "Program" and wait until it reports that it is done (about 20 s).
6. Press the board's RST (reset) button, or unplug and replug it. The screen comes on with the
   default pages and "No host connected".
7. You can unplug the UART cable now.

If "Connect" fails or hangs: hold the BOOT button, press and release RST, release BOOT, then
try again (this forces the chip into its download mode).

Alternative with Python installed:
    python -m pip install esptool
    python -m esptool --chip esp32s3 --port COMx --baud 921600 write_flash 0x0 firmware\rez-pad-firmware-<version>.bin
(COMx = the CH343 port.)

What gets written: bootloader, partition table and the app, starting at address 0x0. The
storage area with the device's layout and pictures is not touched, so updating keeps them.

Once the rez-pad firmware runs, the native port shows up in Windows as a COM port ("USB
Serial Device"), media keys and a keyboard device, all named "rez-pad". The device works on
its own (room climate, media keys on the knobs) and shows "No host connected" while the host
app is not running.


Step 3: Set it up on the config page
------------------------------------
1. Settings > Weather and units: search for your city and pick it, and choose the units.
2. Settings > Screen: brightness, night dimming (from/to and night brightness), and whether
   the screen turns off while the PC sleeps.
3. Settings > Knobs and buttons: which physical knob or button is Knob 1, Button 1 and so on
   (click Assign, then turn or press it on the device), and what turning, clicking and
   long-pressing each one does.
4. Layout: pick a page on the left, add widgets from the gallery, drag them around, resize
   them from the corner.
5. "Save to device" (Ctrl+S). The layout is stored on the rez-pad, so it starts with it even
   when the PC or the host app is off.


Updating
--------
Host app: extract the new zip and run its Install.cmd. It closes the running rez-pad,
replaces it and starts it again. Settings (%APPDATA%\rez-pad) are kept.
Firmware: after updating the host app, open Device on the config page and click "Update the
board" (the new host has the matching firmware built in).

Removing
--------
Settings > Apps > rez-pad > Uninstall, or Uninstall.cmd. It closes rez-pad, removes the
startup task (one UAC prompt), the Start menu entry and the program folder. Your settings stay
in %APPDATA%\rez-pad; run "Uninstall.cmd -RemoveSettings" from a console to delete them too.

Troubleshooting
---------------
- Tray says "Looking for rez-pad...": the native port is not plugged in, or the board runs
  firmware without rez-pad (do step 2), or the cable is charge-only.
- Screen dark: touch a knob (it may be off for the night, the PC sleeps, or it was turned off
  with a knob action).
- Host log: right-click the tray icon > Open log file.

// Firmware update hub (Device > Firmware): shows the board's firmware next to the one built into
// this rez-pad, and updates the board over the same USB cable (/api/firmware). A .bin file can be
// installed instead (a newer or older release, or a board in update mode that has no rez-pad yet).
"use strict";

import { state, h, api, toast, emit, fmtVersion } from "./state.js";
import { icon } from "./icons.js";

let info = null;       // last GET /api/firmware
let timer = 0;
let box = null;        // the panel body while it is on screen

/** Polls while the Device view shows the panel or an update runs (fast while it runs). */
export async function pollFirmware() {
  clearTimeout(timer);
  const r = await api("/api/firmware").catch(() => null);
  const was = info?.status?.active;
  info = r?.ok ? r.body : null;
  state.data.firmware = info;
  state.data.flash = info?.status;
  if (was && !info?.status?.active && info?.status) {
    if (info.status.done) toast(`Firmware ${shortVer(info.status.version) || ""} installed. ${info.status.message || ""}`.replace(/\s+/g, " "));
    else if (info.status.error) toast(`Firmware update failed: ${info.status.error}`, true);
  }
  render();
  emit("firmware");
  const visible = box && box.isConnected && !document.hidden;
  if (info?.status?.active || visible) timer = setTimeout(pollFirmware, info?.status?.active ? 500 : 3000);
}

const shortVer = fmtVersion;

async function start(file) {
  const what = file ? file.name : `rez-pad ${shortVer(info?.bundled?.version) || ""}`;
  const foreign = !info?.device?.connected && info?.bootloader_port;
  const question = foreign
    ? `Install ${what} on the ESP32 board on ${info.bootloader_port}?\n\nThis replaces whatever that board runs now: only do it for your rez-pad board (an ESP32-S3 N16R8). It takes about half a minute; keep the USB cable plugged in.`
    : `Install ${what} on the board?\n\nIt takes about half a minute; the screen goes dark meanwhile. Keep the USB cable plugged in. Your layout, settings and pictures stay on the board.`;
  if (!confirm(question)) return;
  let r;
  if (file) {
    const form = new FormData();
    form.append("file", file);
    r = await api("/api/firmware/update", { method: "POST", body: form }).catch(() => null);
  } else {
    r = await api("/api/firmware/update", { method: "POST" }).catch(() => null);
  }
  if (!r?.ok) {
    toast(r?.body?.error || "The host is not reachable.", true);
    return;
  }
  pollFirmware();
}

function render() {
  if (!box || !box.isConnected) return;
  const st = info?.status;
  const dev = info?.device;
  const bundled = info?.bundled;
  const busy = !!st?.active;
  const file = h("input", { type: "file", accept: ".bin,application/octet-stream", hidden: true, onchange: (e) => e.target.files[0] && start(e.target.files[0]) });
  const canUpdate = !busy && (dev?.connected || info?.bootloader_port);

  let headline;
  let tone = "";
  if (!info) {
    headline = "The host is not reachable.";
  } else if (busy) {
    headline = st.stage;
    tone = "busy";
  } else if (!dev?.connected && info.bootloader_port) {
    headline = `An ESP32 board without rez-pad (or in update mode) is on ${info.bootloader_port}.`;
    tone = "warn";
  } else if (!dev?.connected) {
    headline = "Connect the board to see and update its firmware.";
  } else if (info.update_available) {
    headline = `Update available: ${shortVer(bundled.version)}`;
    tone = "warn";
  } else {
    headline = "The board's firmware is up to date.";
    tone = "ok";
  }

  const versions = h("div", { class: "fw-versions" },
    h("div", {}, h("small", { text: "On the board" }), h("strong", { text: dev?.connected ? shortVer(dev.version) : info?.bootloader_port ? "update mode" : "--" })),
    h("div", { class: "fw-arrow" }, icon("arrow", 18)),
    h("div", {}, h("small", { text: "Built into this rez-pad" }), h("strong", { text: bundled ? shortVer(bundled.version) || "unknown version" : "none" })));

  const progress = busy || st?.error || st?.done
    ? h("div", { class: "fw-progress" + (st.error && !busy ? " bad" : st.done && !busy ? " ok" : "") },
      h("div", { class: "fw-bar" }, h("i", { style: { width: `${Math.round((busy ? st.progress : st.done ? 1 : st.progress) * 100)}%` } })),
      h("p", { text: busy ? `${st.message || ""}` : st.error ? `Failed: ${st.error}. ${st.message || ""}` : `Done. ${st.message || ""}` }))
    : null;

  box.replaceChildren(
    h("div", { class: "fw-head " + tone }, h("span", { class: "fw-dot" }), h("strong", { text: headline })),
    versions,
    progress,
    h("div", { class: "row" },
      h("button", {
        type: "button", class: "primary", disabled: !canUpdate || !bundled,
        title: bundled ? "" : "This build of rez-pad has no firmware inside; choose a file",
        onclick: () => start(null),
      }, icon("flash", 16), info?.update_available ? "Update the board" : !dev?.connected && info?.bootloader_port ? "Install rez-pad" : "Reinstall this version"),
      h("button", { type: "button", disabled: !canUpdate, onclick: () => file.click() }, "Install from a file..."),
      file),
    h("p", { class: "hint", text: "Updates go over the same USB cable and keep your layout, settings and pictures. If an update is cut off, the board waits in update mode and you can start it again. A board that never had rez-pad (or does not start) can be put in update mode by holding BOOT while plugging it in." }));
}

/** The panel body (re-rendered by every poll; the app bar follows the "firmware" event). */
export function firmwarePanel() {
  box = h("div", { class: "fw" });
  render();
  pollFirmware();
  return box;
}

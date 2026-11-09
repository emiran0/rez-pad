// rez-pad config editor: loads the config, catalog and images, polls live data, validates while
// editing and saves (the host validates, writes and sends it to the device; docs/config-schema.md).
"use strict";

import { state, on, emit, dirty, reset, api, postJson, h, toast, undo, redo, canUndo, canRedo, closePopover } from "./state.js";
import { initEditor, renderAll, selectPage, loadAssets } from "./editor.js";
import { initSettings, renderSettings, renderDevice } from "./settings.js";
import { icon } from "./icons.js";
import { updateSpectrum } from "./preview.js";
import { pollFirmware } from "./firmware.js";

const $ = (id) => document.getElementById(id);
const HISTORY = 120;

// ---- loading ----

async function loadConfig() {
  const r = await api("/api/config");
  if (!r.ok) throw new Error(`config: ${r.status}`);
  reset(r.body);
  state.baseCrc = r.headers.get("X-Config-Crc");
  state.error = null;
  state.page = Math.max(0, Math.min(state.page, state.cfg.pages.length - 1));
  state.selected = -1;
  renderAll();
  renderSettings();
  renderDevice();
  updateBar();
}

async function start() {
  try {
    const c = await api("/api/catalog");
    if (!c.ok) throw new Error("catalog");
    state.catalog = c.body;
    await Promise.all([pollLive(), pollSlow(), pollStatus(), loadAssets()]);
    await loadConfig();
  } catch (e) {
    state.data.status = null;
    renderPill();
    console.error(e);
    setTimeout(start, 3000);
    return;
  }
  setInterval(() => !document.hidden && pollLive(), 1000);
  setInterval(() => !document.hidden && pollStatus(), 1500);
  setInterval(() => !document.hidden && pollSlow(), 5000);
  // Firmware: the pill says when an update is available (the Device view polls faster).
  pollFirmware();
  setInterval(() => !document.hidden && pollFirmware(), 30000);
  pollSpectrum();
}

// ---- live data ----

async function pollStatus() {
  const r = await api("/api/status").catch(() => null);
  state.data.status = r?.ok ? r.body : null;
  state.data.connected = !!state.data.status?.deviceConnected;
  renderPill();
  emit("status");
}

// The device pill in the app bar: one dot color and two short lines.
export function renderPill() {
  const s = state.data.status;
  const [st, main, sub] = pillState(s);
  const pill = $("status-chip");
  pill.dataset.state = st;
  $("pill-main").textContent = main;
  $("pill-sub").textContent = sub;
  pill.title = sub ? `${main}: ${sub}` : main;
  document.getElementById("brand-logo")?.classList.toggle("on", !!s?.deviceConnected);
}

function pillState(s) {
  if (!s) return ["bad", "Host not running", "Start rez-pad on this PC"];
  if (state.data.flash?.active) return ["busy", "Updating firmware", state.data.flash.message || ""];
  if (s.linkState === "Incompatible") return ["bad", "Incompatible firmware", "Update it under Device"];
  if (!s.deviceConnected) {
    return [s.linkState === "Connecting" ? "wait" : "off", s.linkState === "Connecting" ? "Connecting..." : "Not connected",
      s.linkState === "Searching" ? "Plug in the native USB port" : ""];
  }
  if (s.config?.progress) return ["busy", "Sending", s.config.progress];
  const update = state.data.firmware?.update_available ? " · update available" : "";
  return s.config?.inSync ? ["ok", "Connected", `Up to date${update}`] : ["warn", "Connected", `Layout not on the device yet${update}`];
}

async function pollLive() {
  const wantMixer = !!document.querySelector(".mixer-prev");
  const [src, media, mixer] = await Promise.all([api("/api/sources").catch(() => null), api("/api/media").catch(() => null),
    wantMixer ? api("/api/mixer").catch(() => null) : null]);
  if (mixer?.ok) state.data.mixer = mixer.body.apps;
  if (src?.ok) {
    for (const s of src.body.sources) {
      state.data.sources.set(s.id, s);
      if (s.available) {
        const list = state.data.history.get(s.id) || [];
        list.push(s.value);
        if (list.length > HISTORY) list.shift();
        state.data.history.set(s.id, list);
      }
    }
  }
  if (media?.ok) {
    const prev = state.data.media?.now_playing?.track_id;
    state.data.media = media.body;
    const np = media.body.now_playing;
    if (np.track_id !== prev || (!state.data.artUrl && media.body.art.source_bytes > 0)) {
      state.data.artUrl = np.state !== "none" && media.body.art.source_bytes > 0 ? `/api/media/art?size=192&t=${np.track_id}` : null;
    }
  }
  emit("live");
}

// Live spectrum for spectrum previews, only while one is on screen (the host captures only when the
// saved layout has a spectrum widget and the device is connected).
async function pollSpectrum() {
  if (!document.hidden && document.querySelector(".spec")) {
    const r = await api("/api/spectrum").catch(() => null);
    if (r?.ok) {
      state.data.spectrum = r.body.capturing ? r.body.bins : null;
      updateSpectrum(state.data.spectrum);
    }
  }
  setTimeout(pollSpectrum, 66);
}

async function pollSlow() {
  const [wx, room] = await Promise.all([api("/api/weather").catch(() => null), api("/api/sensor").catch(() => null)]);
  if (wx?.ok) state.data.weather = wx.body;
  if (room?.ok) state.data.room = room.body.reading;
  emit("live");
}

// ---- validation and saving ----

let validateTimer = 0;
let validateSeq = 0;

function scheduleValidate() {
  clearTimeout(validateTimer);
  validateTimer = setTimeout(async () => {
    const seq = ++validateSeq;
    const r = await postJson("/api/config/validate", state.cfg).catch(() => null);
    if (seq !== validateSeq || !r?.ok) return;
    state.error = r.body.error;
    updateBar();
  }, 250);
}

// "pages[2].widgets[1].source: ..." -> {page: 2, widget: 1}; macros and inputs -> a Settings panel.
function errorTarget(error) {
  const m = /^pages\[(\d+)\](?:\.widgets\[(\d+)\])?/.exec(error || "");
  if (m) return { page: Number(m[1]), widget: m[2] === undefined ? -1 : Number(m[2]) };
  if (/^macros/.test(error || "")) return { panel: "Macros" };
  if (/^inputs/.test(error || "")) return { panel: "Knobs and buttons" };
  return null;
}

// Plain words for the start of a validator path: "pages[1].widgets[2].source: x" becomes
// "\"System\", metric gauge 3 (source): x".
function friendly(error) {
  const field = (f) => (f ? ` (${f.replace(/_/g, " ")})` : "");
  return error.replace(/^pages\[(\d+)\]\.widgets\[(\d+)\](?:\.(\w+))?/, (_, p, w, f) => {
    const page = state.cfg.pages[Number(p)];
    const wd = page?.widgets?.[Number(w)];
    return `"${page?.name || page?.id}", ${wd?.type?.replace(/_/g, " ") || "widget"} ${Number(w) + 1}${field(f)}`;
  }).replace(/^pages\[(\d+)\]/, (_, p) => `"${state.cfg.pages[Number(p)]?.name || "page " + (Number(p) + 1)}"`)
    .replace(/^macros\[(\d+)\](?:\.steps\[(\d+)\])?(?:\.(\w+))?/, (_, m, st, f) => {
      const mac = state.cfg.macros?.[Number(m)];
      return `macro "${mac?.name || mac?.id || Number(m) + 1}"${st === undefined ? "" : `, step ${Number(st) + 1}`}${field(f)}`;
    })
    // A widget just added and not set up yet (no data source or machine picked).
    .replace(/ \(source\): required for \w+$/, ": pick the value it shows")
    .replace(/ \(system\): required.*$/, ": pick a machine");
}

function updateBar() {
  const changed = dirty();
  $("btn-save").disabled = !changed || !!state.error;
  $("btn-undo").disabled = !canUndo();
  $("btn-redo").disabled = !canRedo();
  const banner = $("banner");
  if (state.error) {
    const t = errorTarget(state.error);
    banner.replaceChildren(...[icon("warn", 18), h("span", { class: "msg", text: `Can't save yet: ${friendly(state.error)}` }),
      t ? h("button", { type: "button", class: "ghost", onclick: () => showTarget(t) }, "Show me") : null].filter(Boolean));
    banner.hidden = false;
  } else {
    banner.hidden = true;
  }
  document.title = changed ? "rez-pad • unsaved" : "rez-pad";
}

function showTarget(t) {
  if (t.panel) {
    showView("settings");
    [...document.querySelectorAll(".panel h2")].find((x) => x.textContent === t.panel)?.scrollIntoView({ behavior: "smooth" });
    return;
  }
  showView("layout");
  selectPage(t.page);
  if (t.widget >= 0) {
    state.selected = t.widget;
    emit("selection");
  }
}

async function save() {
  if ($("btn-save").disabled) return;
  closePopover();
  const btn = $("btn-save");
  btn.disabled = true;
  btn.textContent = "Saving...";
  const r = await api(`/api/config?base=${state.baseCrc}`, {
    method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(state.cfg),
  }).catch(() => null);
  btn.textContent = "Save to device";
  if (!r) {
    toast("The host is not reachable; nothing was saved.", true);
  } else if (r.status === 409) {
    toast("The config was changed somewhere else since this page loaded.", true);
    if (confirm("The config was changed somewhere else (another tab, or the weather location). Load that version? Your changes here are dropped.")) await loadConfig();
  } else if (!r.ok) {
    state.error = r.body?.error || `save failed (${r.status})`;
  } else {
    const keep = { page: state.page, selected: state.selected };
    await loadConfig();
    selectPage(keep.page);
    const d = r.body.device;
    if (d === null) toast("Saved. The device gets it when it connects.");
    else if (d.ok) toast(`Saved and shown on the device${d.imagesSent ? ` with ${d.imagesSent} image${d.imagesSent > 1 ? "s" : ""}` : ""} (${(d.ms / 1000).toFixed(1)} s).`);
    else toast(`Saved on this PC, but the device did not take it: ${d.error}`, true);
    pollStatus();
  }
  updateBar();
}

// ---- views ----

function showView(name) {
  for (const b of document.querySelectorAll("[data-view]")) {
    const active = b.dataset.view === name;
    b.classList.toggle("active", active);
    b.setAttribute("aria-selected", String(active));
    $(`view-${b.dataset.view}`).hidden = !active;
  }
  closePopover();
  if (name === "device") renderDevice();
  try {
    localStorage.setItem("rezpad.view", name);
  } catch {
    // storage blocked: the layout view opens next time
  }
}

function initViews() {
  for (const b of document.querySelectorAll("[data-view]")) {
    b.prepend(icon(b.dataset.icon, 16));
    b.addEventListener("click", () => showView(b.dataset.view));
  }
  $("btn-undo").append(icon("undo", 18));
  $("btn-redo").append(icon("redo", 18));
  $("status-chip").addEventListener("click", () => showView("device"));
  document.querySelector(".brand").addEventListener("click", (e) => {
    e.preventDefault();
    showView("layout");
  });
  let last = "layout";
  try {
    last = localStorage.getItem("rezpad.view") || "layout";
  } catch {
    last = "layout";
  }
  showView(["layout", "settings", "device"].includes(last) ? last : "layout");
}

// ---- start ----

initViews();
initEditor();
initSettings();
on("config", () => {
  updateBar();
  scheduleValidate();
});
on("firmware", renderPill);
$("btn-save").addEventListener("click", save);
$("btn-undo").addEventListener("click", () => undo());
$("btn-redo").addEventListener("click", () => canRedo() && redo());
document.addEventListener("keydown", (e) => {
  const typing = /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement?.tagName);
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
    e.preventDefault();
    save();
  } else if ((e.ctrlKey || e.metaKey) && !typing && e.key.toLowerCase() === "z" && !e.shiftKey) {
    e.preventDefault();
    undo();
  } else if ((e.ctrlKey || e.metaKey) && !typing && (e.key.toLowerCase() === "y" || (e.key.toLowerCase() === "z" && e.shiftKey))) {
    e.preventDefault();
    if (canRedo()) redo();
  }
});
window.addEventListener("beforeunload", (e) => {
  if (dirty()) e.preventDefault();
});
start();

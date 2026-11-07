// rez-pad config editor: loads the config and catalog, polls live data, validates while editing
// and saves (host validates, writes and pushes to the device; docs/config-schema.md).
"use strict";

import { state, on, emit, dirty, api, postJson, h } from "./state.js";
import { initEditor, renderAll, selectPage } from "./editor.js";
import { initSettings, renderSettings, linkText } from "./settings.js";

const $ = (id) => document.getElementById(id);
const HISTORY = 120;

// ---- loading ----

async function loadConfig() {
  const r = await api("/api/config");
  if (!r.ok) throw new Error(`config: ${r.status}`);
  state.cfg = r.body;
  state.savedJson = JSON.stringify(r.body);
  state.baseCrc = r.headers.get("X-Config-Crc");
  state.error = null;
  state.page = Math.min(state.page, state.cfg.pages.length - 1);
  state.selected = -1;
  renderAll();
  renderSettings();
  updateSaveBar();
}

async function start() {
  try {
    const c = await api("/api/catalog");
    state.catalog = c.body;
    await Promise.all([pollLive(), pollSlow(), pollStatus()]);
    await loadConfig();
  } catch (e) {
    $("save-msg").textContent = "The rez-pad host is not reachable. Is it running?";
    console.error(e);
    setTimeout(start, 3000);
    return;
  }
  setInterval(() => !document.hidden && pollLive(), 1000);
  setInterval(() => !document.hidden && pollStatus(), 2000);
  setInterval(() => !document.hidden && pollSlow(), 5000);
}

// ---- live data ----

async function pollStatus() {
  const r = await api("/api/status").catch(() => null);
  state.data.status = r?.ok ? r.body : null;
  state.data.connected = !!state.data.status?.deviceConnected;
  const s = state.data.status;
  const dev = $("pill-device");
  dev.textContent = linkText(s);
  dev.className = "pill " + (state.data.connected ? "ok" : "bad");
  const sync = $("pill-sync");
  const synced = s?.config?.inSync;
  sync.hidden = !state.data.connected;
  sync.textContent = synced ? "Device up to date" : "Device not up to date";
  sync.className = "pill " + (synced ? "ok" : "warn");
  emit("status");
}

async function pollLive() {
  const [src, media] = await Promise.all([api("/api/sources").catch(() => null), api("/api/media").catch(() => null)]);
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
    if (np.track_id !== prev) {
      state.data.artUrl = np.state !== "none" && media.body.art.source_bytes > 0 ? `/api/media/art?size=192&t=${np.track_id}` : null;
    }
  }
  emit("live");
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
    state.bytes = r.body.bytes;
    updateSaveBar();
  }, 250);
}

// "pages[2].widgets[1].source: ..." -> {page: 2, widget: 1}
function errorTarget(error) {
  const m = /^pages\[(\d+)\](?:\.widgets\[(\d+)\])?/.exec(error || "");
  return m ? { page: Number(m[1]), widget: m[2] === undefined ? -1 : Number(m[2]) } : null;
}

function updateSaveBar() {
  const changed = dirty();
  const bar = $("savebar");
  bar.classList.toggle("changed", changed);
  $("btn-save").disabled = !changed || !!state.error;
  $("btn-revert").disabled = !changed;
  const msg = $("save-msg");
  if (state.error) {
    const t = errorTarget(state.error);
    msg.replaceChildren(h("span", { class: "crit", text: `Cannot save: ${state.error}` }),
      t ? h("button", { type: "button", class: "link", onclick: () => showTarget(t) }, "Show") : null);
  } else if (changed) {
    msg.textContent = "Unsaved changes.";
  } else if (!msg.dataset.keep) {
    msg.textContent = "All changes saved.";
  }
  delete msg.dataset.keep;
}

function showTarget(t) {
  selectPage(t.page);
  document.querySelector('[data-tab="layout"]').click();
  if (t.widget >= 0) {
    state.selected = t.widget;
    emit("selection");
  }
}

async function save() {
  const msg = $("save-msg");
  $("btn-save").disabled = true;
  msg.textContent = "Saving...";
  const r = await api(`/api/config?base=${state.baseCrc}`, {
    method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(state.cfg),
  }).catch(() => null);
  if (!r) {
    msg.textContent = "The host is not reachable; nothing saved.";
    updateSaveBar();
    return;
  }
  if (r.status === 409) {
    msg.replaceChildren(h("span", { class: "warn", text: "The config was changed elsewhere since this page loaded. " }),
      h("button", { type: "button", class: "link", onclick: () => loadConfig() }, "Load it (drops your changes here)"));
    $("btn-save").disabled = false;
    return;
  }
  if (!r.ok) {
    state.error = r.body?.error || `save failed (${r.status})`;
    updateSaveBar();
    return;
  }
  const keepPage = state.page;
  await loadConfig();
  selectPage(keepPage);
  const d = r.body.device;
  msg.dataset.keep = "1";
  msg.textContent = d === null ? "Saved. The device gets it when it connects."
    : d.ok ? `Saved and shown on the device (${d.ms} ms).` : `Saved on this PC, but the device did not take it: ${d.error}`;
  updateSaveBar();
  pollStatus();
}

// ---- tabs ----

function initTabs() {
  for (const b of document.querySelectorAll("[data-tab]")) {
    b.addEventListener("click", () => {
      for (const o of document.querySelectorAll("[data-tab]")) {
        const active = o === b;
        o.classList.toggle("active", active);
        o.setAttribute("aria-selected", String(active));
        $(`tab-${o.dataset.tab}`).hidden = !active;
      }
      try {
        localStorage.setItem("rezpad.tab", b.dataset.tab);
      } catch {
        // storage blocked: the first tab opens next time
      }
    });
  }
  let last = null;
  try {
    last = localStorage.getItem("rezpad.tab");
  } catch {
    last = null;
  }
  (document.querySelector(`[data-tab="${last}"]`) || document.querySelector("[data-tab]")).click();
}

// ---- start ----

initTabs();
initEditor();
initSettings();
on("config", () => {
  updateSaveBar();
  scheduleValidate();
});
$("btn-save").addEventListener("click", save);
$("btn-revert").addEventListener("click", () => {
  if (confirm("Drop all unsaved changes?")) loadConfig();
});
document.addEventListener("keydown", (e) => {
  if ((e.ctrlKey || e.metaKey) && e.key === "s") {
    e.preventDefault();
    if (!$("btn-save").disabled) save();
  }
});
window.addEventListener("beforeunload", (e) => {
  if (dirty()) e.preventDefault();
});
start();

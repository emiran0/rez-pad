// Settings view (appearance, screen, knobs, weather, sensor, media) and Device view (status,
// maintenance, image library, data sources). Forms change the working copy through set().
"use strict";

import { state, on, get, set, change, h, api, postJson, toast } from "./state.js";
import { THEMES, ACCENTS } from "./preview.js";
import { icon } from "./icons.js";

const $ = (id) => document.getElementById(id);

export const ACTION_NAMES = {
  none: "Nothing",
  "nav.page": "Change page",
  "nav.next_page": "Next page",
  "nav.prev_page": "Previous page",
  "nav.home": "First page",
  "media.play_pause": "Play / pause",
  "media.next": "Next track",
  "media.prev": "Previous track",
  "media.seek": "Seek in the track",
  "media.volume": "Volume",
  "media.mute": "Mute",
  "display.toggle": "Screen off / on",
  "display.brightness": "Brightness",
};

/** Action dropdown; turn gestures only get turn actions and presses only press actions. */
export function actionSelect(rotate, value, emptyLabel, onChange) {
  const allowed = state.catalog.actions.filter((a) => a === "none" || state.catalog.rotate_actions.includes(a) === rotate);
  const sel = h("select", { onchange: (e) => onChange(e.target.value) });
  if (emptyLabel) sel.append(h("option", { value: "", text: emptyLabel, selected: value === undefined }));
  for (const a of allowed) sel.append(h("option", { value: a, text: ACTION_NAMES[a] || a, selected: a === value }));
  if (value !== undefined && !allowed.includes(value)) sel.append(h("option", { value, text: `${value} (not allowed here)`, selected: true }));
  return sel;
}

/** Accent color swatches plus a custom color. */
export function swatches(current, onPick) {
  const cur = (current || "").toUpperCase();
  const custom = h("input", { type: "color", value: cur.toLowerCase() || "#4fc3f7", title: "Custom color", "aria-label": "Custom color", onchange: (e) => onPick(e.target.value.toUpperCase()) });
  return h("div", { class: "swatches" },
    ...ACCENTS.map((c) => h("button", { type: "button", class: c === cur ? "on" : "", title: c, "aria-label": `Accent ${c}`, style: { background: c }, onclick: () => onPick(c) })),
    custom);
}

/** Theme preset cards with a small sample of their colors. */
export function themeCards(current, onPick) {
  return h("div", { class: "themes" }, ...Object.entries(THEMES).map(([name, t]) =>
    h("button", { type: "button", class: "theme-card" + (name === current ? " on" : ""), onclick: () => onPick(name) },
      h("div", { class: "demo", style: { background: t.bg } },
        h("i", { style: { background: t.card, borderColor: t.border } }),
        h("i", { style: { background: t.card, borderColor: t.border } })),
      h("span", { text: t.label }))));
}

// ---- form helpers bound to config paths ----

function field(label, control, hint) {
  return h("label", { class: "field" }, h("span", { text: label }), control, hint ? h("small", { text: hint }) : null);
}

function num(path, opts = {}) {
  return h("input", {
    type: "number", min: opts.min, max: opts.max, step: opts.step ?? 1, value: get(path, ""), placeholder: opts.placeholder,
    oninput: (e) => {
      const v = e.target.value.trim();
      set(path, v === "" ? undefined : Number(v));
    },
  });
}

function check(label, path, fallback = false) {
  return h("label", { class: "check" }, h("input", { type: "checkbox", checked: get(path, fallback), onchange: (e) => set(path, e.target.checked) }), label);
}

function choice(path, options, fallback) {
  const cur = get(path, fallback);
  const opts = options.some((o) => o[0] === cur) ? options : [[cur, String(cur)], ...options];
  return h("select", { onchange: (e) => set(path, opts[e.target.selectedIndex][0]) },
    ...opts.map(([v, label]) => h("option", { value: v, text: label, selected: v === cur })));
}

function time(path, fallback) {
  return h("input", { type: "time", value: get(path, fallback), oninput: (e) => e.target.value && set(path, e.target.value) });
}

function range(path, fallback) {
  const out = h("output", { text: `${get(path, fallback)} %` });
  return h("div", { class: "range-row" },
    h("input", {
      type: "range", min: 0, max: 100, step: 1, value: get(path, fallback),
      oninput: (e) => {
        out.textContent = `${e.target.value} %`;
        set(path, Number(e.target.value));
      },
    }), out);
}

function panel(title, hint, ...children) {
  return h("section", { class: "panel" }, h("h2", { text: title }), hint ? h("p", { class: "hint", text: hint }) : null, ...children);
}

// ---- settings view ----

let searchResults = [];

function appearance() {
  return panel("Appearance", "The look of every page. A page can pick its own colors in Layout.",
    h("div", { class: "field" }, h("span", { text: "Theme" }), themeCards(get("theme.name", "dark"), (name) => { set("theme.name", name); renderSettings(); })),
    h("div", { class: "field" }, h("span", { text: "Accent color" }), swatches(get("theme.accent", "#4FC3F7"), (c) => { set("theme.accent", c); renderSettings(); })));
}

function screen() {
  return panel("Screen", null,
    h("div", { class: "form-grid" },
      field("Brightness", range("display.brightness", 80)),
      field("SPI clock", choice("display.spi_hz", [[20000000, "20 MHz"], [26666667, "26.7 MHz"], [40000000, "40 MHz (tested)"], [80000000, "80 MHz (not tested)"]], 40000000),
        "The ESP32-S3 divides 80 MHz, so these are the clocks it can run")),
    h("h3", { text: "Night", style: { fontSize: "13px", margin: "8px 0 4px" } }),
    h("p", { style: { margin: "0 0 10px", color: "var(--muted)", fontSize: "12.5px" }, text: "Dimming at night and turning off with the PC arrive in Phase 11; these settings are kept until then." }),
    check("Dim at night", "display.night.enabled"),
    h("div", { class: "form-grid" },
      field("From", time("display.night.from", "23:00")),
      field("To", time("display.night.to", "07:30")),
      field("Night brightness", range("display.night.brightness", 15))),
    check("Screen off while the PC sleeps", "display.sleep_on_pc_sleep", true));
}

function knobs() {
  const buttons = get("inputs.buttons", 0);
  const gestures = [["rotate", "Turn", true], ["rotate_pressed", "Press and turn", true], ["click", "Click", false], ["double", "Double click", false], ["long", "Long press", false]];
  const enc = (dev, title) => h("div", {}, h("h3", { text: title }),
    ...gestures.map(([g, label, rotate]) => field(label, actionSelect(rotate, get(["inputs", dev, g], "none"), null, (v) => set(["inputs", dev, g], v)))),
    field("Steps per click", choice(["inputs", dev, "counts_per_detent"], [[1, "1"], [2, "2"], [4, "4 (KY-040)"], [8, "8"]], 4)),
    check("Reverse direction", ["inputs", dev, "invert"]));
  const btn = (dev, title) => h("div", {}, h("h3", { text: title }),
    field("Click", actionSelect(false, get(["inputs", dev, "click"], "none"), null, (v) => set(["inputs", dev, "click"], v))),
    field("Long press", actionSelect(false, get(["inputs", dev, "long"], "none"), null, (v) => set(["inputs", dev, "long"], v))));
  return panel("Knobs and buttons", "What they do on every page; a page can change single gestures in Layout. A double click makes single clicks wait 0.3 s for a second one.",
    h("div", { class: "knob-grid" },
      enc("enc1", "Knob 1"),
      enc("enc2", "Knob 2"),
      h("div", {}, h("h3", { text: "Extra buttons" }),
        field("Buttons fitted", h("select", { onchange: (e) => { set("inputs.buttons", Number(e.target.value)); renderSettings(); } },
          ...[0, 1, 2].map((n) => h("option", { value: n, text: String(n), selected: n === buttons }))))),
      buttons >= 1 ? btn("btn1", "Button 1") : null,
      buttons >= 2 ? btn("btn2", "Button 2") : null));
}

function weather() {
  const loc = get("weather.name", "");
  const lat = get("weather.lat", null);
  const lon = get("weather.lon", null);
  const q = h("input", { type: "search", placeholder: "Search a city", "aria-label": "City", autocomplete: "off" });
  const results = h("ul", { class: "results" }, ...searchResults.map((it) =>
    h("li", {}, h("button", {
      type: "button", class: "ghost",
      onclick: () => {
        change((cfg) => {
          cfg.weather ??= {};
          cfg.weather.name = it.name.slice(0, 100);
          cfg.weather.lat = Math.round(it.lat * 1e5) / 1e5;
          cfg.weather.lon = Math.round(it.lon * 1e5) / 1e5;
        });
        searchResults = [];
        renderSettings();
      },
    }, [it.name, it.admin1, it.country].filter(Boolean).join(", ") + ` (${it.lat.toFixed(2)}, ${it.lon.toFixed(2)})`))));
  const form = h("form", {
    class: "row",
    onsubmit: async (e) => {
      e.preventDefault();
      const text = q.value.trim();
      if (!text) return;
      const r = await api("/api/geocode?q=" + encodeURIComponent(text));
      searchResults = r.ok ? r.body : [];
      if (!searchResults.length) toast(r.ok ? "No places found" : "Search failed", !r.ok);
      renderSettings();
    },
  }, h("div", { style: { flex: "1" } }, q), h("button", { type: "submit" }, icon("search", 16), "Search"));
  return panel("Weather and units", "Weather comes from Open-Meteo, no account needed.",
    h("div", { class: "field" }, h("span", { text: "Place" }), h("strong", { text: lat === null ? "Not set: search for a city" : `${loc} (${lat.toFixed(2)}, ${lon.toFixed(2)})` })),
    form, results,
    h("p", { id: "wx-now", style: { color: "var(--muted)", fontSize: "12.5px" } }),
    h("div", { class: "form-grid" },
      field("Update every", choice("weather.refresh_min", [[5, "5 min"], [10, "10 min"], [15, "15 min"], [30, "30 min"], [60, "1 hour"], [180, "3 hours"]], 15)),
      field("Temperature", choice("units.temp", [["c", "°C"], ["f", "°F"]], "c")),
      field("Wind", choice("units.wind", [["kmh", "km/h"], ["ms", "m/s"], ["mph", "mph"]], "kmh")),
      field("Pressure", choice("units.pressure", [["hpa", "hPa"], ["inhg", "inHg"]], "hpa"))));
}

function sensorMedia() {
  return [
    panel("Room sensor", "The sensor runs a little warm from the board; the offset corrects it (humidity follows).",
      h("div", { class: "field" }, h("span", { text: "Now, with the saved offsets" }), h("strong", { id: "room-now", text: "..." })),
      h("div", { class: "form-grid" },
        field("Temperature offset (°C)", num("sensor.temp_offset_c", { min: -10, max: 10, step: 0.1 })),
        field("Humidity offset (%)", num("sensor.hum_offset_pct", { min: -20, max: 20, step: 0.5 })),
        field("Pressure trend threshold (hPa / 3 h)", num("sensor.trend_threshold_hpa", { min: 0.1, max: 10, step: 0.1 })))),
    panel("Media and system data", null,
      h("div", { class: "form-grid" },
        field("Prefer apps (in order)", h("input", {
          type: "text", value: get("media.app_priority", ["Spotify", "*"]).join(", "),
          oninput: (e) => {
            const list = e.target.value.split(",").map((s) => s.trim()).filter(Boolean);
            set("media.app_priority", list.length ? list : undefined);
          },
        }), "Comma separated, * means any app"),
        field("Volume step per click (%)", num("media.volume_step", { min: 1, max: 20 })),
        field("Seek step per click (s)", num("media.seek_step_s", { min: 1, max: 60 })),
        field("System data updates per second", choice("stats.rate_hz", [[0.5, "0.5"], [1, "1"], [2, "2"], [4, "4"]], 1), "More often costs a little more CPU"))),
  ];
}

export function renderSettings() {
  if (!state.cfg) return;
  const scroll = window.scrollY;
  $("view-settings").replaceChildren(appearance(), screen(), knobs(), weather(), ...sensorMedia());
  window.scrollTo(0, scroll);
  renderLive();
}

// ---- device view ----

const STATE_TEXT = {
  Connected: "Connected",
  Connecting: "Connecting...",
  Searching: "Not found: plug in the board's native USB port",
  Incompatible: "Incompatible firmware",
};

export function linkText(s) {
  if (!s) return "Host not reachable";
  return STATE_TEXT[s.linkState] || s.linkState;
}

export function renderDevice() {
  if (!state.cfg) return;
  const used = new Set(state.cfg.pages.flatMap((p) => (p.widgets || []).filter((w) => w.type === "image").map((w) => w.asset)));
  $("view-device").replaceChildren(
    panel("Device", null, h("dl", { id: "dev-status", class: "status" })),
    panel("Maintenance", null,
      h("div", { class: "row" },
        h("button", { type: "button", onclick: compareDevice }, "Compare the device's copy"),
        h("button", { type: "button", onclick: () => cmd("reboot", "Restart the device?") }, "Restart device"),
        h("button", { type: "button", class: "danger", onclick: () => cmd("factory_reset", "Delete the config stored on the device? It restarts on the built-in default, and this PC sends its config again right after.") }, "Clear device config")),
      h("p", { id: "dev-msg", style: { marginTop: "10px", color: "var(--muted)" } })),
    panel("Images", "Pictures uploaded for image widgets. The device gets a copy sized for each widget.",
      state.assets.length ? h("div", { class: "library" }, ...state.assets.map((a) => h("div", { class: "lib-item" },
        h("img", { src: `/api/assets/${a.id}`, alt: a.name, loading: "lazy" }),
        h("div", { class: "meta" }, h("span", { title: a.name, text: `${a.name} · ${a.w}×${a.h}${a.frames > 1 ? `, ${a.frames} frames` : ""}` }),
          h("button", {
            type: "button", class: "icon-btn danger", title: used.has(a.id) ? "Used by an image widget" : "Delete", "aria-label": "Delete image", disabled: used.has(a.id),
            onclick: async () => {
              if (!confirm(`Delete ${a.name}?`)) return;
              const r = await api(`/api/assets/${a.id}`, { method: "DELETE" });
              if (!r.ok) toast(r.body?.error || "Could not delete", true);
              const l = await api("/api/assets");
              state.assets = l.ok ? l.body : [];
              renderDevice();
            },
          }, icon("trash", 16)))))) : h("p", { class: "dim", text: "No images yet. Add an Image widget in Layout to upload one." })),
    panel("Data sources on this PC", "Everything system widgets can show. \"On screen\" marks the ones in the saved layout.",
      h("table", { id: "sources" }, h("thead", {}, h("tr", {}, h("th", { text: "Source" }), h("th", { text: "Name" }), h("th", { class: "num", text: "Value" }), h("th"))), h("tbody"))));
  renderLive();
}

async function compareDevice() {
  const msg = $("dev-msg");
  msg.textContent = "Reading...";
  const r = await api("/api/device/config");
  if (!r.ok) {
    msg.textContent = r.body?.error || r.body?.detail || `Failed (${r.status})`;
    return;
  }
  const host = await api("/api/config");
  msg.textContent = JSON.stringify(r.body) === JSON.stringify(host.body)
    ? "The device has the same config as this PC."
    : "The device has a different config; saving sends this PC's config to it.";
}

async function cmd(name, question) {
  if (!confirm(question)) return;
  const r = await postJson("/api/device/cmd", { cmd: name });
  $("dev-msg").textContent = r.ok ? "Sent." : r.body?.error || `Failed (${r.status})`;
}

// Live parts of both views.
function renderLive() {
  const wxEl = $("wx-now");
  if (wxEl) {
    const w = state.data.weather;
    const now = w?.latest?.now;
    const u = w?.units;
    const wind = { kmh: "km/h", ms: "m/s", mph: "mph" };
    wxEl.textContent = now
      ? `Now in ${w.latest.loc}: ${now.temp}°${u.temp.toUpperCase()}, feels ${now.feels}°, ${now.hum} % RH, wind ${now.wind} ${wind[u.wind]} (updated ${Math.round(w.age_s / 60)} min ago)`
      : w?.error ? `Error: ${w.error}` : "No weather data yet.";
  }
  const roomEl = $("room-now");
  if (roomEl) {
    const r = state.data.room;
    roomEl.textContent = r ? `${r.temp_c?.toFixed(1) ?? "--"} °C, ${r.hum_pct?.toFixed(0) ?? "--"} % RH, ${r.press_hpa?.toFixed(1) ?? "--"} hPa, trend ${r.trend}` : "No reading (device not connected)";
  }
  const dl = $("dev-status");
  if (dl) {
    const s = state.data.status;
    const c = s?.config;
    const rows = [
      ["Host", s ? `rez-pad ${s.hostVersion}, protocol v${s.protocolVersion}${s.elevated ? ", administrator" : ""}` : "not reachable"],
      ["Device", linkText(s) + (s?.deviceConnected ? ` on ${s.port}, firmware ${s.firmwareVersion} (protocol v${s.firmwareProtocol}), ${s.serial}` : s?.error && s.linkState === "Incompatible" ? `: ${s.error}` : "")],
      ["Config", c ? (c.progress ?? (c.inSync ? "Same on the device" : s.deviceConnected ? "Not on the device yet" : "Device not connected")) : ""],
      ["Last send", c?.lastPush ? (c.lastPush.ok ? `OK in ${c.lastPush.ms} ms${c.lastPush.imagesSent ? `, ${c.lastPush.imagesSent} image(s)` : ""}` : c.lastPush.error) : "none yet"],
      ["Sensors", s?.sensors?.message ?? "All readable"],
    ];
    dl.replaceChildren(...rows.flatMap(([k, v]) => [h("dt", { text: k }), h("dd", { text: v })]));
  }
  const body = document.querySelector("#sources tbody");
  if (body) {
    const saved = JSON.parse(state.savedJson || "{}");
    const used = new Set((saved.pages || []).flatMap((p) => (p.widgets || []).map((w) => w.source)));
    body.replaceChildren(...[...state.data.sources.entries()].map(([id, src]) =>
      h("tr", {},
        h("td", { class: "id", text: id }),
        h("td", { text: src.name }),
        h("td", { class: "num" + (src.available ? "" : " dim"), text: src.available ? `${src.value} ${src.unit}` : "unavailable" }),
        h("td", { class: "dim", text: used.has(id) ? "on screen" : "" }))));
  }
}

export function initSettings() {
  on("live", renderLive);
  on("status", renderLive);
  on("structure", () => {
    renderSettings();
    renderDevice();
  });
  on("assets", () => !$("view-device").hidden && renderDevice());
}

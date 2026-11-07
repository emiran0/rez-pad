// Settings tabs: inputs, display, weather, sensor & media, and the device tab (status, data
// sources, maintenance). Forms write straight into the working copy (state.cfg).
"use strict";

import { state, on, get, set, h, api, postJson } from "./state.js";

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

/** Action dropdown; rotate gestures only get rotate actions and presses only press actions. */
export function actionSelect(rotate, value, emptyLabel, onChange) {
  const allowed = state.catalog.actions.filter((a) => a === "none" || state.catalog.rotate_actions.includes(a) === rotate);
  const sel = h("select", { onchange: (e) => onChange(e.target.value) });
  if (emptyLabel) sel.append(h("option", { value: "", text: emptyLabel, selected: value === undefined }));
  for (const a of allowed) sel.append(h("option", { value: a, text: ACTION_NAMES[a] || a, selected: a === value }));
  if (value !== undefined && !allowed.includes(value)) sel.append(h("option", { value, text: `${value} (not allowed here)`, selected: true }));
  return sel;
}

// ---- field helpers bound to a config path ----

function field(label, control, hint) {
  return h("label", { class: "field" }, h("span", { text: label }), control, hint ? h("small", { class: "dim", text: hint }) : null);
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

function check(path, fallback = false) {
  return h("input", { type: "checkbox", checked: get(path, fallback), onchange: (e) => set(path, e.target.checked) });
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
  const input = h("input", {
    type: "range", min: 0, max: 100, step: 1, value: get(path, fallback),
    oninput: (e) => {
      out.textContent = `${e.target.value} %`;
      set(path, Number(e.target.value));
    },
  });
  return h("span", { class: "row tight" }, input, out);
}

function section(title, hint, ...fields) {
  return h("section", { class: "card-section" }, h("h3", { text: title }), hint ? h("p", { class: "hint", text: hint }) : null, h("div", { class: "fields" }, ...fields));
}

// ---- tabs ----

function renderInputs() {
  const buttons = get("inputs.buttons", 0);
  const gestures = [["rotate", "Turn", true], ["rotate_pressed", "Press and turn", true], ["click", "Click", false],
    ["double", "Double click", false], ["long", "Long press", false]];
  const enc = (dev, title) => section(title, null,
    ...gestures.map(([g, label, rotate]) => field(label, actionSelect(rotate, get(["inputs", dev, g], "none"), null, (v) => set(["inputs", dev, g], v)))),
    field("Steps per click", choice(["inputs", dev, "counts_per_detent"], [[1, "1"], [2, "2"], [4, "4 (KY-040)"], [8, "8"]], 4), "Pulses the encoder gives per click"),
    field("Reverse direction", check(["inputs", dev, "invert"])));
  const btn = (dev, title) => section(title, null,
    field("Click", actionSelect(false, get(["inputs", dev, "click"], "none"), null, (v) => set(["inputs", dev, "click"], v))),
    field("Long press", actionSelect(false, get(["inputs", dev, "long"], "none"), null, (v) => set(["inputs", dev, "long"], v))));
  $("tab-inputs").replaceChildren(
    h("p", { class: "hint", text: "What the knobs and buttons do on every page. A page can change single gestures (Layout tab, \"Knobs on this page\"). Turning actions only fit turns, the others only fit presses. A double click makes single clicks wait 0.3 s for a possible second click." }),
    h("div", { class: "sections" },
      enc("enc1", "Knob 1"),
      enc("enc2", "Knob 2"),
      section("Buttons", "Extra push buttons wired to the board.",
        field("Buttons fitted", h("select", {
          onchange: (e) => {
            set("inputs.buttons", Number(e.target.value));
            renderInputs();
          },
        }, ...[0, 1, 2].map((n) => h("option", { value: n, text: String(n), selected: n === buttons }))))),
      buttons >= 1 ? btn("btn1", "Button 1") : null,
      buttons >= 2 ? btn("btn2", "Button 2") : null));
}

function renderDisplay() {
  $("tab-display").replaceChildren(h("div", { class: "sections" },
    section("Screen", null,
      field("Brightness", range("display.brightness", 80)),
      field("Accent color", h("input", { type: "color", value: (get("theme.accent", "#4FC3F7")).toLowerCase(), oninput: (e) => set("theme.accent", e.target.value.toUpperCase()) })),
      field("SPI clock", choice("display.spi_hz", [[20000000, "20 MHz"], [26666667, "26.7 MHz"], [40000000, "40 MHz (tested)"], [80000000, "80 MHz (not tested)"]], 40000000),
        "The ESP32-S3 divides 80 MHz, so these are the clocks it can run")),
    section("Night", "Dimming at night and turning off when the PC sleeps arrive with Phase 11; the settings are kept until then.",
      field("Dim at night", check("display.night.enabled")),
      field("From", time("display.night.from", "23:00")),
      field("To", time("display.night.to", "07:30")),
      field("Night brightness", range("display.night.brightness", 15)),
      field("Screen off while the PC sleeps", check("display.sleep_on_pc_sleep", true)))));
}

let searchResults = [];

function renderWeather() {
  const loc = get("weather.name", "");
  const lat = get("weather.lat", null);
  const lon = get("weather.lon", null);
  const results = h("ul", { class: "results" }, ...searchResults.map((it) =>
    h("li", {}, h("button", {
      type: "button",
      onclick: () => {
        set("weather.name", it.name.slice(0, 100));
        set("weather.lat", Math.round(it.lat * 1e5) / 1e5);
        set("weather.lon", Math.round(it.lon * 1e5) / 1e5);
        searchResults = [];
        renderWeather();
      },
    }, [it.name, it.admin1, it.country].filter(Boolean).join(", ") + ` (${it.lat.toFixed(2)}, ${it.lon.toFixed(2)})`))));
  const q = h("input", { type: "search", placeholder: "Search a city", "aria-label": "City", autocomplete: "off" });
  const form = h("form", {
    class: "row",
    onsubmit: async (e) => {
      e.preventDefault();
      const text = q.value.trim();
      if (!text) return;
      const r = await api("/api/geocode?q=" + encodeURIComponent(text));
      searchResults = r.ok ? r.body : [];
      renderWeather();
      if (!searchResults.length) $("tab-weather").querySelector(".results").replaceChildren(h("li", { class: "dim", text: r.ok ? "No places found" : "Search failed" }));
    },
  }, q, h("button", { type: "submit" }, "Search"));
  $("tab-weather").replaceChildren(h("div", { class: "sections" },
    section("Location", "Weather comes from Open-Meteo (no account needed).",
      h("div", { class: "field wide" }, h("span", { text: "Place" }),
        h("strong", { text: lat === null ? "Not set: search for a city" : `${loc} (${lat.toFixed(2)}, ${lon.toFixed(2)})` })),
      h("div", { class: "field wide" }, form, results),
      field("Update every", choice("weather.refresh_min", [[5, "5 min"], [10, "10 min"], [15, "15 min"], [30, "30 min"], [60, "1 hour"], [180, "3 hours"]], 15))),
    section("Units", "Used for the weather and the room sensor on the device.",
      field("Temperature", choice("units.temp", [["c", "°C"], ["f", "°F"]], "c")),
      field("Wind", choice("units.wind", [["kmh", "km/h"], ["ms", "m/s"], ["mph", "mph"]], "kmh")),
      field("Pressure", choice("units.pressure", [["hpa", "hPa"], ["inhg", "inHg"]], "hpa"))),
    h("section", { class: "card-section" }, h("h3", { text: "Now" }), h("p", { id: "wx-now", class: "hint" }))));
  renderWeatherLive();
}

function renderWeatherLive() {
  const el = $("wx-now");
  if (!el) return;
  const w = state.data.weather;
  const now = w?.latest?.now;
  const u = w?.units;
  const wind = { kmh: "km/h", ms: "m/s", mph: "mph" };
  el.textContent = now
    ? `${w.latest.loc}: ${now.temp}°${u.temp.toUpperCase()}, feels ${now.feels}°, ${now.hum} % RH, wind ${now.wind} ${wind[u.wind]}, updated ${Math.round(w.age_s / 60)} min ago`
    : w?.error ? `Error: ${w.error}` : "No weather data yet.";
}

function renderSensorMedia() {
  $("tab-sensor").replaceChildren(h("div", { class: "sections" },
    section("Room sensor", "The sensor runs a little warm from the board; the offset corrects it. Humidity is corrected for the temperature offset first.",
      h("div", { class: "field wide" }, h("span", { text: "Now (with the current offsets)" }), h("strong", { id: "room-now", text: "..." })),
      field("Temperature offset (°C)", num("sensor.temp_offset_c", { min: -10, max: 10, step: 0.1 })),
      field("Humidity offset (%)", num("sensor.hum_offset_pct", { min: -20, max: 20, step: 0.5 })),
      field("Pressure trend threshold (hPa / 3 h)", num("sensor.trend_threshold_hpa", { min: 0.1, max: 10, step: 0.1 }))),
    section("Media", null,
      field("Prefer apps (in order)", h("input", {
        type: "text", value: (get("media.app_priority", ["Spotify", "*"])).join(", "),
        oninput: (e) => {
          const list = e.target.value.split(",").map((s) => s.trim()).filter(Boolean);
          set("media.app_priority", list.length ? list : undefined);
        },
      }), "Comma separated, * means any app. A playing app always wins over paused ones."),
      field("Volume step per click (%)", num("media.volume_step", { min: 1, max: 20 })),
      field("Seek step per click (s)", num("media.seek_step_s", { min: 1, max: 60 }))),
    section("System data", null,
      field("Updates per second", choice("stats.rate_hz", [[0.5, "0.5"], [1, "1"], [2, "2"], [4, "4"]], 1), "More often costs a little more CPU on the PC"))));
  renderRoomLive();
}

function renderRoomLive() {
  const el = $("room-now");
  if (!el) return;
  const r = state.data.room;
  el.textContent = r
    ? `${r.temp_c?.toFixed(1) ?? "--"} °C, ${r.hum_pct?.toFixed(0) ?? "--"} % RH, ${r.press_hpa?.toFixed(1) ?? "--"} hPa, trend ${r.trend}`
    : "No reading (device not connected)";
}

// ---- device tab ----

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

function renderDevice() {
  $("tab-device").replaceChildren(h("div", { class: "sections" },
    h("section", { class: "card-section" }, h("h3", { text: "Device" }), h("dl", { id: "dev-status" })),
    h("section", { class: "card-section" }, h("h3", { text: "Maintenance" }),
      h("div", { class: "row" },
        h("button", { type: "button", onclick: compareDevice }, "Compare the device's copy"),
        h("button", { type: "button", onclick: () => cmd("reboot", "Restart the device?") }, "Restart device"),
        h("button", { type: "button", class: "danger", onclick: () => cmd("factory_reset", "Delete the config stored on the device? It restarts on the built-in default, and this host sends its config again right after.") }, "Clear device config")),
      h("p", { id: "dev-msg", class: "hint" })),
    h("section", { class: "card-section wide" }, h("h3", { text: "Data sources on this PC" }),
      h("p", { class: "hint", text: "Everything the editor can offer for system widgets. \"On screen\" marks the ones the saved layout uses." }),
      h("table", { id: "sources" }, h("thead", {}, h("tr", {}, h("th", { text: "Source" }), h("th", { text: "Name" }), h("th", { class: "num", text: "Value" }), h("th"))), h("tbody")))));
  renderDeviceLive();
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
  const same = JSON.stringify(r.body) === JSON.stringify(host.body);
  msg.textContent = same ? "The device has the same config as this PC." : "The device has a different config; saving here sends this PC's config to it.";
}

async function cmd(name, question) {
  if (!confirm(question)) return;
  const r = await postJson("/api/device/cmd", { cmd: name });
  $("dev-msg").textContent = r.ok ? "Sent." : r.body?.error || `Failed (${r.status})`;
}

function renderDeviceLive() {
  const dl = $("dev-status");
  if (!dl || !state.cfg) return;
  const s = state.data.status;
  const rows = [["Host", s ? `rez-pad ${s.hostVersion}, protocol v${s.protocolVersion}${s.elevated ? ", administrator" : ""}` : "not reachable"],
    ["Device", linkText(s) + (s?.deviceConnected ? ` on ${s.port}, firmware ${s.firmwareVersion} (protocol v${s.firmwareProtocol}), ${s.serial}` : s?.error && s.linkState === "Incompatible" ? `: ${s.error}` : "")],
    ["Config", s?.config ? (s.config.inSync ? "Same on the device" : s.deviceConnected ? "Not on the device yet" : "Device not connected") : ""],
    ["Last send", s?.config?.lastPush ? (s.config.lastPush.ok ? `OK, ${s.config.lastPush.ms} ms` : s.config.lastPush.error) : "none yet"],
    ["Sensors", s?.sensors?.message ?? "All readable"]];
  dl.replaceChildren(...rows.flatMap(([k, v]) => [h("dt", { text: k }), h("dd", { text: v })]));

  const body = document.querySelector("#sources tbody");
  if (!body) return;
  const used = new Set(state.cfg ? JSON.parse(state.savedJson || "{}").pages?.flatMap((p) => (p.widgets || []).map((w) => w.source)) ?? [] : []);
  body.replaceChildren(...[...state.data.sources.entries()].map(([id, src]) =>
    h("tr", {},
      h("td", { class: "id", text: id }),
      h("td", { text: src.name }),
      h("td", { class: src.available ? "num" : "num dim", text: src.available ? `${src.value} ${src.unit}` : "unavailable" }),
      h("td", { class: "dim", text: used.has(id) ? "on screen" : "" }))));
}

export function renderSettings() {
  if (!state.cfg) return;
  renderInputs();
  renderDisplay();
  renderWeather();
  renderSensorMedia();
  renderDevice();
}

export function initSettings() {
  on("live", () => {
    renderWeatherLive();
    renderRoomLive();
    renderDeviceLive();
  });
  on("status", renderDeviceLive);
}

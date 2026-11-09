// Settings view (appearance, screen, knobs, weather, sensor, media) and Device view (status,
// maintenance, image library, data sources). Forms change the working copy through set().
"use strict";

import { state, on, get, set, change, h, api, postJson, toast, controlsInUse } from "./state.js";
import { numberingCard } from "./numbering.js";
import { firmwarePanel } from "./firmware.js";
import { THEMES, ACCENTS } from "./preview.js";
import { icon } from "./icons.js";
import { ACTION_NAMES, bindingControl, keysRecorder } from "./bindings.js";
import { sourceButton } from "./sources.js";

const $ = (id) => document.getElementById(id);

export { ACTION_NAMES };

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
    h("p", { style: { margin: "0 0 10px", color: "var(--muted)", fontSize: "12.5px" }, text: "Uses the PC's clock (the device needs the host once after a restart to know the time). Night brightness 0 turns the screen off; a knob or button wakes it for 30 s. The brightness knob changes the day or night level until the settings are saved again." }),
    check("Dim at night", "display.night.enabled"),
    h("div", { class: "form-grid" },
      field("From", time("display.night.from", "23:00")),
      field("To", time("display.night.to", "07:30")),
      field("Night brightness", range("display.night.brightness", 15))),
    check("Screen off while the PC sleeps", "display.sleep_on_pc_sleep", true));
}

const ENC_PRESSES = [["click", "Click"], ["double", "Double click"], ["long", "Long press"]];

// ---- status bar items (status_bar.items, up to 3) ----

const STATUS_TYPES = [
  ["date", "Date"], ["room_temp", "Room temperature"], ["room_hum", "Room humidity"],
  ["outdoor_temp", "Outdoor temperature"], ["volume", "PC volume"], ["metric", "A system value"],
];

function statusBar() {
  const items = get("status_bar.items", []) || [];
  const update = (fn) => {
    change((cfg) => {
      cfg.status_bar ??= {};
      cfg.status_bar.items ??= [];
      fn(cfg.status_bar.items);
      if (!cfg.status_bar.items.length) delete cfg.status_bar;
    });
    renderSettings();
  };
  const sources = [...state.data.sources.values()].filter((s) => s.available !== undefined);
  const rows = items.map((it, i) => {
    const type = h("select", {
      onchange: (e) => update((list) => {
        list[i] = { type: e.target.value, ...(list[i].label ? { label: list[i].label } : {}) };
        if (e.target.value === "metric") list[i].source = sources[0]?.id ?? "sys.cpu.load";
      }),
    }, ...STATUS_TYPES.map(([v, t]) => h("option", { value: v, text: t, selected: v === it.type })));
    const src = it.type === "metric" ? sourceButton(it.source, (id) => update((list) => { list[i].source = id; })) : null;
    const label = h("input", {
      type: "text", maxlength: 7, value: it.label ?? "", placeholder: it.type === "metric" ? "e.g. CPU" : "optional",
      oninput: (e) => {
        const v = e.target.value;
        change((cfg) => {
          if (v) cfg.status_bar.items[i].label = v;
          else delete cfg.status_bar.items[i].label;
        }, `sb${i}.label`);
      },
    });
    return h("div", { class: "status-row" },
      field(`Item ${i + 1}`, type), src ? field("Value", src) : h("span"), field("Short label", label),
      h("button", { type: "button", class: "ghost", title: "Remove", onclick: () => update((list) => list.splice(i, 1)) }, icon("trash", 16)));
  });
  return panel("Status bar", "Up to 3 small items on the right of the status bar, on every page. The page dots show at most 5 pages at a time and slide with the current page.",
    ...rows,
    rows.length ? null : h("p", { class: "empty-note", text: "No items yet: the bar shows the clock, the page name, the page dots and the USB icon." }),
    items.length < 3 ? h("button", { type: "button", class: "ghost", onclick: () => update((list) => list.push({ type: "date" })) }, icon("plus", 16), "Add an item") : null);
}

// ---- integrations (host-only settings; the password is stored encrypted on this PC) ----

let beszelInfo = null;  // GET /api/integrations answer
let beszelTest = null;  // last Test result
let beszelDraft = null; // what is typed in the form (survives re-renders; never saved in the browser)

async function loadIntegrations() {
  const r = await api("/api/integrations").catch(() => null);
  if (r?.ok) {
    beszelInfo = r.body.beszel;
    renderSettings();
  }
}

function integrations() {
  if (!beszelInfo) {
    loadIntegrations();
    return panel("Integrations", "Loading...");
  }
  const s = beszelInfo.settings;
  const st = beszelTest || beszelInfo.status;
  beszelDraft ??= { enabled: s.enabled, url: s.url, email: s.email, password: "", refresh_s: s.refresh_s, insecure_tls: s.insecure_tls };
  const d = beszelDraft;
  const text = (key, attrs) => h("input", { ...attrs, value: d[key], oninput: (e) => { d[key] = e.target.value; } });
  const form = {
    enabled: h("input", { type: "checkbox", checked: d.enabled, onchange: (e) => { d.enabled = e.target.checked; } }),
    url: text("url", { type: "url", placeholder: "http://10.0.0.5:8090" }),
    email: text("email", { type: "email", placeholder: "read-only hub user", autocomplete: "off" }),
    password: text("password", { type: "password", placeholder: s.has_password ? "saved (leave empty to keep)" : "password", autocomplete: "new-password" }),
    refresh: text("refresh_s", { type: "number", min: 5, max: 300 }),
    insecure: h("input", { type: "checkbox", checked: d.insecure_tls, onchange: (e) => { d.insecure_tls = e.target.checked; } }),
  };
  const body = () => ({
    enabled: d.enabled, url: String(d.url).trim(), email: String(d.email).trim(),
    password: d.password, refresh_s: Number(d.refresh_s) || 15, insecure_tls: d.insecure_tls,
  });
  const test = async () => {
    beszelTest = { connected: false, error: "Testing..." };
    renderSettings();
    const r = await postJson("/api/integrations/beszel/test", body()).catch(() => null);
    beszelTest = r?.ok ? r.body : { connected: false, error: r?.body?.error || "the host did not answer" };
    renderSettings();
  };
  const save = async () => {
    const r = await api("/api/integrations/beszel", { method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body()) }).catch(() => null);
    if (!r?.ok) {
      toast(r?.body?.error || "Could not save", true);
      return;
    }
    toast("Beszel settings saved on this PC");
    beszelTest = null;
    beszelDraft = null;
    setTimeout(loadIntegrations, 1500);
  };
  const systems = st?.systems || [];
  const statusLine = st?.connected
    ? `Connected: ${systems.length} system${systems.length === 1 ? "" : "s"} visible to this user.`
    : st?.error ? `Not connected: ${st.error}` : s.enabled ? "Waiting for the first answer..." : "Off.";
  return panel("Integrations", "Apps on your network the host reads for you. Their values appear as data sources (group \"Beszel: <machine>\") for value, bar, gauge and graph widgets and the status bar. Addresses and passwords stay on this PC; the device only gets the values.",
    h("h3", { text: "Beszel", style: { fontSize: "13px", margin: "4px 0 8px" } }),
    h("label", { class: "check" }, form.enabled, "Read systems from a Beszel hub"),
    h("div", { class: "form-grid" },
      field("Hub address", form.url, "The hub's own address, e.g. http://10.20.0.20:8090"),
      field("Hub user (email)", form.email, "A read-only user; it sees the systems shared with it"),
      field("Password", form.password),
      field("Refresh every (s)", form.refresh)),
    h("label", { class: "check" }, form.insecure, "Accept a self-signed HTTPS certificate"),
    h("div", { class: "setup-row" },
      h("button", { type: "button", class: "ghost", onclick: test }, "Test connection"),
      h("button", { type: "button", class: "primary", onclick: save }, "Save")),
    h("p", { class: "hint", style: { color: st?.connected ? "var(--ok, #66BB6A)" : st?.error ? "var(--warn, #FFB300)" : "var(--muted)" }, text: statusLine }),
    systems.length ? h("ul", { class: "plain-list" }, ...systems.map((x) =>
      h("li", {}, h("b", { text: x.name }), ` (${x.status || "?"}), sources bz.${x.slug}.*`))) : null);
}

function knobs() {
  const turns = [["rotate", "Turn"], ["rotate_pressed", "Press and turn"]];
  const press = (dev, g, label) => field(label, bindingControl(get(["inputs", dev, g]) ?? "none", (v) => set(["inputs", dev, g], v ?? "none")));
  const enc = (dev, title) => h("div", {}, h("h3", { text: title }),
    ...turns.map(([g, label]) => field(label, actionSelect(true, get(["inputs", dev, g], "none"), null, (v) => set(["inputs", dev, g], v)))),
    ...ENC_PRESSES.map(([g, label]) => press(dev, g, label)),
    field("Steps per click", choice(["inputs", dev, "counts_per_detent"], [[1, "1"], [2, "2"], [4, "4 (KY-040)"], [8, "8"]], 4)),
    check("Reverse direction", ["inputs", dev, "invert"]));
  const btn = (dev, title) => h("div", {}, h("h3", { text: title }), ...ENC_PRESSES.map(([g, label]) => press(dev, g, label)));
  const used = controlsInUse();
  return panel("Knobs and buttons", "What they do on every page; a page can change single gestures in Layout. A press can run an action, send a key shortcut (the device types it itself, also without this app) or run a macro. A double click makes single clicks wait 0.3 s for a second one.",
    numberingCard(renderSettings),
    used.length ? h("div", { class: "knob-grid" }, ...used.map((c) => (c.knob ? enc(c.dev, c.label) : btn(c.dev, c.label))))
      : h("p", { class: "hint", text: "No knob or button has a number yet: assign one above." }));
}

// ---- macros ----

const STEP_KINDS = [["keys", "Keys"], ["text", "Type text"], ["wait_ms", "Wait"], ["open", "Open"]];

function macroUses(id) {
  const uses = [];
  const scan = (inputs, where) => {
    for (const [dev, m] of Object.entries(inputs || {})) {
      for (const [g, v] of Object.entries(m || {})) {
        if (v && typeof v === "object" && v.macro === id) uses.push({ inputs, dev, g, where });
      }
    }
  };
  scan(state.cfg.inputs, "inputs");
  state.cfg.pages.forEach((p, i) => scan(p.inputs_override, `page ${i}`));
  return uses;
}

function macroId(name) {
  const base = (name.normalize("NFKD").replace(/[^\w\s-]/g, "").trim().toLowerCase().replace(/[\s-]+/g, "_") || "macro").slice(0, 18);
  const ids = (state.cfg.macros || []).map((m) => m.id);
  let id = base;
  for (let n = 2; ids.includes(id); n++) id = `${base}_${n}`;
  return id;
}

function stepControl(mi, si, step) {
  const kind = Object.keys(step)[0];
  const upd = (v) => change((cfg) => { cfg.macros[mi].steps[si] = { [kind]: v }; }, `m${mi}s${si}`);
  if (kind === "keys") return keysRecorder(step.keys, (k) => { upd(k); });
  if (kind === "text") return h("textarea", { rows: 2, maxlength: 500, placeholder: "Text to type (new lines press Enter)", oninput: (e) => upd(e.target.value) }, step.text ?? "");
  if (kind === "wait_ms") {
    return h("div", { class: "row" }, h("input", { type: "number", min: 1, max: 10000, value: step.wait_ms, style: { width: "120px" }, oninput: (e) => upd(Number(e.target.value)) }), h("span", { class: "dim", text: "ms" }));
  }
  return h("input", { type: "text", value: step.open ?? "", maxlength: 260, placeholder: "Program, file, folder or https:// address", oninput: (e) => upd(e.target.value) });
}

function macros() {
  const list = state.cfg.macros || [];
  const cards = list.map((m, mi) => {
    const steps = m.steps || [];
    return h("div", { class: "macro" },
      h("div", { class: "macro-head" },
        h("input", {
          type: "text", value: m.name ?? m.id, maxlength: 31, "aria-label": "Macro name",
          oninput: (e) => change((cfg) => { cfg.macros[mi].name = e.target.value; }, `m${mi}name`),
        }),
        h("button", {
          type: "button", class: "icon-btn danger", title: "Delete macro", "aria-label": "Delete macro",
          onclick: () => {
            const uses = macroUses(m.id);
            if (!confirm(uses.length ? `Delete "${m.name || m.id}"? ${uses.length} button press(es) use it and will be set to Nothing.` : `Delete "${m.name || m.id}"?`)) return;
            change((cfg) => {
              for (const u of macroUses(m.id)) u.inputs[u.dev][u.g] = "none";
              cfg.macros.splice(mi, 1);
              if (!cfg.macros.length) delete cfg.macros;
            });
            renderSettings();
          },
        }, icon("trash", 16))),
      ...steps.map((s, si) => {
        const kind = Object.keys(s)[0];
        return h("div", { class: "step" },
          h("select", {
            "aria-label": "Step type",
            onchange: (e) => {
              const k = e.target.value;
              change((cfg) => { cfg.macros[mi].steps[si] = { [k]: k === "wait_ms" ? 100 : k === "keys" ? "ctrl+c" : "" }; });
              renderSettings();
            },
          }, ...STEP_KINDS.map(([k, label]) => h("option", { value: k, text: label, selected: k === kind }))),
          h("div", { class: "step-value" }, stepControl(mi, si, s)),
          h("div", { class: "step-tools" },
            h("button", { type: "button", class: "icon-btn", title: "Move up", "aria-label": "Move step up", disabled: si === 0, onclick: () => { change((cfg) => { const st = cfg.macros[mi].steps; [st[si - 1], st[si]] = [st[si], st[si - 1]]; }); renderSettings(); } }, icon("up", 16)),
            h("button", { type: "button", class: "icon-btn", title: "Move down", "aria-label": "Move step down", disabled: si === steps.length - 1, onclick: () => { change((cfg) => { const st = cfg.macros[mi].steps; [st[si + 1], st[si]] = [st[si], st[si + 1]]; }); renderSettings(); } }, icon("down", 16)),
            h("button", { type: "button", class: "icon-btn", title: "Remove step", "aria-label": "Remove step", disabled: steps.length <= 1, onclick: () => { change((cfg) => cfg.macros[mi].steps.splice(si, 1)); renderSettings(); } }, icon("close", 16))));
      }),
      h("div", { class: "row" }, ...STEP_KINDS.map(([k, label]) => h("button", {
        type: "button", class: "ghost", disabled: steps.length >= 32,
        onclick: () => { change((cfg) => cfg.macros[mi].steps.push({ [k]: k === "wait_ms" ? 100 : "" })); renderSettings(); },
      }, icon("plus", 14), label))));
  });
  return panel("Macros", "Run by this app when a knob press or button asks for one: key shortcuts, typed text (right on any keyboard layout), waits, and opening a program, folder or web address. Pick them for a press under Knobs and buttons.",
    ...cards,
    h("button", {
      type: "button", disabled: list.length >= 32,
      onclick: () => {
        const name = `Macro ${list.length + 1}`;
        change((cfg) => { (cfg.macros ??= []).push({ id: macroId(name), name, steps: [{ keys: "ctrl+c" }] }); });
        renderSettings();
      },
    }, icon("plus", 16), "New macro"));
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

// Categories in the left column; only the chosen one is shown.
const CATEGORIES = [
  { group: "Look", id: "appearance", title: "Appearance", icon: "palette", panels: () => [appearance()] },
  { group: "Look", id: "screen", title: "Screen", icon: "screen", panels: () => [screen()] },
  { group: "Look", id: "statusbar", title: "Status bar", icon: "statusbar", panels: () => [statusBar()] },
  { group: "Controls", id: "knobs", title: "Knobs and buttons", icon: "knob", panels: () => [knobs()] },
  { group: "Controls", id: "macros", title: "Macros", icon: "bolt", panels: () => [macros()] },
  { group: "Data", id: "weather", title: "Weather and units", icon: "weather_now", panels: () => [weather()] },
  { group: "Data", id: "room", title: "Room sensor", icon: "room_climate", panels: () => [sensorMedia()[0]] },
  { group: "Data", id: "data", title: "Media and system data", icon: "chart", panels: () => [sensorMedia()[1]] },
  { group: "Data", id: "integrations", title: "Integrations", icon: "plug", panels: () => [integrations()] },
];

function currentCategory() {
  let id = null;
  try {
    id = localStorage.getItem("rezpad.settings");
  } catch {
    // storage blocked: start at the first category
  }
  return CATEGORIES.find((c) => c.id === id) || CATEGORIES[0];
}

function chooseCategory(id) {
  try {
    localStorage.setItem("rezpad.settings", id);
  } catch {
    // remembered for this view only
  }
  renderSettings();
  $("view-settings").querySelector(".settings-content")?.scrollTo(0, 0);
}

export function renderSettings() {
  if (!state.cfg) return;
  const cat = currentCategory();
  const content = $("view-settings").querySelector(".settings-content");
  const scroll = content ? content.scrollTop : 0;

  const nav = h("nav", { class: "settings-nav", "aria-label": "Settings categories" });
  let group = "";
  for (const c of CATEGORIES) {
    if (c.group !== group) {
      group = c.group;
      nav.append(h("div", { class: "nav-group", text: group }));
    }
    nav.append(h("button", {
      type: "button", class: "nav-item" + (c.id === cat.id ? " active" : ""), "aria-current": c.id === cat.id ? "page" : null,
      onclick: () => chooseCategory(c.id),
    }, icon(c.icon, 18), h("span", { text: c.title })));
  }

  // One category: its title and description on top, its cards below. A card with the same title
  // as the category gives up its heading (and its description moves up).
  const panels = cat.panels().filter(Boolean);
  let lead = "";
  for (const p of panels) {
    const h2 = p.querySelector(":scope > h2");
    if (h2 && h2.textContent === cat.title) {
      const hint = p.querySelector(":scope > .hint");
      if (hint && !lead) {
        lead = hint.textContent;
        hint.remove();
      }
      h2.remove();
    }
  }
  const main = h("div", { class: "settings-content" },
    h("header", { class: "settings-head" }, h("h1", {}, icon(cat.icon, 22), h("span", { text: cat.title })), lead ? h("p", { text: lead }) : null),
    h("div", { class: "settings-cards" }, ...panels));
  $("view-settings").replaceChildren(nav, main);
  main.scrollTop = scroll;
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
    panel("Firmware", null, firmwarePanel()),
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

// Layout tab: page tabs, the device screen with draggable widget cards, the widget palette and
// the properties of the selected widget and page.
"use strict";

import { renderScreen, cardRect, WIDGET_NAMES, effectiveStyle, SCREEN_W, SCREEN_H, STATUS_H, GRID_H } from "./preview.js";
import { state, on, emit, get, set, update, h } from "./state.js";
import { actionSelect, ACTION_NAMES } from "./settings.js";

const $ = (id) => document.getElementById(id);

const PALETTE_GROUPS = [
  ["Media", ["album_art", "track_info", "media_controls", "progress", "volume"]],
  ["System", ["metric_value", "metric_bar", "metric_gauge", "metric_graph"]],
  ["Weather & room", ["weather_now", "weather_forecast", "room_climate", "clock"]],
  ["Other", ["label"]],
];

const SOURCE_GROUPS = [
  ["sys.cpu.", "CPU"], ["sys.gpu.", "GPU"], ["sys.ram.", "Memory"], ["sys.net.", "Network"],
  ["sys.disk.", "Disks"], ["sys.fan.", "Fans"],
];

let drag = null;  // {index, startX, startY, moved, target:{x,y}}
let screenEl = null;

function page() {
  return state.cfg.pages[state.page];
}

function cols() {
  return get("grid.cols", 4);
}

function rows() {
  return get("grid.rows", 3);
}

function sizesOf(type) {
  const t = state.catalog.widgets.find((w) => w.type === type);
  if (!t) return [];
  if (t.any_size) {
    const all = [];
    for (let hh = 1; hh <= rows(); hh++) for (let ww = 1; ww <= cols(); ww++) all.push([ww, hh]);
    return all;
  }
  return t.sizes;
}

// Is the rectangle inside the grid and free (ignoring widget `skip`)?
function fits(x, y, w, hgt, skip) {
  if (x < 0 || y < 0 || x + w > cols() || y + hgt > rows()) return false;
  return (page().widgets || []).every((o, i) =>
    i === skip || x >= o.x + o.w || o.x >= x + w || y >= o.y + o.h || o.y >= y + hgt);
}

function freeSpot(w, hgt) {
  for (let y = 0; y < rows(); y++) for (let x = 0; x < cols(); x++) if (fits(x, y, w, hgt, -1)) return { x, y };
  return null;
}

// ---- page tabs ----

function slug(name) {
  const base = (name.normalize("NFKD").replace(/[^\w\s-]/g, "").trim().toLowerCase().replace(/[\s-]+/g, "_") || "page").slice(0, 20);
  const ids = state.cfg.pages.map((p) => p.id);
  let id = base;
  for (let n = 2; ids.includes(id); n++) id = `${base}_${n}`;
  return id;
}

function renderTabs() {
  const tabs = $("page-tabs");
  tabs.replaceChildren(
    ...state.cfg.pages.map((p, i) =>
      h("button", {
        type: "button", role: "tab", class: "page-tab" + (i === state.page ? " active" : ""), "aria-selected": String(i === state.page),
        onclick: () => selectPage(i),
      }, p.name || p.id)),
    h("button", {
      type: "button", class: "page-add", title: "Add a page", disabled: state.cfg.pages.length >= state.catalog.limits.pages,
      onclick: () => {
        const name = `Page ${state.cfg.pages.length + 1}`;
        update((c) => c.pages.push({ id: slug(name), name, widgets: [] }));
        selectPage(state.cfg.pages.length - 1);
      },
    }, "+ Page"),
  );
}

export function selectPage(i) {
  state.page = Math.max(0, Math.min(i, state.cfg.pages.length - 1));
  state.selected = -1;
  renderAll();
}

// ---- screen ----

function renderScreenNow() {
  if (!state.cfg) return;
  const { grid, cards } = renderScreen(screenEl, state.cfg, state.page, state.data, state.catalog);
  // Faint cell lines so empty space is visible.
  const lines = h("div", { class: "cell-lines" });
  lines.style.setProperty("--cols", cols());
  lines.style.setProperty("--rows", rows());
  for (let i = 0; i < cols() * rows(); i++) lines.append(h("i"));
  grid.prepend(lines);
  cards.forEach((card, i) => {
    card.dataset.index = i;
    if (i === state.selected) card.classList.add("selected");
    card.addEventListener("pointerdown", (e) => startDrag(e, i));
  });
  if (drag?.moved && drag.target) {
    const w = page().widgets[drag.index];
    const r = cardRect({ ...w, x: drag.target.x, y: drag.target.y }, cols(), rows());
    const ghost = h("div", { class: "ghost" + (drag.valid ? "" : " invalid") });
    Object.assign(ghost.style, { left: r.x + "px", top: r.y + "px", width: r.w + "px", height: r.h + "px" });
    grid.append(ghost);
    cards[drag.index]?.classList.add("dragging");
  }
}

function cellAt(e) {
  const r = screenEl.querySelector(".grid").getBoundingClientRect();
  const fx = (e.clientX - r.left) / r.width;
  const fy = (e.clientY - r.top) / r.height;
  return { x: Math.floor(fx * cols()), y: Math.floor(fy * rows()) };
}

function startDrag(e, index) {
  if (e.button !== 0) return;
  e.preventDefault();
  screenEl.focus({ preventScroll: true });
  if (state.selected !== index) {
    state.selected = index;
    emit("selection");
  }
  const w = page().widgets[index];
  const start = cellAt(e);
  drag = { index, startX: e.clientX, startY: e.clientY, moved: false, grab: { x: start.x - w.x, y: start.y - w.y }, target: null, valid: false };
  // On the window: the cards are re-rendered while dragging, so they cannot hold the listeners.
  window.addEventListener("pointermove", onDragMove);
  window.addEventListener("pointerup", onDragEnd, { once: true });
  window.addEventListener("pointercancel", onDragEnd, { once: true });
}

function onDragMove(e) {
  if (!drag) return;
  if (!drag.moved && Math.hypot(e.clientX - drag.startX, e.clientY - drag.startY) < 5) return;
  drag.moved = true;
  const w = page().widgets[drag.index];
  const c = cellAt(e);
  const target = { x: c.x - drag.grab.x, y: c.y - drag.grab.y };
  if (drag.target && target.x === drag.target.x && target.y === drag.target.y) return;
  drag.target = target;
  drag.valid = fits(target.x, target.y, w.w, w.h, drag.index);
  renderScreenNow();
}

function onDragEnd() {
  window.removeEventListener("pointermove", onDragMove);
  window.removeEventListener("pointerup", onDragEnd);
  window.removeEventListener("pointercancel", onDragEnd);
  const d = drag;
  drag = null;
  if (d?.moved && d.valid) {
    update((cfg) => {
      const w = cfg.pages[state.page].widgets[d.index];
      w.x = d.target.x;
      w.y = d.target.y;
    });
  } else {
    renderScreenNow();
  }
}

function onScreenKey(e) {
  if (state.selected < 0) return;
  const w = page().widgets[state.selected];
  const move = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
  if (move) {
    e.preventDefault();
    const x = w.x + move[0];
    const y = w.y + move[1];
    if (fits(x, y, w.w, w.h, state.selected)) update(() => Object.assign(w, { x, y }));
  } else if (e.key === "Delete" || e.key === "Backspace") {
    e.preventDefault();
    removeSelected();
  } else if (e.key === "Escape") {
    state.selected = -1;
    emit("selection");
  }
}

function removeSelected() {
  if (state.selected < 0) return;
  const i = state.selected;
  state.selected = -1;
  update((c) => c.pages[state.page].widgets.splice(i, 1));
  emit("selection");
}

// ---- palette ----

function newWidget(type, size, spot) {
  const w = { type, x: spot.x, y: spot.y, w: size[0], h: size[1] };
  if (type.startsWith("metric_")) {
    const used = new Set(state.cfg.pages.flatMap((p) => (p.widgets || []).map((x) => x.source)));
    const src = state.catalog.sources.find((s) => !used.has(s.id) && state.data.sources.get(s.id)?.available) || state.catalog.sources[0];
    w.source = src?.id || "sys.cpu.load";
    w.label = state.data.sources.get(w.source)?.name || "";
  }
  if (type === "weather_forecast") w.mode = "daily";
  if (type === "label") w.text = "Text";
  return w;
}

function renderPalette() {
  const full = (page().widgets || []).length >= state.catalog.limits.widgets;
  $("palette").replaceChildren(...PALETTE_GROUPS.map(([group, types]) =>
    h("div", { class: "palette-group" },
      h("div", { class: "palette-title", text: group }),
      ...types.filter((t) => state.catalog.widgets.some((w) => w.type === t)).map((type) => {
        const sizes = sizesOf(type);
        let place = null;
        for (const s of sizes) {
          const spot = freeSpot(s[0], s[1]);
          if (spot) {
            place = { size: s, spot };
            break;
          }
        }
        const sizeText = state.catalog.widgets.find((w) => w.type === type).any_size ? "any size" : sizes.map((s) => s.join("×")).join(", ");
        return h("button", {
          type: "button", class: "palette-item", disabled: full || !place,
          title: full ? "This page is full (16 widgets)" : place ? `Add (sizes ${sizeText})` : `No free space for ${sizeText}`,
          onclick: () => {
            update((c) => {
              const ws = c.pages[state.page].widgets ??= [];
              ws.push(newWidget(type, place.size, place.spot));
              state.selected = ws.length - 1;
            });
            emit("selection");
          },
        }, h("span", { text: WIDGET_NAMES[type] || type }), h("small", { text: sizeText }));
      }))));
}

// ---- properties of the selected widget ----

function numberField(label, key, w, placeholder, opts = {}) {
  return h("label", { class: "field" }, h("span", { text: label }),
    h("input", {
      type: "number", step: opts.step ?? "any", value: w[key] ?? "", placeholder: placeholder ?? "",
      oninput: (e) => {
        const v = e.target.value.trim();
        if (v === "") delete w[key];
        else if (!Number.isNaN(Number(v))) w[key] = Number(v);
        emit("config");
      },
    }));
}

function textField(label, key, w, placeholder, maxBytes) {
  return h("label", { class: "field" }, h("span", { text: label }),
    h("input", {
      type: "text", value: w[key] ?? "", placeholder: placeholder ?? "", "data-max-bytes": maxBytes,
      oninput: (e) => {
        if (e.target.value === "") delete w[key];
        else w[key] = e.target.value;
        emit("config");
      },
    }));
}

function sourceSelect(w) {
  const sel = h("select", {
    onchange: (e) => {
      const old = state.data.sources.get(w.source)?.name;
      w.source = e.target.value;
      // Keep a label the user typed; follow the source when it was the old source's name.
      if (!w.label || w.label === old) w.label = state.data.sources.get(w.source)?.name || "";
      emit("config");
      renderProps();
    },
  });
  const groups = new Map();
  for (const s of state.catalog.sources) {
    const g = SOURCE_GROUPS.find(([p]) => s.id.startsWith(p))?.[1] || "Other";
    if (!groups.has(g)) groups.set(g, []);
    groups.get(g).push(s);
  }
  for (const [g, list] of groups) {
    const og = h("optgroup", { label: g });
    for (const s of list) {
      const live = state.data.sources.get(s.id);
      const value = live?.available ? ` (${Math.round(live.value * 10) / 10} ${live.unit})` : " (unavailable)";
      og.append(h("option", { value: s.id, selected: s.id === w.source, text: s.name + value }));
    }
    sel.append(og);
  }
  if (w.source && !state.catalog.sources.some((s) => s.id === w.source)) {
    sel.prepend(h("option", { value: w.source, selected: true, text: `${w.source} (not on this PC)` }));
  }
  return h("label", { class: "field wide" }, h("span", { text: "Data source" }), sel);
}

function renderProps() {
  const box = $("props");
  const w = state.selected >= 0 ? page().widgets?.[state.selected] : null;
  if (!w) {
    box.replaceChildren(h("p", { class: "hint", text: "Select a widget on the screen to edit it, or add one from the list. Drag widgets to move them; arrow keys move and Delete removes the selected one." }));
    return;
  }
  const sizes = sizesOf(w.type);
  const sizeSel = h("select", {
    onchange: (e) => {
      const [ww, hh] = e.target.value.split("x").map(Number);
      update(() => Object.assign(w, { w: ww, h: hh }));
    },
  }, ...sizes.map(([ww, hh]) => {
    const ok = fits(w.x, w.y, ww, hh, state.selected);
    return h("option", { value: `${ww}x${hh}`, selected: ww === w.w && hh === w.h, disabled: !ok, text: `${ww} × ${hh}` + (ok ? "" : " (no room here)") });
  }));
  if (!sizes.some(([ww, hh]) => ww === w.w && hh === w.h)) sizeSel.prepend(h("option", { selected: true, disabled: true, text: `${w.w} × ${w.h} (not allowed)` }));

  const fields = [h("label", { class: "field" }, h("span", { text: "Size (columns × rows)" }), sizeSel)];
  if (w.type.startsWith("metric_")) {
    const d = effectiveStyle({ source: w.source }, state.catalog);
    fields.push(sourceSelect(w));
    fields.push(textField("Label", "label", w, w.source, 31));
    fields.push(textField("Unit", "unit", w, d.unit ? `${d.unit} (from the source)` : "none", 11));
    if (w.type !== "metric_value") {
      fields.push(numberField("Min", "min", w, String(d.min)));
      fields.push(numberField("Max", "max", w, String(d.max)));
      fields.push(numberField("Warning at", "warn", w, d.warn ?? "none"));
      fields.push(numberField("Critical at", "crit", w, d.crit ?? "none"));
    }
    fields.push(h("label", { class: "field" }, h("span", { text: "Decimals" }),
      h("select", {
        onchange: (e) => {
          if (e.target.value === "") delete w.decimals;
          else w.decimals = Number(e.target.value);
          emit("config");
        },
      }, h("option", { value: "", text: "Automatic", selected: w.decimals === undefined }),
      ...[0, 1, 2, 3].map((n) => h("option", { value: n, text: String(n), selected: w.decimals === n })))));
  } else if (w.type === "weather_forecast") {
    fields.push(h("label", { class: "field" }, h("span", { text: "Shows" }),
      h("select", { onchange: (e) => set(["pages", state.page, "widgets", state.selected, "mode"], e.target.value) },
        h("option", { value: "daily", text: "Today and the next 4 days", selected: (w.mode || "daily") === "daily" }),
        h("option", { value: "hourly", text: "Every 2 hours", selected: w.mode === "hourly" }))));
  } else if (w.type === "label") {
    fields.push(textField("Text", "text", w, "", 63));
    fields.push(textField("Small title (optional)", "label", w, "", 31));
  }

  box.replaceChildren(
    h("div", { class: "props-head" },
      h("strong", { text: WIDGET_NAMES[w.type] || w.type }),
      h("span", { class: "dim", text: ` at column ${w.x + 1}, row ${w.y + 1}` })),
    h("div", { class: "fields" }, ...fields),
    h("div", { class: "row" }, h("button", { type: "button", class: "danger", onclick: removeSelected }, "Remove widget")),
  );
}

// ---- page properties ----

function renderPageProps() {
  const p = page();
  const i = state.page;
  const n = state.cfg.pages.length;
  const ov = p.inputs_override || {};
  const buttons = get("inputs.buttons", 0);
  const gestureRows = [];
  for (const [dev, label, gestures] of [
    ["enc1", "Knob 1", [["rotate", "Turn", true], ["rotate_pressed", "Press and turn", true], ["click", "Click", false], ["double", "Double click", false], ["long", "Long press", false]]],
    ["enc2", "Knob 2", [["rotate", "Turn", true], ["rotate_pressed", "Press and turn", true], ["click", "Click", false], ["double", "Double click", false], ["long", "Long press", false]]],
    ["btn1", "Button 1", [["click", "Click", false], ["long", "Long press", false]]],
    ["btn2", "Button 2", [["click", "Click", false], ["long", "Long press", false]]],
  ]) {
    if (dev === "btn1" && buttons < 1) continue;
    if (dev === "btn2" && buttons < 2) continue;
    for (const [g, gl, rotate] of gestures) {
      const global = get(["inputs", dev, g], "none");
      const sel = actionSelect(rotate, ov[dev]?.[g], `Same as everywhere (${ACTION_NAMES[global] || global})`, (v) => {
        update((c) => {
          const po = c.pages[i];
          po.inputs_override ??= {};
          po.inputs_override[dev] ??= {};
          if (v === "") delete po.inputs_override[dev][g];
          else po.inputs_override[dev][g] = v;
          if (Object.keys(po.inputs_override[dev]).length === 0) delete po.inputs_override[dev];
          if (Object.keys(po.inputs_override).length === 0) delete po.inputs_override;
        });
      });
      gestureRows.push(h("label", { class: "field" }, h("span", { text: `${label}: ${gl}` }), sel));
    }
  }
  const overrides = Object.values(ov).reduce((a, o) => a + Object.keys(o || {}).length, 0);

  $("page-props").replaceChildren(
    h("div", { class: "fields" },
      h("label", { class: "field" }, h("span", { text: "Page name" }),
        h("input", {
          type: "text", value: p.name ?? "", "data-max-bytes": 31,
          oninput: (e) => {
            p.name = e.target.value;
            emit("config");
            renderTabs();
          },
        })),
      h("div", { class: "field" }, h("span", { text: "Order" }),
        h("div", { class: "row tight" },
          h("button", { type: "button", disabled: i === 0, onclick: () => movePage(-1) }, "← Earlier"),
          h("button", { type: "button", disabled: i === n - 1, onclick: () => movePage(1) }, "Later →"),
          h("button", {
            type: "button", class: "danger", disabled: n <= 1,
            onclick: () => {
              if ((p.widgets || []).length && !confirm(`Remove the page "${p.name || p.id}" and its ${p.widgets.length} widgets?`)) return;
              update((c) => c.pages.splice(i, 1));
              selectPage(Math.min(i, state.cfg.pages.length - 1));
            },
          }, "Remove page"))),
      h("div", { class: "field" }, h("span", { text: "Grid (all pages)" }),
        h("div", { class: "row tight" },
          gridSelect("cols", "columns"), h("span", { text: "×" }), gridSelect("rows", "rows")))),
    h("details", { class: "overrides", open: overrides > 0 },
      h("summary", { text: `Knobs on this page${overrides ? ` (${overrides} changed)` : ""}` }),
      h("p", { class: "hint", text: "Change what a knob or button does only while this page is shown. The defaults are on the Inputs tab." }),
      h("div", { class: "fields" }, ...gestureRows)),
  );
}

function gridSelect(key, label) {
  const cur = get(["grid", key], key === "cols" ? 4 : 3);
  return h("select", {
    "aria-label": label,
    onchange: (e) => set(["grid", key], Number(e.target.value)),
  }, ...[1, 2, 3, 4, 5, 6, 7, 8].map((v) => h("option", { value: v, selected: v === cur, text: `${v} ${label}` })));
}

function movePage(delta) {
  const i = state.page;
  update((c) => {
    const [p] = c.pages.splice(i, 1);
    c.pages.splice(i + delta, 0, p);
  });
  state.page = i + delta;
  renderAll();
}

// ---- wiring ----

function fitScreen() {
  const wrap = $("screen-wrap");
  const scale = Math.max(0.5, Math.min(2, wrap.clientWidth / SCREEN_W));
  wrap.style.setProperty("--scale", scale);
  wrap.style.height = SCREEN_H * scale + "px";
}

export function renderAll() {
  if (!state.cfg) return;
  if (state.page >= state.cfg.pages.length) state.page = Math.max(0, state.cfg.pages.length - 1);
  renderTabs();
  renderScreenNow();
  renderPalette();
  renderProps();
  renderPageProps();
}

export function initEditor() {
  screenEl = $("screen");
  screenEl.addEventListener("keydown", onScreenKey);
  screenEl.addEventListener("pointerdown", (e) => {
    if (e.target === screenEl || e.target.classList.contains("grid") || e.target.closest(".cell-lines")) {
      state.selected = -1;
      emit("selection");
    }
  });
  new ResizeObserver(fitScreen).observe($("screen-wrap"));
  fitScreen();
  on("selection", () => {
    renderScreenNow();
    renderProps();
  });
  // Structural changes re-render everything except focused inputs' panels.
  on("config", () => {
    renderScreenNow();
    renderPalette();
    renderTabs();
    if (!$("props").contains(document.activeElement)) renderProps();
    if (!$("page-props").contains(document.activeElement)) renderPageProps();
  });
  on("live", () => {
    if (!drag) renderScreenNow();
  });
  // The grid area starts below the status bar; keep the constants used for hit testing honest.
  console.assert(STATUS_H + GRID_H === SCREEN_H);
}

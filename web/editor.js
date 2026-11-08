// Layout view: page rail with live thumbnails, the device screen (drag to move or swap, corner
// handle to resize, click an empty cell to add there), the inspector (widget gallery, or the
// selected widget's settings) and the page tools (grid, theme, knobs, more).
"use strict";

import {
  renderScreen, renderWidget, cardRect, bigEnough, pageGrid, pageTheme, effectiveStyle,
  WIDGETS, THEMES, SCREEN_W, SCREEN_H,
} from "./preview.js";
import { state, on, emit, get, change, h, api, toast, openPopover, closePopover } from "./state.js";
import { icon } from "./icons.js";
import { actionSelect, ACTION_NAMES, swatches, themeCards } from "./settings.js";

const $ = (id) => document.getElementById(id);
const FRAME = 10;  // px of black bezel around the preview

const SOURCE_GROUPS = [
  ["sys.cpu.", "CPU"], ["sys.gpu.", "GPU"], ["sys.ram.", "Memory"], ["sys.net.", "Network"],
  ["sys.disk.", "Disks"], ["sys.fan.", "Fans"],
];

let drag = null;   // move, resize or add-from-gallery in progress
let screenEl = null;
let gridEl = null;

// ---- geometry ----

const page = () => state.cfg.pages[state.page];
const grid = () => pageGrid(state.cfg, page());

function fits(x, y, w, hh, skip = [-1]) {
  const { cols, rows } = grid();
  if (x < 0 || y < 0 || w < 1 || hh < 1 || x + w > cols || y + hh > rows) return false;
  return (page().widgets || []).every((o, i) =>
    skip.includes(i) || x >= o.x + o.w || o.x >= x + w || y >= o.y + o.h || o.y >= y + hh);
}

const big = (type, w, hh) => {
  const { cols, rows } = grid();
  return bigEnough(state.catalog, type, w, hh, cols, rows);
};

/** Sizes (w, h) the type may take on this page's grid, closest to its usual size first. */
function sizesFor(type) {
  const { cols, rows } = grid();
  const out = [];
  for (let hh = 1; hh <= rows; hh++) for (let w = 1; w <= cols; w++) if (big(type, w, hh)) out.push([w, hh]);
  const [pw, ph] = WIDGETS[type]?.size || [1, 1];
  return out.sort((a, b) => Math.abs(a[0] - pw) + Math.abs(a[1] - ph) - (Math.abs(b[0] - pw) + Math.abs(b[1] - ph)) || b[0] * b[1] - a[0] * a[1]);
}

/** Where a new widget of `type` goes: covering cell `at` if given, else the first free spot. */
function placement(type, at) {
  for (const [w, hh] of sizesFor(type)) {
    if (at) {
      for (let y = at.y; y > at.y - hh; y--) for (let x = at.x; x > at.x - w; x--) if (fits(x, y, w, hh)) return { x, y, w, h: hh };
    } else {
      const { cols, rows } = grid();
      for (let y = 0; y < rows; y++) for (let x = 0; x < cols; x++) if (fits(x, y, w, hh)) return { x, y, w, h: hh };
    }
  }
  return null;
}

function cellAt(e) {
  const r = gridEl.getBoundingClientRect();
  const { cols, rows } = grid();
  const fx = (e.clientX - r.left) / r.width;
  const fy = (e.clientY - r.top) / r.height;
  return { x: Math.floor(fx * cols), y: Math.floor(fy * rows), inside: fx >= 0 && fx < 1 && fy >= 0 && fy < 1 };
}

// ---- new widgets ----

function shortName(name) {
  return name.length > 18 ? name.replace(/ \(.*\)$/, "").slice(0, 18) : name;
}

function newWidget(type, p) {
  const w = { type, x: p.x, y: p.y, w: p.w, h: p.h };
  if (type.startsWith("metric_")) {
    const used = new Set(state.cfg.pages.flatMap((pg) => (pg.widgets || []).map((x) => x.source)));
    const src = state.catalog.sources.find((s) => !used.has(s.id) && state.data.sources.get(s.id)?.available) || state.catalog.sources[0];
    w.source = src?.id || "sys.cpu.load";
    w.label = shortName(state.data.sources.get(w.source)?.name || "");
  }
  if (type === "weather_forecast") w.mode = "daily";
  if (type === "label") w.text = "Hello";
  if (type === "image" && state.assets[0]) w.asset = state.assets[0].id;
  return w;
}

function addWidget(type, at) {
  const p = placement(type, at);
  if (!p) {
    toast(at ? "No room for that widget here; pick a bigger empty spot." : "No room left on this page for that widget.", true);
    return;
  }
  change((cfg) => {
    const ws = (cfg.pages[state.page].widgets ??= []);
    ws.push(newWidget(type, p));
    state.selected = ws.length - 1;
  });
  state.placeAt = null;
  if (type === "image" && !state.assets.length) toast("Now upload an image in the panel on the right.");
  emit("selection");
}

// ---- page rail ----

function renderRail() {
  const list = $("page-list");
  list.replaceChildren(...state.cfg.pages.map((p, i) => {
    const thumb = h("div", { class: "thumb" });
    const scr = h("div", { class: "screen" });
    thumb.append(scr);
    renderScreen(scr, state.cfg, i, state.data, state.catalog);
    const { cols, rows } = pageGrid(state.cfg, p);
    return h("li", {
      class: "page-item" + (i === state.page ? " active" : ""), tabindex: 0, role: "button", "aria-current": i === state.page ? "page" : null,
      onclick: () => selectPage(i),
      onkeydown: (e) => (e.key === "Enter" || e.key === " ") && selectPage(i),
    }, thumb, h("div", { class: "page-meta" }, h("strong", { text: p.name || p.id }), h("small", { text: `${cols}×${rows} · ${(p.widgets || []).length}` })));
  }));
  const add = $("page-add");
  add.replaceChildren(icon("plus", 16), "New page");
  add.disabled = state.cfg.pages.length >= state.catalog.limits.pages;
}

function refreshThumbs() {
  document.querySelectorAll("#page-list .thumb .screen").forEach((scr, i) => renderScreen(scr, state.cfg, i, state.data, state.catalog));
}

function slug(name) {
  const base = (name.normalize("NFKD").replace(/[^\w\s-]/g, "").trim().toLowerCase().replace(/[\s-]+/g, "_") || "page").slice(0, 20);
  const ids = state.cfg.pages.map((p) => p.id);
  let id = base;
  for (let n = 2; ids.includes(id); n++) id = `${base}_${n}`;
  return id;
}

function addPage() {
  const name = `Page ${state.cfg.pages.length + 1}`;
  change((cfg) => cfg.pages.splice(state.page + 1, 0, { id: slug(name), name, widgets: [] }));
  selectPage(state.page + 1);
  $("page-name").select();
}

export function selectPage(i) {
  state.page = Math.max(0, Math.min(i, state.cfg.pages.length - 1));
  state.selected = -1;
  state.placeAt = null;
  closePopover();
  renderAll();
}

// ---- stage head: name and page tools ----

function renderStageHead() {
  const p = page();
  const name = $("page-name");
  if (document.activeElement !== name) name.value = p.name ?? "";
  const { cols, rows } = grid();
  const pal = pageTheme(state.cfg, p);
  const overrides = Object.values(p.inputs_override || {}).reduce((a, o) => a + Object.keys(o || {}).length, 0);
  $("page-tools").replaceChildren(
    h("button", { type: "button", class: "tool", title: "Grid of this page", onclick: (e) => openPopover(e.currentTarget, gridPopover) },
      icon("grid", 16), `${cols} × ${rows}`),
    h("button", { type: "button", class: "tool", title: "Colors of this page", onclick: (e) => openPopover(e.currentTarget, themePopover) },
      h("span", { class: "swatch", style: { background: pal.accent } }), p.theme ? THEMES[pal.name].label : h("span", {}, "Colors ", h("small", { text: "(all pages)" }))),
    h("button", { type: "button", class: "tool", title: "What the knobs do on this page", onclick: (e) => openPopover(e.currentTarget, knobsPopover) },
      icon("knob", 16), overrides ? `Knobs (${overrides})` : "Knobs"),
    h("button", { type: "button", class: "tool icon-btn", title: "More", "aria-label": "More page actions", onclick: (e) => openPopover(e.currentTarget, morePopover) }, icon("more", 18)),
  );
}

function stepper(label, value, min, max, onSet) {
  return h("div", { class: "field" }, h("span", { text: label }),
    h("div", { class: "chips" }, ...Array.from({ length: max - min + 1 }, (_, k) => min + k).map((v) =>
      h("button", { type: "button", class: v === value ? "on" : "", onclick: () => onSet(v) }, String(v)))));
}

function gridPopover(panel) {
  const fill = () => {
    const p = page();
    const { cols, rows } = grid();
    const set = (key, v) => {
      change((cfg) => {
        const pg = cfg.pages[state.page];
        pg.grid = { cols: pg.grid?.cols ?? cfg.grid?.cols ?? 4, rows: pg.grid?.rows ?? cfg.grid?.rows ?? 3, [key]: v };
      });
      fill();
    };
    const bad = (p.widgets || []).filter((w, i) => !fits(w.x, w.y, w.w, w.h, [i]) || !big(w.type, w.w, w.h)).length;
    panel.replaceChildren(
      h("h4", { text: "Grid of this page" }),
      h("p", { class: "hint", text: `Each cell is ${Math.floor(SCREEN_W / cols) - 4} × ${Math.floor(300 / rows) - 4} px. More cells make smaller widgets possible.` }),
      stepper("Columns", cols, 1, 8, (v) => set("cols", v)),
      stepper("Rows", rows, 1, 8, (v) => set("rows", v)),
      bad ? h("p", { class: "hint", style: { color: "var(--crit)" }, text: `${bad} widget${bad > 1 ? "s" : ""} no longer fit (marked red): move, resize or remove them.` }) : null,
      p.grid ? h("button", {
        type: "button", class: "ghost",
        onclick: () => {
          change((cfg) => delete cfg.pages[state.page].grid);
          fill();
        },
      }, `Use the default grid (${get("grid.cols", 4)} × ${get("grid.rows", 3)})`) : null,
    );
  };
  fill();
}

function themePopover(panel) {
  const fill = () => {
    const p = page();
    const own = !!p.theme;
    const pal = pageTheme(state.cfg, p);
    const setTheme = (patch) => {
      change((cfg) => {
        const pg = cfg.pages[state.page];
        pg.theme = { ...(pg.theme || {}), ...patch };
      });
      fill();
    };
    panel.replaceChildren(
      h("h4", { text: "Colors of this page" }),
      h("label", { class: "check" },
        h("input", {
          type: "checkbox", checked: !own,
          onchange: (e) => {
            change((cfg) => {
              const pg = cfg.pages[state.page];
              if (e.target.checked) delete pg.theme;
              else pg.theme = { name: pal.name, accent: pal.accent };
            });
            fill();
          },
        }), "Same as all pages (set in Settings)"),
      own ? h("div", {},
        h("div", { class: "field" }, h("span", { text: "Theme" }), themeCards(pal.name, (name) => setTheme({ name }))),
        h("div", { class: "field" }, h("span", { text: "Accent" }), swatches(pal.accent, (accent) => setTheme({ accent })))) : null,
    );
  };
  fill();
}

function knobsPopover(panel) {
  const p = page();
  const i = state.page;
  const ov = p.inputs_override || {};
  const buttons = get("inputs.buttons", 0);
  const rows = [];
  for (const [dev, label, gestures] of [
    ["enc1", "Knob 1", [["rotate", "Turn", true], ["rotate_pressed", "Press and turn", true], ["click", "Click", false], ["double", "Double click", false], ["long", "Long press", false]]],
    ["enc2", "Knob 2", [["rotate", "Turn", true], ["rotate_pressed", "Press and turn", true], ["click", "Click", false], ["double", "Double click", false], ["long", "Long press", false]]],
    ["btn1", "Button 1", [["click", "Click", false], ["long", "Long press", false]]],
    ["btn2", "Button 2", [["click", "Click", false], ["long", "Long press", false]]],
  ]) {
    if ((dev === "btn1" && buttons < 1) || (dev === "btn2" && buttons < 2)) continue;
    rows.push(h("div", { class: "group-title", text: label }));
    for (const [g, gl, rotate] of gestures) {
      const global = get(["inputs", dev, g], "none");
      const sel = actionSelect(rotate, ov[dev]?.[g], `Default: ${ACTION_NAMES[global] || global}`, (v) => {
        change((cfg) => {
          const po = cfg.pages[i];
          po.inputs_override ??= {};
          po.inputs_override[dev] ??= {};
          if (v === "") delete po.inputs_override[dev][g];
          else po.inputs_override[dev][g] = v;
          if (!Object.keys(po.inputs_override[dev]).length) delete po.inputs_override[dev];
          if (!Object.keys(po.inputs_override).length) delete po.inputs_override;
        });
      });
      rows.push(h("label", { class: "field" }, h("span", { text: gl }), sel));
    }
  }
  panel.style.width = "360px";
  panel.append(h("h4", { text: "Knobs on this page" }), h("p", { class: "hint", text: "Only while this page is shown. The defaults are in Settings." }), ...rows);
}

function morePopover(panel) {
  const i = state.page;
  const n = state.cfg.pages.length;
  const p = page();
  const item = (ic, text, fn, opts = {}) => h("button", {
    type: "button", class: opts.danger ? "danger" : "", disabled: opts.disabled,
    onclick: () => {
      closePopover();
      fn();
    },
  }, icon(ic, 16), text);
  panel.style.width = "220px";
  panel.append(h("div", { class: "menu" },
    item("copy", "Duplicate page", () => {
      change((cfg) => {
        const copy = JSON.parse(JSON.stringify(cfg.pages[i]));
        copy.name = `${p.name || p.id} copy`.slice(0, 31);
        copy.id = slug(copy.name);
        cfg.pages.splice(i + 1, 0, copy);
      });
      selectPage(i + 1);
    }, { disabled: n >= state.catalog.limits.pages }),
    item("up", "Move up", () => movePage(-1), { disabled: i === 0 }),
    item("down", "Move down", () => movePage(1), { disabled: i === n - 1 }),
    item("trash", "Delete page", () => {
      if ((p.widgets || []).length && !confirm(`Delete the page "${p.name || p.id}" and its ${p.widgets.length} widgets?`)) return;
      change((cfg) => cfg.pages.splice(i, 1));
      selectPage(Math.min(i, state.cfg.pages.length - 1));
    }, { danger: true, disabled: n <= 1 })));
}

function movePage(delta) {
  const i = state.page;
  change((cfg) => {
    const [p] = cfg.pages.splice(i, 1);
    cfg.pages.splice(i + delta, 0, p);
  });
  selectPage(i + delta);
}

// ---- screen ----

function renderScreenNow() {
  if (!state.cfg) return;
  const { grid: g, cards } = renderScreen(screenEl, state.cfg, state.page, state.data, state.catalog);
  gridEl = g;
  const { cols, rows } = grid();
  const ws = page().widgets || [];

  // Empty cells: click to add a widget there.
  const cells = h("div", { class: "cells", style: { gridTemplateColumns: `repeat(${cols}, 1fr)`, gridTemplateRows: `repeat(${rows}, 1fr)` } });
  for (let y = 0; y < rows; y++) {
    for (let x = 0; x < cols; x++) {
      if (!fits(x, y, 1, 1)) {
        cells.append(h("i", { style: { visibility: "hidden" } }));
        continue;
      }
      const target = state.placeAt && state.placeAt.x === x && state.placeAt.y === y;
      cells.append(h("div", {
        class: "cell" + (target ? " target" : ""), title: "Add a widget here",
        onpointerdown: (e) => e.stopPropagation(),
        onclick: () => {
          state.placeAt = { x, y };
          state.selected = -1;
          emit("selection");
        },
      }, "+"));
    }
  }
  g.prepend(cells);

  cards.forEach((card, i) => {
    const w = ws[i];
    card.classList.add("editable");
    if (!fits(w.x, w.y, w.w, w.h, [i]) || !big(w.type, w.w, w.h)) {
      card.classList.add("invalid");
      card.title = "Does not fit this grid: move, resize or remove it";
    }
    if (i === state.selected) {
      card.classList.add("selected");
      const handle = h("div", { class: "handle", title: "Drag to resize" });
      handle.addEventListener("pointerdown", (e) => startResize(e, i));
      card.append(handle);
    }
    card.addEventListener("pointerdown", (e) => startMove(e, i));
  });

  if (drag?.active && drag.ghost) {
    const r = cardRect(drag.ghost, cols, rows);
    const ghost = h("div", { class: "ghost-rect" + (drag.ghost.ok ? "" : " invalid") + (drag.ghost.swap !== undefined ? " swap" : ""), text: drag.ghost.text || "" });
    Object.assign(ghost.style, { left: r.x + "px", top: r.y + "px", width: r.w + "px", height: r.h + "px" });
    g.append(ghost);
    if (drag.index >= 0) cards[drag.index]?.classList.add("dragging");
  }
}

function listen(onMove, onEnd) {
  const move = (e) => onMove(e);
  const end = (e) => {
    window.removeEventListener("pointermove", move);
    window.removeEventListener("pointerup", end);
    window.removeEventListener("pointercancel", end);
    onEnd(e);
  };
  window.addEventListener("pointermove", move);
  window.addEventListener("pointerup", end);
  window.addEventListener("pointercancel", end);
}

// Moving onto exactly one other widget swaps them when each fits in the other's place.
function moveTarget(i, x, y) {
  const ws = page().widgets;
  const a = ws[i];
  if (fits(x, y, a.w, a.h, [i])) return { x, y, w: a.w, h: a.h, ok: true };
  const hit = ws.map((_, k) => k).filter((k) => k !== i && !(x >= ws[k].x + ws[k].w || ws[k].x >= x + a.w || y >= ws[k].y + ws[k].h || ws[k].y >= y + a.h));
  if (hit.length === 1) {
    const b = ws[hit[0]];
    const bOk = fits(a.x, a.y, b.w, b.h, [i, hit[0]]);
    const aOk = fits(x, y, a.w, a.h, [i, hit[0]]);
    const apart = a.x >= x + a.w || x >= a.x + b.w || a.y >= y + a.h || y >= a.y + b.h;
    if (bOk && aOk && apart) return { x, y, w: a.w, h: a.h, ok: true, swap: hit[0], text: "Swap" };
  }
  return { x, y, w: a.w, h: a.h, ok: false };
}

function startMove(e, i) {
  if (e.button !== 0 || e.target.classList.contains("handle")) return;
  e.preventDefault();
  screenEl.focus({ preventScroll: true });
  if (state.selected !== i || state.placeAt) {
    state.selected = i;
    state.placeAt = null;
    emit("selection");
  }
  const w = page().widgets[i];
  const c = cellAt(e);
  drag = { kind: "move", index: i, sx: e.clientX, sy: e.clientY, active: false, grab: { x: c.x - w.x, y: c.y - w.y }, ghost: null };
  listen((ev) => {
    if (!drag.active && Math.hypot(ev.clientX - drag.sx, ev.clientY - drag.sy) < 5) return;
    drag.active = true;
    const cc = cellAt(ev);
    const t = moveTarget(i, cc.x - drag.grab.x, cc.y - drag.grab.y);
    if (drag.ghost && drag.ghost.x === t.x && drag.ghost.y === t.y) return;
    drag.ghost = t;
    renderScreenNow();
  }, () => {
    const d = drag;
    drag = null;
    if (d.active && d.ghost?.ok && (d.ghost.x !== w.x || d.ghost.y !== w.y)) {
      change((cfg) => {
        const ws = cfg.pages[state.page].widgets;
        const a = ws[i];
        if (d.ghost.swap !== undefined) [ws[d.ghost.swap].x, ws[d.ghost.swap].y] = [a.x, a.y];
        a.x = d.ghost.x;
        a.y = d.ghost.y;
      });
    } else {
      renderScreenNow();
    }
  });
}

function startResize(e, i) {
  if (e.button !== 0) return;
  e.preventDefault();
  e.stopPropagation();
  const w = page().widgets[i];
  drag = { kind: "resize", index: i, active: true, ghost: { x: w.x, y: w.y, w: w.w, h: w.h, ok: true, text: `${w.w} × ${w.h}` } };
  renderScreenNow();
  listen((ev) => {
    const c = cellAt(ev);
    const nw = Math.max(1, c.x - w.x + 1);
    const nh = Math.max(1, c.y - w.y + 1);
    if (drag.ghost.w === nw && drag.ghost.h === nh) return;
    const ok = fits(w.x, w.y, nw, nh, [i]) && big(w.type, nw, nh);
    drag.ghost = { x: w.x, y: w.y, w: nw, h: nh, ok, text: ok ? `${nw} × ${nh}` : big(w.type, nw, nh) ? "No room" : "Too small" };
    renderScreenNow();
  }, () => {
    const d = drag;
    drag = null;
    if (d.ghost.ok && (d.ghost.w !== w.w || d.ghost.h !== w.h)) {
      change((cfg) => Object.assign(cfg.pages[state.page].widgets[i], { w: d.ghost.w, h: d.ghost.h }));
    } else {
      renderScreenNow();
    }
  });
}

// A gallery tile: click to add, or drag onto the screen.
function startTileDrag(e, type) {
  if (e.button !== 0) return;
  const sx = e.clientX;
  const sy = e.clientY;
  drag = { kind: "add", index: -1, active: false, ghost: null };
  listen((ev) => {
    if (!drag.active && Math.hypot(ev.clientX - sx, ev.clientY - sy) < 6) return;
    drag.active = true;
    const c = cellAt(ev);
    const p = c.inside ? placement(type, c) : null;
    const prev = JSON.stringify(drag.ghost);
    drag.ghost = p ? { ...p, ok: true, text: WIDGETS[type].name } : c.inside ? { x: c.x, y: c.y, w: 1, h: 1, ok: false } : null;
    if (prev !== JSON.stringify(drag.ghost)) renderScreenNow();
  }, (ev) => {
    const d = drag;
    drag = null;
    if (!d.active) {
      addWidget(type, state.placeAt);
      return;
    }
    const c = cellAt(ev);
    if (c.inside && d.ghost?.ok) addWidget(type, c);
    else renderScreenNow();
  });
}

function onScreenKey(e) {
  if (state.selected < 0) return;
  const w = page().widgets[state.selected];
  const k = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
  if (k) {
    e.preventDefault();
    if (e.shiftKey) {
      const nw = w.w + k[0];
      const nh = w.h + k[1];
      if (fits(w.x, w.y, nw, nh, [state.selected]) && big(w.type, nw, nh)) change((cfg) => Object.assign(cfg.pages[state.page].widgets[state.selected], { w: nw, h: nh }));
    } else {
      const t = moveTarget(state.selected, w.x + k[0], w.y + k[1]);
      if (t.ok) {
        change((cfg) => {
          const ws = cfg.pages[state.page].widgets;
          if (t.swap !== undefined) [ws[t.swap].x, ws[t.swap].y] = [w.x, w.y];
          ws[state.selected].x = t.x;
          ws[state.selected].y = t.y;
        });
      }
    }
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
  change((cfg) => cfg.pages[state.page].widgets.splice(i, 1));
  emit("selection");
}

// ---- inspector: gallery ----

function miniPreview(type) {
  const mini = h("div", { class: "mini" });
  const [w, hh] = WIDGETS[type].size;
  const r = { w: w * 120 - 4, h: hh * 100 - 4 };
  const scale = Math.min(140 / r.w, 70 / r.h, 1);
  const pal = pageTheme(state.cfg, page());
  const scr = h("div", { class: "screen", style: { width: r.w + "px", height: r.h + "px", transform: `scale(${scale})`, left: (140 - r.w * scale) / 2 + "px", top: (70 - r.h * scale) / 2 + "px", background: "transparent", color: pal.text } });
  scr.style.setProperty("--d-dim", pal.dim);
  scr.style.setProperty("--d-faint", pal.faint);
  scr.style.setProperty("--d-accent", pal.accent);
  const card = h("div", { class: "card", style: { left: "0", top: "0", width: r.w + "px", height: r.h + "px" } });
  const wd = newWidget(type, { x: 0, y: 0, w, h: hh });
  renderWidget(card, wd, r, state.cfg, pal, state.data, state.catalog);
  scr.append(card);
  mini.append(scr);
  mini.style.background = pal.bg;
  return mini;
}

function renderGallery(box) {
  const full = (page().widgets || []).length >= state.catalog.limits.widgets;
  const at = state.placeAt;
  const groups = new Map();
  for (const [type, meta] of Object.entries(WIDGETS)) {
    if (!state.catalog.widgets.some((w) => w.type === type)) continue;
    if (!groups.has(meta.group)) groups.set(meta.group, []);
    groups.get(meta.group).push(type);
  }
  box.replaceChildren(
    h("div", { class: "insp-head" }, h("h2", {}, icon("plus", 18), at ? "Add here" : "Add a widget"),
      at ? h("button", { type: "button", class: "icon-btn", title: "Cancel", "aria-label": "Cancel", onclick: () => { state.placeAt = null; emit("selection"); } }, icon("close", 16)) : null),
    h("p", { class: "insp-sub", text: full ? "This page is full (16 widgets)." : at ? `Column ${at.x + 1}, row ${at.y + 1}: pick a widget for this spot.` : "Click to add, or drag onto the screen. Click an empty cell to choose the spot first." }),
    ...[...groups].flatMap(([group, types]) => [
      h("div", { class: "group-title", text: group }),
      h("div", { class: "tiles" }, ...types.map((type) => {
        const room = !full && !!placement(type, at);
        return h("button", {
          type: "button", class: "tile", disabled: !room,
          title: room ? `Add ${WIDGETS[type].name}` : at ? "No room at that spot" : "No room on this page",
          onpointerdown: (e) => room && startTileDrag(e, type),
          onkeydown: (e) => (e.key === "Enter" || e.key === " ") && (e.preventDefault(), addWidget(type, at)),
        }, miniPreview(type), h("strong", { text: WIDGETS[type].name }), h("small", { text: WIDGETS[type].desc }));
      })),
    ]),
  );
}

// ---- inspector: selected widget ----

function field(label, control, hint) {
  return h("label", { class: "field" }, h("span", { text: label }), control, hint ? h("small", { text: hint }) : null);
}

function textInput(w, key, placeholder, maxBytes) {
  return h("input", {
    type: "text", value: w[key] ?? "", placeholder: placeholder ?? "",
    oninput: (e) => {
      let v = e.target.value;
      while (new TextEncoder().encode(v).length > maxBytes) v = v.slice(0, -1);
      if (v !== e.target.value) e.target.value = v;
      const i = state.selected;
      change((cfg) => {
        const t = cfg.pages[state.page].widgets[i];
        if (v === "") delete t[key];
        else t[key] = v;
      }, `w${i}.${key}`);
    },
  });
}

function numberInput(w, key, placeholder) {
  return h("input", {
    type: "number", step: "any", value: w[key] ?? "", placeholder: String(placeholder ?? ""),
    oninput: (e) => {
      const v = e.target.value.trim();
      const i = state.selected;
      change((cfg) => {
        const t = cfg.pages[state.page].widgets[i];
        if (v === "" || Number.isNaN(Number(v))) delete t[key];
        else t[key] = Number(v);
      }, `w${i}.${key}`);
    },
  });
}

function segmented(options, value, onPick) {
  return h("div", { class: "segmented", role: "radiogroup" }, ...options.map(([v, label]) =>
    h("button", { type: "button", class: v === value ? "on" : "", role: "radio", "aria-checked": String(v === value), onclick: () => onPick(v) }, label)));
}

// Patch the selected widget; undefined removes a key.
function setWidget(patch) {
  const i = state.selected;
  change((cfg) => {
    const t = cfg.pages[state.page].widgets[i];
    for (const [k, v] of Object.entries(patch)) {
      if (v === undefined) delete t[k];
      else t[k] = v;
    }
  });
  renderInspector();
}

function liveText(id) {
  const s = state.data.sources.get(id);
  return s?.available ? `${Math.round(s.value * 10) / 10} ${s.unit}` : "unavailable";
}

function sourcePicker(w) {
  const name = state.data.sources.get(w.source)?.name || w.source;
  return h("button", {
    type: "button", class: "picker",
    onclick: (e) => openPopover(e.currentTarget, (panel) => {
      panel.style.width = "340px";
      const list = h("div", { class: "pick-list" });
      const search = h("input", { type: "search", placeholder: "Search", "aria-label": "Search data sources" });
      const fillList = () => {
        const q = search.value.trim().toLowerCase();
        const items = state.catalog.sources.filter((s) => !q || s.id.includes(q) || (state.data.sources.get(s.id)?.name || "").toLowerCase().includes(q));
        const groups = new Map();
        for (const s of items) {
          const g = SOURCE_GROUPS.find(([p]) => s.id.startsWith(p))?.[1] || "Other";
          if (!groups.has(g)) groups.set(g, []);
          groups.get(g).push(s);
        }
        list.replaceChildren(...[...groups].flatMap(([g, ss]) => [h("div", { class: "group-title", text: g }), ...ss.map((s) => {
          const live = state.data.sources.get(s.id);
          return h("button", {
            type: "button", class: "pick-item" + (s.id === w.source ? " on" : "") + (live?.available ? "" : " na"),
            onclick: () => {
              const old = shortName(state.data.sources.get(w.source)?.name || "");
              const patch = { source: s.id };
              if (!w.label || w.label === old) patch.label = shortName(live?.name || "");
              closePopover();
              setWidget(patch);
            },
          }, h("span", { text: live?.name || s.id }), h("span", { class: "val", text: liveText(s.id) }));
        })]));
        if (!items.length) list.append(h("p", { class: "hint", text: "Nothing matches." }));
      };
      search.addEventListener("input", fillList);
      panel.append(search, list);
      fillList();
      search.focus();
    }),
  }, h("span", { text: name }), h("span", { class: "val", text: liveText(w.source) }));
}

function imagePicker(w) {
  const input = h("input", { type: "file", accept: "image/png,image/jpeg,image/gif,image/bmp", hidden: true, onchange: (e) => e.target.files[0] && upload(e.target.files[0]) });
  const uploadTile = h("button", {
    type: "button", class: "upload", title: "Upload PNG, JPEG, GIF or BMP (up to 20 MB)",
    onclick: () => input.click(),
    ondragover: (e) => {
      e.preventDefault();
      uploadTile.classList.add("drop");
    },
    ondragleave: () => uploadTile.classList.remove("drop"),
    ondrop: (e) => {
      e.preventDefault();
      uploadTile.classList.remove("drop");
      if (e.dataTransfer.files[0]) upload(e.dataTransfer.files[0]);
    },
  }, icon("upload", 20), "Upload or drop");
  async function upload(file) {
    uploadTile.replaceChildren("Uploading...");
    const form = new FormData();
    form.append("file", file);
    const r = await api("/api/assets", { method: "POST", body: form }).catch(() => null);
    if (!r?.ok) {
      toast(r?.body?.error || "Upload failed", true);
      renderInspector();
      return;
    }
    await loadAssets();
    setWidget({ asset: r.body.id });
    toast(`Uploaded ${r.body.name}${r.body.frames > 1 ? ` (${r.body.frames} frames)` : ""}`);
  }
  return h("div", {},
    h("div", { class: "images" },
      uploadTile,
      ...state.assets.map((a) => h("button", { type: "button", class: a.id === w.asset ? "on" : "", title: `${a.name}, ${a.w}×${a.h}`, onclick: () => setWidget({ asset: a.id }) },
        h("img", { src: `/api/assets/${a.id}`, alt: a.name, loading: "lazy" }), a.frames > 1 ? h("span", { class: "badge", text: "GIF" }) : null))),
    input);
}

function sizeChips(w) {
  const { cols, rows } = grid();
  const opts = [];
  for (let hh = 1; hh <= rows; hh++) {
    for (let ww = 1; ww <= cols; ww++) if (big(w.type, ww, hh) && fits(w.x, w.y, ww, hh, [state.selected])) opts.push([ww, hh]);
  }
  opts.sort((a, b) => a[0] * a[1] - b[0] * b[1] || a[1] - b[1]);
  if (!opts.length) return h("p", { class: "insp-sub", text: "No size fits here; move it to a bigger free spot." });
  return h("div", { class: "chips" }, ...opts.slice(0, 16).map(([ww, hh]) =>
    h("button", { type: "button", class: ww === w.w && hh === w.h ? "on" : "", onclick: () => setWidget({ w: ww, h: hh }) }, `${ww}×${hh}`)));
}

function renderProps(box) {
  const w = page().widgets[state.selected];
  const meta = WIDGETS[w.type] || { name: w.type };
  const sections = [];
  const content = [];
  if (w.type.startsWith("metric_")) {
    content.push(field("Shows", sourcePicker(w)));
    content.push(field("Label", textInput(w, "label", w.source, 31)));
  } else if (w.type === "weather_forecast") {
    content.push(field("Forecast", segmented([["daily", "Days"], ["hourly", "Hours"]], w.mode || "daily", (v) => setWidget({ mode: v }))));
  } else if (w.type === "label") {
    content.push(field("Text", textInput(w, "text", "", 63)));
    content.push(field("Small title", textInput(w, "label", "optional", 31)));
  } else if (w.type === "image") {
    content.push(field("Picture", imagePicker(w), "PNG, JPEG, GIF (animated too) or BMP. Converted to this card's exact size."));
    content.push(field("Fit", segmented([["cover", "Fill the card"], ["contain", "Show it whole"]], w.fit || "cover", (v) => setWidget({ fit: v === "cover" ? undefined : v }))));
  }
  if (content.length) sections.push(h("div", { class: "section first" }, ...content));

  sections.push(h("div", { class: "section" + (content.length ? "" : " first") },
    h("h3", { text: "Size" }), sizeChips(w),
    h("p", { class: "insp-sub", style: { margin: "8px 0 4px" }, text: "Or drag the widget's corner on the screen. Arrow keys move it, Shift + arrows resize." })));

  if (w.type.startsWith("metric_")) {
    const d = effectiveStyle({ source: w.source }, state.catalog);
    const look = [field("Unit", textInput(w, "unit", d.unit ? `${d.unit} (automatic)` : "none", 11))];
    if (w.type !== "metric_value") {
      look.push(h("div", { class: "row2" }, field("Min", numberInput(w, "min", d.min)), field("Max", numberInput(w, "max", d.max))));
      look.push(h("div", { class: "row2" }, field("Warning at", numberInput(w, "warn", d.warn ?? "none")), field("Critical at", numberInput(w, "crit", d.crit ?? "none"))));
    }
    look.push(field("Decimals", segmented([[-1, "Auto"], [0, "0"], [1, "1"], [2, "2"]], w.decimals ?? -1, (v) => setWidget({ decimals: v < 0 ? undefined : v }))));
    sections.push(h("div", { class: "section" }, h("h3", { text: "Look" }), ...look));
  }

  box.replaceChildren(
    h("div", { class: "insp-head" },
      h("button", { type: "button", class: "icon-btn", title: "Back to adding widgets", "aria-label": "Back", onclick: () => { state.selected = -1; emit("selection"); } }, icon("back", 18)),
      h("h2", {}, icon(w.type, 18), meta.name),
      h("button", { type: "button", class: "icon-btn danger", title: "Remove widget (Delete)", "aria-label": "Remove widget", onclick: removeSelected }, icon("trash", 18))),
    h("p", { class: "insp-sub", text: `${w.w} × ${w.h} at column ${w.x + 1}, row ${w.y + 1}` }),
    ...sections,
  );
}

export async function loadAssets() {
  const r = await api("/api/assets").catch(() => null);
  state.assets = r?.ok ? r.body : [];
  emit("assets");
}

function renderInspector() {
  const box = $("inspector");
  const ws = page().widgets || [];
  if (state.selected >= 0 && ws[state.selected]) renderProps(box);
  else renderGallery(box);
}

// ---- wiring ----

function fitScreen() {
  const wrap = $("screen-wrap");
  const scale = Math.max(0.5, Math.min(2, (wrap.clientWidth - 2 * FRAME) / SCREEN_W));
  wrap.style.setProperty("--scale", scale);
  wrap.style.height = SCREEN_H * scale + 2 * FRAME + "px";
}

export function renderAll() {
  if (!state.cfg) return;
  if (state.page >= state.cfg.pages.length) state.page = state.cfg.pages.length - 1;
  renderRail();
  renderStageHead();
  renderScreenNow();
  renderInspector();
  $("stage-hint").textContent = "Drag widgets to move them (drop on another to swap), drag the corner to resize, click an empty cell to add there.";
}

export function initEditor() {
  screenEl = $("screen");
  $("screen-wrap").prepend(h("div", { class: "screen-frame" }));
  screenEl.addEventListener("keydown", onScreenKey);
  $("page-add").addEventListener("click", addPage);
  $("page-name").addEventListener("input", (e) => {
    const v = e.target.value;
    const i = state.page;
    change((cfg) => {
      cfg.pages[i].name = v;
    }, `p${i}.name`);
  });
  new ResizeObserver(fitScreen).observe($("screen-wrap"));
  fitScreen();

  on("selection", () => {
    renderScreenNow();
    renderInspector();
  });
  on("structure", renderAll);
  on("assets", () => state.cfg && renderInspector());
  on("config", () => {
    renderScreenNow();
    renderStageHead();
    renderRail();
    if (!$("inspector").contains(document.activeElement)) renderInspector();
  });
  let tick = 0;
  on("live", () => {
    if (drag || !state.cfg) return;
    renderScreenNow();
    if (++tick % 3 === 0) refreshThumbs();
  });
}

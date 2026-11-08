// Editor state: the working copy of the config, what it was loaded from, an undo history, live
// data from the host, and a tiny change notification. Views read from here and change the config
// only through change(), so every edit can be undone.
"use strict";

export const state = {
  cfg: null,         // working copy
  savedJson: "",     // JSON of the config as loaded or last saved, to detect changes
  baseCrc: null,     // CRC of that config (PUT ?base= refuses to overwrite a newer one)
  catalog: null,     // /api/catalog
  assets: [],        // /api/assets
  page: 0,           // page shown in the editor
  selected: -1,      // selected widget index on that page
  placeAt: null,     // {x, y}: empty cell the next added widget goes to
  error: null,       // validation error of the working copy ("path: reason"), null when valid
  data: {
    connected: false,
    status: null,
    sources: new Map(),  // id -> {name, unit, available, value}
    history: new Map(),  // id -> recent values (graph preview)
    media: null,
    artUrl: null,
    weather: null,
    room: null,
  },
};

const listeners = new Map();

/** on("config" | "live" | "selection" | "status" | "assets", fn) */
export function on(topic, fn) {
  if (!listeners.has(topic)) listeners.set(topic, []);
  listeners.get(topic).push(fn);
}

export function emit(topic) {
  for (const fn of listeners.get(topic) || []) fn();
}

export function dirty() {
  return state.cfg !== null && JSON.stringify(state.cfg) !== state.savedJson;
}

// ---- undo history ----

const undoStack = [];
const redoStack = [];
let lastKey = null;
let lastAt = 0;

/** Replaces the working copy (load, save) and clears the history. */
export function reset(cfg) {
  state.cfg = cfg;
  state.savedJson = JSON.stringify(cfg);
  undoStack.length = 0;
  redoStack.length = 0;
  lastKey = null;
}

/**
 * Changes the config. `key` groups quick repeated edits of one field (typing) into one undo step.
 */
export function change(fn, key = null) {
  const now = Date.now();
  if (!(key && key === lastKey && now - lastAt < 1500)) {
    undoStack.push(JSON.stringify(state.cfg));
    if (undoStack.length > 100) undoStack.shift();
  }
  lastKey = key;
  lastAt = now;
  redoStack.length = 0;
  fn(state.cfg);
  emit("config");
}

export function canUndo() {
  return undoStack.length > 0;
}

export function canRedo() {
  return redoStack.length > 0;
}

export function undo() {
  if (!undoStack.length) return;
  redoStack.push(JSON.stringify(state.cfg));
  state.cfg = JSON.parse(undoStack.pop());
  lastKey = null;
  fixSelection();
  emit("config");
  emit("structure");
}

export function redo() {
  if (!redoStack.length) return;
  undoStack.push(JSON.stringify(state.cfg));
  state.cfg = JSON.parse(redoStack.pop());
  lastKey = null;
  fixSelection();
  emit("config");
  emit("structure");
}

function fixSelection() {
  if (state.page >= state.cfg.pages.length) state.page = state.cfg.pages.length - 1;
  const n = state.cfg.pages[state.page]?.widgets?.length ?? 0;
  if (state.selected >= n) state.selected = -1;
}

// ---- paths like "display.night.from" or ["pages", 0, "name"] ----

function parts(path) {
  return Array.isArray(path) ? path : path.split(".");
}

export function get(path, fallback) {
  let o = state.cfg;
  for (const p of parts(path)) {
    if (o === null || o === undefined) return fallback;
    o = o[p];
  }
  return o === undefined || o === null ? fallback : o;
}

/** Sets a value through change(); undefined removes the key. Creates missing objects. */
export function set(path, value, key) {
  const ps = parts(path);
  change((cfg) => {
    let o = cfg;
    for (const p of ps.slice(0, -1)) {
      if (o[p] === null || typeof o[p] !== "object") o[p] = {};
      o = o[p];
    }
    const last = ps[ps.length - 1];
    if (value === undefined) delete o[last];
    else o[last] = value;
  }, key ?? ps.join("."));
}

// ---- host API ----

export async function api(path, options) {
  const res = await fetch(path, options);
  const text = await res.text();
  let body = null;
  try {
    body = text ? JSON.parse(text) : null;
  } catch {
    body = text;
  }
  return { ok: res.ok, status: res.status, body, headers: res.headers };
}

export function postJson(path, value) {
  return api(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(value) });
}

// ---- DOM helpers shared by the views ----

export function h(tag, attrs, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (k === "text") e.textContent = v;
    else if (k === "style" && typeof v === "object") Object.assign(e.style, v);
    else if (v === true) e.setAttribute(k, "");
    else e.setAttribute(k, v);
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) continue;
    e.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return e;
}

export function toast(text, bad = false) {
  const t = h("div", { class: "toast" + (bad ? " bad" : ""), text });
  document.getElementById("toasts").append(t);
  setTimeout(() => t.remove(), bad ? 7000 : 3500);
}

// ---- popover (one at a time) ----

let popAnchor = null;

export function closePopover() {
  const p = document.getElementById("popover");
  p.hidden = true;
  p.replaceChildren();
  popAnchor = null;
}

/** Opens the popover under `anchor`; `fill(panel)` builds its content. Clicking the anchor again closes it. */
export function openPopover(anchor, fill) {
  const p = document.getElementById("popover");
  if (popAnchor === anchor && !p.hidden) {
    closePopover();
    return;
  }
  popAnchor = anchor;
  p.replaceChildren();
  fill(p);
  p.hidden = false;
  const r = anchor.getBoundingClientRect();
  const w = p.offsetWidth;
  p.style.left = Math.max(12, Math.min(r.left, window.innerWidth - w - 12)) + "px";
  const below = r.bottom + 6;
  p.style.top = (below + p.offsetHeight > window.innerHeight - 12 ? Math.max(12, r.top - p.offsetHeight - 6) : below) + "px";
}

document.addEventListener("pointerdown", (e) => {
  const p = document.getElementById("popover");
  if (!p || p.hidden || p.contains(e.target) || (popAnchor && popAnchor.contains(e.target))) return;
  closePopover();
});
document.addEventListener("keydown", (e) => {
  if (e.key === "Escape") closePopover();
});

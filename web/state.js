// Editor state: the working copy of the config, what it was loaded from, live data from the host,
// and a tiny change notification. Every view reads from here and writes through set()/update().
"use strict";

export const state = {
  cfg: null,         // working copy (edited in place)
  savedJson: "",     // JSON of the config as loaded or last saved, to detect changes
  baseCrc: null,     // CRC of that config (PUT ?base= refuses to overwrite a newer one)
  catalog: null,     // /api/catalog
  page: 0,           // page shown in the editor
  selected: -1,      // selected widget index on that page
  error: null,       // validation error of the working copy ("path: reason"), null when valid
  bytes: 0,          // size of the working copy
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

/** on("config" | "live" | "selection" | "status", fn) */
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

/** Sets a value; undefined (or "" when `emptyDeletes`) removes the key. Creates missing objects. */
export function set(path, value, emptyDeletes = false) {
  const ps = parts(path);
  let o = state.cfg;
  for (const p of ps.slice(0, -1)) {
    if (o[p] === null || typeof o[p] !== "object") o[p] = {};
    o = o[p];
  }
  const last = ps[ps.length - 1];
  if (value === undefined || (emptyDeletes && value === "")) delete o[last];
  else o[last] = value;
  emit("config");
}

export function update(fn) {
  fn(state.cfg);
  emit("config");
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

export function putJson(path, value) {
  return api(path, { method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(value) });
}

export function postJson(path, value) {
  return api(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(value) });
}

// ---- small DOM helpers shared by the views ----

export function h(tag, attrs, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (k === "text") e.textContent = v;
    else if (v === true) e.setAttribute(k, "");
    else e.setAttribute(k, v);
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) continue;
    e.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return e;
}

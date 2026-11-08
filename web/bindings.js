// What a knob press or button does: a built-in action, a key shortcut (sent by the device's USB
// keyboard, works without this app) or a macro (run by this app). Shared by Settings and the
// page knob overrides. Key names follow docs/config-schema.md, "Buttons and macros".
"use strict";

import { state, h } from "./state.js";

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

// KeyboardEvent.code -> key name (positions, like the device's USB keyboard).
const CODE_NAMES = {
  Enter: "enter", Escape: "esc", Backspace: "backspace", Tab: "tab", Space: "space", Minus: "minus", Equal: "equal",
  BracketLeft: "lbracket", BracketRight: "rbracket", Backslash: "backslash", Semicolon: "semicolon", Quote: "quote",
  Backquote: "grave", Comma: "comma", Period: "period", Slash: "slash", CapsLock: "capslock", PrintScreen: "printscreen",
  ScrollLock: "scrolllock", Pause: "pause", Insert: "insert", Home: "home", PageUp: "pgup", Delete: "delete", End: "end",
  PageDown: "pgdn", ArrowRight: "right", ArrowLeft: "left", ArrowDown: "down", ArrowUp: "up", NumLock: "numlock",
  NumpadDivide: "numdiv", NumpadMultiply: "nummul", NumpadSubtract: "numsub", NumpadAdd: "numadd", NumpadEnter: "numenter",
  NumpadDecimal: "numdot", IntlBackslash: "intl", ContextMenu: "menu",
};

function codeName(code) {
  if (/^Key[A-Z]$/.test(code)) return code.slice(3).toLowerCase();
  if (/^Digit[0-9]$/.test(code)) return code.slice(5);
  if (/^F([1-9]|1[0-9]|2[0-4])$/.test(code)) return code.toLowerCase();
  if (/^Numpad[0-9]$/.test(code)) return "num" + code.slice(6);
  return CODE_NAMES[code] || null;
}

const MOD_CODES = new Set(["ControlLeft", "ControlRight", "ShiftLeft", "ShiftRight", "AltLeft", "AltRight", "MetaLeft", "MetaRight"]);

/** "ctrl+shift+m" -> "Ctrl + Shift + M" */
export function prettyKeys(keys) {
  return (keys || "").split("+").filter(Boolean).map((k) => ({ ctrl: "Ctrl", shift: "Shift", alt: "Alt", win: "Win", altgr: "AltGr" }[k] || k.toUpperCase())).join(" + ");
}

/**
 * Records a shortcut: focus it and press the keys. F13 to F24 (no such keys on most keyboards)
 * are offered as buttons.
 */
export function keysRecorder(value, onRecordOuter) {
  // Keep the F13-F24 chips in step with what was recorded.
  const onRecord = (k) => {
    for (const b of fkeys.querySelectorAll("button")) b.classList.toggle("on", b.textContent.toLowerCase() === k);
    input.value = prettyKeys(k);
    onRecordOuter(k);
  };
  const input = h("input", {
    type: "text", readonly: true, value: prettyKeys(value), placeholder: "Click, then press the keys",
    "aria-label": "Key shortcut, click and press the keys",
    onkeydown: (e) => {
      if (e.key === "Tab" && !e.ctrlKey && !e.altKey) return;  // keep keyboard navigation
      e.preventDefault();
      const altgr = e.code === "AltRight" || e.getModifierState("AltGraph");
      const mods = [];
      if (e.ctrlKey && !altgr) mods.push("ctrl");
      if (e.shiftKey) mods.push("shift");
      if (e.altKey && !altgr) mods.push("alt");
      if (e.metaKey) mods.push("win");
      if (altgr) mods.push("altgr");
      const key = MOD_CODES.has(e.code) ? null : codeName(e.code);
      if (!key && !MOD_CODES.has(e.code)) return;  // a key the device cannot send
      input.value = prettyKeys([...mods, key].filter(Boolean).join("+"));
      input.dataset.keys = [...mods, key].filter(Boolean).join("+");
      if (key) onRecord(input.dataset.keys);
    },
    // A modifier-only shortcut (e.g. Ctrl + Shift) is taken when the keys are released.
    onkeyup: () => {
      const k = input.dataset.keys;
      if (k && !k.split("+").some((p) => !["ctrl", "shift", "alt", "win", "altgr"].includes(p))) onRecord(k);
    },
    onfocus: () => {
      input.placeholder = "Press the keys now";
    },
    onblur: () => {
      input.placeholder = "Click, then press the keys";
    },
  });
  const fkeys = h("div", { class: "chips small" }, ...Array.from({ length: 12 }, (_, i) => `f${i + 13}`).map((f) =>
    h("button", { type: "button", class: value === f ? "on" : "", title: "Keys most keyboards do not have; good for app hotkeys", onclick: () => onRecord(f) }, f.toUpperCase())));
  return h("div", { class: "recorder" }, input, fkeys);
}

/**
 * Picker for a press gesture. `value` is an action name, {keys}, {macro} or undefined (only when
 * `emptyLabel` is given, e.g. "Default: ..." for page overrides). onChange gets the new value
 * (undefined for the empty choice).
 */
export function bindingControl(value, onChange, emptyLabel) {
  const macros = state.cfg.macros || [];
  const kind = value === undefined ? "" : typeof value === "string" ? "action" : value.keys !== undefined ? "keys" : "macro";
  const wrap = h("div", { class: "binding" });
  const actions = state.catalog.actions.filter((a) => a === "none" || !state.catalog.rotate_actions.includes(a));
  const sel = h("select", {
    onchange: (e) => {
      const v = e.target.value;
      if (v === "") onChange(undefined);
      else if (v === "__keys") renderKeys(value?.keys);
      else if (v.startsWith("__macro:")) onChange({ macro: v.slice(8) });
      else onChange(v);
    },
  });
  if (emptyLabel) sel.append(h("option", { value: "", text: emptyLabel, selected: kind === "" }));
  sel.append(h("optgroup", { label: "Actions" }, ...actions.map((a) => h("option", { value: a, text: ACTION_NAMES[a] || a, selected: kind === "action" && value === a }))));
  sel.append(h("optgroup", { label: "Keyboard" }, h("option", { value: "__keys", text: "Key shortcut…", selected: kind === "keys" })));
  sel.append(h("optgroup", { label: "Macros" }, ...(macros.length
    ? macros.map((m) => h("option", { value: `__macro:${m.id}`, text: m.name || m.id, selected: kind === "macro" && value.macro === m.id }))
    : [h("option", { disabled: true, text: "No macros yet (Settings, Macros)" })])));
  if (kind === "action" && !actions.includes(value)) sel.append(h("option", { value, text: `${value} (not allowed here)`, selected: true }));
  wrap.append(sel);

  function renderKeys(keys) {
    wrap.querySelector(".recorder")?.remove();
    wrap.append(keysRecorder(keys, (k) => onChange({ keys: k })));
    if (!keys) wrap.querySelector(".recorder input").focus();
  }
  if (kind === "keys") renderKeys(value.keys);
  return wrap;
}

/** Short text for a binding (lists, tooltips). */
export function bindingText(value) {
  if (value === undefined) return "";
  if (typeof value === "string") return ACTION_NAMES[value] || value;
  if (value.keys !== undefined) return prettyKeys(value.keys);
  const m = (state.cfg.macros || []).find((x) => x.id === value.macro);
  return `Macro: ${m?.name || value.macro}`;
}

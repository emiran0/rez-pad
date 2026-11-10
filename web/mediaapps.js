// Settings > Media: which app Now Playing shows. "On this PC now" lists the apps with a media
// session (switch to one with a click); "Preferred apps" is media.app_priority as an ordered
// list (drag or arrows to reorder, add from the apps seen on this PC or by typing a name).
"use strict";

import { state, h, api, get, set } from "./state.js";
import { icon } from "./icons.js";

const ANY = "*";
let data = { apps: [], known: [] };
let box = null;
let timer = 0;

async function load() {
  clearTimeout(timer);
  const r = await api("/api/media/apps").catch(() => null);
  if (r?.ok) data = r.body;
  render();
  if (box?.isConnected && !document.hidden) timer = setTimeout(load, 3000);
}

const list = () => get("media.app_priority", ["Spotify", ANY]);

function save(next) {
  set("media.app_priority", next.length ? next : undefined);
  render();
}

/** The display name for an entry (entries are names or ids, matched as parts of either). */
function label(entry) {
  if (entry === ANY) return "Any other app";
  const k = [...data.known, ...data.apps].find((a) => a.id.toLowerCase() === entry.toLowerCase());
  return k ? k.name : entry;
}

function running(entry) {
  if (entry === ANY) return null;
  const e = entry.toLowerCase();
  return data.apps.find((a) => a.name.toLowerCase().includes(e) || a.id.toLowerCase().includes(e)) || null;
}

let dragFrom = -1;

function prefRow(entry, i, all) {
  const app = running(entry);
  const move = (d) => {
    const next = [...all];
    [next[i], next[i + d]] = [next[i + d], next[i]];
    save(next);
  };
  return h("li", {
    class: "pref-row" + (entry === ANY ? " any" : ""), draggable: "true",
    ondragstart: (e) => {
      dragFrom = i;
      e.dataTransfer.effectAllowed = "move";
      e.currentTarget.classList.add("dragging");
    },
    ondragend: (e) => e.currentTarget.classList.remove("dragging"),
    ondragover: (e) => {
      e.preventDefault();
      e.currentTarget.classList.add("over");
    },
    ondragleave: (e) => e.currentTarget.classList.remove("over"),
    ondrop: (e) => {
      e.preventDefault();
      if (dragFrom < 0 || dragFrom === i) return;
      const next = [...all];
      const [x] = next.splice(dragFrom, 1);
      next.splice(i, 0, x);
      dragFrom = -1;
      save(next);
    },
  },
  h("span", { class: "grip", "aria-hidden": "true", text: "≡" }),
  h("b", { class: "rank", text: String(i + 1) }),
  h("span", { class: "pref-name" }, h("strong", { text: label(entry) }),
    h("small", { text: entry === ANY ? "Everything not listed above" : app ? `Open now${app.state === "playing" ? ", playing" : ""}` : "Not open now" })),
  h("button", { type: "button", class: "icon-btn", title: "Up", "aria-label": "Move up", disabled: i === 0, onclick: () => move(-1) }, icon("up", 16)),
  h("button", { type: "button", class: "icon-btn", title: "Down", "aria-label": "Move down", disabled: i === all.length - 1, onclick: () => move(1) }, icon("down", 16)),
  h("button", { type: "button", class: "icon-btn", title: "Remove", "aria-label": "Remove", onclick: () => save(all.filter((_, k) => k !== i)) }, icon("close", 16)));
}

function addRow(all) {
  const has = (name) => all.some((x) => x.toLowerCase() === name.toLowerCase());
  const offers = [...new Map([...data.apps, ...data.known].map((a) => [a.name.toLowerCase(), a.name])).values()]
    .filter((n) => !has(n)).sort((a, b) => a.localeCompare(b));
  const text = h("input", { type: "text", placeholder: "Or type a name", maxlength: 40 });
  const add = (name) => {
    const v = (name || "").trim();
    if (!v || has(v) || all.length >= 16) return;
    // New names go above "Any other app", which only makes sense last.
    const at = all.indexOf(ANY) >= 0 ? all.indexOf(ANY) : all.length;
    const next = [...all];
    next.splice(at, 0, v);
    save(next);
  };
  text.addEventListener("keydown", (e) => e.key === "Enter" && (e.preventDefault(), add(text.value)));
  return h("div", { class: "pref-add" },
    h("select", { onchange: (e) => add(e.target.value), disabled: !offers.length || all.length >= 16 },
      h("option", { value: "", text: offers.length ? "Add an app seen on this PC..." : "No other apps seen yet" }),
      ...offers.map((n) => h("option", { value: n, text: n }))),
    text,
    h("button", { type: "button", onclick: () => add(text.value), disabled: all.length >= 16 }, icon("plus", 16), "Add"),
    has(ANY) ? null : h("button", { type: "button", class: "ghost", onclick: () => save([...all, ANY]) }, "Add \"any other app\""));
}

function render() {
  if (!box?.isConnected) return;
  const all = list();
  const connected = !!state.data.connected;
  box.replaceChildren(
    h("h3", { text: "On this PC now" }),
    data.apps.length
      ? h("ul", { class: "media-apps" }, ...data.apps.map((a) => h("li", { class: a.current ? "current" : "" },
        h("i", { class: "dot " + a.state }),
        h("span", { class: "pref-name" }, h("strong", { text: a.name }), h("small", { text: a.state === "playing" ? "Playing" : "Paused" })),
        a.current ? h("span", { class: "badge-on", text: "On rez-pad" })
          : h("button", { type: "button", disabled: !connected, onclick: async () => {
            await api(`/api/media/apps/choose?id=${encodeURIComponent(a.id)}`, { method: "POST" }).catch(() => null);
            load();
          } }, "Show on rez-pad"))))
      : h("p", { class: "hint", text: "No app is playing or paused right now." }),
    h("p", { class: "hint", text: "Now Playing stays on the app you used last, also when you pause it, until another app starts playing. Switch with a knob: map \"Switch media app\" to a turn or \"Next media app\" to a press under Knobs and buttons." }),
    h("h3", { text: "Preferred apps" }),
    h("p", { class: "hint", text: "Which app to show when none was used yet (after a restart, or when the one on screen closes), and which one wins when several start at once. Drag to reorder." }),
    h("ol", { class: "pref-list" }, ...all.map((e, i) => prefRow(e, i, all))),
    addRow(all));
}

/** The card body (keeps itself current while it is on screen). */
export function mediaAppsCard() {
  box = h("div", { class: "media-apps-card" });
  render();
  load();
  return box;
}

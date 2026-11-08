// Data source browser: pick a value step by step (where it comes from, then a category, then the
// value) instead of one long list. Used by metric widgets (editor) and status bar items
// (settings). Live values come from state.data.sources (polled by app.js).
"use strict";

import { state, h, openPopover, closePopover } from "./state.js";

// This PC's categories, by id prefix.
const PC_GROUPS = [
  ["sys.cpu.", "CPU"], ["sys.gpu.", "GPU"], ["sys.ram.", "Memory"], ["sys.net.", "Network"],
  ["sys.disk.", "Disks"], ["sys.fan.", "Fans"],
];  // also the order the categories are shown in

// A Beszel machine's categories, by metric.
const BESZEL_GROUPS = [
  ["Usage", /^(cpu_load|mem_used_pct|mem_used_gb|disk_used_pct|disk_used_gb)$/],
  ["Disks", /^fs\d+_used_pct$/],
  ["Load", /^load\d+$/],
  ["Network", /^net_/],
  ["Temperature", /temp$/],
  ["Status", /^(online|uptime_h)$/],
];

export function liveText(id) {
  const s = state.data.sources.get(id);
  if (!s?.available) return "--";
  const v = Math.abs(s.value) >= 100 ? Math.round(s.value) : Math.round(s.value * 10) / 10;
  return `${v}${s.unit && !s.unit.startsWith("°") ? " " : ""}${s.unit || ""}`;
}

export function sourceName(id) {
  return state.data.sources.get(id)?.name || id || "";
}

/**
 * Every source sorted into places and categories:
 * [{ key, title, note, online, cats: [{ key, title, items: [{ id, label }] }] }].
 */
function sourceTree() {
  const ids = new Set([...(state.catalog?.sources || []).map((s) => s.id), ...state.data.sources.keys()]);
  const places = new Map();
  const place = (key, title, order) => {
    if (!places.has(key)) places.set(key, { key, title, order, cats: new Map(), online: null });
    return places.get(key);
  };
  const add = (p, cat, id, label) => {
    if (!p.cats.has(cat)) p.cats.set(cat, { key: cat, title: cat, items: [] });
    p.cats.get(cat).items.push({ id, label });
  };
  for (const id of ids) {
    const live = state.data.sources.get(id);
    const name = live?.name || id;
    const bz = /^bz\.([a-z0-9-]+)\.(.+)$/.exec(id);
    if (bz) {
      const machine = (live?.group || "").replace(/^Beszel: /, "") || bz[1];
      const p = place(`bz.${bz[1]}`, machine, 2);
      p.note = "Beszel";
      if (bz[2] === "online") p.online = live?.available ? live.value >= 0.5 : null;
      const cat = BESZEL_GROUPS.find(([, re]) => re.test(bz[2]))?.[0] || "Other";
      // "Main-PVE-Node CPU" -> "CPU": the machine is already in the path.
      add(p, cat, id, name.startsWith(machine + " ") ? name.slice(machine.length + 1) : name);
    } else if (id.startsWith("room.")) {
      add(place("room", "Room sensor", 1), "Room", id, name);
    } else {
      const cat = PC_GROUPS.find(([pre]) => id.startsWith(pre))?.[1] || "Other";
      add(place("pc", "This PC", 0), cat, id, name);
    }
  }
  // Categories in a fixed order (the most used first), "Other" last.
  const rank = (p, key) => {
    const order = p.key.startsWith("bz.") ? BESZEL_GROUPS.map(([t]) => t) : PC_GROUPS.map(([, t]) => t);
    const i = order.indexOf(key);
    return i < 0 ? order.length : i;
  };
  return [...places.values()]
    .sort((a, b) => a.order - b.order || a.title.localeCompare(b.title))
    .map((p) => ({ ...p, cats: [...p.cats.values()].sort((a, b) => rank(p, a.key) - rank(p, b.key)) }));
}

function locate(tree, id) {
  for (const p of tree) {
    for (const c of p.cats) {
      if (c.items.some((i) => i.id === id)) return { place: p.key, cat: c.key };
    }
  }
  return { place: null, cat: null };
}

/**
 * A button showing the chosen source; it opens the browser. onPick(id) is called with the new id.
 * opts.emptyText: button text when nothing is chosen.
 */
export function sourceButton(value, onPick, opts = {}) {
  return h("button", {
    type: "button", class: "picker",
    onclick: (e) => openPopover(e.currentTarget, (panel) => browse(panel, value, (id) => {
      closePopover();
      onPick(id);
    })),
  }, h("span", { text: value ? sourceName(value) : opts.emptyText || "Choose a value" }), h("span", { class: "val", text: value ? liveText(value) : "" }));
}

function browse(panel, value, pick) {
  panel.classList.add("sb");
  panel.style.width = "420px";
  const tree = sourceTree();
  // Open where the current value lives (changing to a sibling is one click); with a single
  // place there is no "where" step at all.
  const at = locate(tree, value);
  const view = { place: at.place ?? (tree.length === 1 ? tree[0].key : null), cat: at.cat, q: "" };
  const search = h("input", { type: "search", placeholder: "Search every value", "aria-label": "Search data sources" });
  const crumbs = h("div", { class: "sb-crumbs" });
  const body = h("div", { class: "sb-body" });

  const tile = (title, sub, onclick, cls = "", extra = null) =>
    h("button", { type: "button", class: "sb-tile " + cls, onclick }, extra, h("span", { class: "sb-title", text: title }), sub ? h("span", { class: "sb-sub", text: sub }) : null);

  const valueTile = (item, path) => {
    const live = state.data.sources.get(item.id);
    return tile(item.label, path ? `${path} · ${liveText(item.id)}` : liveText(item.id), () => pick(item.id),
      (item.id === value ? "on " : "") + (live?.available ? "" : "na"));
  };

  const render = () => {
    const q = view.q.trim().toLowerCase();
    const place = tree.find((p) => p.key === view.place);
    const cat = place?.cats.find((c) => c.key === view.cat);
    // Path: All > place > category, each a way back.
    const parts = [];
    if (tree.length > 1) parts.push(["All", () => { view.place = null; view.cat = null; render(); }]);
    if (place) parts.push([place.title, () => { view.cat = null; render(); }]);
    if (cat && (place?.cats.length || 0) > 1) parts.push([cat.title, null]);
    crumbs.replaceChildren(...(q ? [h("span", { text: "Search results" })] : parts.flatMap(([t, fn], i) => [
      i ? h("span", { class: "sep", text: "›" }) : null,
      fn && i < parts.length - 1 ? h("button", { type: "button", class: "link", onclick: fn, text: t }) : h("span", { text: t }),
    ].filter(Boolean))));

    if (q) {
      const hits = [];
      for (const p of tree) {
        for (const c of p.cats) {
          for (const it of c.items) {
            if (`${p.title} ${c.title} ${it.label} ${it.id}`.toLowerCase().includes(q)) {
              hits.push(valueTile(it, tree.length > 1 ? `${p.title} › ${c.title}` : c.title));
            }
          }
        }
      }
      body.replaceChildren(hits.length ? h("div", { class: "sb-tiles one" }, ...hits.slice(0, 60)) : h("p", { class: "hint", text: "Nothing matches." }));
      return;
    }
    if (!place) {
      body.replaceChildren(h("div", { class: "sb-tiles" }, ...tree.map((p) => {
        const n = p.cats.reduce((a, c) => a + c.items.length, 0);
        const dot = p.online === null ? null : h("i", { class: "sb-dot " + (p.online ? "up" : "down") });
        return tile(p.title, `${p.note ? p.note + " · " : ""}${n} values`, () => {
          view.place = p.key;
          view.cat = p.cats.length === 1 ? p.cats[0].key : null;
          render();
        }, "place", dot);
      })));
      return;
    }
    if (!cat) {
      body.replaceChildren(h("div", { class: "sb-tiles" }, ...place.cats.map((c) => {
        // A peek at the first value or two.
        const peek = c.items.slice(0, 2).map((i) => liveText(i.id)).filter((t) => t !== "--").join(", ");
        return tile(c.title, `${c.items.length} value${c.items.length === 1 ? "" : "s"}${peek ? " · " + peek : ""}`, () => {
          view.cat = c.key;
          render();
        }, "cat");
      })));
      return;
    }
    body.replaceChildren(h("div", { class: "sb-tiles" }, ...cat.items.map((it) => valueTile(it, null))));
  };

  search.addEventListener("input", () => {
    view.q = search.value;
    render();
  });
  panel.append(search, crumbs, body);
  render();
  search.focus();
}

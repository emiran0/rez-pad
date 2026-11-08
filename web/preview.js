// Device-look rendering of the 480x320 screen: status bar, grid cards and every widget type,
// following the same layout rules as the firmware (firmware/src/widgets/*.cpp, ui/pages.cpp) so the
// preview matches the device at any size. Fed with the host's live data. Pure DOM, no state.
"use strict";

export const SCREEN_W = 480;
export const SCREEN_H = 320;
export const STATUS_H = 20;
export const GRID_H = SCREEN_H - STATUS_H;
export const CELL_GAP = 4;
export const PAD = 7;

const DEG = "°";

// firmware/src/ui/theme.cpp
export const THEMES = {
  dark: { label: "Dark", bg: "#0E1013", card: "#1B1F25", border: "#2A3038", text: "#E8EAED", dim: "#8A939E", faint: "#3A414A", status: "#15181D" },
  midnight: { label: "Midnight", bg: "#0A0F1E", card: "#141C33", border: "#24304F", text: "#E6ECFF", dim: "#8C9AC0", faint: "#33406A", status: "#0F1528" },
  black: { label: "Black", bg: "#000000", card: "#0F0F10", border: "#222224", text: "#F2F2F2", dim: "#8C8C8C", faint: "#333336", status: "#000000" },
  graphite: { label: "Graphite", bg: "#202226", card: "#2E3138", border: "#41454E", text: "#F0F1F3", dim: "#9EA3AD", faint: "#50555F", status: "#26292E" },
};

export const ACCENTS = ["#4FC3F7", "#7C9CFF", "#B388FF", "#F06292", "#F75050", "#FF9F43", "#FFD54F", "#7CFFB2", "#26C6A6", "#E8EAED"];

export const WIDGETS = {
  now_playing: { name: "Now playing", group: "Media", desc: "Art, title and controls in one card", size: [2, 2] },
  album_art: { name: "Album art", group: "Media", desc: "Cover of the current track", size: [2, 2] },
  track_info: { name: "Track info", group: "Media", desc: "Title, artist, album", size: [2, 1] },
  media_controls: { name: "Media controls", group: "Media", desc: "Play, skip and progress", size: [2, 1] },
  progress: { name: "Progress bar", group: "Media", desc: "Position in the track", size: [2, 1] },
  volume: { name: "Volume", group: "Media", desc: "PC volume and mute", size: [1, 1] },
  metric_gauge: { name: "Gauge", group: "System", desc: "A value on a ring", size: [1, 1] },
  metric_value: { name: "Value", group: "System", desc: "A number with its label", size: [1, 1] },
  metric_bar: { name: "Bar", group: "System", desc: "A value as a bar", size: [2, 1] },
  metric_graph: { name: "Graph", group: "System", desc: "History of a value", size: [2, 1] },
  clock: { name: "Clock", group: "Time & weather", desc: "Time and date", size: [1, 1] },
  weather_now: { name: "Weather", group: "Time & weather", desc: "Current conditions", size: [2, 1] },
  weather_forecast: { name: "Forecast", group: "Time & weather", desc: "Next days or hours", size: [4, 1] },
  room_climate: { name: "Room", group: "Time & weather", desc: "Room temperature, humidity", size: [2, 1] },
  image: { name: "Image", group: "Other", desc: "Picture or animated GIF", size: [2, 2] },
  label: { name: "Text", group: "Other", desc: "Your own text", size: [2, 1] },
};

// Device fonts (firmware/src/fonts): line height and the width of a digit.
const FONT = { 14: { lh: 19, dw: 9 }, 20: { lh: 26, dw: 13 }, 28: { lh: 37, dw: 18 }, 48: { lh: 50, dw: 31 } };
const lh = (f) => FONT[f].lh;

function fitFont(w, h, chars) {
  for (const f of [48, 28, 20, 14]) if (FONT[f].lh <= h && FONT[f].dw * chars <= w) return f;
  return 14;
}

export function pageGrid(cfg, page) {
  return { cols: page?.grid?.cols ?? cfg.grid?.cols ?? 4, rows: page?.grid?.rows ?? cfg.grid?.rows ?? 3 };
}

export function pageTheme(cfg, page) {
  const name = page?.theme?.name ?? cfg.theme?.name ?? "dark";
  const accent = page?.theme?.accent ?? cfg.theme?.accent ?? "#4FC3F7";
  return { name, accent, ...(THEMES[name] || THEMES.dark) };
}

// Card rectangle in screen px, the same arithmetic as firmware/src/ui/pages.cpp.
export function cardRect(w, cols, rows) {
  const cellW = Math.floor(SCREEN_W / cols);
  const cellH = Math.floor(GRID_H / rows);
  return { x: w.x * cellW + CELL_GAP / 2, y: w.y * cellH + CELL_GAP / 2, w: w.w * cellW - CELL_GAP, h: w.h * cellH - CELL_GAP };
}

/** Is a w x h widget of `type` big enough on this grid (catalog min content size)? */
export function bigEnough(catalog, type, w, h, cols, rows) {
  const t = catalog.widgets.find((x) => x.type === type);
  if (!t) return false;
  return w * Math.floor(SCREEN_W / cols) - CELL_GAP - 2 * PAD >= t.min_w && h * Math.floor(GRID_H / rows) - CELL_GAP - 2 * PAD >= t.min_h;
}

// ---- DOM helpers ----

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function txt(f, cls, text) {
  const e = el("div", `t f${f} ${cls || ""}`, text);
  e.style.lineHeight = lh(f) + "px";
  return e;
}

// LVGL lv_obj_align() for an absolutely placed child: alignment inside the parent plus offsets.
function align(parent, child, kind, x = 0, y = 0) {
  const s = child.style;
  s.position = "absolute";
  const tx = [];
  if (kind.includes("LEFT")) s.left = x + "px";
  else if (kind.includes("RIGHT")) s.right = -x + "px";
  else { s.left = "50%"; tx.push(`translateX(calc(-50% + ${x}px))`); }
  if (kind.startsWith("TOP")) s.top = y + "px";
  else if (kind.startsWith("BOTTOM")) s.bottom = -y + "px";
  else { s.top = "50%"; tx.push(`translateY(calc(-50% + ${y}px))`); }
  if (tx.length) s.transform = tx.join(" ");
  parent.append(child);
  return child;
}

function at(parent, child, x, y, w, h) {
  Object.assign(child.style, { position: "absolute", left: x + "px", top: y + "px" });
  if (w !== undefined) child.style.width = w + "px";
  if (h !== undefined) child.style.height = h + "px";
  parent.append(child);
  return child;
}

function ellipsis(f, cls, text, w) {
  const e = txt(f, `${cls || ""} ellipsis`, text);
  e.style.width = w + "px";
  return e;
}

// ---- data helpers ----

const DEFAULT_STYLE = { unit: "", min: 0, max: 100, warn: null, crit: null };

// Same rules as the firmware's sourceDefaults() (lib/config_logic).
export function sourceStyle(source, catalog) {
  const known = catalog?.sources?.find((s) => s.id === source);
  if (known) return known.style;
  const s = source || "";
  const ends = (x) => s.endsWith(x);
  if (s.includes(".temp") || s.includes("_temp")) return { unit: DEG + "C", min: 20, max: 100, warn: 75, crit: 90 };
  if (ends(".load") || ends("_load")) return { unit: "%", min: 0, max: 100, warn: 80, crit: 95 };
  if (ends("used_pct") || ends("vram_pct")) return s.startsWith("sys.disk.") ? { unit: "%", min: 0, max: 100, warn: 90, crit: 97 } : { unit: "%", min: 0, max: 100, warn: 85, crit: 95 };
  if (ends("_pct") || s === "room.hum") return { unit: "%", min: 0, max: 100 };
  if (ends("_kbps")) return { unit: "kB/s", min: 0, max: 10000 };
  if (ends("_mhz")) return { unit: "MHz", min: 0, max: 6000 };
  if (ends("_w")) return { unit: "W", min: 0, max: 300 };
  if (ends("_gb")) return { unit: "GB", min: 0, max: 64 };
  if (ends(".rpm") || ends("_rpm")) return { unit: "rpm", min: 0, max: 3000 };
  if (s === "room.pressure") return { unit: "hPa", min: 950, max: 1050 };
  return DEFAULT_STYLE;
}

export function effectiveStyle(w, catalog) {
  const d = sourceStyle(w.source, catalog);
  const pick = (k) => (w[k] !== undefined && w[k] !== null ? w[k] : d[k] ?? null);
  return { unit: pick("unit") ?? "", min: pick("min"), max: pick("max"), warn: pick("warn"), crit: pick("crit"), decimals: w.decimals ?? -1 };
}

function fmtNumber(v, decimals) {
  if (decimals < 0) decimals = Math.abs(v) < 10 ? 1 : 0;
  return v.toFixed(decimals);
}

function fmtValue(v, style) {
  let unit = style.unit;
  let d = style.decimals;
  if (unit === "kB/s" && v >= 1000) {
    v /= 1000;
    unit = "MB/s";
    if (d < 0) d = 1;
  }
  const n = fmtNumber(v, d);
  return { n, unit, text: n + (unit ? (unit.startsWith(DEG) ? "" : " ") + unit : "") };
}

function levelColor(v, s, pal) {
  if (s.crit !== null && s.crit !== undefined && v >= s.crit) return "#EF5350";
  if (s.warn !== null && s.warn !== undefined && v >= s.warn) return "#FFB300";
  return pal.accent;
}

function fmtTime(ms) {
  const s = Math.max(0, Math.floor(ms / 1000));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = String(s % 60).padStart(2, "0");
  return h ? `${h}:${String(m).padStart(2, "0")}:${ss}` : `${m}:${ss}`;
}

const WMO = {
  0: "Clear", 1: "Mostly clear", 2: "Partly cloudy", 3: "Cloudy", 45: "Fog", 48: "Fog",
  51: "Drizzle", 53: "Drizzle", 55: "Drizzle", 56: "Freezing drizzle", 57: "Freezing drizzle",
  61: "Rain", 63: "Rain", 65: "Heavy rain", 66: "Freezing rain", 67: "Freezing rain",
  71: "Snow", 73: "Snow", 75: "Heavy snow", 77: "Snow grains", 80: "Showers", 81: "Showers", 82: "Heavy showers",
  85: "Snow showers", 86: "Snow showers", 95: "Thunderstorm", 96: "Thunderstorm", 99: "Thunderstorm",
};

function wmoIcon(code, day) {
  if (code <= 1) return day ? "clear_day" : "clear_night";
  if (code === 2) return day ? "partly_day" : "partly_night";
  if (code === 45 || code === 48) return "fog";
  if ([51, 53, 55].includes(code)) return "drizzle";
  if ([56, 57, 66, 67].includes(code)) return "sleet";
  if ([61, 63, 80, 81].includes(code)) return "rain";
  if ([65, 82].includes(code)) return "heavy_rain";
  if ([71, 73, 75, 77, 85, 86].includes(code)) return "snow";
  if ([95, 96, 99].includes(code)) return "thunder";
  return "cloudy";
}

function icon(code, day, px) {
  const img = el("img", "wx-icon");
  img.src = `img/weather/${wmoIcon(code, day)}.png`;
  img.width = px;
  img.height = px;
  img.alt = "";
  img.draggable = false;
  return img;
}

const WIND = { kmh: "km/h", ms: "m/s", mph: "mph" };
const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

// ---- media views (ArtView, TextView, ControlsView in media_widgets.cpp) ----

function artView(box, x, y, side, ctx) {
  const a = at(box, el("div", "art"), x, y, side, side);
  a.style.borderRadius = (side >= 80 ? 6 : 4) + "px";
  const np = ctx.data.media?.now_playing;
  if (ctx.data.artUrl && ctx.data.connected) {
    const img = el("img");
    img.src = ctx.data.artUrl;
    img.alt = "";
    img.draggable = false;
    a.append(img);
  } else {
    a.append(txt(side >= 70 ? 28 : 20, np && np.state !== "none" ? "" : "dim", "♫"));
  }
}

function textView(box, x, y, w, h, ctx, center = false) {
  const np = ctx.data.media?.now_playing;
  const none = !np || np.state === "none" || !ctx.data.connected;
  const titleF = h >= 120 ? 28 : h >= 52 ? 20 : 14;
  const artistF = h >= 120 ? 20 : 14;
  let left = h - lh(titleF);
  const artist = left >= lh(artistF);
  if (artist) left -= lh(artistF) + 2;
  const album = h >= 100 && left >= lh(14) + 2;
  if (album) left -= lh(14) + 2;
  const app = left >= lh(14) + 2;
  const col = at(box, el("div", "col" + (center ? " center" : "")), x, y, w, h);
  col.style.gap = (h >= 120 ? 6 : 2) + "px";
  col.append(ellipsis(titleF, "", none ? (ctx.data.connected ? "Nothing playing" : "No host connected") : np.title, w));
  if (artist) col.append(ellipsis(artistF, "", none ? "" : np.artist, w));
  if (album) col.append(ellipsis(14, "dim", none ? "" : np.album, w));
  if (app) col.append(ellipsis(14, "accent", none ? "" : np.app, w));
}

function controlsView(box, x, y, w, h, ctx, withButtons) {
  const np = ctx.data.media?.now_playing;
  const area = at(box, el("div"), x, y, w, h);
  const times = h >= (withButtons ? 70 : 30);
  const bar = !withButtons || h >= 44;
  if (withButtons) {
    const rowH = times ? 34 : bar ? h - 12 : h;
    const row = at(area, el("div", "controls"), 0, 0, w, rowH);
    const big = w >= 120 && rowH >= 30;
    const side = w >= 90 ? 20 : 14;
    const none = !np || np.state === "none" || !ctx.data.connected;
    row.append(txt(side, np?.can_prev ? "" : "faint", "⏮"), txt(big ? 28 : 20, none ? "faint" : "accent", np?.state === "playing" ? "⏸" : "▶"), txt(side, np?.can_next ? "" : "faint", "⏭"));
  }
  let pos = 0;
  let dur = 0;
  const live = np && np.state !== "none" && ctx.data.connected;
  if (live) {
    dur = np.dur_ms;
    pos = Math.min(Math.max(np.pos_ms + (np.state === "playing" ? Date.now() - np.ts : 0), 0), dur);
  }
  if (bar) {
    const b = el("div", "bar thin");
    b.style.width = w + "px";
    const fill = el("div", "fill");
    fill.style.background = ctx.pal.accent;
    fill.style.width = (dur ? (100 * pos) / dur : 0) + "%";
    b.append(fill);
    if (times) align(area, b, "BOTTOM_MID", 0, -20);
    else if (withButtons) align(area, b, "BOTTOM_MID", 0, 0);
    else align(area, b, "LEFT_MID", 0, 0);
  }
  if (times) {
    align(area, txt(14, "dim", live ? fmtTime(pos) : ""), "BOTTOM_LEFT");
    align(area, txt(14, "dim", live ? fmtTime(dur) : ""), "BOTTOM_RIGHT");
  }
}

// ---- widgets: each fills `box` (the card's content area, w x h px) ----

const RENDER = {
  album_art(box, w, h, ctx) {
    const side = Math.min(w, h) + 14 - 4;
    artView(box, (w - side) / 2, (h - side) / 2, side, ctx);
  },

  track_info(box, w, h, ctx) {
    textView(box, 0, 0, w, h, ctx);
  },

  media_controls(box, w, h, ctx) {
    controlsView(box, 0, 0, w, h, ctx, true);
  },

  progress(box, w, h, ctx) {
    controlsView(box, 0, 0, w, h, ctx, false);
  },

  now_playing(box, w, h, ctx) {
    if (w * 2 >= h * 3) {
      const side = h;
      const x = side + 10;
      const cw = w - x;
      const ctrlH = h >= 110 ? 64 : h >= 76 ? 44 : 30;
      artView(box, 0, 0, side, ctx);
      textView(box, x, 0, cw, h - ctrlH - 4, ctx);
      controlsView(box, x, h - ctrlH, cw, ctrlH, ctx, true);
    } else {
      const ctrlH = h >= 200 ? 64 : 44;
      const textH = h >= 200 ? 46 : 40;
      const side = Math.min(h - ctrlH - textH - 8, w);
      const hasArt = side >= 40;
      const top = hasArt ? side + 6 : 0;
      if (hasArt) artView(box, (w - side) / 2, 0, side, ctx);
      textView(box, 0, top, w, h - top - ctrlH - 2, ctx, true);
      controlsView(box, 0, h - ctrlH, w, ctrlH, ctx, true);
    }
  },

  volume(box, w, h, ctx) {
    const v = ctx.data.media?.volume;
    const side = Math.min(w, h);
    const arcW = side >= 120 ? 10 : side >= 70 ? 7 : 5;
    const level = v?.available && ctx.data.connected ? v.level : null;
    const g = gauge(side, arcW, level === null ? null : level / 100, v?.muted ? ctx.pal.dim : ctx.pal.accent, ctx.pal);
    align(box, g, "CENTER");
    const iconOn = side >= 64;
    align(box, txt(side >= 120 ? 28 : side >= 64 ? 20 : 14, "", level === null ? "--" : String(level)), "CENTER", 0, iconOn ? -Math.floor(side / 16) - 2 : 0);
    if (iconOn) align(box, txt(14, "dim", v?.muted ? "\u{1F507}" : "\u{1F50A}"), "CENTER", 0, Math.floor(side / 8) + 6);
  },

  metric_value(box, w, h, ctx) {
    const { value, style, label } = metric(ctx);
    const title = h >= 40;
    if (title) align(box, ellipsis(14, "dim", label, w), "TOP_LEFT");
    let f = fitFont(w, h - (title ? 18 : 0), 7);
    if (f === 48) f = 28;
    align(box, txt(f, "", value === null ? "--" : fmtValue(value, style).text), title ? "BOTTOM_LEFT" : "LEFT_MID");
  },

  metric_bar(box, w, h, ctx) {
    const { value, style, label } = metric(ctx);
    const frac = value === null ? 0 : Math.min(Math.max((value - style.min) / (style.max - style.min), 0), 1);
    const b = el("div", "bar");
    const fill = el("div", "fill");
    fill.style.width = frac * 100 + "%";
    fill.style.background = value === null ? ctx.pal.accent : levelColor(value, style, ctx.pal);
    b.append(fill);
    const text = value === null ? "--" : fmtValue(value, style).text;
    if (h >= 44) {
      align(box, ellipsis(14, "dim", label, w - 80), "TOP_LEFT");
      align(box, txt(h >= 70 ? 28 : 20, "", text), "TOP_RIGHT", 0, -2);
      b.style.width = w + "px";
      b.style.height = (h >= 70 ? 18 : 12) + "px";
      align(box, b, "BOTTOM_MID", 0, -2);
    } else {
      align(box, txt(14, "", text), "RIGHT_MID");
      b.style.width = w - 64 + "px";
      b.style.height = "10px";
      align(box, b, "LEFT_MID");
    }
  },

  metric_gauge(box, w, h, ctx) {
    const { value, style, label } = metric(ctx);
    let side = Math.min(w, h);
    let labelBelow = false;
    if (side >= h - 4 && side < 130) {
      side = Math.min(w, h - 16);
      labelBelow = true;
    }
    const thick = side >= 130 ? 12 : side >= 80 ? 8 : 6;
    const frac = value === null ? null : Math.min(Math.max((value - style.min) / (style.max - style.min), 0), 1);
    align(box, gauge(side, thick, frac, value === null ? ctx.pal.accent : levelColor(value, style, ctx.pal), ctx.pal), "TOP_MID");
    const inner = side - 2 * thick - 6;
    const unitOn = !!style.unit && inner >= 44;
    const f = fitFont(inner, inner - (unitOn ? 16 : 0), 4);
    const top = Math.floor(side / 2) - (unitOn ? Math.floor((lh(f) + 14) / 2) : Math.floor(lh(f) / 2));
    const fv = value === null ? { n: "--", unit: style.unit } : fmtValue(value, style);
    align(box, txt(f, "", fv.n), "TOP_MID", 0, top);
    if (unitOn) align(box, txt(14, "dim", fv.unit), "TOP_MID", 0, top + lh(f) - 2);
    const l = ellipsis(14, "dim center", label, labelBelow ? w : inner + 16);
    if (labelBelow) align(box, l, "BOTTOM_MID", 0, 2);
    else align(box, l, "TOP_MID", 0, side - 18);
  },

  metric_graph(box, w, h, ctx) {
    const { value, style, label, history } = metric(ctx);
    const header = h >= 56;
    if (header) align(box, ellipsis(14, "dim", label, w - 70), "TOP_LEFT");
    const gh = header ? h - 20 : h;
    const points = w >= 300 ? 120 : 60;
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("width", w);
    svg.setAttribute("height", gh);
    const vals = (history || []).slice(-points);
    const pts = vals.map((v, i) => {
      const x = (w * (points - vals.length + i)) / (points - 1);
      const f = Math.min(Math.max((v - style.min) / (style.max - style.min), 0), 1);
      return `${x.toFixed(1)},${(gh - 1 - f * (gh - 2)).toFixed(1)}`;
    });
    if (pts.length > 1) {
      const line = document.createElementNS("http://www.w3.org/2000/svg", "polyline");
      line.setAttribute("points", pts.join(" "));
      line.setAttribute("fill", "none");
      line.setAttribute("stroke", ctx.pal.accent);
      line.setAttribute("stroke-width", "2");
      line.setAttribute("stroke-linejoin", "round");
      svg.append(line);
    }
    align(box, svg, "BOTTOM_MID");
    align(box, txt(14, "", value === null ? "--" : fmtValue(value, style).text), "TOP_RIGHT");
  },

  clock(box, w, h, ctx) {
    const wide = w >= 160;
    const f = w >= 150 && h >= 70 ? 48 : w >= 80 && h >= 40 ? 28 : 20;
    const now = new Date();
    const synced = ctx.data.connected;
    const col = at(box, el("div", "col center"), 0, 0, w, h);
    col.style.gap = "2px";
    col.append(txt(f, "", synced ? `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}` : "--:--"));
    if (h - lh(f) >= 16) {
      col.append(txt(14, "dim", !synced ? "" : wide ? `${DAYS[now.getDay()]} ${now.getDate()} ${MONTHS[now.getMonth()]}` : `${DAYS[now.getDay()]} ${now.getDate()}`));
    }
  },

  room_climate(box, w, h, ctx) {
    const r = ctx.data.connected ? ctx.data.room : null;
    const u = ctx.units;
    const t = r?.temp_c ?? null;
    const temp = t === null ? "--" : `${(u.temp === "f" ? t * 1.8 + 32 : t).toFixed(1)}${DEG}`;
    const hum = r?.hum_pct === null || r?.hum_pct === undefined ? "" : `${Math.round(r.hum_pct)} % RH`;
    const p = r?.press_hpa ?? null;
    const press = p === null ? "" : u.pressure === "inhg" ? `${(p * 0.02953).toFixed(2)} inHg` : `${Math.round(p)} hPa`;
    const tall = h >= 140 && w >= 140;
    if (!tall && w < 160) {
      if (h >= 60) align(box, txt(14, "accent", "Room"), "TOP_LEFT");
      align(box, txt(h >= 50 ? 28 : 20, "", temp), h >= 60 ? "LEFT_MID" : "TOP_LEFT", 0, h >= 60 ? 0 : -2);
      align(box, txt(14, "", hum), "BOTTOM_LEFT");
      if (!(h < 70 || w < 120)) align(box, txt(14, "dim", press), "BOTTOM_RIGHT");
      return;
    }
    align(box, txt(14, "accent", "Room"), "TOP_LEFT");
    if (tall) {
      align(box, txt(48, "", temp), "TOP_LEFT", 0, 24);
      align(box, txt(20, "", hum), "TOP_LEFT", 0, 94);
      align(box, txt(14, "dim", press), "BOTTOM_LEFT", 0, -20);
      const trend = { rising: "↑ rising", falling: "↓ falling", steady: "→ steady" }[r?.trend] || "trend: collecting";
      align(box, txt(14, "dim", trend), "BOTTOM_LEFT");
    } else {
      align(box, txt(28, "", temp), "LEFT_MID", 0, 6);
      align(box, txt(20, "", hum), "RIGHT_MID", 0, -8);
      align(box, txt(14, "dim", press), "BOTTOM_RIGHT");
    }
  },

  weather_now(box, w, h, ctx) {
    const wx = ctx.data.weather?.latest;
    const now = wx?.now;
    const u = wx?.units || ctx.units;
    const age = ctx.data.weather?.age_s;
    const ageText = age === null || age === undefined ? "" : age < 120 ? "now" : age < 3600 ? `${Math.round(age / 60)} min ago` : `${Math.round(age / 3600)} h ago`;
    const temp = now ? `${Math.round(now.temp)}${DEG}` : "--";
    const cond = now ? WMO[now.code] ?? "" : ctx.data.connected ? "waiting for data" : "no data";
    const tall = h >= 140 && w >= 160;
    const narrow = !tall && w < 140;
    const iconPx = tall ? 72 : (h >= 70 && !narrow) || (narrow && h >= 80 && w >= 110) ? 48 : 32;
    if (narrow) {
      if (now) align(box, icon(now.code, now.is_day, iconPx), "TOP_LEFT");
      align(box, txt(h >= 50 ? 28 : 20, "", temp), "TOP_RIGHT");
      align(box, ellipsis(14, "dim", cond, w), "BOTTOM_LEFT");
    } else if (tall) {
      align(box, ellipsis(14, "accent", wx?.loc ?? "Weather", w - 80), "TOP_LEFT");
      align(box, txt(14, "dim", ageText), "TOP_RIGHT");
      if (now) align(box, icon(now.code, now.is_day, iconPx), "TOP_RIGHT", 0, 18);
      align(box, txt(48, "", temp), "TOP_LEFT", 0, 24);
      align(box, ellipsis(20, "", cond, w), "TOP_LEFT", 0, 94);
      if (now) {
        const press = now.press ? (u.pressure === "inhg" ? `   ${now.press.toFixed(2)} inHg` : `   ${Math.round(now.press)} hPa`) : "";
        const d = txt(14, "dim pre", `Feels ${Math.round(now.feels)}${DEG}   Hum ${now.hum} %\nWind ${now.wind.toFixed(u.wind === "ms" ? 1 : 0)} ${WIND[u.wind]}${press}`);
        align(box, d, "BOTTOM_LEFT");
      }
    } else {
      const x = iconPx + 8;
      if (now) align(box, icon(now.code, now.is_day, iconPx), "LEFT_MID");
      align(box, ellipsis(14, "accent", wx?.loc ?? "Weather", w - x - 70), "TOP_LEFT", x, 0);
      align(box, txt(14, "dim", ageText), "TOP_RIGHT");
      align(box, txt(28, "", temp), "LEFT_MID", x, 2);
      align(box, ellipsis(14, "dim", cond, w - x), "BOTTOM_LEFT", x, 0);
    }
  },

  weather_forecast(box, w, h, ctx) {
    const wx = ctx.data.weather?.latest;
    const hourly = ctx.widget.mode === "hourly";
    const count = Math.max(1, Math.min(5, Math.floor(w / 48)));
    const colW = Math.floor(w / count);
    const icons = h >= 60;
    const tempF = colW >= 70 && h >= 60 ? 20 : 14;
    for (let i = 0; i < count; i++) {
      const col = at(box, el("div", "col spread"), i * colW, 0, colW, h);
      let label = "--";
      let item = null;
      if (hourly && wx?.hourly) {
        item = wx.hourly[1 + i * 2];
        if (item) label = String(new Date(item.t * 1000).getHours()).padStart(2, "0") + ":00";
      } else if (!hourly && wx?.daily) {
        item = wx.daily[i];
        if (item) label = i === 0 ? "Today" : DAYS[new Date(item.d + "T12:00:00").getDay()];
      }
      col.append(txt(14, "dim", label));
      if (icons) col.append(item ? icon(item.code, hourly ? item.is_day : true, 32) : el("div", "icon-space"));
      const t = txt(tempF, "", "");
      if (item && hourly) t.textContent = `${Math.round(item.temp)}${DEG}`;
      else if (item) {
        t.append(`${Math.round(item.max)}${DEG} `);
        t.append(el("span", "dim", `${Math.round(item.min)}${DEG}`));
      }
      col.append(t);
      if (i > 0) {
        const sep = at(box, el("div", "sep"), i * colW, 8, 1, Math.round(h * 0.8));
        sep.style.background = ctx.pal.border;
      }
    }
  },

  label(box, w, h, ctx) {
    const wd = ctx.widget;
    const top = wd.label ? 18 : 0;
    if (wd.label) align(box, ellipsis(14, "dim", wd.label, w), "TOP_LEFT");
    const big = h - top >= 60 && (wd.text || "").length * 15 <= w;
    const t = txt(big ? 28 : 20, "center wrap", wd.text || "");
    t.style.width = w + "px";
    align(box, t, "CENTER", 0, top / 2);
  },

  image(box, w, h, ctx) {
    const wd = ctx.widget;
    if (!wd.asset) {
      align(box, txt(14, "dim", "\u{1F5BC}"), "CENTER");
      return;
    }
    const img = el("img", "picture");
    img.src = `/api/assets/${wd.asset}`;
    img.alt = "";
    img.draggable = false;
    img.style.objectFit = wd.fit === "contain" ? "contain" : "cover";
    at(box, img, 0, 0, w, h);
  },
};

function metric(ctx) {
  const wd = ctx.widget;
  const style = effectiveStyle(wd, ctx.catalog);
  const src = ctx.data.sources?.get(wd.source);
  const value = ctx.data.connected && src && src.available ? src.value : null;
  return { value, style, label: wd.label || wd.source || "", history: ctx.data.history?.get(wd.source) };
}

// 270 degree ring like the firmware's lv_arc (rotation 135, bg 0..270).
function gauge(side, thick, frac, color, pal) {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", side);
  svg.setAttribute("height", side);
  const r = side / 2 - thick / 2;
  const c = side / 2;
  const arc = (a0, a1) => {
    const p = (a) => [c + r * Math.cos((a * Math.PI) / 180), c + r * Math.sin((a * Math.PI) / 180)];
    const [x0, y0] = p(a0);
    const [x1, y1] = p(a1);
    return `M ${x0} ${y0} A ${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  };
  const path = (d, stroke) => {
    const e = document.createElementNS("http://www.w3.org/2000/svg", "path");
    e.setAttribute("d", d);
    e.setAttribute("fill", "none");
    e.setAttribute("stroke", stroke);
    e.setAttribute("stroke-width", thick);
    e.setAttribute("stroke-linecap", "round");
    svg.append(e);
  };
  path(arc(135, 405), pal.faint);
  if (frac !== null && frac > 0.001) path(arc(135, 135 + 270 * Math.min(frac, 0.9999)), color);
  return svg;
}

/** Renders one widget into a card element sized for it (also used by the widget gallery). */
export function renderWidget(card, wd, rect, cfg, pal, data, catalog) {
  card.style.background = pal.card;
  card.style.borderColor = pal.border;
  const box = el("div", "content");
  const full = wd.type === "image";
  if (full) box.classList.add("full");
  card.append(box);
  const cw = full ? rect.w - 2 : rect.w - 2 * PAD;
  const ch = full ? rect.h - 2 : rect.h - 2 * PAD;
  const render = RENDER[wd.type];
  if (!render) {
    box.append(txt(14, "crit center", `${wd.type}: not supported`));
    return;
  }
  try {
    render(box, cw, ch, { widget: wd, data, catalog, units: cfg.units || { temp: "c", wind: "kmh", pressure: "hpa" }, pal });
  } catch (e) {
    box.append(txt(14, "crit", String(e)));
  }
}

/**
 * Renders a whole page into `screen` (a 480x320 element). Returns the grid element and the card
 * elements in widget order.
 */
export function renderScreen(screen, cfg, pageIndex, data, catalog) {
  screen.replaceChildren();
  const pages = cfg.pages || [];
  const page = pages[pageIndex];
  const pal = pageTheme(cfg, page);
  Object.assign(screen.style, { background: pal.bg, color: pal.text });
  screen.style.setProperty("--d-text", pal.text);
  screen.style.setProperty("--d-dim", pal.dim);
  screen.style.setProperty("--d-faint", pal.faint);
  screen.style.setProperty("--d-accent", pal.accent);

  const bar = el("div", "statusbar");
  bar.style.background = pal.status;
  const now = new Date();
  align(bar, txt(14, "", data.connected ? `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}` : "--:--"), "LEFT_MID", 8);
  align(bar, txt(14, "dim", page ? page.name || page.id : ""), "LEFT_MID", 64);
  const dots = el("div", "dots");
  for (let i = 0; i < Math.min(pages.length, 8); i++) {
    const d = el("i");
    d.style.background = i === pageIndex ? pal.accent : pal.faint;
    dots.append(d);
  }
  align(bar, dots, "CENTER");
  const usb = txt(14, "", "⬌");
  usb.style.color = data.connected ? "#66BB6A" : pal.faint;
  align(bar, usb, "RIGHT_MID", -8);
  screen.append(bar);

  const grid = el("div", "grid");
  screen.append(grid);
  const cards = [];
  if (!page) return { grid, cards, pal };
  const { cols, rows } = pageGrid(cfg, page);
  (page.widgets || []).forEach((wd) => {
    const r = cardRect(wd, cols, rows);
    const card = el("div", "card");
    Object.assign(card.style, { left: r.x + "px", top: r.y + "px", width: r.w + "px", height: r.h + "px" });
    renderWidget(card, wd, r, cfg, pal, data, catalog);
    grid.append(card);
    cards.push(card);
  });
  return { grid, cards, pal };
}

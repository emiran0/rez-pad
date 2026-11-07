// Device-look rendering of the 480x320 screen: status bar, grid cards and an approximation of
// every widget type (firmware/src/widgets), fed with the host's live data. Pure DOM, no state.
"use strict";

export const SCREEN_W = 480;
export const SCREEN_H = 320;
export const STATUS_H = 20;
export const GRID_H = SCREEN_H - STATUS_H;
export const CELL_GAP = 4;
export const PAD = 7;

const DEG = "°";

export const WIDGET_NAMES = {
  album_art: "Album art",
  track_info: "Track info",
  media_controls: "Media controls",
  progress: "Progress bar",
  volume: "Volume",
  metric_value: "Value",
  metric_bar: "Bar",
  metric_gauge: "Gauge",
  metric_graph: "Graph",
  clock: "Clock",
  room_climate: "Room climate",
  weather_now: "Weather now",
  weather_forecast: "Forecast",
  label: "Text",
};

// Card rectangle in screen px, the same arithmetic as firmware/src/ui/pages.cpp.
export function cardRect(w, cols, rows) {
  const cellW = Math.floor(SCREEN_W / cols);
  const cellH = Math.floor(GRID_H / rows);
  return {
    x: w.x * cellW + CELL_GAP / 2,
    y: w.y * cellH + CELL_GAP / 2,
    w: w.w * cellW - CELL_GAP,
    h: w.h * cellH - CELL_GAP,
  };
}

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

// Same rules as the firmware's sourceDefaults() (lib/config_logic), for widgets that leave out
// unit, range or thresholds. The catalog from /api/catalog has them per source; this covers ids
// the catalog does not list (another PC's sources in a copied config).
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
  return { unit: "", min: 0, max: 100 };
}

// The widget's effective style: its own fields over the source defaults.
export function effectiveStyle(w, catalog) {
  const d = sourceStyle(w.source, catalog);
  const pick = (k) => (w[k] !== undefined && w[k] !== null ? w[k] : d[k] ?? null);
  return { unit: pick("unit") ?? "", min: pick("min"), max: pick("max"), warn: pick("warn"), crit: pick("crit"), decimals: w.decimals ?? -1 };
}

function fmtValue(v, decimals, unit) {
  if (decimals < 0) decimals = Math.abs(v) < 10 ? 1 : 0;
  const space = unit && !unit.startsWith(DEG);
  return v.toFixed(decimals) + (unit ? (space ? " " : "") + unit : "");
}

function fmtMetric(v, style) {
  if (style.unit === "kB/s" && v >= 1000) return fmtValue(v / 1000, style.decimals < 0 ? 1 : style.decimals, "MB/s");
  return fmtValue(v, style.decimals, style.unit);
}

function levelClass(v, style) {
  if (style.crit !== null && style.crit !== undefined && v >= style.crit) return "crit";
  if (style.warn !== null && style.warn !== undefined && v >= style.warn) return "warn";
  return "accent";
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

// firmware/src/widgets/weather_widgets.cpp wmoIcon()
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
  return img;
}

const WIND = { kmh: "km/h", ms: "m/s", mph: "mph" };

function abs(parent, child, css) {
  Object.assign(child.style, css);
  child.style.position = "absolute";
  parent.append(child);
  return child;
}

// ---- widgets: each fills `box` (the card's content area, w x h px) ----

const RENDER = {
  album_art(box, w, h, ctx) {
    const side = Math.min(w, h) + 2 * PAD - 4;
    const art = abs(box, el("div", "art"), { width: side + "px", height: side + "px", left: (w - side) / 2 + "px", top: (h - side) / 2 + "px" });
    if (ctx.data.artUrl) {
      const img = el("img");
      img.src = ctx.data.artUrl;
      img.alt = "";
      art.append(img);
    } else {
      art.append(el("span", "f28", "♫"));
    }
  },

  track_info(box, w, h, ctx) {
    const np = ctx.data.media?.now_playing;
    const tall = h >= 80;
    const none = !np || np.state === "none";
    abs(box, el("div", (tall ? "f28" : "f20") + " ellipsis", none ? (ctx.data.connected ? "Nothing playing" : "No host connected") : np.title), { left: 0, top: 0, width: w + "px" });
    if (!none) {
      abs(box, el("div", (tall ? "f20" : "f14") + " ellipsis", np.artist), { left: 0, top: (tall ? 34 : 24) + "px", width: w + "px" });
      if (tall) abs(box, el("div", "f14 dim ellipsis", np.album), { left: 0, top: "60px", width: w + "px" });
      abs(box, el("div", "f14 accent", np.app), { left: 0, bottom: 0 });
    }
  },

  media_controls(box, w, h, ctx, withButtons = true) {
    const np = ctx.data.media?.now_playing;
    if (withButtons) {
      const row = abs(box, el("div", "controls"), { left: 0, top: 0, width: w + "px", height: "34px" });
      row.append(el("span", "f20", "⏮"), el("span", "f28 accent", np?.state === "playing" ? "⏸" : "▶"), el("span", "f20", "⏭"));
    }
    let pos = 0;
    let dur = 0;
    if (np && np.state !== "none") {
      dur = np.dur_ms;
      pos = np.pos_ms + (np.state === "playing" ? Date.now() - np.ts : 0);
      pos = Math.min(Math.max(pos, 0), dur);
    }
    const bar = abs(box, el("div", "bar thin"), { left: 0, bottom: "20px", width: w + "px" });
    const fill = el("div", "fill accent-bg");
    fill.style.width = (dur ? (100 * pos) / dur : 0) + "%";
    bar.append(fill);
    abs(box, el("div", "f14 dim", fmtTime(pos)), { left: 0, bottom: 0 });
    abs(box, el("div", "f14 dim", fmtTime(dur)), { right: 0, bottom: 0 });
  },

  progress(box, w, h, ctx) {
    RENDER.media_controls(box, w, h, ctx, false);
  },

  volume(box, w, h, ctx) {
    const v = ctx.data.media?.volume;
    const side = Math.min(w, h) - 2;
    const level = v?.available && ctx.data.connected ? v.level : null;
    box.append(gauge(side, w, level === null ? null : level / 100, "accent"));
    abs(box, el("div", "f20 center", level === null ? "--" : String(level)), { left: 0, width: w + "px", top: h / 2 - 16 + "px" });
    abs(box, el("div", "f14 dim center", v?.muted ? "🔇" : "🔊"), { left: 0, width: w + "px", top: h / 2 + 6 + "px" });
  },

  metric_value(box, w, h, ctx) {
    const { value, style, label } = metric(ctx);
    abs(box, el("div", "f14 dim ellipsis", label), { left: 0, top: 0, width: w + "px" });
    abs(box, el("div", h >= 80 ? "f28" : "f20", value === null ? "--" : fmtMetric(value, style)), { left: 0, bottom: 0 });
  },

  metric_bar(box, w, h, ctx) {
    const { value, style, label } = metric(ctx);
    abs(box, el("div", "f14 dim ellipsis", label), { left: 0, top: 0, width: w - 80 + "px" });
    abs(box, el("div", "f20", value === null ? "--" : fmtMetric(value, style)), { right: 0, top: "-2px" });
    const bar = abs(box, el("div", "bar"), { left: 0, bottom: "4px", width: w + "px" });
    const fill = el("div", "fill " + (value === null ? "accent" : levelClass(value, style)) + "-bg");
    const frac = value === null ? 0 : (value - style.min) / (style.max - style.min);
    fill.style.width = Math.min(Math.max(frac, 0), 1) * 100 + "%";
    bar.append(fill);
  },

  metric_gauge(box, w, h, ctx) {
    const { value, style, label } = metric(ctx);
    const big = h >= 120;
    const side = big ? Math.min(w, h) - 4 : Math.min(w, h - 14);
    const frac = value === null ? null : (value - style.min) / (style.max - style.min);
    const g = gauge(side, w, frac === null ? null : Math.min(Math.max(frac, 0), 1), value === null ? "accent" : levelClass(value, style));
    box.append(g);
    abs(box, el("div", (big ? "f28" : "f20") + " center", value === null ? "--" : fmtMetric(value, style)), { left: 0, width: w + "px", top: side / 2 - (big ? 18 : 13) + "px" });
    abs(box, el("div", "f14 dim center ellipsis", label), big ? { left: 0, width: w + "px", top: side / 2 + 16 + "px" } : { left: 0, width: w + "px", bottom: "-2px" });
  },

  metric_graph(box, w, h, ctx) {
    const { value, style, label, history } = metric(ctx);
    abs(box, el("div", "f14 dim ellipsis", label), { left: 0, top: 0, width: w - 70 + "px" });
    abs(box, el("div", "f14", value === null ? "--" : fmtMetric(value, style)), { right: 0, top: 0 });
    const gh = h - 20;
    const points = w >= 300 ? 120 : 60;
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("width", w);
    svg.setAttribute("height", gh);
    svg.setAttribute("viewBox", `0 0 ${w} ${gh}`);
    const vals = (history || []).slice(-points);
    const pts = vals.map((v, i) => {
      const x = (w * (points - vals.length + i)) / (points - 1);
      const f = Math.min(Math.max((v - style.min) / (style.max - style.min), 0), 1);
      return `${x.toFixed(1)},${(gh - 1 - f * (gh - 2)).toFixed(1)}`;
    });
    if (pts.length > 1) {
      const line = document.createElementNS("http://www.w3.org/2000/svg", "polyline");
      line.setAttribute("points", pts.join(" "));
      line.setAttribute("class", "graph-line");
      svg.append(line);
    }
    abs(box, svg, { left: 0, bottom: 0 });
  },

  clock(box, w, h, ctx) {
    const wide = w >= 200;
    const now = new Date();
    const time = ctx.data.connected || ctx.data.clockSynced ? `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}` : "--:--";
    abs(box, el("div", (wide ? "f48" : "f28") + " center", time), { left: 0, width: w + "px", top: h / 2 - (wide ? 34 : 24) + "px" });
    const date = now.toLocaleDateString("en-GB", { weekday: "short", day: "numeric", month: "short" });
    abs(box, el("div", "f14 dim center", time === "--:--" ? "" : date), { left: 0, width: w + "px", bottom: wide ? "2px" : "4px" });
  },

  room_climate(box, w, h, ctx) {
    const r = ctx.data.room;
    const u = ctx.units;
    const tall = h >= 120;
    abs(box, el("div", "f14 accent", "Room"), { left: 0, top: 0 });
    const t = r?.temp_c ?? null;
    const temp = t === null ? "--" : u.temp === "f" ? `${(t * 1.8 + 32).toFixed(1)}${DEG}F` : `${t.toFixed(1)}${DEG}C`;
    abs(box, el("div", tall ? "f48" : "f28", temp), tall ? { left: 0, top: "24px" } : { left: 0, top: h / 2 - 12 + "px" });
    const hum = r?.hum_pct === null || r?.hum_pct === undefined ? "--" : `${Math.round(r.hum_pct)} %`;
    const p = r?.press_hpa ?? null;
    const press = p === null ? "--" : u.pressure === "inhg" ? `${(p * 0.02953).toFixed(2)} inHg` : `${Math.round(p)} hPa`;
    const trend = { rising: "↑ rising", falling: "↓ falling", steady: "→ steady" }[r?.trend] || "";
    if (tall) {
      abs(box, el("div", "f20", hum), { left: 0, top: "94px" });
      abs(box, el("div", "f14 dim", press), { left: 0, bottom: "20px" });
      abs(box, el("div", "f14 dim", trend), { left: 0, bottom: 0 });
    } else {
      abs(box, el("div", "f20", hum), { right: 0, top: h / 2 - 20 + "px" });
      abs(box, el("div", "f14 dim", press), { right: 0, bottom: 0 });
    }
  },

  weather_now(box, w, h, ctx) {
    const wx = ctx.data.weather?.latest;
    const tall = h >= 120;
    const u = wx?.units || ctx.units;
    const now = wx?.now;
    const age = ctx.data.weather?.age_s;
    const ageText = age === null || age === undefined ? "" : age < 3600 ? `${Math.round(age / 60)} min` : `${Math.round(age / 3600)} h`;
    const temp = now ? `${Math.round(now.temp)}${DEG}` : "--";
    if (tall) {
      abs(box, el("div", "f14 accent ellipsis", wx?.loc ?? "No weather"), { left: 0, top: 0, width: w - 80 + "px" });
      abs(box, el("div", "f14 dim", ageText), { right: 0, top: 0 });
      if (now) abs(box, icon(now.code, now.is_day, 72), { right: 0, top: "18px" });
      abs(box, el("div", "f48", temp), { left: 0, top: "24px" });
      abs(box, el("div", "f20 ellipsis", now ? WMO[now.code] ?? "" : ""), { left: 0, top: "94px", width: w + "px" });
      abs(box, el("div", "f14 dim", now ? `Feels ${Math.round(now.feels)}${DEG}  ${now.hum} %  ${now.wind.toFixed(1)} ${WIND[u.wind]}` : ""), { left: 0, bottom: 0 });
    } else {
      const x = now ? 56 : 0;
      if (now) abs(box, icon(now.code, now.is_day, 48), { left: 0, top: h / 2 - 24 + "px" });
      abs(box, el("div", "f14 accent ellipsis", wx?.loc ?? "No weather"), { left: x + "px", top: 0, width: w - x - 70 + "px" });
      abs(box, el("div", "f14 dim", ageText), { right: 0, top: 0 });
      abs(box, el("div", "f28", temp), { left: x + "px", top: h / 2 - 14 + "px" });
      abs(box, el("div", "f14 dim ellipsis", now ? WMO[now.code] ?? "" : ""), { left: x + "px", bottom: 0, width: w - x + "px" });
    }
  },

  weather_forecast(box, w, h, ctx, mode) {
    const wx = ctx.data.weather?.latest;
    const hourly = mode === "hourly";
    const row = abs(box, el("div", "forecast"), { left: 0, top: 0, width: w + "px", height: h + "px" });
    const days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
    for (let i = 0; i < 5; i++) {
      const col = el("div", "fc-col");
      let label = "--";
      let item = null;
      if (hourly && wx?.hourly) {
        item = wx.hourly[1 + i * 2];
        if (item) label = String(new Date(item.t * 1000).getHours()).padStart(2, "0") + ":00";
      } else if (!hourly && wx?.daily) {
        item = wx.daily[i];
        if (item) label = i === 0 ? "Today" : days[new Date(item.d + "T12:00:00").getDay()];
      }
      col.append(el("div", "f14 dim", label));
      if (item) col.append(icon(item.code, hourly ? item.is_day : true, 32));
      col.append(el("div", "f20", item ? (hourly ? `${Math.round(item.temp)}${DEG}` : `${Math.round(item.max)}${DEG}/${Math.round(item.min)}${DEG}`) : ""));
      row.append(col);
    }
  },

  label(box, w, h, ctx) {
    const wd = ctx.widget;
    const top = wd.label ? 18 : 0;
    if (wd.label) abs(box, el("div", "f14 dim ellipsis", wd.label), { left: 0, top: 0, width: w + "px" });
    const big = h - top >= 60 && (wd.text || "").length * 15 <= w;
    const t = abs(box, el("div", (big ? "f28" : "f20") + " center wrap", wd.text || ""), { left: 0, width: w + "px", top: top + "px", height: h - top + "px" });
    t.style.display = "flex";
    t.style.alignItems = "center";
    t.style.justifyContent = "center";
  },
};

function metric(ctx) {
  const wd = ctx.widget;
  const style = effectiveStyle(wd, ctx.catalog);
  const src = ctx.data.sources?.get(wd.source);
  const value = ctx.data.connected && src && src.available ? src.value : null;
  return { value, style, label: wd.label || wd.source || "", history: ctx.data.history?.get(wd.source) };
}

// 270 degree arc like the firmware's lv_arc (rotation 135, bg 0..270).
function gauge(side, boxW, frac, cls) {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", side);
  svg.setAttribute("height", side);
  svg.style.position = "absolute";
  svg.style.left = (boxW - side) / 2 + "px";
  svg.style.top = "0";
  const sw = side >= 120 ? 10 : 7;
  const r = side / 2 - sw / 2;
  const c = side / 2;
  const arc = (a0, a1) => {
    const p = (a) => [c + r * Math.cos((a * Math.PI) / 180), c + r * Math.sin((a * Math.PI) / 180)];
    const [x0, y0] = p(a0);
    const [x1, y1] = p(a1);
    return `M ${x0} ${y0} A ${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  };
  const bg = document.createElementNS("http://www.w3.org/2000/svg", "path");
  bg.setAttribute("d", arc(135, 405));
  bg.setAttribute("class", "arc-bg");
  bg.setAttribute("stroke-width", sw);
  svg.append(bg);
  if (frac !== null && frac > 0.001) {
    const fg = document.createElementNS("http://www.w3.org/2000/svg", "path");
    fg.setAttribute("d", arc(135, 135 + 270 * Math.min(frac, 0.9999)));
    fg.setAttribute("class", "arc-fg " + cls);
    fg.setAttribute("stroke-width", sw);
    svg.append(fg);
  }
  return svg;
}

/**
 * Renders the whole screen into `screen` (a 480x320 element).
 * page: the config page; cfg: the whole config; data: live data; catalog: /api/catalog.
 * Returns the card elements in widget order (the editor adds selection and dragging).
 */
export function renderScreen(screen, cfg, pageIndex, data, catalog) {
  screen.replaceChildren();
  screen.style.setProperty("--accent-dev", cfg.theme?.accent || "#4FC3F7");
  const pages = cfg.pages || [];
  const page = pages[pageIndex];

  const bar = el("div", "statusbar");
  const now = new Date();
  bar.append(el("span", "sb-clock f14", data.connected ? `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}` : "--:--"));
  bar.append(el("span", "sb-name f14 dim", page ? page.name || page.id : ""));
  const dots = el("span", "sb-dots");
  for (let i = 0; i < Math.min(pages.length, 8); i++) dots.append(el("i", i === pageIndex ? "on" : ""));
  bar.append(dots);
  bar.append(el("span", "sb-usb f14 " + (data.connected ? "ok" : "faint"), "⬌"));
  screen.append(bar);

  const grid = el("div", "grid");
  screen.append(grid);
  const cards = [];
  if (!page) return { grid, cards };
  const cols = cfg.grid?.cols ?? 4;
  const rows = cfg.grid?.rows ?? 3;
  const units = cfg.units || { temp: "c", wind: "kmh", pressure: "hpa" };
  (page.widgets || []).forEach((wd, i) => {
    const r = cardRect(wd, cols, rows);
    const card = el("div", "card");
    Object.assign(card.style, { left: r.x + "px", top: r.y + "px", width: r.w + "px", height: r.h + "px" });
    const box = el("div", "content");
    card.append(box);
    const render = RENDER[wd.type];
    if (render && r.w > 2 * PAD && r.h > 2 * PAD) {
      try {
        render(box, r.w - 2 * PAD, r.h - 2 * PAD, { widget: wd, data, catalog, units }, wd.mode);
      } catch (e) {
        box.append(el("div", "f14 crit", String(e)));
      }
    } else if (!render) {
      card.classList.add("error");
      box.append(el("div", "f14 crit center", `${wd.type}\nnot supported`));
    }
    grid.append(card);
    cards.push(card);
  });
  return { grid, cards };
}

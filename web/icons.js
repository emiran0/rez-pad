// Small inline SVG icons (24x24 grid, stroked, inherit currentColor). Original, drawn for rez-pad.
"use strict";

const P = {
  plus: "M12 5v14M5 12h14",
  close: "M6 6l12 12M18 6L6 18",
  back: "M15 5l-7 7 7 7",
  trash: "M4 7h16M10 11v6M14 11v6M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12M9 7V4h6v3",
  more: "M5 12h.01M12 12h.01M19 12h.01",
  grid: "M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z",
  palette: "M12 3a9 9 0 1 0 0 18c1.1 0 1.5-.8 1.5-1.5 0-1.2-1-1.6-1-2.5 0-.8.7-1.5 1.5-1.5H16a5 5 0 0 0 5-5c0-4.1-4-7.5-9-7.5zM7.5 11h.01M10 7.5h.01M14.5 7.5h.01",
  knob: "M12 3v4M12 21a7 7 0 1 0 0-14 7 7 0 0 0 0 14zM12 14l3-3",
  up: "M6 15l6-6 6 6",
  down: "M6 9l6 6 6-6",
  copy: "M9 9h10v10H9zM5 15V5h10",
  upload: "M12 16V4M7 9l5-5 5 5M4 20h16",
  check: "M5 12l5 5 9-10",
  warn: "M12 4l9 16H3zM12 10v4M12 17h.01",
  search: "M11 18a7 7 0 1 0 0-14 7 7 0 0 0 0 14zM20 20l-4-4",
  // settings categories
  screen: "M3 5h18v12H3zM8 21h8M12 17v4",
  statusbar: "M3 4h18v16H3zM3 8h18M6 6h3M15 6h3",
  bolt: "M13 3L5 14h6l-1 7 8-11h-6z",
  plug: "M9 3v5M15 3v5M7 8h10v3a5 5 0 0 1-10 0zM12 16v5",
  chart: "M4 20V10M10 20V4M16 20v-7M22 20H2",
  // widget types
  now_playing: "M4 5h7v7H4zM14 6h6M14 10h4M5 17h14M5 20h9",
  album_art: "M4 4h16v16H4zM9 15a2 2 0 1 0 0-4 2 2 0 0 0 0 4zM11 13V7l4 1",
  track_info: "M4 6h16M4 11h12M4 16h8",
  media_controls: "M6 7v10M18 7v10M9 12l6-4v8z",
  progress: "M3 12h18M3 12h10",
  volume: "M4 10v4h4l5 4V6L8 10zM16 9a4 4 0 0 1 0 6M19 6a8 8 0 0 1 0 12",
  metric_gauge: "M5 17a8 8 0 1 1 14 0M12 13l4-4",
  metric_value: "M5 8l3-2v12M12 9a2 2 0 1 1 4 0c0 2-4 4-4 7h4M19 18h.01",
  metric_bar: "M4 9h16v6H4zM4 9h10v6H4z",
  metric_graph: "M3 17l5-5 4 3 6-7 3 3",
  clock: "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM12 7v5l3 2",
  weather_now: "M7 18h10a4 4 0 0 0 0-8 6 6 0 0 0-11.5 1.5A3.5 3.5 0 0 0 7 18z",
  weather_forecast: "M4 5h16v15H4zM4 10h16M9 3v4M15 3v4",
  room_climate: "M10 14V5a2 2 0 1 1 4 0v9a4 4 0 1 1-4 0z",
  label: "M5 6h14M12 6v13M9 19h6",
  image: "M4 5h16v14H4zM4 16l5-5 4 4 3-3 4 4M15 9h.01",
};

export function icon(name, size = 18) {
  const span = document.createElement("span");
  span.className = "icon";
  span.innerHTML = `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="${P[name] || P.grid}"/></svg>`;
  return span;
}

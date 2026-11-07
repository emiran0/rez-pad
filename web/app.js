"use strict";

const STATE_TEXT = {
  Connected: "connected",
  Connecting: "connecting...",
  Searching: "not found (plug in the native USB port)",
  Incompatible: "incompatible firmware",
};

async function refreshStatus() {
  try {
    const res = await fetch("/api/status");
    const s = await res.json();
    document.getElementById("host-ver").textContent = s.hostVersion;
    document.getElementById("proto-ver").textContent = "v" + s.protocolVersion;
    let device = STATE_TEXT[s.linkState] || s.linkState;
    if (s.deviceConnected) {
      device += ` on ${s.port}, firmware ${s.firmwareVersion} (protocol v${s.firmwareProtocol}), ${s.serial}`;
    } else if (s.error && s.linkState === "Incompatible") {
      device += `: ${s.error}`;
    }
    document.getElementById("device").textContent = device;
    const sensors = document.getElementById("sensors");
    sensors.textContent = s.sensors?.message ?? (s.elevated ? "all readable (running as administrator)" : "all readable");
    sensors.className = s.sensors?.message ? "warn" : "";
  } catch {
    document.getElementById("device").textContent = "host not reachable";
  }
}

async function refreshSources() {
  try {
    const res = await fetch("/api/sources");
    const data = await res.json();
    const body = document.querySelector("#sources tbody");
    body.replaceChildren(...data.sources.map((s) => {
      const tr = document.createElement("tr");
      const value = s.available ? `${s.value} ${s.unit}` : "unavailable";
      for (const [text, cls] of [[s.id, "id"], [s.name, ""], [value, s.available ? "num" : "num dim"], [s.used ? "on device" : "", "dim"]]) {
        const td = document.createElement("td");
        td.textContent = text;
        if (cls) td.className = cls;
        tr.append(td);
      }
      return tr;
    }));
  } catch {
    // Host not reachable; the status line already says so.
  }
}

const WIND = { kmh: "km/h", ms: "m/s", mph: "mph" };

async function refreshWeather() {
  try {
    const w = await (await fetch("/api/weather")).json();
    const loc = w.location;
    document.getElementById("wx-loc").textContent = loc.is_set
      ? `${loc.name} (${loc.lat.toFixed(2)}, ${loc.lon.toFixed(2)}), every ${loc.refresh_min} min`
      : "not set: search for a city below";
    for (const k of ["temp", "wind", "pressure"]) {
      const sel = document.getElementById(`u-${k}`);
      if (document.activeElement !== sel) sel.value = w.units[k];
    }
    const now = w.latest?.now;
    const u = w.units;
    document.getElementById("wx-now").textContent = now
      ? `${now.temp}°${u.temp.toUpperCase()}, feels ${now.feels}°, ${now.hum} % RH, wind ${now.wind} ${WIND[u.wind]}, code ${now.code}, ` +
        `updated ${Math.round(w.age_s / 60)} min ago`
      : (w.error ? `error: ${w.error}` : "no data yet");
  } catch {
    document.getElementById("wx-now").textContent = "host not reachable";
  }
  try {
    const s = await (await fetch("/api/sensor")).json();
    const r = s.reading;
    document.getElementById("room").textContent = r
      ? `${r.temp_c ?? "--"} °C, ${r.hum_pct ?? "--"} % RH, ${r.press_hpa ?? "--"} hPa, trend ${r.trend} (${s.age_s} s ago)`
      : "no reading (device not connected)";
  } catch {
    // status line covers it
  }
}

async function saveWeather(body) {
  const res = await fetch("/api/weather", { method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  if (!res.ok) alert("Could not save: " + (await res.text()));
  setTimeout(refreshWeather, 1500);
}

document.getElementById("wx-search").addEventListener("submit", async (e) => {
  e.preventDefault();
  const q = document.getElementById("wx-q").value.trim();
  const list = document.getElementById("wx-results");
  list.replaceChildren();
  if (!q) return;
  const res = await fetch("/api/geocode?q=" + encodeURIComponent(q));
  const items = res.ok ? await res.json() : [];
  if (items.length === 0) {
    const li = document.createElement("li");
    li.textContent = res.ok ? "No places found" : "Search failed";
    list.append(li);
    return;
  }
  for (const it of items) {
    const li = document.createElement("li");
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = [it.name, it.admin1, it.country].filter(Boolean).join(", ") + ` (${it.lat.toFixed(2)}, ${it.lon.toFixed(2)})`;
    b.addEventListener("click", () => {
      list.replaceChildren();
      saveWeather({ name: it.name, lat: it.lat, lon: it.lon });
    });
    li.append(b);
    list.append(li);
  }
});

for (const k of ["temp", "wind", "pressure"]) {
  document.getElementById(`u-${k}`).addEventListener("change", () => saveWeather({
    units: {
      temp: document.getElementById("u-temp").value,
      wind: document.getElementById("u-wind").value,
      pressure: document.getElementById("u-pressure").value,
    },
  }));
}

refreshStatus();
refreshSources();
refreshWeather();
setInterval(refreshWeather, 5000);
setInterval(refreshStatus, 2000);
setInterval(refreshSources, 1000);

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

refreshStatus();
refreshSources();
setInterval(refreshStatus, 2000);
setInterval(refreshSources, 1000);

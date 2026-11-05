"use strict";

async function refreshStatus() {
  try {
    const res = await fetch("/api/status");
    const s = await res.json();
    document.getElementById("host-ver").textContent = s.hostVersion;
    document.getElementById("proto-ver").textContent = "v" + s.protocolVersion;
    document.getElementById("device").textContent = s.deviceConnected ? "connected" : "not connected";
  } catch {
    document.getElementById("device").textContent = "host not reachable";
  }
}

refreshStatus();
setInterval(refreshStatus, 2000);

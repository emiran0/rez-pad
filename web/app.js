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
  } catch {
    document.getElementById("device").textContent = "host not reachable";
  }
}

refreshStatus();
setInterval(refreshStatus, 2000);

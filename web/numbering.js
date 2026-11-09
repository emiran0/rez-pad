// Knob and button numbering (Settings > Knobs and buttons). Each number (Knob 1 to 3, Button 1
// to 4) reads one physical port; "Assign" asks the device for the next control turned or pressed
// (/api/device/learn) and gives that control this number. Gestures stay with the number; a knob's
// "steps per click" and "reverse direction" describe the physical knob, so they move with it.
"use strict";

import { state, h, api, change, toast, knobPorts, buttonPorts } from "./state.js";
import { icon } from "./icons.js";

const KNOB_PINS = ["GPIO 4, 5, 6", "GPIO 7, 15, 16", "GPIO 39, 40, 41"];
const BUTTON_PINS = ["GPIO 17", "GPIO 18", "GPIO 8", "GPIO 13"];
const LEARN_MS = 15000;

// The slot waiting for a control: {knob, n, until}; message: {knob, n, text, bad} after a try.
let learning = null;
let message = null;
let rerender = () => {};
let tickTimer = 0;

const kindName = (knob) => (knob ? "knob" : "button");
const portsKey = () => JSON.stringify([knobPorts(), buttonPorts()]);
const label = (knob, n) => `${knob ? "Knob" : "Button"} ${n}`;
const portText = (knob, port) => `port ${port} (${(knob ? KNOB_PINS : BUTTON_PINS)[port - 1]})`;

/** Trailing unused numbers are left out ("[2, 1, 0]" is stored as "[2, 1]"). */
function trim(arr) {
  const out = [...arr];
  while (out.length && !out[out.length - 1]) out.pop();
  return out;
}

/**
 * Gives number n the physical port. A number that had this port takes n's old port (a swap), or
 * is no longer used when n had none. Returns the message for the user.
 */
export function assign(knob, n, port) {
  const ports = knob ? knobPorts() : buttonPorts();
  const i = n - 1;
  const old = ports[i];
  if (old === port) return { text: `${label(knob, n)} is already this ${kindName(knob)} (${portText(knob, port)}).` };
  const other = ports.findIndex((p, k) => p === port && k !== i);
  const next = [...ports];
  next[i] = port;
  if (other >= 0) next[other] = old;
  change((cfg) => {
    cfg.inputs ??= {};
    if (knob) {
      cfg.inputs.knob_ports = trim(next);
      if (other >= 0) {
        // Steps per click and direction belong to the physical knob: they move with it.
        const a = (cfg.inputs[`enc${n}`] ??= {});
        const b = (cfg.inputs[`enc${other + 1}`] ??= {});
        for (const key of ["counts_per_detent", "invert"]) {
          const t = a[key];
          if (b[key] === undefined) delete a[key];
          else a[key] = b[key];
          if (t === undefined) delete b[key];
          else b[key] = t;
        }
      }
    } else {
      cfg.inputs.button_ports = trim(next);
      delete cfg.inputs.buttons;  // replaced by button_ports
    }
  });
  let text = `${label(knob, n)} is now the ${kindName(knob)} on ${portText(knob, port)}.`;
  if (other >= 0) {
    text += old
      ? ` ${label(knob, other + 1)} had it and moved to ${portText(knob, old)}.`
      : ` ${label(knob, other + 1)} had it and is not used now (its actions are kept for when you assign it again).`;
  }
  return { text: text + " Save to send it to the device." };
}

/** Number n no longer reads any port (its actions are kept). */
function unassign(knob, n) {
  const ports = knob ? knobPorts() : buttonPorts();
  ports[n - 1] = 0;
  change((cfg) => {
    cfg.inputs ??= {};
    cfg.inputs[knob ? "knob_ports" : "button_ports"] = trim(ports);
    if (!knob) delete cfg.inputs.buttons;
  });
  message = { knob, n, text: `${label(knob, n)} is not used now. Save to send it to the device.`, ports: portsKey() };
  rerender();
}

async function learn(knob, n) {
  if (learning) await cancel();
  message = null;
  learning = { knob, n, until: Date.now() + LEARN_MS };
  clearInterval(tickTimer);
  tickTimer = setInterval(() => (learning ? rerender() : clearInterval(tickTimer)), 1000);
  rerender();
  const mine = learning;
  const r = await api(`/api/device/learn?ms=${LEARN_MS}`, { method: "POST" }).catch(() => null);
  if (learning !== mine) return;  // cancelled or replaced meanwhile
  learning = null;
  clearInterval(tickTimer);
  const res = r?.ok ? r.body : { error: "the host is not reachable" };
  if (res.error === "cancelled" || res.error === "replaced by a newer request") {
    message = null;
  } else if (res.error) {
    message = { knob, n, bad: true, text: `Could not listen for it: ${res.error}.` };
  } else if (!res.kind) {
    message = { knob, n, bad: true, text: "Nothing was turned or pressed in time. Try again." };
  } else if ((res.kind === "knob") !== knob) {
    message = { knob, n, bad: true, text: `That was a ${res.kind} (${portText(res.kind === "knob", res.port)}). Turn or press a ${kindName(knob)} for ${label(knob, n)}.` };
  } else {
    message = { knob, n, ...assign(knob, n, res.port) };
    toast(message.text.replace(/ Save to send.*$/, ""));
  }
  if (message) message.ports = portsKey();
  rerender();
}

async function cancel() {
  learning = null;
  clearInterval(tickTimer);
  await api("/api/device/learn", { method: "DELETE" }).catch(() => null);
  rerender();
}

function slot(knob, n, port) {
  const busy = learning && learning.knob === knob && learning.n === n;
  // A message describes one numbering: hide it once that changed (undo, another edit).
  const msg = message && message.knob === knob && message.n === n && message.ports === portsKey() ? message : null;
  const connected = !!state.data.connected;
  const left = busy ? Math.max(0, Math.ceil((learning.until - Date.now()) / 1000)) : 0;
  return h("div", { class: "num-slot" + (port ? "" : " unused") + (busy ? " busy" : "") },
    h("div", { class: "num-badge", "aria-hidden": "true" }, icon(knob ? "knob" : "button", 18), h("b", { text: String(n) })),
    h("div", { class: "num-body" },
      h("strong", { text: label(knob, n) }),
      h("small", { text: busy ? `${knob ? "Turn or press" : "Press"} the ${kindName(knob)} that should be ${label(knob, n)}... ${left} s` : port ? portText(knob, port) : "Not used" }),
      msg ? h("p", { class: "num-msg" + (msg.bad ? " bad" : ""), text: msg.text }) : null),
    h("div", { class: "num-actions" },
      busy
        ? h("button", { type: "button", class: "ghost", onclick: cancel }, "Cancel")
        : h("button", {
          type: "button", disabled: !connected || !!learning,
          title: connected ? `Then ${knob ? "turn or press" : "press"} the ${kindName(knob)} on the device` : "Connect the device first",
          onclick: () => learn(knob, n),
        }, port ? "Change" : "Assign"),
      port && !busy ? h("button", {
        type: "button", class: "icon-btn", title: `Stop using ${label(knob, n)}`, "aria-label": `Stop using ${label(knob, n)}`,
        disabled: !!learning, onclick: () => unassign(knob, n),
      }, icon("close", 16)) : null));
}

/** The numbering card. `onChange` re-renders the settings view. */
export function numberingCard(onChange) {
  rerender = onChange;
  const kp = knobPorts();
  const bp = buttonPorts();
  return h("div", { class: "numbering" },
    h("h3", { text: "Numbering" }),
    h("p", { class: "hint", text: "Which physical knob or button is Knob 1, Button 2 and so on. Click Assign, then turn or press that control on the device. Its actions below stay with the number. A control that already had a number swaps with it."
      + (state.data.connected ? "" : " (Connect the device to assign.)") }),
    h("div", { class: "num-grid" },
      ...kp.map((p, i) => slot(true, i + 1, p)),
      ...bp.map((p, i) => slot(false, i + 1, p))));
}

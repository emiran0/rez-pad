#include "usb_device.h"

#include <USB.h>
#include <USBCDC.h>
#include <esp_mac.h>

#include "hid/media_keys.h"

#ifndef FW_VERSION_BCD
#define FW_VERSION_BCD 0x0010  // 0.1.0 as BCD for bcdDevice
#endif

namespace rp {
namespace usb {

namespace {

USBCDC gCdc(0);
char gSerial[20] = "";
volatile bool gMounted = false;
volatile bool gSuspended = false;

void onUsbEvent(void*, esp_event_base_t base, int32_t id, void*) {
  if (base != ARDUINO_USB_EVENTS) return;
  switch (id) {
    case ARDUINO_USB_STARTED_EVENT:
      gMounted = true;
      gSuspended = false;
      Serial.println("[usb] connected to host");
      break;
    case ARDUINO_USB_STOPPED_EVENT:
      gMounted = false;
      Serial.println("[usb] disconnected");
      break;
    case ARDUINO_USB_SUSPEND_EVENT:
      gSuspended = true;
      Serial.println("[usb] suspended (PC asleep)");
      break;
    case ARDUINO_USB_RESUME_EVENT:
      gSuspended = false;
      Serial.println("[usb] resumed");
      break;
    default:
      break;
  }
}

void onCdcEvent(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base != ARDUINO_USB_CDC_EVENTS || id != ARDUINO_USB_CDC_LINE_STATE_EVENT) return;
  const auto* e = static_cast<arduino_usb_cdc_event_data_t*>(data);
  Serial.printf("[usb] %lu ms: CDC line state dtr=%d rts=%d\n", static_cast<unsigned long>(millis()),
                e->line_state.dtr, e->line_state.rts);
}

}  // namespace

bool begin() {
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  snprintf(gSerial, sizeof(gSerial), "RP-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);

  USB.VID(kVid);
  USB.PID(kPid);
  USB.manufacturerName("rez-pad");
  USB.productName("rez-pad");
  USB.serialNumber(gSerial);
  USB.firmwareVersion(FW_VERSION_BCD);
  USB.onEvent(onUsbEvent);

  hid::begin();      // registers the HID consumer-control interface
  gCdc.onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT, onCdcEvent);  // diagnostics: host opens/closes the port
  gCdc.begin();      // baud rate is ignored on USB CDC
  gCdc.setRxBufferSize(4096);  // art and config chunks arrive in 4 KB bursts (Phase 6+)
  gCdc.setTxTimeoutMs(50);     // never stall the link task for long if the host stops reading
  // No reboot-to-bootloader on esptool's DTR/RTS sequence or a 1200 baud "touch": a normal port
  // open/close by the host can produce the same line-state order (seen with .NET SerialPort, which
  // raises RTS before DTR) and would drop the device into the ROM bootloader. Flash through the
  // USB-UART port instead, or hold BOOT and tap RESET to flash through the native port.
  gCdc.enableReboot(false);
  return USB.begin();
}

bool mounted() { return gMounted && !gSuspended; }
bool suspended() { return gSuspended; }
const char* serialNumber() { return gSerial; }
USBCDC& cdc() { return gCdc; }

}  // namespace usb
}  // namespace rp

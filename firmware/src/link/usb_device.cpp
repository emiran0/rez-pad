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
  gCdc.begin();      // baud rate is ignored on USB CDC
  // esptool's DTR/RTS reset sequence (and a 1200 baud "touch") reboots into the bootloader, so the
  // native port can still be used for flashing. The host app must not open the port at 1200 baud.
  gCdc.enableReboot(true);
  return USB.begin();
}

bool mounted() { return gMounted && !gSuspended; }
bool suspended() { return gSuspended; }
const char* serialNumber() { return gSerial; }
USBCDC& cdc() { return gCdc; }

}  // namespace usb
}  // namespace rp

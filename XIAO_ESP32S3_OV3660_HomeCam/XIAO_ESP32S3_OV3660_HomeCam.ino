#include <Arduino.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_system.h>
#include <nvs_flash.h>

#include "camera_pins.h"
#include "config.h"
#include "media_services.h"

namespace {

char g_provisioningName[24];
char g_hostname[32];
volatile bool g_provisioningActive = false;
uint32_t g_buttonDownAt = 0;
bool g_resetTriggered = false;
uint32_t g_lastReconnectAttempt = 0;
bool g_timeConfigured = false;

uint32_t shortChipId() {
  const uint64_t mac = ESP.getEfuseMac();
  return static_cast<uint32_t>(mac & 0xFFFFFF);
}

void networkEvent(arduino_event_t *event) {
  switch (event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      g_provisioningActive = true;
      Serial.println("Bluetooth Wi-Fi provisioning started");
      break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
      Serial.printf("Received Wi-Fi network: %s\n",
                    event->event_info.prov_cred_recv.ssid);
      break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("Wi-Fi provisioning failed; check SSID and password");
      break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      Serial.println("Wi-Fi provisioning successful");
      break;
    case ARDUINO_EVENT_PROV_END:
      g_provisioningActive = false;
      Serial.println("Provisioning service stopped");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("Wi-Fi connected. IP: %s\n",
                    WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Wi-Fi disconnected");
      break;
    default:
      break;
  }
}

void startProvisioning() {
  snprintf(g_provisioningName, sizeof(g_provisioningName), "PROV_%06lX",
           static_cast<unsigned long>(shortChipId()));
  snprintf(g_hostname, sizeof(g_hostname), "xiao-homecam-%06lx",
           static_cast<unsigned long>(shortChipId()));

  uint8_t serviceUuid[16] = {0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b,
                             0xf4, 0xbf, 0xea, 0x4a, 0x82, 0x03,
                             0x04, 0x90, 0x1a, 0x02};

  WiFiProv.beginProvision(
      NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BLE,
      NETWORK_PROV_SECURITY_1, HOME_CAM_PROVISIONING_POP, g_provisioningName,
      nullptr, serviceUuid, false);
  WiFiProv.printQR(g_provisioningName, HOME_CAM_PROVISIONING_POP, "ble");
}

void checkFactoryResetButton() {
  const bool pressed = digitalRead(FACTORY_RESET_BUTTON_PIN) == LOW;
  if (!pressed) {
    g_buttonDownAt = 0;
    return;
  }

  if (g_buttonDownAt == 0) {
    g_buttonDownAt = millis();
  }
  if (!g_resetTriggered &&
      millis() - g_buttonDownAt >= HOME_CAM_FACTORY_RESET_MS) {
    g_resetTriggered = true;
    Serial.println("Factory reset: erasing Wi-Fi settings and restarting");
    nvs_flash_erase();
    delay(500);
    ESP.restart();
  }
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED || g_provisioningActive) {
    return;
  }
  if (millis() - g_lastReconnectAttempt >= 15000) {
    g_lastReconnectAttempt = millis();
    Serial.println("Attempting Wi-Fi reconnect");
    WiFi.reconnect();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nXIAO ESP32S3 Sense OV3660 HomeCam starting");

  pinMode(FACTORY_RESET_BUTTON_PIN, INPUT_PULLUP);
  WiFi.onEvent(networkEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (strcmp(HOME_CAM_PROVISIONING_POP, "change-me-7391") == 0) {
    Serial.println(
        "SECURITY WARNING: change HOME_CAM_PROVISIONING_POP in config.h");
  }

  startProvisioning();
}

void loop() {
  checkFactoryResetButton();
  maintainWiFi();

  if (WiFi.status() == WL_CONNECTED && !mediaServicesStarted()) {
    if (!g_timeConfigured) {
      // Recording still works if NTP is unavailable; it falls back to a boot
      // identifier. Once time arrives, new segments use UTC timestamps.
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      g_timeConfigured = true;
    }
    if (!startMediaServices(g_hostname)) {
      Serial.println("Media startup failed; restarting in five seconds");
      delay(5000);
      ESP.restart();
    }
  }

  delay(20);
}

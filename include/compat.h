#pragma once

/**
 * Thin ESP32 / ESP8266 wrappers so the word clock can share one firmware
 * tree. Pin defaults live in config.h.
 */

#include <Arduino.h>
#include <time.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Updater.h>
using WebServer = ESP8266WebServer;
#ifndef WIFI_AUTH_OPEN
#define WIFI_AUTH_OPEN ENC_TYPE_NONE
#endif
#else
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#endif

#include <ArduinoOTA.h>
#include <Preferences.h>

namespace compat {

inline void setHostname(const char* name) {
#ifdef ESP8266
  WiFi.hostname(name);
#else
  WiFi.setHostname(name);
#endif
}

inline void wifiDisconnectKeepCreds() {
#ifdef ESP8266
  WiFi.disconnect(false);
#else
  WiFi.disconnect(false, false);
#endif
}

inline void mdnsUpdate() {
#ifdef ESP8266
  MDNS.update();
#endif
}

inline void mdnsStop() {
#ifdef ESP8266
  MDNS.close();
#else
  MDNS.end();
#endif
}

inline void captureWifiCreds(char* ssid, size_t ssid_n, char* pass,
                             size_t pass_n) {
  strncpy(ssid, WiFi.SSID().c_str(), ssid_n - 1);
  ssid[ssid_n - 1] = '\0';
  strncpy(pass, WiFi.psk().c_str(), pass_n - 1);
  pass[pass_n - 1] = '\0';
}

inline void startNtp(const char* tz, const char* ntp) {
#ifdef ESP8266
  configTime(0, 0, ntp, "time.nist.gov");
  setenv("TZ", tz, 1);
  tzset();
#else
  configTzTime(tz, ntp, "time.nist.gov");
  setenv("TZ", tz, 1);
  tzset();
#endif
}

inline bool waitLocalTime(struct tm* out, uint32_t timeout_ms) {
#ifdef ESP8266
  const uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    const time_t now = time(nullptr);
    if (now > 8 * 3600L) {
      localtime_r(&now, out);
      return true;
    }
    delay(40);
    yield();
  }
  return false;
#else
  return getLocalTime(out, timeout_ms);
#endif
}

inline bool otaBegin(size_t content_len) {
#ifdef ESP8266
  const size_t space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  return Update.begin(content_len ? content_len : space);
#else
  return Update.begin(content_len ? content_len : UPDATE_SIZE_UNKNOWN);
#endif
}

inline const char* chipName() {
#ifdef ESP8266
  return "ESP8266";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  return "ESP32-C3";
#else
  return "ESP32";
#endif
}

}  // namespace compat

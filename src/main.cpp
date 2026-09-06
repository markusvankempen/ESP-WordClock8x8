/**
 * ESP-WordClock8x8 — 8×8 WS2812 English word clock (ESP32-S3 / ESP8266).
 * Copyright (c) 2026 Markus Van Kempen  MIT
 */

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <sys/time.h>
#include <time.h>

#include <Arduino.h>
#include <cmath>
#include <cstring>

#include "compat.h"
#include "config.h"
#include "face.h"

namespace {

Adafruit_NeoPixel s_leds(config::kLedCount, static_cast<int16_t>(config::kLedPin),
                         NEO_GRB + NEO_KHZ800);
Preferences s_prefs;
WiFiManager s_wm;
WebServer s_http(80);

bool s_zigzag = false;
bool s_rgb_order = false;
bool s_flip_x = false;
bool s_flip_y = false;
bool s_col_major = false;
uint8_t s_cal_mode = 0;  // 0 clock, 1 corners, 2 led0, 3 led63, 4-7 face corners
uint8_t s_brightness = config::kDefaultBrightness;
uint8_t s_fx_bright[6] = {40, 40, 36, 48, 44, 56};  // per seconds effect
uint8_t s_color_mode = 0;  // 0 hour, 1 solid, 2 rainbow, 3 warm
uint8_t s_sec_fx = 5;      // 0 off, 1 pulse, 2 trail, 3 tick, 4 sparkle, 5 star
bool s_min_dots = true;    // extra minutes as 1-4 dots on * / unused cells
bool s_word_preview = false;
face::Word s_preview_word = face::Word::kTwenty;
uint8_t s_preview_row = 255;

// Schedule: night dimming, blackout window and a daily alarm. Times are
// minutes since midnight so a window can wrap past midnight.
bool s_night_on = false;
uint16_t s_night_start = 21 * 60;
uint16_t s_night_end = 7 * 60;
uint8_t s_night_bright = 10;
uint8_t s_night_cmode = 4;  // 0-3 as colour modes, 4 = keep the daytime colour
bool s_sleep_on = false;
uint16_t s_sleep_start = 23 * 60;
uint16_t s_sleep_end = 6 * 60;
bool s_alarm_on = false;
uint16_t s_alarm_min = 7 * 60;
uint8_t s_alarm_len = 60;  // seconds
bool s_night_now = false;
bool s_sleep_now = false;
bool s_alarm_ringing = false;
unsigned long s_alarm_stop_ms = 0;
int s_alarm_fired_day = -1;
int s_alarm_fired_min = -1;
uint8_t s_cr = 255;
uint8_t s_cg = 160;
uint8_t s_cb = 40;
uint32_t s_color = 0;
bool s_used[64] = {};
bool s_time_ok = false;
bool s_web_started = false;
bool s_portal_open = false;
bool s_portal_routes = false;
bool s_mdns_ok = false;
bool s_wifi_started = false;
unsigned long s_portal_got_ip_ms = 0;
unsigned long s_last_ntp_try = 0;
unsigned long s_last_show = 0;
unsigned long s_last_wait_led = 0;
uint16_t s_wait_pixel = 0;
int s_override_h = -1;
int s_override_m = -1;

bool s_btn_was_down = false;
unsigned long s_btn_down_ms = 0;

#ifdef ESP8266
char s_line[384];
#else
char s_line[768];
#endif
size_t s_line_len = 0;
char s_ntp[64] = {};
char s_tz[48] = {};
char s_hostname[32] = {};

WiFiClient s_net;
PubSubClient s_mqtt(s_net);
bool s_mqtt_on = false;
bool s_ha_disco = true;
char s_mqtt_host[64] = {};
uint16_t s_mqtt_port = config::kDefaultMqttPort;
char s_mqtt_user[32] = {};
char s_mqtt_pass[64] = {};
char s_mqtt_prefix[32] = {};
char s_mqtt_status[24] = "off";
unsigned long s_last_mqtt_pub = 0;
unsigned long s_last_ntp_ok_ms = 0;
bool s_ota_ready = false;

// Credentials in use before a dashboard join, so a bad password can roll back.
char s_prev_ssid[33] = {};
char s_prev_pass[65] = {};
char s_join_ssid[33] = {};
char s_join_pass[65] = {};
bool s_join_pending = false;
bool s_joining = false;
unsigned long s_join_deadline = 0;
unsigned long s_portal_grace_ms = 18000;

WiFiManagerParameter s_param_note(
    "<p class='wc-note'>Join this setup AP, pick your <b>2.4 GHz</b> network, "
    "then wait here. The new clock address appears as soon as it connects.</p>");
WiFiManagerParameter s_param_ntp("ntp", "NTP server", config::kNtpServer, 63);
WiFiManagerParameter s_param_tz("tz", "POSIX timezone", config::kPosixTz, 47);

const char kWmMenuExtra[] =
    "<details class='wc-fold'><summary>More</summary>"
    "<form action='/info' method='get'><button type='submit'>Info</button></form>"
    "<form action='/exit' method='get'><button type='submit'>Exit setup</button></form>"
    "<form action='/restart' method='get'><button type='submit'>Restart</button></form>"
    "</details>";

const char kWmHead[] =
    "<style>"
    "body.invert,body.invert a,body.invert h1{background:#08101f;color:#e8eef8}"
    "body.invert{margin:0;padding:20px;font-family:system-ui,sans-serif}"
    "body.invert .wrap{text-align:left;display:block;margin:auto;background:#122040;"
    "border:1px solid #2a3a60;border-radius:16px;padding:18px;min-width:0;max-width:520px}"
    "h1{font-size:.95rem;letter-spacing:.08em;text-transform:uppercase;color:#9aa8c0;"
    "font-weight:600;margin:0 0 6px}"
    "h3{font-size:1.15rem;margin:0 0 14px;color:#fff;font-weight:700}"
    "button,input[type=submit]{background:#16c060!important;border-radius:10px!important;"
    "font-weight:700;line-height:2.2rem!important}"
    "input,select{background:#081028!important;color:#e8eef8!important;border:1px solid #2a3a60;"
    "border-radius:8px}"
    "a{color:#7ee0a8!important}"
    "body.invert .msg{background:#081028;border-color:#2a3a60;border-left-color:#16c060}"
    ".wc-note{color:#9aa8c0;font-size:.9rem;line-height:1.45;margin:0 0 12px}"
    ".wc-fold{margin-top:16px;color:#9aa8c0}"
    ".wc-fold summary{cursor:pointer;padding:10px 0;font-weight:600}"
    "#wc-ip{position:sticky;top:8px;z-index:9}"
    ".wc-banner{background:#163528;border:1px solid #16c060;border-radius:14px;padding:16px;"
    "margin:0 0 14px;color:#e8eef8}"
    ".wc-banner.wait{background:#1a3050;border-color:#3a5a80}"
    ".wc-banner a{display:block;font:700 1.35rem/1.3 ui-monospace,Menlo,monospace;"
    "color:#fff!important;margin:10px 0;word-break:break-all}"
    ".wc-banner small{color:#9aa8c0}"
    "</style>"
    "<script>"
    "(function(){"
    "function $(id){return document.getElementById(id)}"
    "function box(){var b=$('wc-ip');if(b)return b;b=document.createElement('div');"
    "b.id='wc-ip';var w=document.querySelector('.wrap');"
    "(w||document.body).insertBefore(b,w?w.firstChild:null);return b}"
    "function paint(j){"
    "var b=box();"
    "if(!j.ok){"
    "if(document.querySelector('.msg')&&!b.dataset.wait){"
    "b.dataset.wait=1;"
    "b.innerHTML='<div class=\\'wc-banner wait\\'>Connecting to WiFi... keep this page open."
    "</div>';}"
    "return;}"
    "var url='http://'+j.ip+'/';"
    "var mdns='http://'+j.host+'/';"
    "b.innerHTML='<div class=\\'wc-banner\\'><div>Connected to <b>'+j.ssid+'</b></div>'"
    "+'<a id=wc-go href=\\''+url+'\\'>'+url+'</a>'"
    "+'<small>Join that same Wi-Fi, then tap the address.<br>Also try "
    "<a href=\\''+mdns+'\\'>'+mdns+'</a></small></div>';"
    "if(!b.dataset.go){b.dataset.go=1;setTimeout(function(){location.href=url},3000);}"
    "}"
    "function poll(){fetch('/sta').then(function(r){return r.json()}).then(paint).catch(function(){})}"
    "setInterval(poll,1000);poll();"
    "})();"
    "</script>";

constexpr char kPrefsNs[] = "wordclk";
constexpr uint8_t kBrightSteps[] = {12, 24, 40, 56, 80};

void pollSerial();
void showWait(uint32_t color);
void collectStars(uint16_t* out, uint16_t* count);
void refreshFace();
bool readClock(int* hour24, int* minute, int* second = nullptr);
void lightRow(int y);
void applyTimezone();
bool syncNtp();
void startWeb();
void restartMdns();
void saveWifiPrefs();
void saveMqttPrefs();
void openPortal();
void mqttLoop();
void mqttPublishState(bool force = false);
void setupOta();
void serviceOta();
void sendSettingsJson();
bool applySettingsJson(const char* json, String* err);
String htmlEscape(const String& in);
face::Word wordFromName(const char* s);

/** Keep only what mDNS and DHCP accept: lowercase letters, digits, dashes. */
void sanitizeHostname(char* out, size_t out_n, const char* in) {
  size_t n = 0;
  for (const char* p = in; p && *p && n + 1 < out_n; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c + ('a' - 'A'));
    }
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) {
      continue;
    }
    if (c == '-' && n == 0) {
      continue;  // no leading dash
    }
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == '-') {
    --n;  // no trailing dash
  }
  out[n] = '\0';
  if (n == 0) {
    strncpy(out, config::kHostname, out_n - 1);
    out[out_n - 1] = '\0';
  }
}

struct TzChoice {
  const char* id;
  const char* label;
  const char* posix;
};

constexpr TzChoice kTimezones[] = {
    {"ast", "Atlantic (Halifax / AST)", "AST4ADT,M3.2.0/2,M11.1.0/2"},
    {"est", "Eastern (Toronto / New York)", "EST5EDT,M3.2.0/2,M11.1.0/2"},
    {"cst", "Central (Chicago / Winnipeg)", "CST6CDT,M3.2.0/2,M11.1.0/2"},
    {"mst", "Mountain (Denver / Edmonton)", "MST7MDT,M3.2.0/2,M11.1.0/2"},
    {"pst", "Pacific (Vancouver / LA)", "PST8PDT,M3.2.0/2,M11.1.0/2"},
    {"az", "Arizona (no DST)", "MST7"},
    {"utc", "UTC", "UTC0"},
    {"uk", "United Kingdom", "GMT0BST,M3.5.0/1,M10.5.0/2"},
    {"cet", "Central Europe", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"syd", "Sydney", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
    {"jst", "Japan", "JST-9"},
};

const char* timezoneIdFor(const char* posix) {
  for (const auto& z : kTimezones) {
    if (strcmp(posix, z.posix) == 0) {
      return z.id;
    }
  }
  return "custom";
}

const char* timezonePosix(const char* id) {
  for (const auto& z : kTimezones) {
    if (strcmp(id, z.id) == 0) {
      return z.posix;
    }
  }
  return nullptr;
}

uint16_t pixelIndex(uint8_t x, uint8_t y) {
  if (x >= config::kMatrixW || y >= config::kMatrixH) {
    return 0;
  }
  uint8_t col = x;
  uint8_t row = y;
  if (s_flip_x) {
    col = static_cast<uint8_t>(7 - x);
  }
  if (s_flip_y) {
    row = static_cast<uint8_t>(7 - y);
  }
  if (s_col_major) {
    if (s_zigzag && (col & 1)) {
      row = static_cast<uint8_t>(7 - row);
    }
    return static_cast<uint16_t>(col * 8 + row);
  }
  if (s_zigzag && (row & 1)) {
    col = static_cast<uint8_t>(7 - col);
  }
  return static_cast<uint16_t>(row * 8 + col);
}

uint8_t clampBright(int value) {
  if (value < 1) {
    return 1;
  }
  if (value > config::kMaxBrightness) {
    return config::kMaxBrightness;
  }
  return static_cast<uint8_t>(value);
}

/** Brightness in force right now, after the night schedule is applied. */
uint8_t activeBright() {
  return s_night_now ? s_night_bright : s_brightness;
}

/** Colour mode in force right now. Night can override it, 4 means "keep". */
uint8_t activeColorMode() {
  if (s_night_now && s_night_cmode <= 3) {
    return s_night_cmode;
  }
  return s_color_mode;
}

float wordGain() {
  return activeBright() / static_cast<float>(config::kMaxBrightness);
}

float fxGain(uint8_t fx) {
  if (fx >= 6) {
    fx = 0;
  }
  uint8_t b = s_fx_bright[fx];
  if (s_night_now && b > s_night_bright) {
    b = s_night_bright;  // effects never outshine the dimmed words
  }
  return b / static_cast<float>(config::kMaxBrightness);
}

bool inWindow(int now_min, int start, int end) {
  if (start == end) {
    return false;
  }
  if (start < end) {
    return now_min >= start && now_min < end;
  }
  return now_min >= start || now_min < end;  // wraps past midnight
}

bool parseHhMm(const char* s, uint16_t* out) {
  int h = 0;
  int m = 0;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2) {
    return false;
  }
  if (h < 0 || h > 23 || m < 0 || m > 59) {
    return false;
  }
  *out = static_cast<uint16_t>(h * 60 + m);
  return true;
}

void fmtHhMm(uint16_t mins, char* out, size_t out_n) {
  snprintf(out, out_n, "%02u:%02u", mins / 60U, mins % 60U);
}

void applyBrightness() { s_leds.setBrightness(config::kMaxBrightness); }

void beginLeds() {
  s_leds.updateType((s_rgb_order ? NEO_RGB : NEO_GRB) + NEO_KHZ800);
  s_leds.setPin(static_cast<int16_t>(config::kLedPin));
  s_leds.begin();
  applyBrightness();
}

void clearLeds() { s_leds.clear(); }

void showNow() { s_leds.show(); }

void fill(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t c = s_leds.Color(r, g, b);
  for (uint16_t i = 0; i < config::kLedCount; ++i) {
    s_leds.setPixelColor(i, c);
  }
  showNow();
}

void bootFlash() {
  Serial.printf("LED boot flash on GPIO %d  order=%s  bright=%u\n",
                static_cast<int>(config::kLedPin), s_rgb_order ? "RGB" : "GRB",
                s_brightness);
  fill(60, 60, 60);
  delay(250);
  fill(80, 0, 0);
  delay(200);
  fill(0, 80, 0);
  delay(200);
  fill(0, 0, 80);
  delay(200);
  clearLeds();
  showNow();
}

uint32_t scaleColor(uint32_t c, float gain) {
  if (gain < 0) gain = 0;
  if (gain > 1.4f) gain = 1.4f;
  uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFF);
  uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFF);
  uint8_t b = static_cast<uint8_t>(c & 0xFF);
  r = static_cast<uint8_t>(r * gain);
  g = static_cast<uint8_t>(g * gain);
  b = static_cast<uint8_t>(b * gain);
  return s_leds.Color(r, g, b);
}

uint32_t themeColor(int hour12, uint8_t index) {
  switch (activeColorMode()) {
    case 1:
      return s_leds.Color(s_cr, s_cg, s_cb);
    case 2:
      return s_leds.gamma32(s_leds.ColorHSV(
          static_cast<uint16_t>(hour12 * 5461 + index * 2500), 200, 255));
    case 3:
      return s_leds.Color(255, 170, 70);
    default:
      return s_leds.gamma32(
          s_leds.ColorHSV(static_cast<uint16_t>((hour12 % 12) * 5461), 180, 255));
  }
}

void lightWord(face::Word word, int hour12) {
  size_t n = 0;
  const face::Pixel* px = face::pixels(word, &n);
  for (size_t i = 0; i < n; ++i) {
    const uint16_t idx = pixelIndex(px[i].x, px[i].y);
    if (idx < 64) {
      s_used[idx] = true;
    }
    s_leds.setPixelColor(
        idx, scaleColor(themeColor(hour12, static_cast<uint8_t>(i + idx)),
                        wordGain()));
  }
}

void lightWordAlign(face::Word word) {
  size_t n = 0;
  const face::Pixel* px = face::pixels(word, &n);
  const uint32_t c = s_leds.Color(90, 40, 0);
  for (size_t i = 0; i < n; ++i) {
    s_leds.setPixelColor(pixelIndex(px[i].x, px[i].y), c);
  }
}

void showWait(uint32_t color) {
  if (millis() - s_last_wait_led < 50) {
    return;
  }
  s_last_wait_led = millis();
  clearLeds();
  const float breath = 0.25f + 0.75f * (0.5f + 0.5f * sinf(millis() / 420.0f));
  // Soft pulse on the * / unused top-right cells so a missing NTP lock
  // never looks like a dead matrix.
  uint16_t stars[8];
  uint16_t n = 0;
  collectStars(stars, &n);
  const uint32_t glow = scaleColor(color, breath);
  for (uint16_t i = 0; i < n; ++i) {
    s_leds.setPixelColor(stars[i], glow);
  }
  s_leds.setPixelColor(s_wait_pixel, color);
  s_wait_pixel = static_cast<uint16_t>((s_wait_pixel + 1) % config::kLedCount);
  showNow();
}

void collectStars(uint16_t* out, uint16_t* count) {
  *count = 0;
  for (uint8_t y = 0; y < 8; ++y) {
    for (uint8_t x = 0; x < 8; ++x) {
      if (face::letter(x, y) == '*' && *count < 8) {
        out[(*count)++] = pixelIndex(x, y);
      }
    }
  }
  if (*count == 0) {
    out[(*count)++] = pixelIndex(7, 0);
  }
}

void applySeconds(int second) {
  if (s_sec_fx == 0) {
    return;
  }
  const float gain = fxGain(s_sec_fx);
  if (s_sec_fx == 5) {
    uint16_t stars[8];
    uint16_t n = 0;
    collectStars(stars, &n);
    const bool on = (millis() % 1000UL) < 500UL;
    if (on) {
      const uint32_t c = scaleColor(s_leds.Color(255, 220, 80), gain);
      for (uint16_t i = 0; i < n; ++i) {
        s_leds.setPixelColor(stars[i], c);
      }
    }
    return;
  }
  if (s_sec_fx == 1) {
    const float breath =
        0.35f + (0.30f + 0.35f * gain) * (0.5f + 0.5f * sinf(millis() / 280.0f));
    for (uint16_t i = 0; i < 64; ++i) {
      if (s_used[i]) {
        s_leds.setPixelColor(i, scaleColor(s_leds.getPixelColor(i), breath));
      }
    }
    return;
  }
  uint16_t unused[64];
  uint16_t n = 0;
  for (uint16_t i = 0; i < 64; ++i) {
    if (!s_used[i]) {
      unused[n++] = i;
    }
  }
  if (n == 0) {
    return;
  }
  const float frac = static_cast<float>(second) + (millis() % 1000) / 1000.0f;
  const uint16_t head = static_cast<uint16_t>((frac * n) / 60.0f) % n;
  if (s_sec_fx == 2) {
    for (uint16_t i = 0; i <= head; ++i) {
      const float trail = ((i == head) ? 1.0f : 0.35f + 0.45f * (i / (float)n)) * gain;
      s_leds.setPixelColor(
          unused[i],
          scaleColor(s_leds.gamma32(s_leds.ColorHSV(
                         static_cast<uint16_t>(i * 900 + second * 400), 200,
                         220)),
                     trail));
    }
  } else if (s_sec_fx == 3) {
    s_leds.setPixelColor(unused[static_cast<uint16_t>(second % n)],
                         scaleColor(s_leds.Color(0, 160, 110), gain));
  } else {
    const uint16_t spark = static_cast<uint16_t>(
        (millis() / 80 + static_cast<unsigned>(second) * 7) % n);
    s_leds.setPixelColor(unused[spark],
                         scaleColor(s_leds.Color(180, 180, 200), gain));
    if (n > 3) {
      s_leds.setPixelColor(unused[(spark + n / 3) % n],
                           scaleColor(s_leds.Color(40, 50, 80), gain * 0.45f));
    }
  }
}

void lightFaceCell(uint8_t x, uint8_t y, uint32_t color) {
  s_leds.setPixelColor(pixelIndex(x, y), color);
}

void showCalibration() {
  clearLeds();
  if (s_cal_mode == 2) {
    s_leds.setPixelColor(0, s_leds.Color(0, 180, 0));
  } else if (s_cal_mode == 3) {
    s_leds.setPixelColor(63, s_leds.Color(180, 180, 0));
  } else if (s_cal_mode == 4) {
    lightFaceCell(0, 0, s_leds.Color(180, 0, 0));
  } else if (s_cal_mode == 5) {
    lightFaceCell(7, 7, s_leds.Color(0, 40, 180));
  } else if (s_cal_mode == 6) {
    lightFaceCell(7, 0, s_leds.Color(0, 180, 0));
  } else if (s_cal_mode == 7) {
    lightFaceCell(0, 7, s_leds.Color(180, 160, 0));
  } else {
    lightFaceCell(0, 0, s_leds.Color(180, 0, 0));
    lightFaceCell(7, 0, s_leds.Color(0, 180, 0));
    lightFaceCell(0, 7, s_leds.Color(180, 160, 0));
    lightFaceCell(7, 7, s_leds.Color(0, 40, 180));
  }
  showNow();
}

void applyMinuteDots(int extra, int hour12) {
  if (!s_min_dots || extra <= 0) {
    return;
  }
  uint16_t dots[8];
  uint16_t n = 0;
  for (uint8_t y = 0; y < 8 && n < 8; ++y) {
    for (uint8_t x = 0; x < 8 && n < 8; ++x) {
      const char c = face::letter(x, y);
      if (c == '*' || c == '.' || c == ':') {
        dots[n++] = pixelIndex(x, y);
      }
    }
  }
  if (n < 4) {
    for (int x = 7; x >= 0 && n < 4; --x) {
      const uint16_t idx = pixelIndex(static_cast<uint8_t>(x), 0);
      bool known = false;
      for (uint16_t i = 0; i < n; ++i) {
        if (dots[i] == idx) {
          known = true;
          break;
        }
      }
      if (!known && !s_used[idx]) {
        dots[n++] = idx;
      }
    }
  }
  const uint8_t lit = static_cast<uint8_t>(extra > 4 ? 4 : extra);
  const uint32_t c =
      scaleColor(s_leds.Color(255, 210, 70), wordGain() * 0.85f);
  for (uint8_t i = 0; i < lit && i < n; ++i) {
    if (dots[i] < 64) {
      s_used[dots[i]] = true;
    }
    s_leds.setPixelColor(dots[i], c);
  }
  (void)hour12;
}

void composeTime(int hour24, int minute, int second) {
  int slot = 0;
  int extra = 0;
  if (s_min_dots) {
    slot = minute / 5;
    extra = minute % 5;
  } else {
    slot = (minute + 2) / 5;
  }
  if (slot >= 12) {
    slot = 0;
    hour24 = (hour24 + 1) % 24;
  }

  int hour12 = hour24 % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  const bool to_hour = slot > 6;
  if (to_hour) {
    hour12 = hour12 == 12 ? 1 : hour12 + 1;
    slot = 12 - slot;
  }

  memset(s_used, 0, sizeof(s_used));
  clearLeds();

  switch (slot) {
    case 1:
      lightWord(face::Word::kFiveMin, hour12);
      break;
    case 2:
      lightWord(face::Word::kTenMin, hour12);
      break;
    case 3:
      lightWord(face::Word::kQuarter, hour12);
      break;
    case 4:
      lightWord(face::Word::kTwenty, hour12);
      break;
    case 5:
      lightWord(face::Word::kTwenty, hour12);
      lightWord(face::Word::kFiveMin, hour12);
      break;
    case 6:
      lightWord(face::Word::kHalf, hour12);
      break;
    default:
      break;
  }

  if (slot != 0) {
    lightWord(to_hour ? face::Word::kTo : face::Word::kPast, hour12);
  }

  switch (hour12) {
    case 1:
      lightWord(face::Word::kOne, hour12);
      break;
    case 2:
      lightWord(face::Word::kTwo, hour12);
      break;
    case 3:
      lightWord(face::Word::kThree, hour12);
      break;
    case 4:
      lightWord(face::Word::kFour, hour12);
      break;
    case 5:
      lightWord(face::Word::kFiveHour, hour12);
      break;
    case 6:
      lightWord(face::Word::kSix, hour12);
      break;
    case 7:
      lightWord(face::Word::kSeven, hour12);
      break;
    case 8:
      lightWord(face::Word::kEight, hour12);
      break;
    case 9:
      lightWord(face::Word::kNine, hour12);
      break;
    case 10:
      lightWord(face::Word::kTenHour, hour12);
      break;
    case 11:
      lightWord(face::Word::kEleven, hour12);
      break;
    default:
      lightWord(face::Word::kTwelve, hour12);
      break;
  }

  applyMinuteDots(extra, hour12);
  applySeconds(second);
  showNow();
}

void formatSpokenTime(char* out, size_t out_n) {
  if (s_alarm_ringing) {
    strncpy(out, "ALARM", out_n - 1);
    out[out_n - 1] = '\0';
    return;
  }
  if (s_cal_mode != 0) {
    strncpy(out, "alignment", out_n - 1);
    out[out_n - 1] = '\0';
    return;
  }
  if (s_preview_row < 8) {
    snprintf(out, out_n, "row %u  %s",
             static_cast<unsigned>(s_preview_row + 1),
             face::row(s_preview_row));
    return;
  }
  if (s_word_preview) {
    strncpy(out, face::name(s_preview_word), out_n - 1);
    out[out_n - 1] = '\0';
    return;
  }
  if (s_sleep_now) {
    strncpy(out, "blackout", out_n - 1);
    out[out_n - 1] = '\0';
    return;
  }
  int h = 0;
  int m = 0;
  if (!readClock(&h, &m)) {
    strncpy(out, "waiting for time", out_n - 1);
    out[out_n - 1] = '\0';
    return;
  }
  int slot = 0;
  int extra = 0;
  if (s_min_dots) {
    slot = m / 5;
    extra = m % 5;
  } else {
    slot = (m + 2) / 5;
  }
  if (slot >= 12) {
    slot = 0;
    h = (h + 1) % 24;
  }
  int hour12 = h % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  bool to_hour = slot > 6;
  if (to_hour) {
    hour12 = hour12 == 12 ? 1 : hour12 + 1;
    slot = 12 - slot;
  }
  static const char* kHours[] = {"",     "ONE",   "TWO",   "THREE",
                                 "FOUR", "FIVE",  "SIX",   "SEVEN",
                                 "EIGHT","NINE",  "TEN",   "ELEVEN",
                                 "TWELVE"};
  static const char* kMins[] = {"", "FIVE", "TEN", "QUARTER", "TWENTY",
                                "TWENTY FIVE", "HALF"};
  char buf[48] = {};
  if (slot == 0) {
    snprintf(buf, sizeof(buf), "%s", kHours[hour12]);
  } else {
    snprintf(buf, sizeof(buf), "%s %s %s", kMins[slot], to_hour ? "TO" : "PAST",
             kHours[hour12]);
  }
  if (extra > 0 && s_min_dots) {
    snprintf(out, out_n, "%s  +%d", buf, extra);
  } else {
    strncpy(out, buf, out_n - 1);
    out[out_n - 1] = '\0';
  }
}

bool readClock(int* hour24, int* minute, int* second) {
  if (s_override_h >= 0 && s_override_m >= 0) {
    *hour24 = s_override_h;
    *minute = s_override_m;
    if (second) {
      *second = static_cast<int>((millis() / 1000UL) % 60UL);
    }
    return true;
  }
  time_t now = time(nullptr);
  struct tm local {};
  if (now < 8 * 3600L) {
    return false;
  }
  localtime_r(&now, &local);
  *hour24 = local.tm_hour;
  *minute = local.tm_min;
  if (second) {
    *second = local.tm_sec;
  }
  return true;
}

void stopAlarm() {
  if (!s_alarm_ringing) {
    return;
  }
  s_alarm_ringing = false;
  s_alarm_stop_ms = 0;
  Serial.println("Alarm: off");
}

void startAlarm(uint8_t seconds) {
  s_alarm_ringing = true;
  s_alarm_stop_ms = millis() + static_cast<unsigned long>(seconds) * 1000UL;
  Serial.printf("Alarm: ringing for %u s (button or Stop to silence)\n", seconds);
}

/**
 * Recompute night / blackout state and fire the alarm. Called once per face
 * refresh so the per-pixel gain lookups stay cheap.
 */
void updateSchedule() {
  int h = 0;
  int m = 0;
  if (!readClock(&h, &m)) {
    s_night_now = false;
    s_sleep_now = false;
    return;
  }
  const int now_min = h * 60 + m;
  s_night_now = s_night_on && inWindow(now_min, s_night_start, s_night_end);
  s_sleep_now = s_sleep_on && inWindow(now_min, s_sleep_start, s_sleep_end);

  if (s_alarm_ringing && static_cast<long>(millis() - s_alarm_stop_ms) >= 0) {
    stopAlarm();
  }
  if (!s_alarm_on) {
    return;
  }
  const time_t now = time(nullptr);
  struct tm lt {};
  localtime_r(&now, &lt);
  // Fire once per day, and only in the minute the alarm is set for.
  if (now_min == s_alarm_min &&
      !(s_alarm_fired_day == lt.tm_yday && s_alarm_fired_min == now_min)) {
    s_alarm_fired_day = lt.tm_yday;
    s_alarm_fired_min = now_min;
    startAlarm(s_alarm_len);
  }
}

/** Full-matrix flash, deliberately ignoring night dimming and blackout. */
void showAlarm() {
  const bool on = (millis() / 350UL) % 2UL == 0UL;
  clearLeds();
  if (on) {
    const uint32_t c = s_leds.Color(255, 90, 0);
    for (uint16_t i = 0; i < config::kLedCount; ++i) {
      s_leds.setPixelColor(i, c);
    }
  } else {
    // Keep the corners lit so the panel never looks dead between flashes.
    const uint32_t c = s_leds.Color(60, 12, 0);
    s_leds.setPixelColor(pixelIndex(0, 0), c);
    s_leds.setPixelColor(pixelIndex(7, 0), c);
    s_leds.setPixelColor(pixelIndex(0, 7), c);
    s_leds.setPixelColor(pixelIndex(7, 7), c);
  }
  showNow();
}

unsigned long showIntervalMs() {
  if (s_alarm_ringing) {
    return 60;
  }
  if (s_cal_mode != 0) {
    return 400;
  }
  if (s_sleep_now) {
    return 1000;
  }
  if (s_sec_fx == 1 || s_sec_fx == 4 || s_sec_fx == 5) {
    return 50;
  }
  if (s_sec_fx != 0) {
    return 200;
  }
  return config::kShowIntervalMs;
}

void refreshFace() {
  updateSchedule();
  if (s_alarm_ringing) {
    showAlarm();
    return;
  }
  if (s_cal_mode != 0) {
    showCalibration();
    return;
  }
  if (s_preview_row < 8) {
    lightRow(s_preview_row);
    return;
  }
  if (s_word_preview) {
    clearLeds();
    lightWordAlign(s_preview_word);
    showNow();
    return;
  }
  if (s_sleep_now) {
    clearLeds();
    showNow();
    return;
  }
  int h = 0;
  int m = 0;
  int s = 0;
  if (!readClock(&h, &m, &s)) {
    const uint32_t c = WiFi.status() == WL_CONNECTED ? s_leds.Color(80, 60, 0)
                                                    : s_leds.Color(0, 40, 80);
    showWait(c);
    return;
  }
  composeTime(h, m, s);
}

void applyTimezone() {
  setenv("TZ", s_tz, 1);
  tzset();
}

/** Long local date, e.g. "Sunday, 06 September 2026". Empty until time is set. */
void formatDate(char* out, size_t out_n) {
  const time_t now = time(nullptr);
  if (now < 8 * 3600L) {
    strncpy(out, "date not set", out_n - 1);
    out[out_n - 1] = '\0';
    return;
  }
  struct tm lt {};
  localtime_r(&now, &lt);
  if (strftime(out, out_n, "%A, %d %B %Y", &lt) == 0) {
    out[0] = '\0';
  }
}

/**
 * Zone abbreviation and UTC offset for the active TZ, e.g. "ADT (UTC-3)".
 * tm_zone / tm_gmtoff are not in this newlib build, so the offset is derived
 * by re-reading the UTC calendar fields as local time.
 */
void zoneLabel(char* out, size_t out_n) {
  const time_t now = time(nullptr);
  struct tm lt {};
  struct tm gt {};
  localtime_r(&now, &lt);
  gmtime_r(&now, &gt);
  char abbr[8] = "?";
  if (strftime(abbr, sizeof(abbr), "%Z", &lt) == 0) {
    strncpy(abbr, "?", sizeof(abbr) - 1);
  }
  gt.tm_isdst = -1;
  const time_t as_local = mktime(&gt);
  const long offset = as_local > 0 ? static_cast<long>(now - as_local) : 0;
  const long hours = offset / 3600;
  const long mins = labs(offset % 3600) / 60;
  if (mins) {
    snprintf(out, out_n, "%s (UTC%+ld:%02ld)", abbr, hours, mins);
  } else {
    snprintf(out, out_n, "%s (UTC%+ld)", abbr, hours);
  }
}

bool setManualTime(int hour, int minute, int second = 0) {
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
      second > 59) {
    return false;
  }
  applyTimezone();
  time_t now = time(nullptr);
  struct tm local {};
  if (now >= 8 * 3600L) {
    localtime_r(&now, &local);
  } else {
    memset(&local, 0, sizeof(local));
    local.tm_year = 2026 - 1900;
    local.tm_mon = 8;
    local.tm_mday = 6;
  }
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_sec = second;
  const time_t epoch = mktime(&local);
  if (epoch < 0) {
    return false;
  }
  timeval tv{};
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  s_time_ok = true;
  s_override_h = -1;
  s_override_m = -1;
  refreshFace();
  return true;
}

bool syncNtp() {
  s_last_ntp_try = millis();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("NTP: WiFi is down");
    return false;
  }
  Serial.printf("NTP: fetching from %s ...\n", s_ntp);
  compat::startNtp(s_tz, s_ntp);
  applyTimezone();
  struct tm local {};
  if (!compat::waitLocalTime(&local, 1500)) {
    Serial.println("NTP: no reply");
    return false;
  }
  s_time_ok = true;
  s_last_ntp_ok_ms = millis();
  s_override_h = -1;
  s_override_m = -1;
  char zone[24];
  zoneLabel(zone, sizeof(zone));
  Serial.printf("NTP: %04d-%02d-%02d  %02d:%02d:%02d  %s  tz %s\n",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec, zone, s_tz);
  refreshFace();
  return true;
}

void saveNtpPrefs() {
  s_prefs.begin(kPrefsNs, false);
  s_prefs.putString("ntp", s_ntp);
  s_prefs.putString("tz", s_tz);
  s_prefs.end();
}

void saveWifiPrefs() {
  s_prefs.begin(kPrefsNs, false);
  s_prefs.putString("host", s_hostname);
  s_prefs.end();
}

void saveMqttPrefs() {
  s_prefs.begin(kPrefsNs, false);
  s_prefs.putBool("mqon", s_mqtt_on);
  s_prefs.putBool("mqha", s_ha_disco);
  s_prefs.putString("mqh", s_mqtt_host);
  s_prefs.putUShort("mqp", s_mqtt_port);
  s_prefs.putString("mqu", s_mqtt_user);
  s_prefs.putString("mqpw", s_mqtt_pass);
  s_prefs.putString("mqpx", s_mqtt_prefix);
  s_prefs.end();
}

void saveLookPrefs() {
  s_prefs.begin(kPrefsNs, false);
  s_prefs.putBool("zigzag", s_zigzag);
  s_prefs.putBool("rgb", s_rgb_order);
  s_prefs.putUChar("bright", s_brightness);
  s_prefs.putUChar("cmode", s_color_mode);
  s_prefs.putUChar("secfx", s_sec_fx);
  s_prefs.putUChar("cr", s_cr);
  s_prefs.putUChar("cg", s_cg);
  s_prefs.putUChar("cb", s_cb);
  s_prefs.putBool("flipx", s_flip_x);
  s_prefs.putBool("flipy", s_flip_y);
  s_prefs.putBool("colmaj", s_col_major);
  s_prefs.putBool("mdots", s_min_dots);
  s_prefs.putString("plate", face::plateId());
  char custom[80] = {};
  face::copyRowsText(custom, sizeof(custom));
  s_prefs.putString("crows", custom);
  for (uint8_t i = 1; i < 6; ++i) {
    char key[] = "fb0";
    key[2] = static_cast<char>('0' + i);
    s_prefs.putUChar(key, s_fx_bright[i]);
  }
  s_prefs.putBool("non", s_night_on);
  s_prefs.putUShort("nst", s_night_start);
  s_prefs.putUShort("nen", s_night_end);
  s_prefs.putUChar("nbr", s_night_bright);
  s_prefs.putUChar("ncm", s_night_cmode);
  s_prefs.putBool("son", s_sleep_on);
  s_prefs.putUShort("sst", s_sleep_start);
  s_prefs.putUShort("sen", s_sleep_end);
  s_prefs.putBool("aon", s_alarm_on);
  s_prefs.putUShort("amn", s_alarm_min);
  s_prefs.putUChar("alen", s_alarm_len);
  s_prefs.end();
}

void parseHexColor(const char* hex) {
  if (!hex || hex[0] == '\0') {
    return;
  }
  if (hex[0] == '#') {
    ++hex;
  }
  unsigned v = 0;
  if (sscanf(hex, "%x", &v) != 1) {
    return;
  }
  s_cr = static_cast<uint8_t>((v >> 16) & 0xFF);
  s_cg = static_cast<uint8_t>((v >> 8) & 0xFF);
  s_cb = static_cast<uint8_t>(v & 0xFF);
}

const char* colorModeName(uint8_t mode) {
  switch (mode) {
    case 1:
      return "solid";
    case 2:
      return "rainbow";
    case 3:
      return "warm";
    default:
      return "hour";
  }
}

const char* nightColorModeName(uint8_t mode) {
  switch (mode) {
    case 0:
      return "hour";
    case 1:
      return "solid";
    case 2:
      return "rainbow";
    case 3:
      return "warm";
    default:
      return "same";
  }
}

const char* secondsName(uint8_t fx) {
  switch (fx) {
    case 1:
      return "pulse";
    case 2:
      return "trail";
    case 3:
      return "tick";
    case 4:
      return "sparkle";
    case 5:
      return "star";
    default:
      return "off";
  }
}

void loadPrefs() {
  strncpy(s_ntp, config::kNtpServer, sizeof(s_ntp) - 1);
  strncpy(s_tz, config::kPosixTz, sizeof(s_tz) - 1);
  strncpy(s_hostname, config::kHostname, sizeof(s_hostname) - 1);
  s_prefs.begin(kPrefsNs, true);
  s_zigzag = s_prefs.getBool("zigzag", false);
  s_rgb_order = s_prefs.getBool("rgb", false);
  s_brightness = s_prefs.getUChar("bright", config::kDefaultBrightness);
  s_color_mode = s_prefs.getUChar("cmode", 0);
  s_sec_fx = s_prefs.getUChar("secfx", 5);
  s_flip_x = s_prefs.getBool("flipx", false);
  s_flip_y = s_prefs.getBool("flipy", false);
  if (!s_prefs.isKey("flipx") && s_prefs.isKey("origin")) {
    const uint8_t origin = s_prefs.getUChar("origin", 0);
    s_flip_x = origin == 1 || origin == 3;
    s_flip_y = origin == 2 || origin == 3;
  }
  s_col_major = s_prefs.getBool("colmaj", false);
  s_min_dots = s_prefs.getBool("mdots", true);
  s_cr = s_prefs.getUChar("cr", 255);
  s_cg = s_prefs.getUChar("cg", 160);
  s_cb = s_prefs.getUChar("cb", 40);
  for (uint8_t i = 1; i < 6; ++i) {
    char key[] = "fb0";
    key[2] = static_cast<char>('0' + i);
    s_fx_bright[i] = clampBright(s_prefs.getUChar(key, s_fx_bright[i]));
  }
  s_night_on = s_prefs.getBool("non", false);
  s_night_start = s_prefs.getUShort("nst", 21 * 60);
  s_night_end = s_prefs.getUShort("nen", 7 * 60);
  s_night_bright = clampBright(s_prefs.getUChar("nbr", 10));
  s_night_cmode = s_prefs.getUChar("ncm", 4);
  s_sleep_on = s_prefs.getBool("son", false);
  s_sleep_start = s_prefs.getUShort("sst", 23 * 60);
  s_sleep_end = s_prefs.getUShort("sen", 6 * 60);
  s_alarm_on = s_prefs.getBool("aon", false);
  s_alarm_min = s_prefs.getUShort("amn", 7 * 60);
  s_alarm_len = s_prefs.getUChar("alen", 60);
  char plate[12] = {};
  s_prefs.getString("plate", plate, sizeof(plate));
  if (plate[0]) {
    face::setPlateId(plate);
  }
  if (s_prefs.isKey("crows")) {
    char custom[80] = {};
    s_prefs.getString("crows", custom, sizeof(custom));
    if (custom[0] && strcmp(plate, "custom") == 0) {
      face::setCustomRows(custom);
    }
  }
  if (s_prefs.isKey("ntp")) {
    s_prefs.getString("ntp", s_ntp, sizeof(s_ntp));
  }
  if (s_prefs.isKey("tz")) {
    s_prefs.getString("tz", s_tz, sizeof(s_tz));
  }
  if (s_prefs.isKey("host")) {
    char raw[32] = {};
    s_prefs.getString("host", raw, sizeof(raw));
    sanitizeHostname(s_hostname, sizeof(s_hostname), raw);
  }
  s_mqtt_on = s_prefs.getBool("mqon", false);
  s_ha_disco = s_prefs.getBool("mqha", true);
  if (s_prefs.isKey("mqh")) {
    s_prefs.getString("mqh", s_mqtt_host, sizeof(s_mqtt_host));
  }
  s_mqtt_port = s_prefs.getUShort("mqp", config::kDefaultMqttPort);
  if (s_prefs.isKey("mqu")) {
    s_prefs.getString("mqu", s_mqtt_user, sizeof(s_mqtt_user));
  }
  if (s_prefs.isKey("mqpw")) {
    s_prefs.getString("mqpw", s_mqtt_pass, sizeof(s_mqtt_pass));
  }
  if (s_prefs.isKey("mqpx")) {
    s_prefs.getString("mqpx", s_mqtt_prefix, sizeof(s_mqtt_prefix));
  }
  if (s_mqtt_prefix[0] == '\0') {
    strncpy(s_mqtt_prefix, config::kDefaultMqttPrefix, sizeof(s_mqtt_prefix) - 1);
  }
  if (s_mqtt_port == 0) {
    s_mqtt_port = config::kDefaultMqttPort;
  }
  s_prefs.end();
  if (s_brightness < 1 || s_brightness > config::kMaxBrightness) {
    s_brightness = config::kDefaultBrightness;
  }
  if (s_color_mode > 3) {
    s_color_mode = 0;
  }
  if (s_sec_fx > 5) {
    s_sec_fx = 5;
  }
  if (s_night_cmode > 4) {
    s_night_cmode = 4;
  }
  if (s_night_start > 1439) {
    s_night_start = 21 * 60;
  }
  if (s_night_end > 1439) {
    s_night_end = 7 * 60;
  }
  if (s_sleep_start > 1439) {
    s_sleep_start = 23 * 60;
  }
  if (s_sleep_end > 1439) {
    s_sleep_end = 6 * 60;
  }
  if (s_alarm_min > 1439) {
    s_alarm_min = 7 * 60;
  }
  if (s_alarm_len < 5) {
    s_alarm_len = 5;
  }
  if (s_ntp[0] == '\0') {
    strncpy(s_ntp, config::kNtpServer, sizeof(s_ntp) - 1);
  }
  if (s_tz[0] == '\0') {
    strncpy(s_tz, config::kPosixTz, sizeof(s_tz) - 1);
  }
}

void runTest() {
  Serial.println("LED walk: pixel 0 is DIN start. If this is dark, check 5V/GND/G2.");
  for (uint16_t i = 0; i < config::kLedCount; ++i) {
    clearLeds();
    s_leds.setPixelColor(i, s_leds.Color(0, 90, 40));
    showNow();
    delay(30);
    pollSerial();
  }
  refreshFace();
}

void lightRow(int y) {
  if (y < 0 || y > 7) {
    return;
  }
  clearLeds();
  s_color = s_leds.Color(90, 40, 0);
  for (uint8_t x = 0; x < 8; ++x) {
    s_leds.setPixelColor(pixelIndex(x, static_cast<uint8_t>(y)), s_color);
  }
  showNow();
  Serial.printf("Row %d: %s\n", y, face::row(y));
}

bool wifiHasIp() {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void startMdns() {
  if (s_mdns_ok || !wifiHasIp()) {
    return;
  }
  compat::mdnsStop();
  if (MDNS.begin(s_hostname)) {
    MDNS.addService("http", "tcp", 80);
    s_mdns_ok = true;
    Serial.printf("mDNS: http://%s.local\n", s_hostname);
  }
}

void restartMdns() {
  compat::mdnsStop();
  s_mdns_ok = false;
  s_ota_ready = false;
  startMdns();
}

void handlePortalSta() {
  if (!s_wm.server) {
    return;
  }
  String json = F("{\"ok\":");
  json += wifiHasIp() ? F("true") : F("false");
  json += F(",\"ip\":\"");
  if (wifiHasIp()) {
    json += WiFi.localIP().toString();
  }
  json += F("\",\"ssid\":\"");
  json += htmlEscape(WiFi.SSID());
  json += F("\",\"host\":\"");
  json += s_hostname;
  json += F(".local\"}");
  s_wm.server->send(200, "application/json", json);
}

void bindPortalRoutes() {
  if (s_portal_routes || !s_wm.server) {
    return;
  }
  s_wm.server->on("/sta", handlePortalSta);
  s_portal_routes = true;
}

void onPortalStarted(WiFiManager*) {
  s_portal_open = true;
  s_portal_routes = false;
  s_portal_got_ip_ms = 0;
  Serial.println();
  Serial.printf("WiFi setup AP: %s\n", config::kApName);
  Serial.println("Open http://192.168.4.1  (2.4 GHz only)");
}

void onPortalSaved() {
  const char* ntp = s_param_ntp.getValue();
  const char* tz = s_param_tz.getValue();
  if (ntp && ntp[0]) {
    strncpy(s_ntp, ntp, sizeof(s_ntp) - 1);
  }
  if (tz && tz[0]) {
    strncpy(s_tz, tz, sizeof(s_tz) - 1);
  }
  saveNtpPrefs();
  Serial.printf("Saved NTP=%s  TZ=%s\n", s_ntp, s_tz);
}

void setupWifiManager() {
  static bool ready = false;
  if (ready) {
    s_param_ntp.setValue(s_ntp, 63);
    s_param_tz.setValue(s_tz, 47);
    return;
  }
  ready = true;
  s_wm.setHostname(s_hostname);
  s_wm.setTitle("Word Clock");
  s_wm.setDarkMode(true);
  s_wm.setCustomHeadElement(kWmHead);
  s_wm.setCustomMenuHTML(kWmMenuExtra);
  const char* menu[] = {"wifi", "custom"};
  s_wm.setMenu(menu, 2);
  s_wm.setShowPassword(true);
  s_wm.setScanDispPerc(true);
  s_wm.setShowInfoUpdate(false);
  s_wm.setConfigPortalBlocking(false);
  s_wm.setConfigPortalTimeout(0);
  s_wm.setConnectTimeout(20);
  s_wm.setDisableConfigPortal(false);
  s_wm.setAPCallback(onPortalStarted);
  s_wm.setSaveParamsCallback(onPortalSaved);
  s_param_ntp.setValue(s_ntp, 63);
  s_param_tz.setValue(s_tz, 47);
  s_wm.addParameter(&s_param_note);
  s_wm.addParameter(&s_param_ntp);
  s_wm.addParameter(&s_param_tz);
}

void openPortal() {
  setupWifiManager();
  if (s_web_started) {
    s_http.stop();
    s_web_started = false;
  }
  s_portal_routes = false;
  s_portal_got_ip_ms = 0;
  s_wm.startConfigPortal(config::kApName);
  s_portal_open = true;
  Serial.printf("Join AP %s and open http://192.168.4.1\n", config::kApName);
}

void closePortalForLan() {
  if (!s_portal_open) {
    return;
  }
  Serial.println("Closing setup AP — opening the clock dashboard on the LAN IP");
  s_wm.stopConfigPortal();
  s_portal_open = false;
  s_portal_routes = false;
  s_portal_got_ip_ms = 0;
}

void startWifi() {
  if (s_wifi_started) {
    return;
  }
  s_wifi_started = true;
  setupWifiManager();
  WiFi.mode(WIFI_STA);
  compat::setHostname(s_hostname);
  WiFi.setAutoReconnect(true);
  if (s_wm.getWiFiIsSaved()) {
    Serial.println("\nWiFi: connecting in background");
    Serial.print("> ");
    WiFi.begin();
  } else {
    Serial.printf("\nWiFi: AP %s  http://192.168.4.1\n> ", config::kApName);
    openPortal();
  }
}

String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 4);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c >= 0x20) {
      out += c;
    }
  }
  return out;
}

const char* clockModeName() {
  if (s_alarm_ringing) {
    return "ALARM";
  }
  if (s_sleep_now) {
    return "blackout";
  }
  if (s_night_now) {
    return "night";
  }
  return "day";
}

void mqttTopic(char* out, size_t out_n, const char* suffix) {
  snprintf(out, out_n, "%s/%s",
           s_mqtt_prefix[0] ? s_mqtt_prefix : config::kDefaultMqttPrefix,
           suffix);
}

void handleMqttCommand(const char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (!err) {
    if (doc["bright"].is<int>()) {
      s_brightness = clampBright(doc["bright"].as<int>());
      applyBrightness();
    }
    if (doc["color"].is<const char*>()) {
      const char* v = doc["color"];
      if (strcmp(v, "hour") == 0) s_color_mode = 0;
      else if (strcmp(v, "solid") == 0) s_color_mode = 1;
      else if (strcmp(v, "rainbow") == 0) s_color_mode = 2;
      else if (strcmp(v, "warm") == 0) s_color_mode = 3;
    }
    if (doc["night"].is<bool>()) {
      s_night_on = doc["night"].as<bool>();
    }
    if (doc["sleep"].is<bool>()) {
      s_sleep_on = doc["sleep"];
    }
    if (doc["blackout"].is<bool>()) {
      s_sleep_on = doc["blackout"];
    }
    if (doc["alarm"].is<const char*>() &&
        strcmp(doc["alarm"], "stop") == 0) {
      stopAlarm();
    }
    if (doc["reboot"].is<bool>() && doc["reboot"].as<bool>()) {
      saveLookPrefs();
      delay(200);
      ESP.restart();
    }
    saveLookPrefs();
    refreshFace();
    mqttPublishState(true);
    return;
  }

  char buf[96];
  strncpy(buf, payload, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  for (char* p = buf; *p; ++p) {
    if (*p >= 'A' && *p <= 'Z') {
      *p = static_cast<char>(*p + ('a' - 'A'));
    }
  }
  if (strcmp(buf, "alarm stop") == 0 || strcmp(buf, "stop") == 0) {
    stopAlarm();
  } else if (strncmp(buf, "bright ", 7) == 0) {
    s_brightness = clampBright(atoi(buf + 7));
    applyBrightness();
    saveLookPrefs();
  } else if (strcmp(buf, "night on") == 0) {
    s_night_on = true;
    saveLookPrefs();
  } else if (strcmp(buf, "night off") == 0) {
    s_night_on = false;
    saveLookPrefs();
  } else if (strcmp(buf, "sleep on") == 0 || strcmp(buf, "blackout on") == 0) {
    s_sleep_on = true;
    saveLookPrefs();
  } else if (strcmp(buf, "sleep off") == 0 ||
             strcmp(buf, "blackout off") == 0) {
    s_sleep_on = false;
    saveLookPrefs();
  }
  refreshFace();
  mqttPublishState(true);
}

void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  char msg[160];
  if (len >= sizeof(msg)) {
    len = sizeof(msg) - 1;
  }
  memcpy(msg, payload, len);
  msg[len] = '\0';
  Serial.printf("MQTT: %s <= %s\n", topic, msg);
  handleMqttCommand(msg);
}

void mqttPublishDiscovery() {
  if (!s_ha_disco || !s_mqtt.connected()) {
    return;
  }
  char topic[96];
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/state/config",
           s_hostname);
  JsonDocument doc;
  doc["name"] = "Word Clock";
  doc["uniq_id"] = String(s_hostname) + "_state";
  char stat[80];
  mqttTopic(stat, sizeof(stat), "state");
  doc["stat_t"] = stat;
  doc["val_tpl"] = "{{ value_json.mode }}";
  doc["json_attr_t"] = stat;
  doc["dev"]["ids"][0] = s_hostname;
  doc["dev"]["name"] = s_hostname;
  doc["dev"]["mf"] = config::kAuthor;
  doc["dev"]["mdl"] = "ESP-WordClock8x8";
  doc["dev"]["sw"] = config::kFirmwareVersion;
  String body;
  serializeJson(doc, body);
  s_mqtt.publish(topic, body.c_str(), true);
}

void mqttPublishState(bool force) {
  if (!s_mqtt_on || !s_mqtt.connected()) {
    return;
  }
  if (!force && millis() - s_last_mqtt_pub < config::kMqttPublishMs) {
    return;
  }
  s_last_mqtt_pub = millis();
  int h = 0, m = 0, s = 0;
  const bool ok = readClock(&h, &m, &s);
  JsonDocument doc;
  doc["ok"] = ok;
  if (ok) {
    char t[16];
    snprintf(t, sizeof(t), "%02d:%02d:%02d", h, m, s);
    doc["time"] = t;
  }
  doc["mode"] = clockModeName();
  doc["bright"] = s_brightness;
  doc["color"] = colorModeName(s_color_mode);
  doc["night"] = s_night_now;
  doc["blackout"] = s_sleep_now;
  doc["alarm"] = s_alarm_ringing;
  doc["fw"] = config::kFirmwareVersion;
  doc["ip"] = WiFi.localIP().toString();
  char topic[80];
  mqttTopic(topic, sizeof(topic), "state");
  String body;
  serializeJson(doc, body);
  s_mqtt.publish(topic, body.c_str(), true);
}

void mqttLoop() {
  if (!s_mqtt_on || s_mqtt_host[0] == '\0' || WiFi.status() != WL_CONNECTED) {
    strncpy(s_mqtt_status, s_mqtt_on ? "wait" : "off", sizeof(s_mqtt_status) - 1);
    if (s_mqtt.connected()) {
      s_mqtt.disconnect();
    }
    return;
  }
  s_mqtt.setServer(s_mqtt_host, s_mqtt_port);
  s_mqtt.setCallback(onMqttMessage);
  if (!s_mqtt.connected()) {
    char cmd[80];
    mqttTopic(cmd, sizeof(cmd), "cmd");
    const bool ok =
        s_mqtt_user[0]
            ? s_mqtt.connect(s_hostname, s_mqtt_user, s_mqtt_pass)
            : s_mqtt.connect(s_hostname);
    if (ok) {
      strncpy(s_mqtt_status, "live", sizeof(s_mqtt_status) - 1);
      s_mqtt.subscribe(cmd);
      mqttPublishDiscovery();
      mqttPublishState(true);
      Serial.printf("MQTT: connected %s:%u  %s/#\n", s_mqtt_host, s_mqtt_port,
                    s_mqtt_prefix);
    } else {
      strncpy(s_mqtt_status, "error", sizeof(s_mqtt_status) - 1);
    }
    return;
  }
  s_mqtt.loop();
  strncpy(s_mqtt_status, "live", sizeof(s_mqtt_status) - 1);
  mqttPublishState(false);
}

void setupOta() {
  if (s_ota_ready || WiFi.status() != WL_CONNECTED) {
    return;
  }
  ArduinoOTA.setHostname(s_hostname);
  ArduinoOTA.onStart([]() { Serial.println("OTA: start"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA: done"); });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("OTA: error %u\n", static_cast<unsigned>(err));
  });
  ArduinoOTA.begin();
  s_ota_ready = true;
  Serial.printf("OTA: ready  pio run -t upload --upload-port %s.local\n",
                s_hostname);
}

void serviceOta() {
  if (s_ota_ready) {
    ArduinoOTA.handle();
  } else {
    setupOta();
  }
}

void sendSettingsJson() {
  JsonDocument doc;
  doc["fw"] = config::kFirmwareVersion;
  doc["chip"] = compat::chipName();
  doc["host"] = s_hostname;
  doc["tz"] = s_tz;
  doc["ntp"] = s_ntp;
  doc["bright"] = s_brightness;
  doc["cmode"] = s_color_mode;
  doc["secfx"] = s_sec_fx;
  doc["fxbright"] = s_fx_bright[s_sec_fx];
  doc["cr"] = s_cr;
  doc["cg"] = s_cg;
  doc["cb"] = s_cb;
  doc["zigzag"] = s_zigzag;
  doc["flipx"] = s_flip_x;
  doc["flipy"] = s_flip_y;
  doc["colmaj"] = s_col_major;
  doc["rgb"] = s_rgb_order;
  doc["mdots"] = s_min_dots;
  doc["plate"] = face::plateId();
  char letters[80] = {};
  face::copyRowsText(letters, sizeof(letters));
  doc["letters"] = letters;
  doc["night_on"] = s_night_on;
  doc["night_start"] = s_night_start;
  doc["night_end"] = s_night_end;
  doc["nbright"] = s_night_bright;
  doc["ncmode"] = s_night_cmode;
  doc["sleep_on"] = s_sleep_on;
  doc["sleep_start"] = s_sleep_start;
  doc["sleep_end"] = s_sleep_end;
  doc["alarm_on"] = s_alarm_on;
  doc["alarm_min"] = s_alarm_min;
  doc["alen"] = s_alarm_len;
  doc["mqtt_on"] = s_mqtt_on;
  doc["mqtt_host"] = s_mqtt_host;
  doc["mqtt_port"] = s_mqtt_port;
  doc["mqtt_user"] = s_mqtt_user;
  doc["mqtt_pass_set"] = s_mqtt_pass[0] != '\0';
  doc["mqtt_prefix"] = s_mqtt_prefix;
  doc["ha"] = s_ha_disco;
  String out;
  serializeJsonPretty(doc, out);
  s_http.send(200, "application/json", out);
}

bool applySettingsJson(const char* json, String* err) {
  JsonDocument doc;
  const DeserializationError derr = deserializeJson(doc, json);
  if (derr) {
    if (err) {
      *err = derr.c_str();
    }
    return false;
  }
  if (doc["host"].is<const char*>()) {
    sanitizeHostname(s_hostname, sizeof(s_hostname), doc["host"]);
    saveWifiPrefs();
    compat::setHostname(s_hostname);
    restartMdns();
  }
  if (doc["tz"].is<const char*>()) {
    strncpy(s_tz, doc["tz"], sizeof(s_tz) - 1);
  }
  if (doc["ntp"].is<const char*>()) {
    strncpy(s_ntp, doc["ntp"], sizeof(s_ntp) - 1);
  }
  if (doc["bright"].is<int>()) {
    s_brightness = clampBright(doc["bright"]);
    applyBrightness();
  }
  if (doc["cmode"].is<int>()) {
    s_color_mode = static_cast<uint8_t>(doc["cmode"].as<int>() & 3);
  }
  if (doc["secfx"].is<int>()) {
    int fx = doc["secfx"];
    if (fx < 0) fx = 0;
    if (fx > 5) fx = 5;
    s_sec_fx = static_cast<uint8_t>(fx);
  }
  if (doc["fxbright"].is<int>()) {
    s_fx_bright[s_sec_fx] = clampBright(doc["fxbright"]);
  }
  if (doc["cr"].is<int>()) s_cr = static_cast<uint8_t>(doc["cr"].as<int>());
  if (doc["cg"].is<int>()) s_cg = static_cast<uint8_t>(doc["cg"].as<int>());
  if (doc["cb"].is<int>()) s_cb = static_cast<uint8_t>(doc["cb"].as<int>());
  if (doc["zigzag"].is<bool>()) s_zigzag = doc["zigzag"];
  if (doc["flipx"].is<bool>()) s_flip_x = doc["flipx"];
  if (doc["flipy"].is<bool>()) s_flip_y = doc["flipy"];
  if (doc["colmaj"].is<bool>()) s_col_major = doc["colmaj"];
  if (doc["rgb"].is<bool>()) {
    const bool rgb = doc["rgb"];
    if (rgb != s_rgb_order) {
      s_rgb_order = rgb;
      beginLeds();
    }
  }
  if (doc["mdots"].is<bool>()) s_min_dots = doc["mdots"];
  if (doc["plate"].is<const char*>()) {
    face::setPlateId(doc["plate"]);
  }
  if (doc["letters"].is<const char*>()) {
    face::setCustomRows(doc["letters"]);
  }
  if (doc["night_on"].is<bool>()) s_night_on = doc["night_on"];
  if (doc["night_start"].is<int>()) s_night_start = doc["night_start"];
  if (doc["night_end"].is<int>()) s_night_end = doc["night_end"];
  if (doc["nbright"].is<int>()) s_night_bright = clampBright(doc["nbright"]);
  if (doc["ncmode"].is<int>()) {
    int c = doc["ncmode"];
    if (c < 0 || c > 4) c = 4;
    s_night_cmode = static_cast<uint8_t>(c);
  }
  if (doc["sleep_on"].is<bool>()) s_sleep_on = doc["sleep_on"];
  if (doc["sleep_start"].is<int>()) s_sleep_start = doc["sleep_start"];
  if (doc["sleep_end"].is<int>()) s_sleep_end = doc["sleep_end"];
  if (doc["alarm_on"].is<bool>()) s_alarm_on = doc["alarm_on"];
  if (doc["alarm_min"].is<int>()) s_alarm_min = doc["alarm_min"];
  if (doc["alen"].is<int>()) {
    int len = doc["alen"];
    if (len < 5) len = 5;
    if (len > 180) len = 180;
    s_alarm_len = static_cast<uint8_t>(len);
  }
  if (doc["mqtt_on"].is<bool>()) s_mqtt_on = doc["mqtt_on"];
  if (doc["mqtt_host"].is<const char*>()) {
    strncpy(s_mqtt_host, doc["mqtt_host"], sizeof(s_mqtt_host) - 1);
  }
  if (doc["mqtt_port"].is<int>()) {
    s_mqtt_port = static_cast<uint16_t>(doc["mqtt_port"].as<int>());
    if (s_mqtt_port == 0) s_mqtt_port = config::kDefaultMqttPort;
  }
  if (doc["mqtt_user"].is<const char*>()) {
    strncpy(s_mqtt_user, doc["mqtt_user"], sizeof(s_mqtt_user) - 1);
  }
  if (doc["mqtt_pass"].is<const char*>() && doc["mqtt_pass"].as<const char*>()[0]) {
    strncpy(s_mqtt_pass, doc["mqtt_pass"], sizeof(s_mqtt_pass) - 1);
  }
  if (doc["mqtt_prefix"].is<const char*>()) {
    strncpy(s_mqtt_prefix, doc["mqtt_prefix"], sizeof(s_mqtt_prefix) - 1);
  }
  if (doc["ha"].is<bool>()) s_ha_disco = doc["ha"];
  saveLookPrefs();
  saveNtpPrefs();
  saveMqttPrefs();
  applyTimezone();
  refreshFace();
  return true;
}

/**
 * Switch networks from the dashboard. The HTTP reply is sent first, then the
 * radio is retuned here so the browser is not cut off mid-response. A failed
 * join rolls back to the previous network instead of stranding the clock.
 */
void serviceWifiJoin() {
  if (s_join_pending) {
    s_join_pending = false;
    // The driver still holds the working credentials at this point.
    compat::captureWifiCreds(s_prev_ssid, sizeof(s_prev_ssid), s_prev_pass,
                             sizeof(s_prev_pass));
    Serial.printf("\nWiFi: joining %s ...\n", s_join_ssid);
    s_http.stop();
    s_web_started = false;
    compat::mdnsStop();
    s_mdns_ok = false;
    s_ota_ready = false;
    compat::wifiDisconnectKeepCreds();
    delay(120);
    WiFi.mode(WIFI_STA);
    compat::setHostname(s_hostname);
    WiFi.begin(s_join_ssid, s_join_pass);
    s_joining = true;
    s_join_deadline = millis() + 20000;
    return;
  }
  if (!s_joining) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    s_joining = false;
    memset(s_join_pass, 0, sizeof(s_join_pass));
    memset(s_prev_pass, 0, sizeof(s_prev_pass));
    restartMdns();
    Serial.printf("WiFi: joined %s  http://%s  http://%s.local\n> ",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                  s_hostname);
    return;
  }
  if (static_cast<long>(millis() - s_join_deadline) < 0) {
    return;
  }
  s_joining = false;
  memset(s_join_pass, 0, sizeof(s_join_pass));
  Serial.printf("WiFi: could not join %s\n", s_join_ssid);
  if (s_prev_ssid[0]) {
    Serial.printf("WiFi: going back to %s\n> ", s_prev_ssid);
    compat::wifiDisconnectKeepCreds();
    delay(120);
    WiFi.begin(s_prev_ssid, s_prev_pass);
    memset(s_prev_pass, 0, sizeof(s_prev_pass));
    s_portal_grace_ms = millis() + 25000;
  } else {
    Serial.printf("WiFi: no previous network, opening setup AP\n> ");
    openPortal();
  }
}

void appendChoice(String& page, const char* value, const char* current,
                  const char* label) {
  page += F("<option value='");
  page += value;
  page += '\'';
  if (strcmp(value, current) == 0) {
    page += F(" selected");
  }
  page += '>';
  page += label;
  page += F("</option>");
}

void appendOption(String& page, int value, int current, const char* label) {
  page += F("<option value='");
  page += String(value);
  page += '\'';
  if (value == current) {
    page += F(" selected");
  }
  page += '>';
  page += label;
  page += F("</option>");
}

void sendTimeJson() {
  int h = 0, m = 0, s = 0;
  const bool ok = readClock(&h, &m, &s);
  char nowbuf[16] = "--:--:--";
  if (ok) {
    snprintf(nowbuf, sizeof(nowbuf), "%02d:%02d:%02d", h, m, s);
  }
  char datebuf[48];
  char zone[24];
  formatDate(datebuf, sizeof(datebuf));
  zoneLabel(zone, sizeof(zone));
  String json = F("{\"ok\":");
  json += ok ? F("true") : F("false");
  json += F(",\"time\":\"");
  json += nowbuf;
  json += F("\",\"date\":\"");
  json += jsonEscape(String(datebuf));
  json += F("\",\"zone\":\"");
  json += jsonEscape(String(zone));
  json += F("\",\"color\":\"");
  json += colorModeName(s_color_mode);
  json += F("\",\"seconds\":\"");
  json += secondsName(s_sec_fx);
  json += F("\",\"mode\":\"");
  json += clockModeName();
  json += F("\",\"mqtt\":\"");
  json += s_mqtt_status;
  json += F("\",\"ntp_ok\":");
  json += s_time_ok ? F("true") : F("false");
  json += F("}");
  s_http.send(200, "application/json", json);
}

void sendFaceJson() {
  char words[48];
  formatSpokenTime(words, sizeof(words));
  String json;
  json.reserve(640);
  json += F("{\"ok\":true,\"mode\":\"");
  json += clockModeName();
  json += F("\",\"fx\":\"");
  json += secondsName(s_sec_fx);
  json += F("\",\"words\":\"");
  json += jsonEscape(String(words));
  json += F("\",\"letters\":\"");
  for (uint8_t y = 0; y < 8; ++y) {
    for (uint8_t x = 0; x < 8; ++x) {
      const char c = face::letter(x, y);
      json += (c >= 32 && c < 127) ? c : ' ';
    }
  }
  json += F("\",\"rgb\":\"");
  char hex[7];
  for (uint8_t y = 0; y < 8; ++y) {
    for (uint8_t x = 0; x < 8; ++x) {
      const uint32_t c = s_leds.getPixelColor(pixelIndex(x, y));
      snprintf(hex, sizeof(hex), "%02X%02X%02X",
               static_cast<unsigned>((c >> 16) & 0xFF),
               static_cast<unsigned>((c >> 8) & 0xFF),
               static_cast<unsigned>(c & 0xFF));
      json += hex;
    }
  }
  json += F("\"}");
  s_http.sendHeader("Cache-Control", "no-store");
  s_http.send(200, "application/json", json);
}

void sendDashboard() {
  int h = 0, m = 0, s = 0;
  const bool ok = readClock(&h, &m, &s);
  char nowbuf[16] = "--:--:--";
  if (ok) {
    snprintf(nowbuf, sizeof(nowbuf), "%02d:%02d:%02d", h, m, s);
  }
  char hex[8];
  snprintf(hex, sizeof(hex), "#%02X%02X%02X", s_cr, s_cg, s_cb);
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                            : String("192.168.4.1");
  const char* tzid = timezoneIdFor(s_tz);
  String page;
  page.reserve(20000);
  page += F("<!DOCTYPE html><html><head><meta charset=utf-8>");
  page += F("<meta name=viewport content='width=device-width,initial-scale=1'>");
  page += F("<title>Word Clock</title><style>");
  page += F("body{font-family:system-ui,sans-serif;background:#08101f;color:#e8eef8;margin:0;padding:16px}");
  page += F(".box{max-width:560px;margin:auto}");
  page += F(".card{background:#122040;border:1px solid #2a3a60;border-radius:16px;padding:16px;margin:0 0 12px}");
  page += F("h1{font-size:.85rem;margin:0 0 8px;color:#9aa8c0;letter-spacing:.08em;text-transform:uppercase}");
  page += F("#clock{font:700 3rem/1 ui-monospace,Menlo,monospace;color:#fff}");
  page += F("#date{color:#e8eef8;font-size:.95rem;font-weight:600;margin:8px 0 2px}");
  page += F("#zone{color:#9aa8c0;font-size:.8rem}");
  page += F(".head{display:flex;flex-wrap:wrap;gap:14px 28px;align-items:flex-start;justify-content:space-between}");
  page += F(".head .who{flex:1 1 180px;min-width:0}");
  page += F(".simwrap{flex:0 0 auto}");
  page += F(".sim{width:214px;background:#05080f;border:1px solid #243152;border-radius:18px;padding:12px 12px 10px;box-shadow:inset 0 0 0 1px #0b1220}");
  page += F(".simgrid{display:grid;grid-template-columns:repeat(8,1fr);gap:2px}");
  page += F(".simgrid i{aspect-ratio:1;display:flex;align-items:center;justify-content:center;border-radius:3px;background:#0a0e18;color:#222b40;font:700 .7rem ui-monospace,Menlo,monospace}");
  page += F(".simgrid i.on{color:#fff}");
  page += F(".simwords{text-align:center;margin:8px 0 0;font:700 .78rem system-ui,sans-serif;color:#e8eef8;min-height:1.2em;letter-spacing:.02em}");
  page += F(".grid i.on{box-shadow:inset 0 0 0 1px #3a4a70}");
  page += F(".ip a,.meta{color:#9aa8c0;font-size:.85rem}.ip a{color:#7ee0a8;font:700 .95rem ui-monospace,Menlo,monospace}");
  page += F(".tabs{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 12px;position:sticky;top:0;z-index:5;background:#08101f;padding:8px 0}");
  page += F(".tabs button{flex:1;min-width:64px;padding:10px 6px;border-radius:8px;border:0;background:#1a3050;color:#e8eef8;font-weight:700;font-size:.8rem}");
  page += F(".tabs button.on{background:#16c060;color:#fff}");
  page += F(".pane{display:none}.pane.on{display:block}");
  page += F("label{display:block;margin:10px 0 4px;color:#9aa8c0;font-size:.85rem}");
  page += F("input,select,textarea{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #2a3a60;background:#081028;color:#e8eef8}");
  page += F("textarea{font:700 .85rem/1.4 ui-monospace,Menlo,monospace;min-height:150px}");
  page += F("input[type=color]{height:46px;padding:4px}input[type=checkbox],input[type=range]{width:auto}");
  page += F("input[type=range]{width:100%}");
  page += F("button,.btn{display:inline-block;margin:8px 8px 0 0;padding:9px 12px;border-radius:8px;border:0;background:#16c060;color:#fff;font-weight:700;text-decoration:none;font-size:.85rem}");
  page += F(".sec{background:#1a3050}.danger{background:#8a2030}");
  page += F("p{color:#9aa8c0;font-size:.85rem;line-height:1.45}");
  page += F(".about h2{font-size:1rem;color:#fff;margin:18px 0 8px}");
  page += F(".about h2:first-child{margin-top:0}");
  page += F(".about ul{margin:8px 0;padding-left:18px}");
  page += F(".about li{margin:6px 0}");
  page += F(".mono{font:700 .85rem ui-monospace,Menlo,monospace;color:#7ee0a8;word-break:break-all}");
  page += F(".about a{color:#7ee0a8}");
  page += F(".grid{display:grid;grid-template-columns:repeat(8,1fr);gap:3px;margin:10px 0}");
  page += F(".grid i{display:flex;align-items:center;justify-content:center;background:#081028;border-radius:5px;aspect-ratio:1;font:700 .8rem ui-monospace,Menlo,monospace}");
  page += F(".grid i.star{color:#ffd24a}");
  page += F(".face2{display:flex;flex-wrap:wrap;gap:0 24px}");
  page += F(".face2 .grid{flex:1 1 240px;max-width:340px}.face2 .fcol{flex:1 1 260px}");
  // Wide screens: roomier container, bigger clock, prose in two columns, and
  // form controls kept at a readable width instead of stretching edge to edge.
  page += F("@media(min-width:900px){");
  page += F("body{padding:26px}.box{max-width:1040px}");
  page += F(".card{padding:20px 24px}");
  page += F("#clock{font-size:4.2rem}#date{font-size:1.1rem}");
  page += F(".head .who{text-align:right;flex:0 1 auto}");
  page += F(".sim{width:248px}.simgrid i{font-size:.78rem}");
  page += F("input,select,textarea{max-width:560px}");
  page += F("textarea{min-height:190px}");
  page += F(".about{column-count:2;column-gap:36px}");
  page += F(".about h2,.about ul,.about ol,.about p{break-inside:avoid}");
  page += F(".tabs button{flex:0 1 130px;font-size:.85rem}");
  page += F("}");
  page += F("@media(min-width:1400px){.box{max-width:1240px}}");
  page += F("</style></head><body><div class=box>");
  char datebuf[48];
  char zonebuf[24];
  formatDate(datebuf, sizeof(datebuf));
  zoneLabel(zonebuf, sizeof(zonebuf));

  page += F("<div class=card><h1>Word Clock ");
  page += config::kFirmwareVersion;
  page += F("</h1><div class=head><div><div id=clock>");
  page += nowbuf;
  page += F("</div><div id=date>");
  page += datebuf;
  page += F("</div><div id=zone>");
  page += zonebuf;
  page += F(" &nbsp; <b id=mode>");
  page += s_alarm_ringing ? "ALARM"
                          : (s_sleep_now ? "blackout"
                                         : (s_night_now ? "night" : "day"));
  page += F("</b></div></div><div class=simwrap><h1>Live face</h1><div class=sim><div class=simgrid id=simgrid>");
  for (uint8_t y = 0; y < 8; ++y) {
    for (uint8_t x = 0; x < 8; ++x) {
      const char c = face::letter(x, y);
      page += F("<i>");
      page += (c ? c : ' ');
      page += F("</i>");
    }
  }
  page += F("</div><div class=simwords id=simwords>");
  {
    char spoken[48];
    formatSpokenTime(spoken, sizeof(spoken));
    page += htmlEscape(String(spoken));
  }
  page += F("</div></div></div><div class=who>");
  page += F("<p class=ip><a href='http://");
  page += ip;
  page += F("/'>http://");
  page += ip;
  page += F("/</a><br><a href='http://");
  page += s_hostname;
  page += F(".local/'>");
  page += s_hostname;
  page += F(".local</a></p><p class=meta>WiFi <b style=color:#fff>");
  page += htmlEscape(WiFi.SSID());
  page += F("</b><br>Face <b style=color:#fff>");
  page += face::plateTitle();
  page += F("</b></p></div></div></div>");
  page += F("<div class=tabs>");
  page += F("<button type=button id=tclock onclick=\"go('clock')\">Time</button>");
  page += F("<button type=button id=tlook onclick=\"go('look')\">Look</button>");
  page += F("<button type=button id=tface onclick=\"go('face')\">Face</button>");
  page += F("<button type=button id=tnight onclick=\"go('night')\">Night</button>");
  page += F("<button type=button id=talign onclick=\"go('align')\">Align</button>");
  page += F("<button type=button id=twifi onclick=\"go('wifi')\">WiFi</button>");
  page += F("<button type=button id=tabout onclick=\"go('about')\">About</button></div>");
  page += F("<form method=POST action=/save>");

  page += F("<div id=pclock class='pane card'><h1>Time zone</h1>");
  {
    const time_t tnow = time(nullptr);
    struct tm lt {};
    struct tm gt {};
    localtime_r(&tnow, &lt);
    gmtime_r(&tnow, &gt);
    char zone[24];
    zoneLabel(zone, sizeof(zone));
    char zbuf[110];
    snprintf(zbuf, sizeof(zbuf),
             "Now <b style=color:#fff>%02d:%02d</b> %s &nbsp; UTC %02d:%02d",
             lt.tm_hour, lt.tm_min, zone, gt.tm_hour, gt.tm_min);
    page += F("<p>");
    page += zbuf;
    page += F("</p>");
  }
  page += F("<label>Where is the clock</label><select name=tzpick>");
  for (const auto& z : kTimezones) {
    appendChoice(page, z.id, tzid, z.label);
  }
  appendChoice(page, "custom", tzid, "Custom POSIX");
  page += F("</select><label>Custom POSIX (only if needed)</label><input name=tz value='");
  page += htmlEscape(s_tz);
  page += F("'><label>Set time now (HH:MM)</label><input name=manual placeholder='14:35'>");
  page += F("<label>NTP server</label><input name=ntp value='");
  page += htmlEscape(s_ntp);
  page += F("'><p><a class='btn sec' href=/fetch>Fetch time from internet</a></p>");
  page += F("<button type=submit name=next value=clock>Save time</button></div>");

  page += F("<div id=plook class='pane card'><h1>Colors and seconds</h1>");
  page += F("<label>Word color</label><select name=cmode>");
  appendOption(page, 0, s_color_mode, "Hour hue");
  appendOption(page, 1, s_color_mode, "Solid color");
  appendOption(page, 2, s_color_mode, "Rainbow letters");
  appendOption(page, 3, s_color_mode, "Warm white");
  page += F("</select><label>Solid color</label><input type=color name=hex value='");
  page += hex;
  page += F("'><label>Seconds effect</label><select name=secfx>");
  appendOption(page, 0, s_sec_fx, "Off - words only");
  appendOption(page, 5, s_sec_fx, "* blink - unused star ticks each second");
  appendOption(page, 1, s_sec_fx, "Pulse - words breathe");
  appendOption(page, 2, s_sec_fx, "Trail - unused pixels fill the minute");
  appendOption(page, 3, s_sec_fx, "Tick - one unused pixel hops");
  appendOption(page, 4, s_sec_fx, "Sparkle - twinkle unused letters");
  page += F("</select><label>Time / words brightness ");
  page += String(s_brightness);
  page += F("</label><input name=bright type=range min=1 max=80 value='");
  page += String(s_brightness);
  page += F("'><label>This effect brightness ");
  page += String(s_fx_bright[s_sec_fx]);
  page += F("</label><input name=fxbright type=range min=1 max=80 value='");
  page += String(s_fx_bright[s_sec_fx]);
  page += F("'><label><input type=checkbox name=mdots value=1");
  if (s_min_dots) page += F(" checked");
  page += F("> Minute dots (1-4 extra minutes on * / unused cells)</label>");
  page += F("<button type=submit name=next value=look>Save look</button></div>");

  page += F("<div id=pface class='pane card'><h1>Clock face</h1>");
  page += F("<label>Letter plate</label><select name=plate>");
  appendChoice(page, "home", face::plateId(), "Home (TWENTYH* / SETPASTO)");
  appendChoice(page, "doro", face::plateId(), "ATWENTYD letter plate");
  appendChoice(page, "custom", face::plateId(), "Custom 8x8 letters");
  page += F("</select><div class=face2><div class=grid id=facegrid>");
  for (uint8_t y = 0; y < 8; ++y) {
    for (uint8_t x = 0; x < 8; ++x) {
      const char c = face::letter(x, y);
      page += (c == '*') ? F("<i class=star>") : F("<i>");
      page += (c ? c : ' ');
      page += F("</i>");
    }
  }
  page += F("</div><div class=fcol><label>Custom letters (8 rows)</label><textarea name=custom>");
  for (int y = 0; y < 8; ++y) {
    if (y) {
      page += '\n';
    }
    page += face::row(y);
  }
  page += F("</textarea></div></div>");
  page += F("<p>Save, then use Align to light words and match the printed face.</p>");
  page += F("<button type=submit name=next value=face>Save face</button></div>");

  {
    char t1[8];
    char t2[8];
    fmtHhMm(s_night_start, t1, sizeof(t1));
    fmtHhMm(s_night_end, t2, sizeof(t2));
    page += F("<div id=pnight class='pane card'><h1>Night, blackout and alarm</h1>");
    page += F("<p>Status now: <b style=color:#fff>");
    page += s_sleep_now ? F("blacked out") : (s_night_now ? F("night") : F("day"));
    page += F("</b></p>");

    page += F("<label><input type=checkbox name=nighton value=1");
    if (s_night_on) page += F(" checked");
    page += F("> Dim the clock at night</label>");
    page += F("<label>Night starts</label><input type=time name=nstart value='");
    page += t1;
    page += F("'><label>Night ends</label><input type=time name=nend value='");
    page += t2;
    page += F("'><label>Night brightness ");
    page += String(s_night_bright);
    page += F("</label><input name=nbright type=range min=1 max=80 value='");
    page += String(s_night_bright);
    page += F("'><label>Night color</label><select name=ncmode>");
    appendOption(page, 4, s_night_cmode, "Same as daytime");
    appendOption(page, 3, s_night_cmode, "Warm white - easiest on the eyes");
    appendOption(page, 1, s_night_cmode, "Solid color");
    appendOption(page, 0, s_night_cmode, "Hour hue");
    appendOption(page, 2, s_night_cmode, "Rainbow letters");
    page += F("</select>");

    fmtHhMm(s_sleep_start, t1, sizeof(t1));
    fmtHhMm(s_sleep_end, t2, sizeof(t2));
    page += F("<h1 style='margin-top:18px'>Blackout</h1>");
    page += F("<p>The matrix goes fully dark. The clock stays on the network and ");
    page += F("an alarm still wakes it.</p>");
    page += F("<label><input type=checkbox name=sleepon value=1");
    if (s_sleep_on) page += F(" checked");
    page += F("> Blackout the matrix</label>");
    page += F("<label>Blackout starts</label><input type=time name=sstart value='");
    page += t1;
    page += F("'><label>Blackout ends</label><input type=time name=send value='");
    page += t2;
    page += F("'>");

    fmtHhMm(s_alarm_min, t1, sizeof(t1));
    page += F("<h1 style='margin-top:18px'>Alarm</h1>");
    page += F("<label><input type=checkbox name=alarmon value=1");
    if (s_alarm_on) page += F(" checked");
    page += F("> Daily alarm</label>");
    page += F("<label>Alarm time</label><input type=time name=atime value='");
    page += t1;
    page += F("'><label>Ring for ");
    page += String(s_alarm_len);
    page += F(" seconds</label><input name=alen type=range min=5 max=180 value='");
    page += String(s_alarm_len);
    page += F("'>");
    page += F("<p>The whole matrix flashes orange. Press the Atom button or Stop ");
    page += F("to silence it.</p>");
    page += F("<p><a class='btn sec' href='/alarm?a=test'>Test alarm</a>");
    page += F("<a class='btn danger' href='/alarm?a=stop'>Stop alarm</a></p>");
    page += F("<button type=submit name=next value=night>Save schedule</button>");
    page += F("</div>");
  }

  page += F("<div id=palign class='pane card'><h1>Align the matrix</h1>");
  page += F("<p>Hold the printed face in front of you. Tap a corner button, then adjust ");
  page += F("mirrors and zigzag until the lit pixel sits on that corner letter.</p>");
  page += F("<p><b>Colors:</b> (1,1) red &nbsp; (1,8) green &nbsp; (8,1) yellow &nbsp; (8,8) blue</p>");
  page += F("<p><a class='btn sec' href='/cal?m=4'>Face (1,1)</a>");
  page += F("<a class='btn sec' href='/cal?m=6'>Face (1,8)</a>");
  page += F("<a class='btn sec' href='/cal?m=7'>Face (8,1)</a>");
  page += F("<a class='btn sec' href='/cal?m=5'>Face (8,8)</a>");
  page += F("<a class='btn sec' href='/cal?m=1'>All 4 corners</a></p>");
  page += F("<p><a class='btn sec' href='/cal?m=2'>Strip LED 0 (DIN)</a>");
  page += F("<a class='btn sec' href='/cal?m=3'>Strip LED 63</a>");
  page += F("<a class='btn sec' href='/cal?m=0'>Show clock</a></p>");
  page += F("<label>Wiring</label><p>");
  page += F("<label><input type=checkbox name=flipx value=1");
  if (s_flip_x) page += F(" checked");
  page += F("> Mirror left / right</label><br>");
  page += F("<label><input type=checkbox name=flipy value=1");
  if (s_flip_y) page += F(" checked");
  page += F("> Mirror top / bottom</label><br>");
  page += F("<label><input type=checkbox name=zigzag value=1");
  if (s_zigzag) page += F(" checked");
  page += F("> Zigzag / serpentine rows</label><br>");
  page += F("<label><input type=checkbox name=colmaj value=1");
  if (s_col_major) page += F(" checked");
  page += F("> Column-major strip</label><br>");
  page += F("<label><input type=checkbox name=rgb value=1");
  if (s_rgb_order) page += F(" checked");
  page += F("> RGB color order (default GRB)</label></p>");
  page += F("<p>Quick try (shows all 4 corners):</p>");
  page += F("<a class='btn sec' href='/align?flipx=0&flipy=0&zigzag=0'>Plain</a>");
  page += F("<a class='btn sec' href='/align?flipx=1&flipy=0&zigzag=1'>Mirror X + zigzag</a>");
  page += F("<a class='btn sec' href='/align?flipx=0&flipy=0&zigzag=1'>Zigzag only</a>");
  page += F("<a class='btn sec' href='/align?flipx=1&flipy=0&zigzag=0'>Mirror X only</a>");
  page += F("<button type=submit name=next value=align>Save wiring</button>");
  page += F("<p>If (1,1) lights top-right but (8,8) is correct, try <b>Mirror X + zigzag</b>.</p>");
  page += F("<p>Words - tap one and check against the printed face</p>");
  static const char* kAlignWords[] = {
      "twenty", "quarter", "fivemin", "half", "past", "to", "one", "two",
      "three",  "four",    "five",    "six",  "seven","eight","nine","ten",
      "eleven", "twelve"};
  for (const char* w : kAlignWords) {
    page += F("<a class='btn sec' href='/word?n=");
    page += w;
    page += F("'>");
    page += w;
    page += F("</a>");
  }
  page += F("<p>Rows (1 is top)</p>");
  for (int r = 0; r < 8; ++r) {
    page += F("<a class='btn sec' href='/row?r=");
    page += String(r);
    page += F("'>Row ");
    page += String(r + 1);
    page += F(" ");
    page += face::row(r);
    page += F("</a>");
  }
  page += F("</div></form>");

  page += F("<div id=pwifi class='pane card'><h1>WiFi</h1>");
  page += F("<p>Connected to <b style=color:#fff>");
  page += htmlEscape(WiFi.SSID());
  page += F("</b></p><p>Dashboard <a class=ip href='http://");
  page += ip;
  page += F("/'>http://");
  page += ip;
  page += F("/</a></p>");

  page += F("<form method=POST action=/save>");
  page += F("<label>Device name</label><input name=host maxlength=31 value='");
  page += htmlEscape(String(s_hostname));
  page += F("'><p>Lowercase letters, numbers and dashes. The clock answers at ");
  page += F("<span class=mono>http://");
  page += s_hostname;
  page += F(".local/</span> - reboot after changing it.</p>");
  page += F("<button type=submit name=next value=wifi>Save name</button></form>");

  page += F("<h1 style='margin-top:18px'>Change network</h1>");
  page += F("<p>Pick a network here - no need to join the setup AP.</p>");
  page += F("<form method=POST action=/wifi/join>");
  page += F("<label>Networks in range</label>");
  page += F("<select id=ssidsel onchange=pick()><option value=''>Tap Scan</option></select>");
  page += F("<label>Network name</label><input id=ssidtxt name=ssid maxlength=32 value='");
  page += htmlEscape(WiFi.SSID());
  page += F("'><label>Password</label><input name=pass type=password maxlength=64 ");
  page += F("placeholder='leave blank for open networks'>");
  page += F("<p><a class='btn sec' href='javascript:void(0)' onclick=scan()>Scan</a>");
  page += F("<button type=submit>Join network</button></p></form>");
  page += F("<p>If the new network fails, the clock returns to <b style=color:#fff>");
  page += htmlEscape(WiFi.SSID());
  page += F("</b> on its own.</p>");

  page += F("<form method=POST action=/save>");
  page += F("<h1 style='margin-top:18px'>MQTT / Home Assistant</h1>");
  page += F("<p>Status: <b style=color:#fff>");
  page += s_mqtt_status;
  page += F("</b> &nbsp; publish <span class=mono>");
  page += s_mqtt_prefix[0] ? s_mqtt_prefix : config::kDefaultMqttPrefix;
  page += F("/state</span> &nbsp; commands <span class=mono>");
  page += s_mqtt_prefix[0] ? s_mqtt_prefix : config::kDefaultMqttPrefix;
  page += F("/cmd</span></p>");
  page += F("<label><input type=checkbox name=mqon value=1");
  if (s_mqtt_on) page += F(" checked");
  page += F("> Enable MQTT</label>");
  page += F("<label><input type=checkbox name=mqha value=1");
  if (s_ha_disco) page += F(" checked");
  page += F("> Home Assistant discovery</label>");
  page += F("<label>Broker</label><input name=mqh value='");
  page += htmlEscape(s_mqtt_host);
  page += F("'><label>Port</label><input name=mqp type=number value='");
  page += String(s_mqtt_port);
  page += F("'><label>Username</label><input name=mqu value='");
  page += htmlEscape(s_mqtt_user);
  page += F("'><label>Password</label><input name=mqpw type=password placeholder='");
  page += s_mqtt_pass[0] ? F("saved — leave blank to keep") : F("optional");
  page += F("'><label>Topic prefix</label><input name=mqpx value='");
  page += htmlEscape(s_mqtt_prefix[0] ? s_mqtt_prefix : config::kDefaultMqttPrefix);
  page += F("'><button type=submit name=next value=mqtt>Save MQTT</button></form>");

  page += F("<h1 style='margin-top:18px'>Tools</h1>");
  page += F("<p><a class='btn sec' href=/portal>Open setup AP</a>");
  page += F("<a class='btn sec' href=/test>LED walk test</a>");
  page += F("<a class='btn sec' href=/reboot>Reboot</a>");
  page += F("<a class='btn danger' href=/reset>Forget WiFi</a></p>");
  page += F("<p>Serial 115200: help, set HH:MM, effect star, bright 40</p></div>");

  page += F("<div id=pabout class='pane card about'><h1>About</h1>");
  page += F("<h2>ESP-WordClock8x8</h2>");
  page += F("<p>Firmware for an English 8&times;8 word clock. The LED matrix and this ");
  page += F("dashboard run on the same microcontroller. Time is synchronized from NTP ");
  page += F("over Wi-Fi.</p>");
  page += F("<p>Firmware <b>");
  page += config::kFirmwareVersion;
  page += F("</b> &nbsp; Board <b>");
  page += config::kBoardName;
  page += F("</b> (");
  page += compat::chipName();
  page += F(")<br>License: <b>MIT</b> &nbsp; Copyright (c) 2026 ");
  page += config::kAuthor;
  page += F("<br>Author: <a href='mailto:");
  page += config::kContactEmail;
  page += F("'>");
  page += config::kContactEmail;
  page += F("</a></p>");
  page += F("<p>This is original firmware by a single author. There are no project ");
  page += F("collaborators. Third-party Arduino libraries are used under their own licenses.</p>");

  page += F("<h2>Dashboard</h2><ul>");
  page += F("<li><b>Time</b> &mdash; time zone, manual set, NTP server</li>");
  page += F("<li><b>Look</b> &mdash; colours, seconds effects, brightness, minute dots</li>");
  page += F("<li><b>Live face</b> &mdash; web simulator of the 8&times;8 matrix, including animations</li>");
  page += F("<li><b>Face</b> &mdash; home, ATWENTYD, or custom letter plate</li>");
  page += F("<li><b>Night</b> &mdash; dimming, blackout window, daily alarm</li>");
  page += F("<li><b>Align</b> &mdash; wiring, corners, word and row tests</li>");
  page += F("<li><b>Wi-Fi</b> &mdash; hostname, scan/join, MQTT, reboot</li>");
  page += F("<li><b>About</b> &mdash; license, backup, and firmware update</li></ul>");

  page += F("<h2>Hardware</h2><ul>");
  page += F("<li>Board: ");
  page += config::kBoardName;
  page += F("</li>");
  page += F("<li>Matrix: 8&times;8 WS2812 / NeoPixel, 5 V, single data line</li>");
  page += F("<li>Data in (DIN): GPIO ");
  page += String(static_cast<unsigned>(config::kLedPin));
  page += F("</li>");
  page += F("<li>Button: GPIO ");
  page += String(static_cast<unsigned>(config::kButtonPin));
  page += F(" (short press = brightness, hold ~1.5 s = LED walk)</li>");
  page += F("<li>Use modest brightness on USB power</li></ul>");

  page += F("<h2>Default letter plate (row 1 is top)</h2>");
  page += F("<p class=mono>TWENTYH*<br>QUARTERM<br>FIVEHALF<br>SETPASTO<br>");
  page += F("FIVEIGHT<br>SIXTHREE<br>TWELEVEN<br>FOURNINE</p>");
  page += F("<p>Shared letters: PAST/TO, FIVE/EIGHT, TWELVE/ELEVEN, TWO, SEVEN, ONE. ");
  page += F("The <b>*</b> cells can blink each second or show extra-minute dots.</p>");

  page += F("<h2>First boot</h2><ol>");
  page += F("<li>Flash firmware and power the board and matrix</li>");
  page += F("<li>Join access point <b>");
  page += config::kApName;
  page += F("</b> (2.4 GHz)</li>");
  page += F("<li>Open <span class=mono>http://192.168.4.1</span></li>");
  page += F("<li>Enter the home network; the portal shows the LAN address</li>");
  page += F("<li>Open <span class=mono>http://");
  page += s_hostname;
  page += F(".local</span> or the displayed IP</li></ol>");
  page += F("<p>After the first join, the Wi-Fi tab can scan and switch networks. ");
  page += F("The setup access point is only required when no saved network is reachable.</p>");

  page += F("<h2>Serial console (115200 baud)</h2>");
  page += F("<p>Type <span class=mono>help</span> for the full command list. Examples: ");
  page += F("<span class=mono>status</span>, <span class=mono>set 14:35</span>, ");
  page += F("<span class=mono>fetch</span>, <span class=mono>word twenty</span>, ");
  page += F("<span class=mono>export</span>, <span class=mono>mqtt on</span>, ");
  page += F("<span class=mono>hostname &lt;name&gt;</span>.</p>");

  page += F("<h2>Backup and firmware</h2>");
  page += F("<p>Chip <b>");
  page += compat::chipName();
  page += F("</b> &nbsp; NTP ");
  page += s_time_ok ? F("synchronized") : F("waiting");
  page += F(" &nbsp; MQTT <b>");
  page += s_mqtt_status;
  page += F("</b></p>");
  page += F("<p><a class='btn sec' href=/api/settings download='wordclock-settings.json'>Export settings</a></p>");
  page += F("<form method=POST action=/api/settings enctype=multipart/form-data>");
  page += F("<label>Import settings JSON</label><textarea name=json placeholder='Paste exported JSON'></textarea>");
  page += F("<button type=submit>Import settings</button></form>");
  page += F("<form method=POST action=/update enctype=multipart/form-data>");
  page += F("<label>OTA firmware image (.bin)</label><input type=file name=firmware accept=.bin>");
  page += F("<button type=submit>Install firmware</button></form>");
  page += F("<p>Network OTA: <span class=mono>pio run -e atoms3lite -t upload --upload-port ");
  page += s_hostname;
  page += F(".local</span></p>");

  page += F("<h2>License</h2>");
  page += F("<p>MIT License. Copyright (c) 2026 ");
  page += config::kAuthor;
  page += F(". Sole author. No collaborators.</p></div>");

  page += F("</div><script>");
  page += F("function go(id){document.querySelectorAll('.pane').forEach(function(e){e.classList.remove('on')});");
  page += F("document.querySelectorAll('.tabs button').forEach(function(e){e.classList.remove('on')});");
  page += F("var p=document.getElementById('p'+id),t=document.getElementById('t'+id);");
  page += F("if(p)p.classList.add('on');if(t)t.classList.add('on');");
  page += F("if(history.replaceState)history.replaceState(null,'','#'+id);}");
  page += F("function boot(){var id=(location.hash||'#clock').slice(1);if(!document.getElementById('p'+id))id='clock';go(id);}");
  page += F("boot();");
  page += F("function set(id,v){var e=document.getElementById(id);if(e&&v)e.textContent=v;}");
  page += F("async function tick(){try{const r=await fetch('/api/time');const j=await r.json();");
  page += F("set('clock',j.time);set('date',j.date);set('zone',j.zone);set('mode',j.mode);");
  page += F("var e=document.getElementById('mode');");
  page += F("if(e)e.style.color=j.mode=='ALARM'?'#ff8a3d':(j.mode=='day'?'#7ee0a8':'#9aa8c0');");
  page += F("}catch(e){}}");
  page += F("function paintCell(el,ch,rgb){if(!el)return;el.textContent=ch||' ';");
  page += F("var r=parseInt(rgb.slice(0,2),16),g=parseInt(rgb.slice(2,4),16),b=parseInt(rgb.slice(4,6),16);");
  page += F("var on=r+g+b>22;el.className=on?'on':'';");
  page += F("el.style.background=on?('#'+rgb):'#0a0e18';");
  page += F("el.style.color=on?'#fff':'#222b40';");
  page += F("el.style.textShadow=on?('0 0 8px #'+rgb):'none';}");
  page += F("async function sim(){if(document.hidden)return;try{const r=await fetch('/api/face');const j=await r.json();");
  page += F("var a=document.querySelectorAll('#simgrid i'),b=document.querySelectorAll('#facegrid i');");
  page += F("for(var i=0;i<64;i++){var rgb=(j.rgb||'').substr(i*6,6)||'000000';var ch=(j.letters||' ').charAt(i);");
  page += F("paintCell(a[i],ch,rgb);paintCell(b[i],ch,rgb);}set('simwords',j.words||'');");
  page += F("}catch(e){}}");
  page += F("setInterval(tick,1000);setInterval(sim,180);sim();");
  page += F("function pick(){var s=document.getElementById('ssidsel');");
  page += F("if(s.value)document.getElementById('ssidtxt').value=s.value;}");
  page += F("async function scan(){var s=document.getElementById('ssidsel');");
  page += F("s.innerHTML='<option>Scanning...</option>';");
  page += F("for(var i=0;i<25;i++){try{var r=await fetch('/wifi/scan');var j=await r.json();");
  page += F("if(j.status=='done'){s.innerHTML='';if(!j.nets.length){");
  page += F("s.innerHTML='<option>No networks found</option>';return;}");
  page += F("j.nets.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;");
  page += F("o.textContent=n.ssid+'  '+n.rssi+' dBm'+(n.lock?'  (locked)':'');s.appendChild(o)});");
  page += F("pick();return;}}catch(e){}");
  page += F("await new Promise(function(f){setTimeout(f,700)});}");
  page += F("s.innerHTML='<option>Scan failed - try again</option>';}");
  page += F("</script></body></html>");
  s_http.send(200, "text/html; charset=us-ascii", page);
}

void handleSave() {
  if (s_http.hasArg("ntp") && s_http.arg("ntp").length()) {
    strncpy(s_ntp, s_http.arg("ntp").c_str(), sizeof(s_ntp) - 1);
  }
  if (s_http.hasArg("tzpick")) {
    const char* posix = timezonePosix(s_http.arg("tzpick").c_str());
    if (posix) {
      strncpy(s_tz, posix, sizeof(s_tz) - 1);
    } else if (s_http.hasArg("tz") && s_http.arg("tz").length()) {
      strncpy(s_tz, s_http.arg("tz").c_str(), sizeof(s_tz) - 1);
    }
  } else if (s_http.hasArg("tz") && s_http.arg("tz").length()) {
    strncpy(s_tz, s_http.arg("tz").c_str(), sizeof(s_tz) - 1);
  }
  if (s_http.hasArg("bright")) {
    s_brightness = clampBright(s_http.arg("bright").toInt());
    applyBrightness();
  }
  if (s_http.hasArg("host")) {
    char clean[sizeof(s_hostname)] = {};
    sanitizeHostname(clean, sizeof(clean), s_http.arg("host").c_str());
    if (strcmp(clean, s_hostname) != 0) {
      strncpy(s_hostname, clean, sizeof(s_hostname) - 1);
      saveWifiPrefs();
      compat::setHostname(s_hostname);
      restartMdns();
      Serial.printf("Hostname: %s.local\n", s_hostname);
    }
  }
  if (s_http.hasArg("plate")) {
    face::setPlateId(s_http.arg("plate").c_str());
  }
  if (s_http.hasArg("custom") && s_http.arg("plate") == "custom") {
    face::setCustomRows(s_http.arg("custom").c_str());
  }
  // Checkboxes only report when ticked, so they are read from the pane that
  // actually owns them. Other panes post their own subset of the form.
  if (s_http.arg("next") == "night") {
    s_night_on = s_http.hasArg("nighton");
    s_sleep_on = s_http.hasArg("sleepon");
    s_alarm_on = s_http.hasArg("alarmon");
    uint16_t v = 0;
    if (parseHhMm(s_http.arg("nstart").c_str(), &v)) {
      s_night_start = v;
    }
    if (parseHhMm(s_http.arg("nend").c_str(), &v)) {
      s_night_end = v;
    }
    if (parseHhMm(s_http.arg("sstart").c_str(), &v)) {
      s_sleep_start = v;
    }
    if (parseHhMm(s_http.arg("send").c_str(), &v)) {
      s_sleep_end = v;
    }
    if (parseHhMm(s_http.arg("atime").c_str(), &v)) {
      s_alarm_min = v;
    }
    if (s_http.hasArg("nbright")) {
      s_night_bright = clampBright(s_http.arg("nbright").toInt());
    }
    if (s_http.hasArg("ncmode")) {
      int c = s_http.arg("ncmode").toInt();
      if (c < 0 || c > 4) {
        c = 4;
      }
      s_night_cmode = static_cast<uint8_t>(c);
    }
    if (s_http.hasArg("alen")) {
      int len = s_http.arg("alen").toInt();
      if (len < 5) len = 5;
      if (len > 180) len = 180;
      s_alarm_len = static_cast<uint8_t>(len);
    }
    // Re-arm so a time change can fire again today.
    s_alarm_fired_day = -1;
    s_alarm_fired_min = -1;
  }
  if (s_http.arg("next") == "align") {
    s_zigzag = s_http.hasArg("zigzag");
    s_flip_x = s_http.hasArg("flipx");
    s_flip_y = s_http.hasArg("flipy");
    s_col_major = s_http.hasArg("colmaj");
    const bool rgb = s_http.hasArg("rgb");
    if (rgb != s_rgb_order) {
      s_rgb_order = rgb;
      beginLeds();
    }
  }
  if (s_http.arg("next") == "look") {
    s_min_dots = s_http.hasArg("mdots");
  }
  if (s_http.arg("next") == "mqtt") {
    s_mqtt_on = s_http.hasArg("mqon");
    s_ha_disco = s_http.hasArg("mqha");
    if (s_http.hasArg("mqh")) {
      strncpy(s_mqtt_host, s_http.arg("mqh").c_str(), sizeof(s_mqtt_host) - 1);
    }
    if (s_http.hasArg("mqp")) {
      int p = s_http.arg("mqp").toInt();
      s_mqtt_port = p > 0 ? static_cast<uint16_t>(p) : config::kDefaultMqttPort;
    }
    if (s_http.hasArg("mqu")) {
      strncpy(s_mqtt_user, s_http.arg("mqu").c_str(), sizeof(s_mqtt_user) - 1);
    }
    if (s_http.hasArg("mqpw") && s_http.arg("mqpw").length()) {
      strncpy(s_mqtt_pass, s_http.arg("mqpw").c_str(), sizeof(s_mqtt_pass) - 1);
    }
    if (s_http.hasArg("mqpx") && s_http.arg("mqpx").length()) {
      strncpy(s_mqtt_prefix, s_http.arg("mqpx").c_str(), sizeof(s_mqtt_prefix) - 1);
    }
    saveMqttPrefs();
    if (s_mqtt.connected()) {
      s_mqtt.disconnect();
    }
  }
  if (s_http.hasArg("cmode")) {
    int m = s_http.arg("cmode").toInt();
    if (m < 0) m = 0;
    if (m > 3) m = 3;
    s_color_mode = static_cast<uint8_t>(m);
  }
  if (s_http.hasArg("secfx")) {
    int fx = s_http.arg("secfx").toInt();
    if (fx < 0) fx = 0;
    if (fx > 5) fx = 5;
    s_sec_fx = static_cast<uint8_t>(fx);
  }
  if (s_http.hasArg("fxbright")) {
    s_fx_bright[s_sec_fx] = clampBright(s_http.arg("fxbright").toInt());
  }
  if (s_http.hasArg("hex") && s_http.arg("hex").length()) {
    parseHexColor(s_http.arg("hex").c_str());
  }
  saveLookPrefs();
  saveNtpPrefs();
  applyTimezone();
  if (s_http.hasArg("manual") && s_http.arg("manual").length()) {
    int hh = 0, mm = 0;
    if (sscanf(s_http.arg("manual").c_str(), "%d:%d", &hh, &mm) == 2) {
      setManualTime(hh, mm);
    }
  }
  refreshFace();
  String next = F("/");
  if (s_http.hasArg("next") && s_http.arg("next").length()) {
    if (s_http.arg("next") == "mqtt") {
      next = F("/#wifi");
    } else {
      next = String("/#") + s_http.arg("next");
    }
  }
  s_http.sendHeader("Location", next);
  s_http.send(303);
}

void clearPreview() {
  s_cal_mode = 0;
  s_word_preview = false;
  s_preview_row = 255;
}

void redirectAlign() {
  s_http.sendHeader("Location", "/#align");
  s_http.send(303);
}

void handleCal() {
  clearPreview();
  if (s_http.hasArg("m")) {
    int m = s_http.arg("m").toInt();
    if (m < 0) m = 0;
    if (m > 7) m = 7;
    s_cal_mode = static_cast<uint8_t>(m);
  }
  refreshFace();
  redirectAlign();
}

void handleAlignPreset() {
  if (s_http.hasArg("flipx")) {
    s_flip_x = s_http.arg("flipx") == "1";
  }
  if (s_http.hasArg("flipy")) {
    s_flip_y = s_http.arg("flipy") == "1";
  }
  if (s_http.hasArg("zigzag")) {
    s_zigzag = s_http.arg("zigzag") == "1";
  }
  if (s_http.hasArg("colmaj")) {
    s_col_major = s_http.arg("colmaj") == "1";
  }
  saveLookPrefs();
  clearPreview();
  s_cal_mode = 1;
  refreshFace();
  redirectAlign();
}

void handleAlarm() {
  const String a = s_http.arg("a");
  if (a == "test") {
    startAlarm(s_alarm_len);
  } else {
    stopAlarm();
  }
  refreshFace();
  s_http.sendHeader("Location", "/#night");
  s_http.send(303);
}

void handleWifiScan() {
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    s_http.send(200, "application/json", F("{\"status\":\"scanning\"}"));
    return;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true, true);
    s_http.send(200, "application/json", F("{\"status\":\"scanning\"}"));
    return;
  }
  String json = F("{\"status\":\"done\",\"nets\":[");
  int listed = 0;
  for (int i = 0; i < n && listed < 20; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) {
      continue;
    }
    bool dup = false;
    for (int j = 0; j < i; ++j) {
      if (WiFi.SSID(j) == ssid) {
        dup = true;
        break;
      }
    }
    if (dup) {
      continue;
    }
    if (listed) {
      json += ',';
    }
    json += F("{\"ssid\":\"");
    json += jsonEscape(ssid);
    json += F("\",\"rssi\":");
    json += String(WiFi.RSSI(i));
    json += F(",\"lock\":");
    json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? F("false") : F("true");
    json += '}';
    ++listed;
  }
  json += F("]}");
  WiFi.scanDelete();
  s_http.send(200, "application/json", json);
}

void handleWifiStatus() {
  String json = F("{\"ok\":");
  json += wifiHasIp() ? F("true") : F("false");
  json += F(",\"joining\":");
  json += s_joining || s_join_pending ? F("true") : F("false");
  json += F(",\"ssid\":\"");
  json += jsonEscape(WiFi.SSID());
  json += F("\",\"ip\":\"");
  json += wifiHasIp() ? WiFi.localIP().toString() : String();
  json += F("\",\"host\":\"");
  json += s_hostname;
  json += F(".local\"}");
  s_http.sendHeader("Access-Control-Allow-Origin", "*");
  s_http.send(200, "application/json", json);
}

void handleWifiJoin() {
  String ssid = s_http.arg("ssid");
  ssid.trim();
  if (ssid.length() == 0) {
    s_http.sendHeader("Location", "/#wifi");
    s_http.send(303);
    return;
  }
  strncpy(s_join_ssid, ssid.c_str(), sizeof(s_join_ssid) - 1);
  strncpy(s_join_pass, s_http.arg("pass").c_str(), sizeof(s_join_pass) - 1);
  s_join_pending = true;

  String page;
  page.reserve(2200);
  page += F("<!DOCTYPE html><html><head><meta charset=utf-8>");
  page += F("<meta name=viewport content='width=device-width,initial-scale=1'>");
  page += F("<title>Joining WiFi</title><style>");
  page += F("body{font-family:system-ui,sans-serif;background:#08101f;color:#e8eef8;margin:0;padding:24px}");
  page += F(".card{max-width:520px;margin:auto;background:#122040;border:1px solid #2a3a60;");
  page += F("border-radius:16px;padding:18px}h1{font-size:1.1rem;margin:0 0 10px}");
  page += F("p{color:#9aa8c0;line-height:1.5;font-size:.9rem}");
  page += F("a{color:#7ee0a8;font-weight:700;word-break:break-all}</style></head><body><div class=card>");
  page += F("<h1>Joining ");
  page += htmlEscape(ssid);
  page += F("</h1><p id=s>Switching networks. This can take up to 20 seconds.</p>");
  page += F("<p>The clock gets a new address on that network. Join the same WiFi, then open ");
  page += F("<a id=h href='http://");
  page += s_hostname;
  page += F(".local/'>http://");
  page += s_hostname;
  page += F(".local/</a></p>");
  page += F("<p>If the password was wrong, the clock returns to its old network and this page ");
  page += F("starts working again.</p>");
  page += F("</div><script>");
  page += F("var host='http://");
  page += s_hostname;
  page += F(".local';");
  page += F("async function probe(u){try{var c=new AbortController();setTimeout(function(){c.abort()},2500);");
  page += F("var r=await fetch(u+'/wifi/status',{signal:c.signal});var j=await r.json();");
  page += F("if(j.ok&&!j.joining){location.href=u+'/';return true}}catch(e){}return false}");
  page += F("setInterval(async function(){if(await probe(host))return;await probe('')},3000);");
  page += F("</script></body></html>");
  s_http.send(200, "text/html; charset=us-ascii", page);
}

void handleWordPreview() {
  clearPreview();
  if (s_http.hasArg("n")) {
    String name = s_http.arg("n");
    name.toLowerCase();
    const face::Word w = wordFromName(name.c_str());
    if (w != face::Word::kCount) {
      s_preview_word = w;
      s_word_preview = true;
    }
  }
  refreshFace();
  redirectAlign();
}

void handleRowPreview() {
  clearPreview();
  if (s_http.hasArg("r")) {
    int r = s_http.arg("r").toInt();
    if (r >= 0 && r < 8) {
      s_preview_row = static_cast<uint8_t>(r);
    }
  }
  refreshFace();
  redirectAlign();
}

void registerRoutes() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  s_http.on("/", HTTP_GET, sendDashboard);
  s_http.on("/api/time", HTTP_GET, sendTimeJson);
  s_http.on("/api/face", HTTP_GET, sendFaceJson);
  s_http.on("/save", HTTP_POST, handleSave);
  s_http.on("/api/settings", HTTP_GET, sendSettingsJson);
  s_http.on("/api/settings", HTTP_POST, []() {
    String err;
    const String body =
        s_http.hasArg("json") ? s_http.arg("json") : s_http.arg("plain");
    if (body.length() == 0) {
      s_http.send(400, "text/plain", "missing json");
      return;
    }
    if (!applySettingsJson(body.c_str(), &err)) {
      s_http.send(400, "text/plain", err);
      return;
    }
    s_http.sendHeader("Location", "/#about");
    s_http.send(303);
  });
  s_http.on(
      "/update", HTTP_POST,
      []() {
        const bool ok = !Update.hasError();
        s_http.send(200, "text/plain", ok ? "OK rebooting" : "OTA failed");
        if (ok) {
          delay(400);
          ESP.restart();
        }
      },
      []() {
        HTTPUpload& up = s_http.upload();
        if (up.status == UPLOAD_FILE_START) {
          Serial.printf("OTA: web upload %s\n", up.filename.c_str());
          if (!compat::otaBegin(up.totalSize)) {
#ifdef ESP32
            Update.abort();
#endif
          }
        } else if (up.status == UPLOAD_FILE_WRITE) {
          if (Update.write(up.buf, up.currentSize) != up.currentSize) {
#ifdef ESP32
            Update.abort();
#endif
          }
        } else if (up.status == UPLOAD_FILE_END) {
          if (!Update.end(true)) {
            Serial.println("OTA: end failed");
          }
        }
      });
  s_http.on("/cal", HTTP_GET, handleCal);
  s_http.on("/align", HTTP_GET, handleAlignPreset);
  s_http.on("/word", HTTP_GET, handleWordPreview);
  s_http.on("/row", HTTP_GET, handleRowPreview);
  s_http.on("/alarm", HTTP_GET, handleAlarm);
  s_http.on("/wifi/scan", HTTP_GET, handleWifiScan);
  s_http.on("/wifi/status", HTTP_GET, handleWifiStatus);
  s_http.on("/wifi/join", HTTP_POST, handleWifiJoin);
  s_http.on("/wifi/join", HTTP_GET, handleWifiJoin);
  s_http.on("/fetch", HTTP_GET, []() {
    syncNtp();
    s_http.sendHeader("Location", "/");
    s_http.send(303);
  });
  s_http.on("/test", HTTP_GET, []() {
    runTest();
    s_http.sendHeader("Location", "/");
    s_http.send(303);
  });
  s_http.on("/reboot", HTTP_GET, []() {
    s_http.send(200, "text/plain", "Rebooting");
    delay(200);
    ESP.restart();
  });
  s_http.on("/reset", HTTP_GET, []() {
    s_http.send(200, "text/plain", "Resetting WiFi");
    delay(200);
    s_wm.resetSettings();
    ESP.restart();
  });
  s_http.on("/portal", HTTP_GET, []() {
    s_http.send(200, "text/html; charset=us-ascii",
                F("<!DOCTYPE html><html><body style='font-family:system-ui;background:#08101f;"
                  "color:#e8eef8;padding:24px'><h1>Starting WiFi setup</h1>"
                  "<p>Join <b>WordClock8x8</b> and open "
                  "<a style='color:#7ee0a8' href='http://192.168.4.1'>http://192.168.4.1</a>.</p>"
                  "</body></html>"));
    delay(300);
    openPortal();
  });
}

void startWeb() {
  if (s_web_started || s_portal_open) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  registerRoutes();
  s_http.begin();
  s_web_started = true;
  Serial.printf("Web UI: http://%s  and  http://%s.local\n",
                WiFi.localIP().toString().c_str(), s_hostname);
}

void printHelp() {
  Serial.println();
  Serial.println("=== Word Clock serial (115200) ===");
  Serial.println("-- Time --");
  Serial.println("status               Full status");
  Serial.println("fetch                Sync from NTP");
  Serial.println("ntp <host>           Set NTP server and fetch");
  Serial.println("set HH:MM            Manual time preview");
  Serial.println("live                 Back to NTP time");
  Serial.println("tz <posix>           Set POSIX timezone string");
  Serial.println("tz <id>              Preset: ast est cst mst pst az utc uk cet syd jst");
  Serial.println("tzlist               List timezone presets");
  Serial.println("-- Look --");
  Serial.println("bright <1-80>        Daytime word brightness");
  Serial.println("color hour|solid|rainbow|warm");
  Serial.println("rgb <r> <g> <b>      Solid colour (0-255)");
  Serial.println("effect off|star|pulse|trail|tick|sparkle");
  Serial.println("fxbright <1-80>      Current seconds effect brightness");
  Serial.println("mdots on|off         Extra-minute dots");
  Serial.println("-- Night / alarm --");
  Serial.println("night on|off | night HH:MM HH:MM");
  Serial.println("nbright <1-80>       Night brightness");
  Serial.println("ncolor same|warm|solid|hour|rainbow");
  Serial.println("sleep on|off | sleep HH:MM HH:MM");
  Serial.println("blackout             Alias for sleep");
  Serial.println("alarm HH:MM|on|off|test|stop");
  Serial.println("alen <5-180>          Alarm ring length (seconds)");
  Serial.println("-- Face / align --");
  Serial.println("plate home|doro|custom");
  Serial.println("letters              Print or set 8 rows (A|B|... or 8 lines)");
  Serial.println("rowset <0-7> <text>  Set one custom face row");
  Serial.println("face                 Print letter grid");
  Serial.println("                       Live web simulator: GET /api/face");
  Serial.println("word <name>          Light one word (twenty, half, eleven...)");
  Serial.println("row <0-7>            Light one row");
  Serial.println("cal off|corners|led0|led63|11|18|81|88");
  Serial.println("flipx on|off         Mirror left / right");
  Serial.println("flipy on|off         Mirror top / bottom");
  Serial.println("zigzag on|off        Serpentine rows");
  Serial.println("colmaj on|off        Column-major strip order");
  Serial.println("order grb|rgb        LED colour order");
  Serial.println("align plain|mirrorxz|zigzag|mirrorx   Wiring presets");
  Serial.println("-- WiFi --");
  Serial.println("scan                 List networks");
  Serial.println("join <ssid> <pass>   Switch network");
  Serial.println("hostname <name>      Set http://name.local/");
  Serial.println("wifi                 Open setup AP");
  Serial.println("wifireset            Forget WiFi and reboot");
  Serial.println("-- MQTT / backup --");
  Serial.println("mqtt on|off          Enable broker");
  Serial.println("mqtt host|port|user|pass|prefix <v>");
  Serial.println("export               Dump settings JSON");
  Serial.println("import {json}        Restore settings");
  Serial.println("ota                  ArduinoOTA status");
  Serial.println("-- Other --");
  Serial.println("test                 LED walk");
  Serial.println("fill                 All white (power test)");
  Serial.println("reboot               Restart");
  Serial.println("help                 This menu");
  Serial.println("==================================");
}

void printStatus() {
  int h = 0, m = 0, s = 0;
  const bool ok = readClock(&h, &m, &s);
  Serial.println("--- status ---");
  Serial.printf("Firmware: %s  %s  GPIO DIN %u  button %u\n",
                config::kFirmwareVersion, config::kBoardName,
                static_cast<unsigned>(config::kLedPin),
                static_cast<unsigned>(config::kButtonPin));
  Serial.printf("LED pin: GPIO %d  count %u  order %s  colmaj %s\n",
                static_cast<int>(config::kLedPin), config::kLedCount,
                s_rgb_order ? "RGB" : "GRB", s_col_major ? "on" : "off");
  Serial.printf("Bright time %u  effect %s %u  mirror X %s Y %s  zigzag %s  plate %s\n",
                s_brightness, secondsName(s_sec_fx), s_fx_bright[s_sec_fx],
                s_flip_x ? "on" : "off", s_flip_y ? "on" : "off",
                s_zigzag ? "on" : "off", face::plateId());
  Serial.printf("Color: %s  #%02X%02X%02X  mdots %s\n",
                colorModeName(s_color_mode), s_cr, s_cg, s_cb,
                s_min_dots ? "on" : "off");
  {
    char t1[8];
    char t2[8];
    fmtHhMm(s_night_start, t1, sizeof(t1));
    fmtHhMm(s_night_end, t2, sizeof(t2));
    Serial.printf("Night: %s  %s -> %s  bright %u  color %s  now %s\n",
                  s_night_on ? "on" : "off", t1, t2, s_night_bright,
                  nightColorModeName(s_night_cmode),
                  s_night_now ? "NIGHT" : "day");
    fmtHhMm(s_sleep_start, t1, sizeof(t1));
    fmtHhMm(s_sleep_end, t2, sizeof(t2));
    Serial.printf("Sleep: %s  %s -> %s  now %s\n", s_sleep_on ? "on" : "off", t1,
                  t2, s_sleep_now ? "DARK" : "lit");
    fmtHhMm(s_alarm_min, t1, sizeof(t1));
    Serial.printf("Alarm: %s  %s  %u s%s\n", s_alarm_on ? "on" : "off", t1,
                  s_alarm_len, s_alarm_ringing ? "  RINGING" : "");
  }
  if (ok) {
    Serial.printf("Time: %02d:%02d:%02d%s\n", h, m, s,
                  s_override_h >= 0 ? " (override)" : "");
  } else {
    Serial.println("Time: not set — type  set 14:35  or  fetch");
  }
  Serial.printf("NTP: %s\n", s_ntp);
  Serial.printf("TZ:  %s  (%s)\n", s_tz, timezoneIdFor(s_tz));
  {
    const time_t now = time(nullptr);
    struct tm gt {};
    gmtime_r(&now, &gt);
    char zone[24];
    zoneLabel(zone, sizeof(zone));
    Serial.printf("Zone: %s  (UTC now %02d:%02d)\n", zone, gt.tm_hour, gt.tm_min);
  }
  Serial.printf("Host: %s.local\n", s_hostname);
  Serial.printf("MQTT: %s  %s:%u  %s/#\n", s_mqtt_status,
                s_mqtt_host[0] ? s_mqtt_host : "-", s_mqtt_port,
                s_mqtt_prefix[0] ? s_mqtt_prefix : config::kDefaultMqttPrefix);
  Serial.printf("OTA: %s  last NTP %s\n", s_ota_ready ? "ready" : "idle",
                s_time_ok ? "ok" : "waiting");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi: %s  http://%s  http://%s.local\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), s_hostname);
  } else if (s_joining) {
    Serial.printf("WiFi: joining %s ...\n", s_join_ssid);
  } else if (s_portal_open) {
    Serial.printf("WiFi: setup AP %s  http://192.168.4.1\n", config::kApName);
  } else {
    Serial.println("WiFi: down");
  }
  Serial.println("--------------");
}

face::Word wordFromName(const char* s) {
  if (strcmp(s, "twenty") == 0) return face::Word::kTwenty;
  if (strcmp(s, "quarter") == 0) return face::Word::kQuarter;
  if (strcmp(s, "fivemin") == 0 || strcmp(s, "five_min") == 0) {
    return face::Word::kFiveMin;
  }
  if (strcmp(s, "half") == 0) return face::Word::kHalf;
  if (strcmp(s, "past") == 0) return face::Word::kPast;
  if (strcmp(s, "to") == 0) return face::Word::kTo;
  if (strcmp(s, "one") == 0) return face::Word::kOne;
  if (strcmp(s, "two") == 0) return face::Word::kTwo;
  if (strcmp(s, "three") == 0) return face::Word::kThree;
  if (strcmp(s, "four") == 0) return face::Word::kFour;
  if (strcmp(s, "five") == 0 || strcmp(s, "fivehour") == 0) {
    return face::Word::kFiveHour;
  }
  if (strcmp(s, "six") == 0) return face::Word::kSix;
  if (strcmp(s, "seven") == 0) return face::Word::kSeven;
  if (strcmp(s, "eight") == 0) return face::Word::kEight;
  if (strcmp(s, "nine") == 0) return face::Word::kNine;
  if (strcmp(s, "ten") == 0) return face::Word::kTenHour;
  if (strcmp(s, "eleven") == 0) return face::Word::kEleven;
  if (strcmp(s, "twelve") == 0) return face::Word::kTwelve;
  return face::Word::kCount;
}

void lowerInPlace(char* s) {
  for (; s && *s; ++s) {
    if (*s >= 'A' && *s <= 'Z') {
      *s = static_cast<char>(*s + ('a' - 'A'));
    }
  }
}

void handleLine(char* line) {
  char* cmd = strtok(line, " \t");
  if (cmd == nullptr) {
    return;
  }
  lowerInPlace(cmd);

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
    return;
  }
  if (strcmp(cmd, "status") == 0) {
    printStatus();
    return;
  }
  if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "restart") == 0) {
    Serial.println("Rebooting...");
    delay(150);
    ESP.restart();
    return;
  }
  if (strcmp(cmd, "reset") == 0 || strcmp(cmd, "wifireset") == 0) {
    Serial.println("Clearing WiFi, rebooting to setup AP...");
    s_wm.resetSettings();
    delay(200);
    ESP.restart();
    return;
  }
  if (strcmp(cmd, "wifi") == 0) {
    startWifi();
    openPortal();
    return;
  }
  if (strcmp(cmd, "scan") == 0) {
    Serial.println("Scanning...");
    const int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
      Serial.printf("  %-32s %4d dBm  %s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "locked");
    }
    WiFi.scanDelete();
    Serial.printf("%d networks. Join with:  join <ssid> <password>\n", n);
    return;
  }
  if (strcmp(cmd, "join") == 0) {
    char* ssid = strtok(nullptr, " \t");
    char* pass = strtok(nullptr, "");
    if (!ssid || !ssid[0]) {
      Serial.println("ERR join <ssid> <password>");
      return;
    }
    strncpy(s_join_ssid, ssid, sizeof(s_join_ssid) - 1);
    s_join_pass[0] = '\0';
    if (pass) {
      while (*pass == ' ') {
        ++pass;
      }
      strncpy(s_join_pass, pass, sizeof(s_join_pass) - 1);
    }
    s_join_pending = true;
    Serial.printf("OK switching to %s\n", s_join_ssid);
    return;
  }
  if (strcmp(cmd, "night") == 0) {
    char* a = strtok(nullptr, " \t");
    char* b = strtok(nullptr, " \t");
    if (a && strcmp(a, "on") == 0) {
      s_night_on = true;
    } else if (a && strcmp(a, "off") == 0) {
      s_night_on = false;
    } else if (a && b) {
      uint16_t s = 0;
      uint16_t e = 0;
      if (!parseHhMm(a, &s) || !parseHhMm(b, &e)) {
        Serial.println("ERR night on|off | night HH:MM HH:MM");
        return;
      }
      s_night_start = s;
      s_night_end = e;
      s_night_on = true;
    }
    saveLookPrefs();
    refreshFace();
    char t1[8];
    char t2[8];
    fmtHhMm(s_night_start, t1, sizeof(t1));
    fmtHhMm(s_night_end, t2, sizeof(t2));
    Serial.printf("night %s  %s -> %s  bright %u  (now %s)\n",
                  s_night_on ? "on" : "off", t1, t2, s_night_bright,
                  s_night_now ? "night" : "day");
    return;
  }
  if (strcmp(cmd, "nbright") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      s_night_bright = clampBright(atoi(v));
      saveLookPrefs();
      refreshFace();
    }
    Serial.printf("nbright %u\n", s_night_bright);
    return;
  }
  if (strcmp(cmd, "ncolor") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      lowerInPlace(v);
      if (strcmp(v, "same") == 0) {
        s_night_cmode = 4;
      } else if (strcmp(v, "warm") == 0) {
        s_night_cmode = 3;
      } else if (strcmp(v, "solid") == 0) {
        s_night_cmode = 1;
      } else if (strcmp(v, "hour") == 0 || strcmp(v, "hue") == 0) {
        s_night_cmode = 0;
      } else if (strcmp(v, "rainbow") == 0) {
        s_night_cmode = 2;
      } else {
        Serial.println("ERR ncolor same|warm|solid|hour|rainbow");
        return;
      }
      saveLookPrefs();
      refreshFace();
    }
    Serial.printf("ncolor %s\n", nightColorModeName(s_night_cmode));
    return;
  }
  if (strcmp(cmd, "sleep") == 0 || strcmp(cmd, "blackout") == 0) {
    char* a = strtok(nullptr, " \t");
    char* b = strtok(nullptr, " \t");
    if (a && strcmp(a, "on") == 0) {
      s_sleep_on = true;
    } else if (a && strcmp(a, "off") == 0) {
      s_sleep_on = false;
    } else if (a && b) {
      uint16_t s = 0;
      uint16_t e = 0;
      if (!parseHhMm(a, &s) || !parseHhMm(b, &e)) {
        Serial.println("ERR sleep on|off | sleep HH:MM HH:MM");
        return;
      }
      s_sleep_start = s;
      s_sleep_end = e;
      s_sleep_on = true;
    }
    saveLookPrefs();
    refreshFace();
    char t1[8];
    char t2[8];
    fmtHhMm(s_sleep_start, t1, sizeof(t1));
    fmtHhMm(s_sleep_end, t2, sizeof(t2));
    Serial.printf("sleep %s  %s -> %s  (now %s)\n", s_sleep_on ? "on" : "off", t1,
                  t2, s_sleep_now ? "dark" : "lit");
    return;
  }
  if (strcmp(cmd, "alarm") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v && strcmp(v, "stop") == 0) {
      stopAlarm();
      refreshFace();
      return;
    }
    if (v && strcmp(v, "test") == 0) {
      startAlarm(s_alarm_len);
      refreshFace();
      return;
    }
    if (v && strcmp(v, "on") == 0) {
      s_alarm_on = true;
    } else if (v && strcmp(v, "off") == 0) {
      s_alarm_on = false;
    } else if (v) {
      uint16_t t = 0;
      if (!parseHhMm(v, &t)) {
        Serial.println("ERR alarm HH:MM | on | off | test | stop");
        return;
      }
      s_alarm_min = t;
      s_alarm_on = true;
      s_alarm_fired_day = -1;
      s_alarm_fired_min = -1;
    }
    saveLookPrefs();
    char t1[8];
    fmtHhMm(s_alarm_min, t1, sizeof(t1));
    Serial.printf("alarm %s  %s  rings %u s\n", s_alarm_on ? "on" : "off", t1,
                  s_alarm_len);
    return;
  }
  if (strcmp(cmd, "alen") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      int len = atoi(v);
      if (len < 5) {
        len = 5;
      }
      if (len > 180) {
        len = 180;
      }
      s_alarm_len = static_cast<uint8_t>(len);
      saveLookPrefs();
    }
    Serial.printf("alen %u\n", s_alarm_len);
    return;
  }
  if (strcmp(cmd, "hostname") == 0 || strcmp(cmd, "host") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v && v[0]) {
      sanitizeHostname(s_hostname, sizeof(s_hostname), v);
      saveWifiPrefs();
      compat::setHostname(s_hostname);
      restartMdns();
    }
    Serial.printf("Host: %s.local  (reboot to update the DHCP name)\n", s_hostname);
    return;
  }
  if (strcmp(cmd, "fetch") == 0 || strcmp(cmd, "ntp") == 0) {
    char* host = strtok(nullptr, " \t");
    if (host && host[0] && strcmp(cmd, "ntp") == 0) {
      strncpy(s_ntp, host, sizeof(s_ntp) - 1);
      saveNtpPrefs();
    }
    syncNtp();
    return;
  }
  if (strcmp(cmd, "tzlist") == 0) {
    Serial.println("Timezone presets (tz <id> or full POSIX string):");
    for (const auto& z : kTimezones) {
      Serial.printf("  %-4s  %s\n    %s\n", z.id, z.label, z.posix);
    }
    return;
  }
  if (strcmp(cmd, "tz") == 0) {
    char* v = strtok(nullptr, "");
    if (v) {
      while (*v == ' ') {
        ++v;
      }
    }
    if (v && v[0]) {
      char idbuf[12];
      size_t i = 0;
      while (v[i] && v[i] != ' ' && v[i] != ',' && i + 1 < sizeof(idbuf)) {
        idbuf[i] = v[i];
        ++i;
      }
      idbuf[i] = '\0';
      lowerInPlace(idbuf);
      const char* posix = timezonePosix(idbuf);
      if (posix && strchr(v, ',') == nullptr) {
        strncpy(s_tz, posix, sizeof(s_tz) - 1);
      } else {
        strncpy(s_tz, v, sizeof(s_tz) - 1);
      }
      s_tz[sizeof(s_tz) - 1] = '\0';
      saveNtpPrefs();
      applyTimezone();
      refreshFace();
    }
    const time_t now = time(nullptr);
    struct tm lt {};
    localtime_r(&now, &lt);
    char zone[24];
    zoneLabel(zone, sizeof(zone));
    Serial.printf("TZ: %s  (%s)  ->  %02d:%02d  %s\n", s_tz,
                  timezoneIdFor(s_tz), lt.tm_hour, lt.tm_min, zone);
    return;
  }
  if (strcmp(cmd, "face") == 0) {
    face::printFace();
    return;
  }
  if (strcmp(cmd, "live") == 0) {
    s_override_h = -1;
    s_override_m = -1;
    clearPreview();
    refreshFace();
    Serial.println("OK live clock");
    return;
  }
  if (strcmp(cmd, "test") == 0) {
    runTest();
    return;
  }
  if (strcmp(cmd, "fill") == 0) {
    fill(70, 70, 70);
    Serial.println("OK fill white — if this is dark, check 5V, GND, and G2 DIN");
    return;
  }
  if (strcmp(cmd, "row") == 0) {
    char* v = strtok(nullptr, " \t");
    if (!v) {
      Serial.println("ERR row <0-7>");
      return;
    }
    clearPreview();
    s_preview_row = static_cast<uint8_t>(atoi(v));
    refreshFace();
    return;
  }
  if (strcmp(cmd, "word") == 0) {
    char* v = strtok(nullptr, " \t");
    if (!v) {
      Serial.println("ERR word <name>");
      return;
    }
    lowerInPlace(v);
    const face::Word w = wordFromName(v);
    if (w == face::Word::kCount) {
      Serial.println("ERR unknown word");
      return;
    }
    clearPreview();
    s_preview_word = w;
    s_word_preview = true;
    refreshFace();
    face::printWordPixels(w);
    return;
  }
  if (strcmp(cmd, "set") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v && (strcmp(v, "time") == 0 || strcmp(v, "clock") == 0)) {
      v = strtok(nullptr, " \t");
    }
    int h = 0, m = 0;
    if (!v || sscanf(v, "%d:%d", &h, &m) != 2) {
      Serial.println("ERR set HH:MM");
      return;
    }
    if (setManualTime(h, m)) {
      Serial.printf("OK time %02d:%02d\n", h, m);
    } else {
      Serial.println("ERR invalid time");
    }
    return;
  }
  if (strcmp(cmd, "zigzag") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      s_zigzag = strcmp(v, "on") == 0 || strcmp(v, "1") == 0;
      saveLookPrefs();
    }
    Serial.printf("zigzag %s\n", s_zigzag ? "on" : "off");
    refreshFace();
    return;
  }
  if (strcmp(cmd, "flipx") == 0 || strcmp(cmd, "mirrorx") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      s_flip_x = strcmp(v, "on") == 0 || strcmp(v, "1") == 0;
      saveLookPrefs();
    }
    Serial.printf("flipx %s\n", s_flip_x ? "on" : "off");
    refreshFace();
    return;
  }
  if (strcmp(cmd, "flipy") == 0 || strcmp(cmd, "mirrory") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      s_flip_y = strcmp(v, "on") == 0 || strcmp(v, "1") == 0;
      saveLookPrefs();
    }
    Serial.printf("flipy %s\n", s_flip_y ? "on" : "off");
    refreshFace();
    return;
  }
  if (strcmp(cmd, "colmaj") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      s_col_major = strcmp(v, "on") == 0 || strcmp(v, "1") == 0;
      saveLookPrefs();
    }
    Serial.printf("colmaj %s\n", s_col_major ? "on" : "off");
    refreshFace();
    return;
  }
  if (strcmp(cmd, "align") == 0) {
    char* v = strtok(nullptr, " \t");
    if (!v) {
      Serial.println("ERR align plain|mirrorxz|zigzag|mirrorx");
      return;
    }
    lowerInPlace(v);
    if (strcmp(v, "plain") == 0) {
      s_flip_x = false;
      s_flip_y = false;
      s_zigzag = false;
      s_col_major = false;
    } else if (strcmp(v, "mirrorxz") == 0) {
      s_flip_x = true;
      s_flip_y = false;
      s_zigzag = true;
      s_col_major = false;
    } else if (strcmp(v, "zigzag") == 0) {
      s_flip_x = false;
      s_flip_y = false;
      s_zigzag = true;
      s_col_major = false;
    } else if (strcmp(v, "mirrorx") == 0) {
      s_flip_x = true;
      s_flip_y = false;
      s_zigzag = false;
      s_col_major = false;
    } else {
      Serial.println("ERR align plain|mirrorxz|zigzag|mirrorx");
      return;
    }
    saveLookPrefs();
    Serial.printf("align %s  flipx %s  flipy %s  zigzag %s  colmaj %s\n", v,
                  s_flip_x ? "on" : "off", s_flip_y ? "on" : "off",
                  s_zigzag ? "on" : "off", s_col_major ? "on" : "off");
    refreshFace();
    return;
  }
  if (strcmp(cmd, "order") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      lowerInPlace(v);
      s_rgb_order = strcmp(v, "rgb") == 0;
      s_prefs.begin(kPrefsNs, false);
      s_prefs.putBool("rgb", s_rgb_order);
      s_prefs.end();
      beginLeds();
    }
    Serial.printf("order %s\n", s_rgb_order ? "RGB" : "GRB");
    fill(70, 70, 70);
    delay(200);
    refreshFace();
    return;
  }
  if (strcmp(cmd, "color") == 0 || strcmp(cmd, "colour") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      lowerInPlace(v);
      if (strcmp(v, "hour") == 0 || strcmp(v, "hue") == 0) {
        s_color_mode = 0;
      } else if (strcmp(v, "solid") == 0) {
        s_color_mode = 1;
      } else if (strcmp(v, "rainbow") == 0) {
        s_color_mode = 2;
      } else if (strcmp(v, "warm") == 0) {
        s_color_mode = 3;
      } else {
        Serial.println("ERR color hour|solid|rainbow|warm");
        return;
      }
      saveLookPrefs();
    }
    Serial.printf("color %s\n", colorModeName(s_color_mode));
    refreshFace();
    return;
  }
  if (strcmp(cmd, "rgb") == 0) {
    char* r = strtok(nullptr, " \t");
    char* g = strtok(nullptr, " \t");
    char* b = strtok(nullptr, " \t");
    if (!r || !g || !b) {
      Serial.println("ERR rgb <r> <g> <b>");
      return;
    }
    int rv = atoi(r), gv = atoi(g), bv = atoi(b);
    if (rv < 0) rv = 0;
    if (gv < 0) gv = 0;
    if (bv < 0) bv = 0;
    if (rv > 255) rv = 255;
    if (gv > 255) gv = 255;
    if (bv > 255) bv = 255;
    s_cr = static_cast<uint8_t>(rv);
    s_cg = static_cast<uint8_t>(gv);
    s_cb = static_cast<uint8_t>(bv);
    s_color_mode = 1;
    saveLookPrefs();
    Serial.printf("color solid #%02X%02X%02X\n", s_cr, s_cg, s_cb);
    refreshFace();
    return;
  }
  if (strcmp(cmd, "effect") == 0 || strcmp(cmd, "seconds") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      lowerInPlace(v);
      if (strcmp(v, "off") == 0 || strcmp(v, "none") == 0) {
        s_sec_fx = 0;
      } else if (strcmp(v, "pulse") == 0) {
        s_sec_fx = 1;
      } else if (strcmp(v, "trail") == 0) {
        s_sec_fx = 2;
      } else if (strcmp(v, "tick") == 0) {
        s_sec_fx = 3;
      } else if (strcmp(v, "sparkle") == 0) {
        s_sec_fx = 4;
      } else if (strcmp(v, "star") == 0 || strcmp(v, "*") == 0 ||
                 strcmp(v, "blink") == 0) {
        s_sec_fx = 5;
      } else {
        Serial.println("ERR effect off|star|pulse|trail|tick|sparkle");
        return;
      }
      saveLookPrefs();
    }
    Serial.printf("effect %s\n", secondsName(s_sec_fx));
    refreshFace();
    return;
  }
  if (strcmp(cmd, "fxbright") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      s_fx_bright[s_sec_fx] = clampBright(atoi(v));
      saveLookPrefs();
    }
    Serial.printf("fxbright %s %u\n", secondsName(s_sec_fx),
                  s_fx_bright[s_sec_fx]);
    refreshFace();
    return;
  }
  if (strcmp(cmd, "plate") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v && face::setPlateId(v)) {
      saveLookPrefs();
    }
    Serial.printf("plate %s\n", face::plateId());
    face::printFace();
    refreshFace();
    return;
  }
  if (strcmp(cmd, "cal") == 0) {
    char* v = strtok(nullptr, " \t");
    clearPreview();
    if (!v || strcmp(v, "off") == 0 || strcmp(v, "clock") == 0) {
      s_cal_mode = 0;
    } else if (strcmp(v, "corners") == 0) {
      s_cal_mode = 1;
    } else if (strcmp(v, "led0") == 0) {
      s_cal_mode = 2;
    } else if (strcmp(v, "led63") == 0) {
      s_cal_mode = 3;
    } else if (strcmp(v, "11") == 0) {
      s_cal_mode = 4;
    } else if (strcmp(v, "18") == 0) {
      s_cal_mode = 6;
    } else if (strcmp(v, "81") == 0) {
      s_cal_mode = 7;
    } else if (strcmp(v, "88") == 0) {
      s_cal_mode = 5;
    } else {
      Serial.println("ERR cal off|corners|led0|led63|11|18|81|88");
      return;
    }
    refreshFace();
    Serial.printf("cal %u\n", s_cal_mode);
    return;
  }
  if (strcmp(cmd, "mdots") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      s_min_dots = strcmp(v, "on") == 0 || strcmp(v, "1") == 0;
      saveLookPrefs();
      refreshFace();
    }
    Serial.printf("mdots %s\n", s_min_dots ? "on" : "off");
    return;
  }
  if (strcmp(cmd, "letters") == 0) {
    char* v = strtok(nullptr, "");
    if (v) {
      while (*v == ' ') {
        ++v;
      }
    }
    if (v && v[0]) {
      for (char* p = v; *p; ++p) {
        if (*p == '|') {
          *p = '\n';
        }
      }
      face::setCustomRows(v);
      saveLookPrefs();
    }
    face::printFace();
    refreshFace();
    return;
  }
  if (strcmp(cmd, "rowset") == 0) {
    char* n = strtok(nullptr, " \t");
    char* text = strtok(nullptr, "");
    if (!n || !text) {
      Serial.println("ERR rowset <0-7> <letters>");
      return;
    }
    const int y = atoi(n);
    if (y < 0 || y > 7) {
      Serial.println("ERR row 0-7");
      return;
    }
    char rows[80] = {};
    face::copyRowsText(rows, sizeof(rows));
    // Replace one line.
    char rebuilt[80] = {};
    size_t w = 0;
    int row = 0;
    const char* p = rows;
    while (row < 8 && w + 10 < sizeof(rebuilt)) {
      if (row) {
        rebuilt[w++] = '\n';
      }
      if (row == y) {
        while (*text == ' ') {
          ++text;
        }
        for (int x = 0; x < 8 && text[x] && text[x] != '\n'; ++x) {
          char c = text[x];
          if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - ('a' - 'A'));
          }
          rebuilt[w++] = c;
        }
        while (*p && *p != '\n') {
          ++p;
        }
        if (*p == '\n') {
          ++p;
        }
      } else {
        while (*p && *p != '\n' && w + 1 < sizeof(rebuilt)) {
          rebuilt[w++] = *p++;
        }
        if (*p == '\n') {
          ++p;
        }
      }
      ++row;
    }
    rebuilt[w] = '\0';
    face::setCustomRows(rebuilt);
    saveLookPrefs();
    face::printFace();
    refreshFace();
    return;
  }
  if (strcmp(cmd, "mqtt") == 0) {
    char* a = strtok(nullptr, " \t");
    char* b = strtok(nullptr, "");
    if (a && (strcmp(a, "on") == 0 || strcmp(a, "off") == 0)) {
      s_mqtt_on = strcmp(a, "on") == 0;
    } else if (a && b) {
      while (*b == ' ') {
        ++b;
      }
      if (strcmp(a, "host") == 0) {
        strncpy(s_mqtt_host, b, sizeof(s_mqtt_host) - 1);
      } else if (strcmp(a, "port") == 0) {
        int p = atoi(b);
        s_mqtt_port = p > 0 ? static_cast<uint16_t>(p) : config::kDefaultMqttPort;
      } else if (strcmp(a, "user") == 0) {
        strncpy(s_mqtt_user, b, sizeof(s_mqtt_user) - 1);
      } else if (strcmp(a, "pass") == 0) {
        strncpy(s_mqtt_pass, b, sizeof(s_mqtt_pass) - 1);
      } else if (strcmp(a, "prefix") == 0) {
        strncpy(s_mqtt_prefix, b, sizeof(s_mqtt_prefix) - 1);
      } else {
        Serial.println("ERR mqtt on|off|host|port|user|pass|prefix");
        return;
      }
    }
    saveMqttPrefs();
    if (s_mqtt.connected()) {
      s_mqtt.disconnect();
    }
    Serial.printf("mqtt %s  %s:%u  %s  user %s\n", s_mqtt_on ? "on" : "off",
                  s_mqtt_host[0] ? s_mqtt_host : "-", s_mqtt_port,
                  s_mqtt_prefix, s_mqtt_user[0] ? s_mqtt_user : "-");
    return;
  }
  if (strcmp(cmd, "export") == 0) {
    JsonDocument doc;
    doc["fw"] = config::kFirmwareVersion;
    doc["host"] = s_hostname;
    doc["tz"] = s_tz;
    doc["ntp"] = s_ntp;
    doc["bright"] = s_brightness;
    doc["cmode"] = s_color_mode;
    doc["mdots"] = s_min_dots;
    doc["plate"] = face::plateId();
    char letters[80] = {};
    face::copyRowsText(letters, sizeof(letters));
    doc["letters"] = letters;
    doc["mqtt_on"] = s_mqtt_on;
    doc["mqtt_host"] = s_mqtt_host;
    doc["mqtt_port"] = s_mqtt_port;
    doc["mqtt_prefix"] = s_mqtt_prefix;
    serializeJsonPretty(doc, Serial);
    Serial.println();
    Serial.println("Full dump: GET /api/settings");
    return;
  }
  if (strcmp(cmd, "import") == 0) {
    char* v = strtok(nullptr, "");
    if (!v || v[0] == '\0') {
      Serial.println("ERR import {json}");
      return;
    }
    while (*v == ' ') {
      ++v;
    }
    String err;
    if (applySettingsJson(v, &err)) {
      Serial.println("OK settings imported");
    } else {
      Serial.printf("ERR %s\n", err.c_str());
    }
    return;
  }
  if (strcmp(cmd, "ota") == 0) {
    Serial.printf("OTA %s  hostname %s.local\n",
                  s_ota_ready ? "ready" : "not started (need WiFi)",
                  s_hostname);
    return;
  }
  if (strcmp(cmd, "bright") == 0) {
    char* v = strtok(nullptr, " \t");
    if (v) {
      int b = atoi(v);
      if (b < 1) b = 1;
      if (b > config::kMaxBrightness) b = config::kMaxBrightness;
      s_brightness = static_cast<uint8_t>(b);
      s_prefs.begin(kPrefsNs, false);
      s_prefs.putUChar("bright", s_brightness);
      s_prefs.end();
      applyBrightness();
    }
    Serial.printf("bright %u\n", s_brightness);
    refreshFace();
    return;
  }
  Serial.println("ERR unknown — type help");
}

void pollSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      Serial.println();
      s_line[s_line_len] = '\0';
      if (s_line_len > 0) {
        handleLine(s_line);
      }
      s_line_len = 0;
      Serial.print("> ");
      continue;
    }
    if (c == 0x08 || c == 0x7f) {
      if (s_line_len > 0) {
        --s_line_len;
        Serial.print("\b \b");
      }
      continue;
    }
    if (c < 32) {
      continue;
    }
    if (s_line_len + 1 < sizeof(s_line)) {
      s_line[s_line_len++] = c;
      Serial.print(c);
    }
  }
}

void cycleBrightness() {
  size_t idx = 0;
  for (size_t i = 0; i < sizeof(kBrightSteps); ++i) {
    if (kBrightSteps[i] >= s_brightness) {
      idx = (i + 1) % sizeof(kBrightSteps);
      break;
    }
  }
  s_brightness = kBrightSteps[idx];
  s_prefs.begin(kPrefsNs, false);
  s_prefs.putUChar("bright", s_brightness);
  s_prefs.end();
  applyBrightness();
  refreshFace();
  Serial.printf("bright %u\n", s_brightness);
}

void pollButton() {
  const bool down = digitalRead(config::kButtonPin) == LOW;
  const unsigned long now = millis();
  if (down && !s_btn_was_down) {
    s_btn_down_ms = now;
  }
  if (!down && s_btn_was_down) {
    const unsigned long held = now - s_btn_down_ms;
    if (s_alarm_ringing) {
      stopAlarm();  // any press silences the alarm
      refreshFace();
    } else if (held >= 40 && held < 1500) {
      cycleBrightness();
    } else if (held >= 1500) {
      runTest();
    }
  }
  s_btn_was_down = down;
}

}  // namespace

void setup() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setRxBufferSize(1024);
#endif
  Serial.begin(115200);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(500);
  Serial.println();
  Serial.println("========================================");
  Serial.printf("ESP-WordClock8x8  %s  DIN GPIO %d  %s\n",
                config::kBoardName, static_cast<int>(config::kLedPin),
                config::kFirmwareVersion);
  Serial.println("Type help  |  if the matrix is dark: fill");
  Serial.println("========================================");

  pinMode(config::kButtonPin, INPUT_PULLUP);
  face::init();
  loadPrefs();
  beginLeds();
  bootFlash();
  face::printFace();
  printHelp();

  applyTimezone();
  Serial.println("Serial is live. Type help and press Enter.");
  Serial.print("> ");
  refreshFace();
}

void loop() {
  pollSerial();
  pollButton();
  if (!s_wifi_started && millis() > 2500) {
    startWifi();
  }
  if (s_wifi_started) {
    s_wm.process();
    serviceWifiJoin();
  }
  if (s_wifi_started && !s_portal_open && !s_joining && !s_join_pending &&
      WiFi.status() != WL_CONNECTED && millis() > s_portal_grace_ms) {
    Serial.printf("\nWiFi: no join — AP %s  http://192.168.4.1\n> ",
                  config::kApName);
    openPortal();
  }

  if (s_portal_open) {
    bindPortalRoutes();
    if (wifiHasIp()) {
      if (s_portal_got_ip_ms == 0) {
        s_portal_got_ip_ms = millis();
        startMdns();
        Serial.printf("\nWiFi: %s  dashboard http://%s  http://%s.local\n> ",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                      s_hostname);
      } else if (millis() - s_portal_got_ip_ms >= config::kPortalHoldAfterIpMs) {
        closePortalForLan();
      }
    }
  }

  if (wifiHasIp()) {
    if (s_portal_open && !s_wm.getConfigPortalActive()) {
      s_portal_open = false;
      s_portal_routes = false;
    }
    static bool s_announced = false;
    if (!s_announced) {
      s_announced = true;
      startMdns();
      Serial.printf("\nWiFi: %s  http://%s  http://%s.local\n> ",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                    s_hostname);
    }
    if (!s_portal_open) {
      startWeb();
    }
    if (s_web_started) {
      s_http.handleClient();
    }
    serviceOta();
    mqttLoop();
    compat::mdnsUpdate();
    if (!s_time_ok && millis() - s_last_ntp_try > config::kNtpRetryMs) {
      syncNtp();
    }
  } else if (s_portal_open) {
    showWait(s_leds.Color(0, 40, 80));
  }

  if (millis() - s_last_show >= showIntervalMs()) {
    s_last_show = millis();
    refreshFace();
  }
}

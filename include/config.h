#pragma once

#include <cstdint>

namespace config {

constexpr char kFirmwareVersion[] = "0.8.1";
constexpr char kApName[] = "WordClock8x8";
/** Default only — the live name is stored in preferences and set from the UI. */
constexpr char kHostname[] = "wordclock8x8";
constexpr char kAuthor[] = "Markus Van Kempen";
constexpr char kContactEmail[] = "markus.van.kempen@gmail.com";
constexpr unsigned long kPortalHoldAfterIpMs = 25000;

#ifdef ESP8266
/** D2 / GPIO4 — avoid GPIO2 (often the onboard LED on ESP8266 boards). */
constexpr uint8_t kLedPin = 4;
/** FLASH / GPIO0 — hold-to-flash button doubles as the face button. */
constexpr uint8_t kButtonPin = 0;
constexpr char kBoardName[] = "ESP8266";
#else
#ifdef WC_LED_PIN
constexpr uint8_t kLedPin = WC_LED_PIN;
#else
/** WS2812 DIN on the Atom S3 Lite Grove port (white / G2). */
constexpr uint8_t kLedPin = 2;
#endif
#ifdef WC_BUTTON_PIN
constexpr uint8_t kButtonPin = WC_BUTTON_PIN;
#else
constexpr uint8_t kButtonPin = 41;
#endif
#ifdef WC_BOARD_NAME
constexpr char kBoardName[] = WC_BOARD_NAME;
#else
constexpr char kBoardName[] = "M5 Atom S3 Lite";
#endif
#endif

constexpr uint16_t kLedCount = 64;
constexpr uint8_t kMatrixW = 8;
constexpr uint8_t kMatrixH = 8;

/** Keep this low on USB power — a full-white 8×8 can draw amps. */
constexpr uint8_t kDefaultBrightness = 40;
constexpr uint8_t kMaxBrightness = 80;

/**
 * Atlantic Time with DST. Change if the clock lives elsewhere.
 * POSIX: std offset dst [offset],start[/time],end[/time]
 */
constexpr char kPosixTz[] = "AST4ADT,M3.2.0/2,M11.1.0/2";
constexpr char kNtpServer[] = "pool.ntp.org";

constexpr unsigned long kShowIntervalMs = 1000;
constexpr unsigned long kNtpRetryMs = 30000;
constexpr unsigned long kMqttPublishMs = 30000;

constexpr char kDefaultMqttPrefix[] = "wordclock";
constexpr uint16_t kDefaultMqttPort = 1883;

}  // namespace config

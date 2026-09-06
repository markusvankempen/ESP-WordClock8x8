#include "face.h"

#include <Arduino.h>
#include <cstring>

namespace face {
namespace {

char s_custom[8][9] = {};
Plate s_plate = Plate::kHome;

constexpr char kHomeRows[8][9] = {
    "TWENTYH*", "QUARTERM", "FIVEHALF", "SETPASTO",
    "FIVEIGHT", "SIXTHREE", "TWELEVEN", "FOURNINE",
};

constexpr char kDoroRows[8][9] = {
    "ATWENTYD", "QUARTERY", "FIVEHALF", "DPASTORO",
    "FIVEIGHT", "SIXTHREE", "TWELEVEN", "FOURNINE",
};

constexpr Pixel kHomeTwenty[] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}};
constexpr Pixel kHomeQuarter[] = {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}};
constexpr Pixel kHomeFiveMin[] = {{0, 2}, {1, 2}, {2, 2}, {3, 2}};
constexpr Pixel kHomeHalf[] = {{4, 2}, {5, 2}, {6, 2}, {7, 2}};
constexpr Pixel kHomePast[] = {{3, 3}, {4, 3}, {5, 3}, {6, 3}};
constexpr Pixel kHomeTo[] = {{6, 3}, {7, 3}};
constexpr Pixel kHomeFiveHr[] = {{0, 4}, {1, 4}, {2, 4}, {3, 4}};
constexpr Pixel kHomeEight[] = {{3, 4}, {4, 4}, {5, 4}, {6, 4}, {7, 4}};
constexpr Pixel kHomeSix[] = {{0, 5}, {1, 5}, {2, 5}};
constexpr Pixel kHomeThree[] = {{3, 5}, {4, 5}, {5, 5}, {6, 5}, {7, 5}};
constexpr Pixel kHomeTwelve[] = {{0, 6}, {1, 6}, {2, 6}, {3, 6}, {5, 6}, {6, 6}};
constexpr Pixel kHomeEleven[] = {{2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {7, 6}};
constexpr Pixel kHomeTwo[] = {{0, 6}, {1, 6}, {1, 7}};
constexpr Pixel kHomeSeven[] = {{0, 5}, {4, 6}, {5, 6}, {6, 6}, {7, 6}};
constexpr Pixel kHomeFour[] = {{0, 7}, {1, 7}, {2, 7}, {3, 7}};
constexpr Pixel kHomeNine[] = {{4, 7}, {5, 7}, {6, 7}, {7, 7}};
constexpr Pixel kHomeOne[] = {{1, 7}, {4, 7}, {7, 7}};
constexpr Pixel kHomeTenHr[] = {{3, 5}, {6, 5}, {7, 6}};
constexpr Pixel kHomeTenMin[] = {{0, 0}, {2, 0}, {3, 0}};

constexpr Pixel kDoroTwenty[] = {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}};
constexpr Pixel kDoroQuarter[] = {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}};
constexpr Pixel kDoroPast[] = {{1, 3}, {2, 3}, {3, 3}, {4, 3}};
constexpr Pixel kDoroTo[] = {{4, 3}, {5, 3}};
constexpr Pixel kDoroTenMin[] = {{1, 0}, {3, 0}, {4, 0}};

struct Entry {
  Word id;
  const char* label;
  const Pixel* home;
  size_t home_n;
  const Pixel* doro;
  size_t doro_n;
};

#define MAP(id, label, home, doro)                                            \
  {                                                                            \
    Word::id, label, home, sizeof(home) / sizeof(home[0]), doro,              \
        sizeof(doro) / sizeof(doro[0])                                        \
  }

constexpr Entry kWords[] = {
    MAP(kTwenty, "TWENTY", kHomeTwenty, kDoroTwenty),
    MAP(kQuarter, "QUARTER", kHomeQuarter, kDoroQuarter),
    MAP(kFiveMin, "FIVE(min)", kHomeFiveMin, kHomeFiveMin),
    MAP(kHalf, "HALF", kHomeHalf, kHomeHalf),
    MAP(kPast, "PAST", kHomePast, kDoroPast),
    MAP(kTo, "TO", kHomeTo, kDoroTo),
    MAP(kOne, "ONE", kHomeOne, kHomeOne),
    MAP(kTwo, "TWO", kHomeTwo, kHomeTwo),
    MAP(kThree, "THREE", kHomeThree, kHomeThree),
    MAP(kFour, "FOUR", kHomeFour, kHomeFour),
    MAP(kFiveHour, "FIVE(hr)", kHomeFiveHr, kHomeFiveHr),
    MAP(kSix, "SIX", kHomeSix, kHomeSix),
    MAP(kSeven, "SEVEN", kHomeSeven, kHomeSeven),
    MAP(kEight, "EIGHT", kHomeEight, kHomeEight),
    MAP(kNine, "NINE", kHomeNine, kHomeNine),
    MAP(kTenHour, "TEN(hr)", kHomeTenHr, kHomeTenHr),
    MAP(kEleven, "ELEVEN", kHomeEleven, kHomeEleven),
    MAP(kTwelve, "TWELVE", kHomeTwelve, kHomeTwelve),
    MAP(kTenMin, "TEN(min)", kHomeTenMin, kDoroTenMin),
};

#undef MAP

Pixel s_scan[8];
size_t s_scan_n = 0;

const char* scanName(Word word);

const char (*rows())[9] {
  if (s_plate == Plate::kDoro) {
    return kDoroRows;
  }
  if (s_plate == Plate::kCustom) {
    return s_custom;
  }
  return kHomeRows;
}

bool fillScanMatch(int x, int y, const char* word) {
  const size_t len = strlen(word);
  if (len == 0 || len > 8) {
    return false;
  }
  s_scan_n = len;
  for (size_t i = 0; i < len; ++i) {
    s_scan[i] = {static_cast<uint8_t>(x + static_cast<int>(i)),
                 static_cast<uint8_t>(y)};
  }
  return true;
}

bool scanWordAt(const char* word, int row_y) {
  const size_t len = strlen(word);
  if (len == 0 || len > 8 || row_y < 0 || row_y > 7) {
    return false;
  }
  const auto grid = rows();
  for (int x = 0; x <= 8 - static_cast<int>(len); ++x) {
    if (strncmp(grid[row_y] + x, word, len) == 0) {
      return fillScanMatch(x, row_y, word);
    }
  }
  return false;
}

bool scanWordOccurrence(const char* word, int want) {
  const size_t len = strlen(word);
  if (len == 0 || len > 8 || want < 1) {
    return false;
  }
  const auto grid = rows();
  int seen = 0;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x <= 8 - static_cast<int>(len); ++x) {
      if (strncmp(grid[y] + x, word, len) == 0) {
        ++seen;
        if (seen == want) {
          return fillScanMatch(x, y, word);
        }
      }
    }
  }
  return false;
}

bool scanWord(const char* word) { return scanWordOccurrence(word, 1); }

bool usesHardcodedMap(Word word) {
  switch (word) {
    case Word::kTenMin:
    case Word::kTenHour:
    case Word::kOne:
    case Word::kTwo:
    case Word::kSeven:
    case Word::kEleven:
    case Word::kTwelve:
      return true;
    default:
      return false;
  }
}

bool scanCustomWord(Word word) {
  switch (word) {
    case Word::kFiveMin:
      return scanWordAt("FIVE", 2) || scanWordOccurrence("FIVE", 1);
    case Word::kFiveHour:
      return scanWordOccurrence("FIVE", 2) || scanWordAt("FIVE", 4);
    case Word::kHalf:
      return scanWordAt("HALF", 2) || scanWord("HALF");
    case Word::kTwenty:
      return scanWordAt("TWENTY", 0) || scanWord("TWENTY");
    case Word::kQuarter:
      return scanWordAt("QUARTER", 1) || scanWord("QUARTER");
    case Word::kPast:
      return scanWord("PAST");
    case Word::kTo:
      return scanWord("TO");
    case Word::kThree:
      return scanWordAt("THREE", 5) || scanWord("THREE");
    case Word::kFour:
      return scanWordAt("FOUR", 7) || scanWord("FOUR");
    case Word::kSix:
      return scanWordAt("SIX", 5) || scanWord("SIX");
    case Word::kEight:
      return scanWordAt("EIGHT", 4) || scanWord("EIGHT");
    case Word::kNine:
      return scanWordAt("NINE", 7) || scanWord("NINE");
    case Word::kEleven:
      return scanWordAt("ELEVEN", 6) || scanWord("ELEVEN");
    default:
      break;
  }
  const char* w = scanName(word);
  return w != nullptr && scanWord(w);
}

const Entry* find(Word word) {
  for (const auto& e : kWords) {
    if (e.id == word) {
      return &e;
    }
  }
  return nullptr;
}

const char* scanName(Word word) {
  switch (word) {
    case Word::kTwenty:
      return "TWENTY";
    case Word::kQuarter:
      return "QUARTER";
    case Word::kFiveMin:
      return "FIVE";
    case Word::kHalf:
      return "HALF";
    case Word::kPast:
      return "PAST";
    case Word::kTo:
      return "TO";
    case Word::kThree:
      return "THREE";
    case Word::kFour:
      return "FOUR";
    case Word::kFiveHour:
      return "FIVE";
    case Word::kSix:
      return "SIX";
    case Word::kEight:
      return "EIGHT";
    case Word::kNine:
      return "NINE";
    case Word::kEleven:
      return "ELEVEN";
    default:
      return nullptr;
  }
}

}  // namespace

void init() {
  for (int y = 0; y < 8; ++y) {
    memcpy(s_custom[y], kHomeRows[y], 9);
  }
}

Plate plate() { return s_plate; }

const char* plateId() {
  switch (s_plate) {
    case Plate::kDoro:
      return "doro";
    case Plate::kCustom:
      return "custom";
    default:
      return "home";
  }
}

const char* plateTitle() {
  switch (s_plate) {
    case Plate::kDoro:
      return "ATWENTYD letter plate";
    case Plate::kCustom:
      return "Custom letters";
    default:
      return "Home (TWENTYH* / SETPASTO)";
  }
}

bool setPlateId(const char* id) {
  if (id == nullptr) {
    return false;
  }
  if (strcmp(id, "doro") == 0) {
    s_plate = Plate::kDoro;
    return true;
  }
  if (strcmp(id, "custom") == 0) {
    s_plate = Plate::kCustom;
    return true;
  }
  if (strcmp(id, "home") == 0) {
    s_plate = Plate::kHome;
    return true;
  }
  return false;
}

const char* row(int y) {
  if (y < 0 || y > 7) {
    return "";
  }
  return rows()[y];
}

char letter(uint8_t x, uint8_t y) {
  if (x > 7 || y > 7) {
    return ' ';
  }
  return rows()[y][x];
}

bool setCustomRows(const char* text) {
  if (text == nullptr) {
    return false;
  }
  char tmp[8][9] = {};
  int y = 0;
  int x = 0;
  for (const char* p = text; *p && y < 8; ++p) {
    if (*p == '\r') {
      continue;
    }
    if (*p == '\n') {
      ++y;
      x = 0;
      continue;
    }
    if (x < 8) {
      char c = *p;
      if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - ('a' - 'A'));
      }
      tmp[y][x++] = c;
    }
  }
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      if (tmp[r][c] == '\0') {
        tmp[r][c] = ' ';
      }
    }
    tmp[r][8] = '\0';
    memcpy(s_custom[r], tmp[r], 9);
  }
  s_plate = Plate::kCustom;
  return true;
}

size_t copyRowsText(char* out, size_t out_n) {
  if (out == nullptr || out_n == 0) {
    return 0;
  }
  size_t n = 0;
  for (int y = 0; y < 8 && n + 1 < out_n; ++y) {
    if (y) {
      out[n++] = '\n';
    }
    const char* r = row(y);
    for (int x = 0; x < 8 && r[x] && n + 1 < out_n; ++x) {
      out[n++] = r[x];
    }
  }
  out[n] = '\0';
  return n;
}

const Pixel* pixels(Word word, size_t* count) {
  const Entry* e = find(word);
  if (e == nullptr) {
    *count = 0;
    return nullptr;
  }

  // Home / Doro plates: hardcoded maps match the known letter plates.
  // Grid scanning is only for the custom plate (and shared-letter words always
  // use their explicit pixel lists because TWELVE/ELEVEN/TEN/etc. are not
  // contiguous substrings).
  if (s_plate != Plate::kCustom || usesHardcodedMap(word)) {
    if (s_plate == Plate::kDoro) {
      *count = e->doro_n;
      return e->doro;
    }
    *count = e->home_n;
    return e->home;
  }

  if (scanCustomWord(word)) {
    *count = s_scan_n;
    return s_scan;
  }

  *count = e->home_n;
  return e->home;
}

const char* name(Word word) {
  const Entry* e = find(word);
  return e != nullptr ? e->label : "?";
}

void printWordPixels(Word word) {
  size_t n = 0;
  const Pixel* px = pixels(word, &n);
  Serial.printf("%s (%u):", name(word), static_cast<unsigned>(n));
  for (size_t i = 0; i < n; ++i) {
    const char c = letter(px[i].x, px[i].y);
    Serial.printf(" (%u,%u)=%c", static_cast<unsigned>(px[i].x + 1),
                  static_cast<unsigned>(px[i].y + 1), c ? c : '?');
  }
  Serial.println();
}

void printFace() {
  Serial.printf("Clock face [%s]:\n", plateTitle());
  for (int y = 0; y < 8; ++y) {
    Serial.printf("  %d  %s\n", y + 1, row(y));
  }
}

}  // namespace face

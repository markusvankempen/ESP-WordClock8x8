#pragma once

#include <cstddef>
#include <cstdint>

namespace face {

struct Pixel {
  uint8_t x;
  uint8_t y;
};

enum class Plate : uint8_t {
  kHome = 0,  // Your printed face
  kDoro = 1,  // ATWENTYD letter plate (common 8×8 stencil)
  kCustom = 2,
};

enum class Word : uint8_t {
  kTwenty,
  kQuarter,
  kFiveMin,
  kHalf,
  kPast,
  kTo,
  kOne,
  kTwo,
  kThree,
  kFour,
  kFiveHour,
  kSix,
  kSeven,
  kEight,
  kNine,
  kTenHour,
  kEleven,
  kTwelve,
  kTenMin,
  kCount
};

void init();
Plate plate();
const char* plateId();
const char* plateTitle();
bool setPlateId(const char* id);

const char* row(int y);
char letter(uint8_t x, uint8_t y);
/** Eight lines of up to 8 letters. Used for the custom plate. */
bool setCustomRows(const char* text);
/** Current plate as 8 newline-separated rows. */
size_t copyRowsText(char* out, size_t out_n);

const Pixel* pixels(Word word, size_t* count);
const char* name(Word word);
/** Serial debug: print grid letters for each lit pixel. */
void printWordPixels(Word word);
void printFace();

}  // namespace face

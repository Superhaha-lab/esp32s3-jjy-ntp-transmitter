#include "jjy_encoder.h"

static void setBit(JjyFrame& frame, int second, bool value) {
  if (second >= 0 && second < 60) {
    frame.bits[second] = value ? JJY_ONE : JJY_ZERO;
  }
}

static bool bitOfDigit(int digit, int mask) {
  return (digit & mask) != 0;
}

JjyFrame buildJjyFrame(const tm& localTime) {
  JjyFrame frame{};
  frame.valid = false;

  for (int i = 0; i < 60; ++i) {
    frame.bits[i] = JJY_ZERO;
  }

  // Marker positions: P0, P1, P2, P3, P4, P5, P0.
  frame.bits[0] = JJY_MARKER;
  frame.bits[9] = JJY_MARKER;
  frame.bits[19] = JJY_MARKER;
  frame.bits[29] = JJY_MARKER;
  frame.bits[39] = JJY_MARKER;
  frame.bits[49] = JJY_MARKER;
  frame.bits[59] = JJY_MARKER;

  const int yearFull = localTime.tm_year + 1900;
  const int year2 = yearFull % 100;
  const int year10 = year2 / 10;
  const int year1 = year2 % 10;

  const int yday = localTime.tm_yday + 1;  // 1..366
  const int yday100 = (yday / 100) % 10;
  const int yday10 = (yday / 10) % 10;
  const int yday1 = yday % 10;

  const int hour = localTime.tm_hour;
  const int hour10 = hour / 10;
  const int hour1 = hour % 10;

  const int minute = localTime.tm_min;
  const int min10 = minute / 10;
  const int min1 = minute % 10;

  // JJY weekday: 0 = Sunday, 1 = Monday, ... 6 = Saturday.
  const int wday = localTime.tm_wday;

  uint8_t pa1 = 0; // hour parity
  uint8_t pa2 = 0; // minute parity

  // Minute BCD: 40, 20, 10, 8, 4, 2, 1.
  setBit(frame, 1, bitOfDigit(min10, 4)); pa2 ^= frame.bits[1];
  setBit(frame, 2, bitOfDigit(min10, 2)); pa2 ^= frame.bits[2];
  setBit(frame, 3, bitOfDigit(min10, 1)); pa2 ^= frame.bits[3];
  setBit(frame, 4, false);
  setBit(frame, 5, bitOfDigit(min1, 8)); pa2 ^= frame.bits[5];
  setBit(frame, 6, bitOfDigit(min1, 4)); pa2 ^= frame.bits[6];
  setBit(frame, 7, bitOfDigit(min1, 2)); pa2 ^= frame.bits[7];
  setBit(frame, 8, bitOfDigit(min1, 1)); pa2 ^= frame.bits[8];

  // Hour BCD: 20, 10, 8, 4, 2, 1.
  setBit(frame, 10, false);
  setBit(frame, 11, false);
  setBit(frame, 12, bitOfDigit(hour10, 2)); pa1 ^= frame.bits[12];
  setBit(frame, 13, bitOfDigit(hour10, 1)); pa1 ^= frame.bits[13];
  setBit(frame, 14, false);
  setBit(frame, 15, bitOfDigit(hour1, 8)); pa1 ^= frame.bits[15];
  setBit(frame, 16, bitOfDigit(hour1, 4)); pa1 ^= frame.bits[16];
  setBit(frame, 17, bitOfDigit(hour1, 2)); pa1 ^= frame.bits[17];
  setBit(frame, 18, bitOfDigit(hour1, 1)); pa1 ^= frame.bits[18];

  // Day of year BCD: 200, 100, 80, 40, 20, 10, 8, 4, 2, 1.
  setBit(frame, 20, false);
  setBit(frame, 21, false);
  setBit(frame, 22, bitOfDigit(yday100, 2));
  setBit(frame, 23, bitOfDigit(yday100, 1));
  setBit(frame, 24, false);
  setBit(frame, 25, bitOfDigit(yday10, 8));
  setBit(frame, 26, bitOfDigit(yday10, 4));
  setBit(frame, 27, bitOfDigit(yday10, 2));
  setBit(frame, 28, bitOfDigit(yday10, 1));
  setBit(frame, 30, bitOfDigit(yday1, 8));
  setBit(frame, 31, bitOfDigit(yday1, 4));
  setBit(frame, 32, bitOfDigit(yday1, 2));
  setBit(frame, 33, bitOfDigit(yday1, 1));
  setBit(frame, 34, false);
  setBit(frame, 35, false);

  // Parity bits. pa1 / pa2 are XOR of data bits, giving even parity.
  setBit(frame, 36, pa1 != 0);
  setBit(frame, 37, pa2 != 0);

  // SU1 and SU2.
  setBit(frame, 38, false);
  setBit(frame, 40, false);

  // Year BCD: 80, 40, 20, 10, 8, 4, 2, 1.
  setBit(frame, 41, bitOfDigit(year10, 8));
  setBit(frame, 42, bitOfDigit(year10, 4));
  setBit(frame, 43, bitOfDigit(year10, 2));
  setBit(frame, 44, bitOfDigit(year10, 1));
  setBit(frame, 45, bitOfDigit(year1, 8));
  setBit(frame, 46, bitOfDigit(year1, 4));
  setBit(frame, 47, bitOfDigit(year1, 2));
  setBit(frame, 48, bitOfDigit(year1, 1));

  // Weekday.
  setBit(frame, 50, (wday & 4) != 0);
  setBit(frame, 51, (wday & 2) != 0);
  setBit(frame, 52, (wday & 1) != 0);

  // Leap second and service interruption fields are fixed to 0 in this basic transmitter.
  setBit(frame, 53, false);
  setBit(frame, 54, false);
  setBit(frame, 55, false);
  setBit(frame, 56, false);
  setBit(frame, 57, false);
  setBit(frame, 58, false);

  frame.valid = true;
  return frame;
}

JjyBitType jjyBitAtSecond(const JjyFrame& frame, int second) {
  if (!frame.valid || second < 0 || second >= 60) {
    return JJY_MARKER;
  }
  return frame.bits[second];
}

uint16_t jjyActiveWindowMillis(JjyBitType bit) {
  switch (bit) {
    case JJY_MARKER:
      return 200;
    case JJY_ONE:
      return 500;
    case JJY_ZERO:
    default:
      return 800;
  }
}

const char* jjyBitToString(JjyBitType bit) {
  switch (bit) {
    case JJY_MARKER:
      return "MARKER";
    case JJY_ONE:
      return "ONE";
    case JJY_ZERO:
      return "ZERO";
    default:
      return "UNKNOWN";
  }
}

#pragma once

#include <Arduino.h>
#include <time.h>

enum JjyBitType : uint8_t {
  JJY_ZERO = 0,
  JJY_ONE = 1,
  JJY_MARKER = 2
};

struct JjyFrame {
  JjyBitType bits[60];
  bool valid;
};

JjyFrame buildJjyFrame(const tm& localTime);
JjyBitType jjyBitAtSecond(const JjyFrame& frame, int second);
uint16_t jjyActiveWindowMillis(JjyBitType bit);
const char* jjyBitToString(JjyBitType bit);

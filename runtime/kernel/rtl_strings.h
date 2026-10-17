// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <xbox.h>

// Lengths are bytes; buffer is a guest pointer, never a host char*/wchar_t*.
struct X_ANSI_STRING
{
    be<uint16_t> length;
    be<uint16_t> maximumLength;
    be<uint32_t> buffer;
};
struct X_UNICODE_STRING
{
    be<uint16_t> length;
    be<uint16_t> maximumLength;
    be<uint32_t> buffer;
};
static_assert(sizeof(X_ANSI_STRING) == 8 && sizeof(X_UNICODE_STRING) == 8);

void RtlFreeAnsiString(X_ANSI_STRING* string);
uint32_t RtlUnicodeStringToAnsiString(X_ANSI_STRING* destination,
    const X_UNICODE_STRING* source, uint8_t allocateDestination);

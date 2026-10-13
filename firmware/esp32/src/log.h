#pragma once

#include <Arduino.h>
#include <stdarg.h>

// Neblokirajući ispis na USB serijski port.
//
// Nano ESP32 koristi nativni USB CDC. Ako je port otvoren na PC-u, ali ga
// nitko ne čita (PC spava, monitor zamrznut), Serial.print() se vrti zauvijek
// bez yielda (USBCDC::write u jezgri) i blokira task koji ispisuje. Zato se
// ispisuje samo kad u TX bufferu ima mjesta za cijelu poruku; inače se poruka
// odbacuje. Bez spojenog hosta availableForWrite() vraća 0.
inline void logf(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;

    if (Serial.availableForWrite() < n) return;
    Serial.write((const uint8_t*)buf, n);
}

#include "input.h"
#include "config.h"

static bool     btn_pressed       = false;
static bool     btn_long          = false;
static bool     btn_was_down      = false;
static bool     btn_long_fired    = false;
static uint32_t btn_down_since    = 0;
static uint32_t btn_change_ms     = 0;

static int8_t   joy_x             = 0;
static int8_t   joy_y             = 0;

static int8_t   joy_y_latched     = 0;
static uint32_t joy_y_last_ms     = 0;

static constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
static constexpr uint32_t JOY_REPEAT_DELAY_MS = 180;

void input_init() {}

void input_update() {
    uint32_t now = millis();

    // ---------------- Button ----------------
    uint16_t btnVal = analogRead(PIN_BUTTON);
    bool raw_down = (btnVal > BUTTON_THRESHOLD);

    btn_pressed = false;
    btn_long    = false;

    static bool raw_prev = false;
    if (raw_down != raw_prev) {
        raw_prev = raw_down;
        btn_change_ms = now;
    }

    bool btn_down = btn_was_down;
    if ((now - btn_change_ms) >= BUTTON_DEBOUNCE_MS) {
        btn_down = raw_down;
    }

    if (btn_down && !btn_was_down) {
        btn_down_since = now;
        btn_long_fired = false;

    } else if (btn_down && btn_was_down) {
        if (!btn_long_fired && (now - btn_down_since >= ABORT_PRESS_MS)) {
            btn_long = true;
            btn_long_fired = true;
        }

    } else if (!btn_down && btn_was_down) {
        if (!btn_long_fired) {
            btn_pressed = true;
        }
    }

    btn_was_down = btn_down;

    // ---------------- Joystick ----------------
    uint16_t jy = analogRead(PIN_JOY_Y);

    int8_t raw_y = (jy < JOY_THRESHOLD_LOW) ? -1 :
                   (jy > JOY_THRESHOLD_HIGH) ? 1 : 0;

    joy_y = 0;

    if (raw_y == 0) {
        joy_y_latched = 0;

    } else if (raw_y != joy_y_latched) {
        joy_y = raw_y;                // instant first move
        joy_y_latched = raw_y;
        joy_y_last_ms = now;

    } else if ((now - joy_y_last_ms) >= JOY_REPEAT_DELAY_MS) {
        joy_y = raw_y;                // delayed repeat
        joy_y_last_ms = now;
    }

 //   uint16_t jx = analogRead(PIN_JOY_X);
 //   joy_x = (jx < JOY_THRESHOLD_LOW) ? -1 :
 //           (jx > JOY_THRESHOLD_HIGH) ? 1 : 0;
}

bool input_buttonPressed() { return btn_pressed; }
bool input_longPress()     { return btn_long; }
int8_t input_joystickY()   { return joy_y; }
int8_t input_joystickX()   { return joy_x; }
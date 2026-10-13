// The Wii's buttons, folded into one set of bits for every controller (main.c reads them), named like the 3DS's so
// ui.c's menu code works unchanged.
#pragma once
#define KEY_A       (1u << 0)    // OK: A, or 2 on a sideways Wii Remote
#define KEY_B       (1u << 1)    // back: B, or 1 on a sideways Wii Remote
#define KEY_SELECT  (1u << 2)    // - / Z: the operator's settings
#define KEY_START   (1u << 3)    // + / START: pause
#define KEY_DRIGHT  (1u << 4)
#define KEY_DLEFT   (1u << 5)
#define KEY_DUP     (1u << 6)
#define KEY_DDOWN   (1u << 7)
#define KEY_GO      (1u << 8)    // the throttle
#define KEY_HOME    (1u << 9)
#define KEY_UP      KEY_DUP
#define KEY_DOWN    KEY_DDOWN
#define KEY_LEFT    KEY_DLEFT
#define KEY_RIGHT   KEY_DRIGHT

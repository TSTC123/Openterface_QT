/**
 * SerialProtocolAdapter.h
 *
 * Bridge between Openterface_Core packet builders and QT's QByteArray flow.
 * Calls Core's builders, strips the trailing checksum byte for sendCommandAsync.
 */

#pragma once

#include "openterface/protocol_ch9329.h"
#include "openterface/input.h"
#include <QByteArray>
#include <cstdint>

namespace SerialProtocolAdapter {

QByteArray buildKeyboardPacket(uint8_t modifiers, const uint8_t keys[], int numKeys);
QByteArray buildKeyboardRelease();
QByteArray buildMouseRelPacket(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
QByteArray buildMouseAbsPacket(uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel);
QByteArray buildKeyboardRawPacket(uint8_t modifiers, const uint8_t keys[], int numKeys);
QByteArray buildKeyboardCh9329Packet(uint8_t modifiers, const uint8_t extraKeys[], int numExtraKeys);

// ── Checksum bridge (delegates to Core op_input_checksum) ────────────────

inline uint8_t calculateChecksum(const QByteArray& data) {
    return op_input_checksum(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
}

inline uint8_t calculateChecksum(const uint8_t* data, int len) {
    return op_input_checksum(data, len);
}

// ── Protocol constants (re-exported from Core for convenience) ─────────

constexpr uint8_t HEADER_0 = OP_CH9329_HEADER_0;       // 0x57
constexpr uint8_t HEADER_1 = OP_CH9329_HEADER_1;       // 0xAB
constexpr uint8_t CMD_KEYBOARD = OP_CH9329_CMD_KEYBOARD;     // 0x02
constexpr uint8_t CMD_MOUSE_REL = OP_CH9329_CMD_MOUSE_REL;   // 0x05
constexpr uint8_t CMD_MOUSE_ABS = OP_CH9329_CMD_MOUSE_ABS;   // 0x04
constexpr uint8_t CMD_USB_SWITCH = OP_CH9329_CMD_USB_SWITCH;  // 0x17

// Modifier bitmask constants
constexpr uint8_t MOD_NONE   = OP_INPUT_MOD_NONE;
constexpr uint8_t MOD_LCTRL  = OP_INPUT_MOD_LCTRL;
constexpr uint8_t MOD_LSHIFT = OP_INPUT_MOD_LSHIFT;
constexpr uint8_t MOD_LALT   = OP_INPUT_MOD_LALT;
constexpr uint8_t MOD_LGUI   = OP_INPUT_MOD_LGUI;
constexpr uint8_t MOD_RCTRL  = OP_INPUT_MOD_RCTRL;
constexpr uint8_t MOD_RSHIFT = OP_INPUT_MOD_RSHIFT;
constexpr uint8_t MOD_RALT   = OP_INPUT_MOD_RALT;
constexpr uint8_t MOD_RGUI   = OP_INPUT_MOD_RGUI;

// Mouse button constants
constexpr uint8_t BTN_NONE   = OP_INPUT_MS_BTN_NONE;
constexpr uint8_t BTN_LEFT   = OP_INPUT_MS_BTN_LEFT;
constexpr uint8_t BTN_RIGHT  = OP_INPUT_MS_BTN_RIGHT;
constexpr uint8_t BTN_MIDDLE = OP_INPUT_MS_BTN_MIDDLE;

} // namespace SerialProtocolAdapter

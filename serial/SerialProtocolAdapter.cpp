/**
 * SerialProtocolAdapter.cpp
 *
 * Bridge functions calling Core packet builders.
 */

#include "SerialProtocolAdapter.h"
#include "openterface/input.h"

namespace SerialProtocolAdapter {

QByteArray buildKeyboardPacket(uint8_t modifiers, const uint8_t keys[], int numKeys) {
    uint8_t buf[OP_CH9329_PKT_KEYBOARD_SIZE]; // 14 bytes
    op_ch9329_build_keyboard_packet(buf, modifiers, keys, numKeys, OP_INPUT_KB_FLAG_NONE);
    // Strip checksum (last byte) for Qt's sendCommandAsync pipeline
    return QByteArray(reinterpret_cast<const char*>(buf), OP_CH9329_PKT_KEYBOARD_SIZE - 1);
}

QByteArray buildKeyboardRelease() {
    uint8_t keys[] = { 0 };
    return buildKeyboardPacket(OP_INPUT_MOD_NONE, keys, 0);
}

QByteArray buildMouseRelPacket(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    uint8_t buf[OP_CH9329_PKT_MOUSE_REL_SIZE]; // 11 bytes
    op_ch9329_build_mouse_rel_packet(buf, buttons, dx, dy, wheel);
    return QByteArray(reinterpret_cast<const char*>(buf), OP_CH9329_PKT_MOUSE_REL_SIZE - 1);
}

QByteArray buildMouseAbsPacket(uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel) {
    uint8_t buf[OP_CH9329_PKT_MOUSE_ABS_SIZE]; // 13 bytes
    op_ch9329_build_mouse_abs_packet(buf, buttons, x, y, wheel);
    return QByteArray(reinterpret_cast<const char*>(buf), OP_CH9329_PKT_MOUSE_ABS_SIZE - 1);
}

QByteArray buildKeyboardRawPacket(uint8_t modifiers,
                                  const uint8_t keys[],
                                  int numKeys) {
    uint8_t buf[OP_CH9329_PKT_KEYBOARD_SIZE]; // 14 bytes
    // Raw mode: modifier byte used verbatim, no CH9329 workaround.
    // Used for Ctrl+Alt+Del and paste where exact modifier control is needed.
    op_ch9329_build_keyboard_packet(buf, modifiers, keys, numKeys, OP_INPUT_KB_FLAG_NONE);
    return QByteArray(reinterpret_cast<const char*>(buf), OP_CH9329_PKT_KEYBOARD_SIZE - 1);
}

QByteArray buildKeyboardCh9329Packet(uint8_t modifiers,
                                     const uint8_t extraKeys[],
                                     int numExtraKeys) {
    uint8_t buf[OP_CH9329_PKT_KEYBOARD_SIZE]; // 14 bytes
    // Delegate to Core with CH9329 workaround flag:
    // - Modifier byte: only Ctrl/Shift bits preserved
    // - Key array: all modifiers expanded to HID codes (0xE0-0xE7)
    op_ch9329_build_keyboard_packet(buf, modifiers, extraKeys, numExtraKeys,
                                    OP_INPUT_KB_FLAG_CH9329_WORKAROUND);
    // No checksum — stripped for sendCommandAsync pipeline
    return QByteArray(reinterpret_cast<const char*>(buf), OP_CH9329_PKT_KEYBOARD_SIZE - 1);
}

} // namespace SerialProtocolAdapter

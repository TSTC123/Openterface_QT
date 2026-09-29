/**
 * UsbModeAdapter.h
 *
 * Bridge between Openterface_Core USB mode APIs and Qt's USB mode types.
 * Provides type conversion helpers for Core USB mode functions.
 *
 * Key features:
 * - USB mode type conversion (Core op_usb_mode_t ↔ Qt bool isTarget)
 * - USB mode label generation
 *
 * NOTE: Full delegation to Core's op_usb_mode_* API requires bridging Qt's
 * IHIDTransport to Core's op_transport_t/op_hid_device_session_t (future work).
 * For now, this adapter provides type conversions.
 *
 * The Core implementation in usb_mode_ms21xx.c performs the same register
 * read/modify/write operations as Qt's VideoHid::setSpdifout(), including
 * firmware version-dependent bit/mask selection.
 */

#pragma once

#include "openterface/usb_mode.h"
#include <QString>

namespace UsbModeAdapter {

// ── USB mode type conversion (Core ↔ Qt) ─────────────────────────────

/**
 * Convert Core USB mode to Qt boolean (isTarget).
 * OP_USB_MODE_TARGET → true, OP_USB_MODE_HOST → false
 */
inline bool toQtIsTarget(op_usb_mode_t coreMode) {
    return coreMode == OP_USB_MODE_TARGET;
}

/**
 * Convert Qt boolean (isTarget) to Core USB mode.
 * true → OP_USB_MODE_TARGET, false → OP_USB_MODE_HOST
 */
inline op_usb_mode_t toCoreUsbMode(bool isTarget) {
    return isTarget ? OP_USB_MODE_TARGET : OP_USB_MODE_HOST;
}

// ── USB mode string labels ────────────────────────────────────────────────

/**
 * Get human-readable label for USB mode
 */
inline QString usbModeLabel(op_usb_mode_t mode) {
    switch (mode) {
        case OP_USB_MODE_HOST:
            return QStringLiteral("Host");
        case OP_USB_MODE_TARGET:
            return QStringLiteral("Target");
        case OP_USB_MODE_UNKNOWN:
        default:
            return QStringLiteral("Unknown");
    }
}

/**
 * Get human-readable label for Qt boolean USB mode
 */
inline QString usbModeLabel(bool isTarget) {
    return isTarget ? QStringLiteral("Target") : QStringLiteral("Host");
}

// ── Future: Full delegation to Core ───────────────────────────────────────
//
// When the transport/HID session bridge is implemented (Phase 3.6), the
// following functions can be added to fully delegate USB mode switching:
//
// /**
//  * Set USB mode via Core API
//  * Requires: Qt IHIDTransport wrapped as op_transport_t
//  */
// bool setUsbMode(IHIDTransport* transport, const DeviceInfo& device, bool isTarget);
//
// /**
//  * Get current USB mode via Core API
//  * Requires: Qt IHIDTransport wrapped as op_transport_t
//  */
// op_usb_mode_t getUsbMode(IHIDTransport* transport, const DeviceInfo& device);
//
// Implementation would:
// 1. Wrap IHIDTransport as op_transport_t using vtable pattern
// 2. Create op_usb_mode_endpoint_t
// 3. Bind device, transport, and optionally chip_controller
// 4. Call op_usb_mode_set() or op_usb_mode_get()
// 5. Handle errors and log results

} // namespace UsbModeAdapter

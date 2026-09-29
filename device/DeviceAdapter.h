/**
 * DeviceAdapter.h
 *
 * Bridge between Openterface_Core device APIs and Qt's device management.
 * Provides type conversion and convenience wrappers for Core device functions.
 *
 * This adapter makes Core the single source of truth for device management,
 * while Qt layer handles UI interaction and platform-specific backends.
 *
 * Key features:
 * - Video chip type conversion between Core and Qt representations
 * - Device info conversion (Qt DeviceInfo ↔ Core op_device_info_t)
 * - Video chip detection via Core API (replaces hardcoded VID/PID checks)
 * - Profile matching delegation to Core's op_profile_match()
 * - Capability query delegation to Core's op_device_info_has_capability()
 *
 * Usage:
 * - DeviceManager::getChipTypeForDevice() uses detectVideoChipType()
 * - DeviceInfo::toCoreDeviceInfo() uses profile and capability APIs
 */

#pragma once

#include "openterface/device.h"
#include "openterface/chip.h"
#include "openterface/profile.h"
#include "openterface/capability.h"
#include "DeviceInfo.h"
#include <QString>
#include <cstdint>

namespace DeviceAdapter {

// ── Video chip type conversion (Core ↔ Qt) ──────────────────────────────

/**
 * Convert Core video chip kind to Qt VideoChipType enum
 */
inline VideoChipType toQtVideoChipType(op_video_chip_kind_t coreKind) {
    switch (coreKind) {
        case OP_VIDEO_CHIP_MS2109:
            return VideoChipType::MS2109;
        case OP_VIDEO_CHIP_MS2109S:
            return VideoChipType::MS2109S;
        case OP_VIDEO_CHIP_MS2130S:
            return VideoChipType::MS2130S;
        case OP_VIDEO_CHIP_UNKNOWN:
        default:
            return VideoChipType::UNKNOWN;
    }
}

/**
 * Convert Qt VideoChipType enum to Core video chip kind
 */
inline op_video_chip_kind_t toCoreVideoChipKind(VideoChipType qtType) {
    switch (qtType) {
        case VideoChipType::MS2109:
            return OP_VIDEO_CHIP_MS2109;
        case VideoChipType::MS2109S:
            return OP_VIDEO_CHIP_MS2109S;
        case VideoChipType::MS2130S:
            return OP_VIDEO_CHIP_MS2130S;
        case VideoChipType::UNKNOWN:
        default:
            return OP_VIDEO_CHIP_UNKNOWN;
    }
}

// ── Device info conversion (Qt DeviceInfo ↔ Core op_device_info_t) ──

/**
 * Convert Qt DeviceInfo to Core op_device_info_t.
 * Partial conversion - only fills fields available in Qt DeviceInfo.
 * Used by DeviceInfo::toCoreDeviceInfo() to bridge Qt and Core.
 */
inline op_device_info_t toCoreDeviceInfo(const DeviceInfo& qtDevice) {
    op_device_info_t coreDevice;
    op_device_info_init(&coreDevice);

    // Convert VID/PID from QString to uint16_t
    if (!qtDevice.vid.isEmpty()) {
        bool ok;
        uint16_t vid = qtDevice.vid.toUShort(&ok, 16);
        if (ok) coreDevice.vendor_id = vid;
    }

    if (!qtDevice.pid.isEmpty()) {
        bool ok;
        uint16_t pid = qtDevice.pid.toUShort(&ok, 16);
        if (ok) coreDevice.product_id = pid;
    }

    // Copy device paths (with bounds checking)
    if (!qtDevice.serialPortPath.isEmpty()) {
        QByteArray path = qtDevice.serialPortPath.toLocal8Bit();
        strncpy(coreDevice.serial_path, path.constData(), OP_DEVICE_PATH_CAP - 1);
        coreDevice.serial_path[OP_DEVICE_PATH_CAP - 1] = '\0';
    }

    if (!qtDevice.hidDevicePath.isEmpty()) {
        QByteArray path = qtDevice.hidDevicePath.toLocal8Bit();
        strncpy(coreDevice.hid_path, path.constData(), OP_DEVICE_PATH_CAP - 1);
        coreDevice.hid_path[OP_DEVICE_PATH_CAP - 1] = '\0';
    }

    if (!qtDevice.cameraDevicePath.isEmpty()) {
        QByteArray path = qtDevice.cameraDevicePath.toLocal8Bit();
        strncpy(coreDevice.camera_path, path.constData(), OP_DEVICE_PATH_CAP - 1);
        coreDevice.camera_path[OP_DEVICE_PATH_CAP - 1] = '\0';
    }

    if (!qtDevice.audioDevicePath.isEmpty()) {
        QByteArray path = qtDevice.audioDevicePath.toLocal8Bit();
        strncpy(coreDevice.audio_path, path.constData(), OP_DEVICE_PATH_CAP - 1);
        coreDevice.audio_path[OP_DEVICE_PATH_CAP - 1] = '\0';
    }

    // Set interface flags based on available paths
    if (!qtDevice.hidDevicePath.isEmpty()) {
        coreDevice.interface_flags |= OP_DEVICE_IF_HID;
    }
    if (!qtDevice.serialPortPath.isEmpty()) {
        coreDevice.interface_flags |= OP_DEVICE_IF_SERIAL;
    }
    if (!qtDevice.cameraDevicePath.isEmpty()) {
        coreDevice.interface_flags |= OP_DEVICE_IF_CAMERA;
    }
    if (!qtDevice.audioDevicePath.isEmpty()) {
        coreDevice.interface_flags |= OP_DEVICE_IF_AUDIO;
    }

    return coreDevice;
}

// ── Video chip detection (delegates to Core) ────────────────────────────
//
// Replaces hardcoded VID/PID checks with Core API delegation.
// DeviceManager::getChipTypeForDevice() uses detectVideoChipType() to
// identify video chips via Core's op_video_chip_detect_from_device().

/**
 * Detect video chip type from Qt DeviceInfo.
 * Delegates to Core's op_video_chip_detect_from_device().
 * Replaces the previous hardcoded VID/PID checks in DeviceManager.
 */
inline VideoChipType detectVideoChipType(const DeviceInfo& qtDevice) {
    // Convert to Core device info and detect chip via Core API
    op_device_info_t coreDevice = toCoreDeviceInfo(qtDevice);
    op_video_chip_kind_t chipKind = op_video_chip_detect_from_device(&coreDevice);
    return toQtVideoChipType(chipKind);
}

/**
 * Get string label for video chip type.
 * Delegates to Core's op_video_chip_kind_label().
 */
inline QString videoChipTypeLabel(VideoChipType chipType) {
    op_video_chip_kind_t coreKind = toCoreVideoChipKind(chipType);
    return QString::fromUtf8(op_video_chip_kind_label(coreKind));
}

} // namespace DeviceAdapter

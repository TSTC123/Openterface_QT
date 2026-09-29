/**
 * VideoStatusAdapter.h
 *
 * Bridge between Openterface_Core video status APIs and Qt's video status types.
 * Provides type conversion and unit conversion helpers for Core video status functions.
 *
 * Key features:
 * - Video status type conversion (Core op_video_input_status_t ↔ Qt VideoHidResolutionInfo)
 * - Unit conversions: fps_milli ↔ fps float, pixel_clock_khz ↔ pixclk MHz
 * - Video chip type conversion (reuse from DeviceAdapter)
 * - Resolution normalization delegation to Core
 *
 * NOTE: Full delegation to Core's op_video_status_* API requires bridging Qt's
 * IHIDTransport to Core's op_transport_t/op_hid_device_session_t (future work).
 * For now, this adapter provides type and unit conversions.
 *
 * The Core implementation in video_status_poller.c performs the same register
 * reads and normalization as Qt's VideoHid::getInputStatus() and
 * normalizeResolution().
 *
 * Unit conventions:
 *   Qt:   fps = float (e.g. 30.0), pixclk = float MHz (e.g. 148.5)
 *   Core: fps_milli = uint32_t milli-fps (e.g. 30000), pixel_clock_khz = uint32_t kHz (e.g. 148500)
 */

#pragma once

#include "openterface/video_status.h"
#include "openterface/chip.h"
#include "videohid.h"  // For VideoHidResolutionInfo, VideoChipType

namespace VideoStatusAdapter {

// ── Video status type conversion (Core ↔ Qt) ──────────────────────────────

/**
 * Convert Core op_video_input_status_t to Qt VideoHidResolutionInfo.
 * Handles unit conversions:
 *   - fps_milli (e.g. 30000) → fps float (e.g. 30.0)
 *   - pixel_clock_khz (e.g. 148500) → pixclk float MHz (e.g. 148.5)
 */
inline VideoHidResolutionInfo toQtVideoStatus(const op_video_input_status_t& coreStatus) {
    VideoHidResolutionInfo qtStatus;
    qtStatus.hdmiConnected = coreStatus.hdmi_connected != 0;
    qtStatus.width = coreStatus.width;
    qtStatus.height = coreStatus.height;
    qtStatus.fps = static_cast<float>(coreStatus.fps_milli) / 1000.0f;
    qtStatus.pixclk = static_cast<float>(coreStatus.pixel_clock_khz) / 1000.0f;
    return qtStatus;
}

/**
 * Convert Qt VideoHidResolutionInfo to Core op_video_input_status_t.
 * Handles unit conversions:
 *   - fps float (e.g. 30.0) → fps_milli (e.g. 30000)
 *   - pixclk float MHz (e.g. 148.5) → pixel_clock_khz (e.g. 148500)
 */
inline op_video_input_status_t toCoreVideoStatus(const VideoHidResolutionInfo& qtStatus) {
    op_video_input_status_t coreStatus;
    coreStatus.hdmi_connected = qtStatus.hdmiConnected ? 1 : 0;
    coreStatus.width = static_cast<uint16_t>(qtStatus.width);
    coreStatus.height = static_cast<uint16_t>(qtStatus.height);
    coreStatus.fps_milli = static_cast<uint32_t>(qtStatus.fps * 1000.0f);
    coreStatus.pixel_clock_khz = static_cast<uint32_t>(qtStatus.pixclk * 1000.0f);
    return coreStatus;
}

// ── Video chip type conversion (reuse from DeviceAdapter) ──────────────────

/**
 * Convert Qt VideoChipType to Core op_video_chip_kind_t
 * Note: This is the same as DeviceAdapter::toCoreVideoChipKind()
 */
inline op_video_chip_kind_t toCoreChipKind(VideoChipType qtType) {
    switch (qtType) {
        case VideoChipType::MS2109:  return OP_VIDEO_CHIP_MS2109;
        case VideoChipType::MS2109S: return OP_VIDEO_CHIP_MS2109S;
        case VideoChipType::MS2130S: return OP_VIDEO_CHIP_MS2130S;
        case VideoChipType::UNKNOWN:
        default:                     return OP_VIDEO_CHIP_UNKNOWN;
    }
}

// ── Normalization delegation ──────────────────────────────────────────────

/**
 * Normalize video status using Core's normalization logic
 * Delegates to op_video_input_status_normalize() which handles chip-specific quirks:
 *   - MS2109: double resolution if pixel clock > 189 MHz (unless already 4K)
 *   - MS2130S: fix 3840x1080 → 3840x2160
 *
 * This can be called on Qt status data without requiring HID access.
 */
inline void normalizeVideoStatus(VideoChipType chipType, VideoHidResolutionInfo& qtStatus) {
    op_video_chip_kind_t coreKind = toCoreChipKind(chipType);
    op_video_input_status_t coreStatus = toCoreVideoStatus(qtStatus);
    op_video_input_status_normalize(coreKind, &coreStatus);
    qtStatus = toQtVideoStatus(coreStatus);
}

// ── String labels ─────────────────────────────────────────────────────────

/**
 * Get human-readable summary of video status
 */
inline QString videoStatusLabel(const VideoHidResolutionInfo& status) {
    if (!status.hdmiConnected) {
        return QStringLiteral("No HDMI");
    }
    return QString("%1x%2 @ %3 fps")
        .arg(status.width)
        .arg(status.height)
        .arg(status.fps, 0, 'f', 1);
}

// ── Future: Full delegation to Core ───────────────────────────────────────
//
// When the transport/HID session bridge is implemented (Phase 3.6), the
// following functions can be added to fully delegate video status polling:
//
// /**
//  * Create a video status poller using Core API
//  * Requires: Qt IHIDTransport wrapped as op_transport_t
//  */
// op_video_status_poller_t* createPoller(IHIDTransport* transport, const DeviceInfo& device);
//
// /**
//  * Poll video status via Core API
//  * Returns Qt-friendly VideoHidResolutionInfo
//  */
// VideoHidResolutionInfo pollVideoStatus(op_video_status_poller_t* poller);
//
// Implementation would:
// 1. Wrap IHIDTransport as op_transport_t using vtable pattern
// 2. Create op_video_chip_controller_t from HID session
// 3. Create op_video_status_poller_t from controller
// 4. Call op_video_status_poller_poll()
// 5. Convert op_video_input_status_t to VideoHidResolutionInfo
// 6. Handle errors and log results

} // namespace VideoStatusAdapter

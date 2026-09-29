/*
 * ========================================================================== *
 *                                                                            *
 *    This file is part of the Openterface Mini KVM App QT version            *
 *                                                                            *
 *    Copyright (C) 2024   <info@openterface.com>                             *
 *                                                                            *
 *    This program is free software: you can redistribute it and/or modify    *
 *    it under the terms of the GNU General Public License version 3.         *
 *                                                                            *
 *    This program is distributed in the hope that it will be useful, but     *
 *    WITHOUT ANY WARRANTY; without even the implied warranty of              *
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
 *    General Public License for more details.                                *
 *                                                                            *
 *    You should have received a copy of the GNU General Public License       *
 *    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
 *                                                                            *
 * ========================================================================== *
 */

#ifndef WATCHDOG_ADAPTER_H
#define WATCHDOG_ADAPTER_H

#include "ConnectionWatchdog.h"
#include "openterface/watchdog.h"
#include "openterface/transport.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

/**
 * @brief Adapter layer between Qt ConnectionWatchdog and Core op_watchdog_t
 *
 * This adapter provides:
 * - State mapping between Core (op_connection_state_t) and Qt (ConnectionState)
 * - Configuration mapping from WatchdogConfig to op_watchdog_config_t
 * - Stub transport implementation (Core requires non-NULL transport)
 * - Bridge functions for Core callbacks (state_cb, health_probe)
 *
 * Design: ConnectionWatchdog delegates state machine to Core, while Qt retains
 * control of actual recovery execution (port switching, RTS reset, etc.)
 */
namespace WatchdogAdapter {

// ============================================================================
// State Mapping
// ============================================================================

/**
 * @brief Map Core connection state to Qt connection state
 *
 * Core states: CONNECTED, DEGRADED, RECOVERING, DISCONNECTED
 * Qt states: Disconnected, Connecting, Connected, Unstable, Recovering, Failed
 */
inline ConnectionState toQtConnectionState(op_connection_state_t coreState) {
    switch (coreState) {
        case OP_CONN_STATE_CONNECTED:
            return ConnectionState::Connected;
        case OP_CONN_STATE_DEGRADED:
            return ConnectionState::Unstable;
        case OP_CONN_STATE_RECOVERING:
            return ConnectionState::Recovering;
        case OP_CONN_STATE_DISCONNECTED:
            return ConnectionState::Failed;  // or Disconnected, depending on context
        default:
            return ConnectionState::Disconnected;
    }
}

/**
 * @brief Map Qt connection state to Core connection state
 *
 * Note: Qt's "Connecting" state has no direct Core equivalent.
 * It's typically transient and maps to CONNECTED (assuming connection in progress).
 */
inline op_connection_state_t toCoreConnectionState(ConnectionState qtState) {
    switch (qtState) {
        case ConnectionState::Connected:
            return OP_CONN_STATE_CONNECTED;
        case ConnectionState::Unstable:
            return OP_CONN_STATE_DEGRADED;
        case ConnectionState::Recovering:
            return OP_CONN_STATE_RECOVERING;
        case ConnectionState::Disconnected:
        case ConnectionState::Failed:
            return OP_CONN_STATE_DISCONNECTED;
        case ConnectionState::Connecting:
            return OP_CONN_STATE_CONNECTED;  // Connecting maps to CONNECTED
        default:
            return OP_CONN_STATE_DISCONNECTED;
    }
}

// ============================================================================
// Configuration Mapping
// ============================================================================

/**
 * @brief Convert Qt WatchdogConfig to Core op_watchdog_config_t
 *
 * Maps Qt configuration parameters to Core equivalents:
 * - maxConsecutiveErrors → recovery_threshold
 * - maxConsecutiveErrors/2 → degrade_threshold
 * - maxRetryAttempts → max_recovery_attempts
 * - baseRetryDelayMs, maxRetryDelayMs → backoff configuration
 */
inline op_watchdog_config_t toCoreWatchdogConfig(const WatchdogConfig& qtConfig,
                                                   op_transport_t* stubTransport) {
    op_watchdog_config_t coreConfig = {};

    // Core requires non-NULL transport (even if it's a stub)
    coreConfig.transport = stubTransport;

    // Error thresholds
    // Qt: maxConsecutiveErrors triggers recovery
    // Core: recovery_threshold triggers recovery, degrade_threshold triggers degraded
    coreConfig.recovery_threshold = static_cast<uint32_t>(qtConfig.maxConsecutiveErrors);
    coreConfig.degrade_threshold = static_cast<uint32_t>(qtConfig.maxConsecutiveErrors / 2);

    // Recovery attempts
    coreConfig.max_recovery_attempts = static_cast<uint32_t>(qtConfig.maxRetryAttempts);

    // Exponential backoff
    // Qt: baseDelay * 2^attempt, capped at maxDelay
    // Core: base * multiplier^attempt, capped at max
    coreConfig.backoff.base_interval_ms = static_cast<uint32_t>(qtConfig.baseRetryDelayMs);
    coreConfig.backoff.max_interval_ms = static_cast<uint32_t>(qtConfig.maxRetryDelayMs);
    coreConfig.backoff.multiplier = 2.0f;  // Qt uses 2^attempt

    // Health check interval (Qt's watchdog interval)
    coreConfig.health_check_interval_ms = static_cast<uint32_t>(qtConfig.watchdogIntervalMs);

    // Note: state_cb and health_probe are set separately by ConnectionWatchdog

    return coreConfig;
}

// ============================================================================
// Stub Transport
// ============================================================================

/**
 * @brief Stub transport implementation for Core watchdog
 *
 * Core's op_watchdog_create() requires a non-NULL transport. Since Qt handles
 * recovery execution externally (via IRecoveryHandler), we provide a stub
 * transport where open/close are no-ops.
 *
 * The actual recovery is coordinated through:
 * 1. Core's state_cb notifies Qt when state changes to RECOVERING
 * 2. Qt calls IRecoveryHandler::performRecovery()
 * 3. Qt calls op_watchdog_report_ok() or op_watchdog_report_error() based on result
 */
inline op_status_t stubTransportOpen(void* /*context*/) {
    // No-op: Qt handles actual port open via IRecoveryHandler
    return OP_STATUS_OK;
}

inline op_status_t stubTransportClose(void* /*context*/) {
    // No-op: Qt handles actual port close via IRecoveryHandler
    return OP_STATUS_OK;
}

inline op_status_t stubTransportRead(void* /*context*/, uint8_t* /*buffer*/,
                                      size_t /*capacity*/, size_t* outLength) {
    *outLength = 0;
    return OP_STATUS_OK;
}

inline op_status_t stubTransportWrite(void* /*context*/, const uint8_t* /*buffer*/,
                                       size_t /*length*/) {
    return OP_STATUS_OK;
}

inline op_status_t stubTransportControl(void* /*context*/, uint32_t /*request*/,
                                         const uint8_t* /*input*/, size_t /*inputLength*/,
                                         uint8_t* /*output*/, size_t /*outputCapacity*/,
                                         size_t* outLength) {
    *outLength = 0;
    return OP_STATUS_NOT_SUPPORTED;
}

/**
 * @brief Initialize a stub transport for Core watchdog
 */
inline void initStubTransport(op_transport_t* transport) {
    static const op_transport_vtable_t stubVtable = {
        stubTransportOpen,
        stubTransportClose,
        stubTransportRead,
        stubTransportWrite,
        stubTransportControl
    };
    op_transport_init(transport, OP_TRANSPORT_KIND_STUB, nullptr, &stubVtable);
    // Mark as open so Core's recovery cycle doesn't try to "open" it
    transport->is_open = 1;
}

// ============================================================================
// Bridge Functions (declared here, implemented in ConnectionWatchdog.cpp)
// ============================================================================

/**
 * @brief Core state change callback bridge
 *
 * Called by Core when connection state changes. This function:
 * 1. Maps Core state to Qt state
 * 2. Retrieves ConnectionWatchdog instance from user_data
 * 3. Emits appropriate Qt signals
 *
 * Implementation in ConnectionWatchdog.cpp (needs access to private members)
 */
void coreStateChangeCallback(const op_connection_event_t* event);

/**
 * @brief Core health probe callback bridge
 *
 * Called by Core to verify connection health. This function:
 * 1. Retrieves ConnectionWatchdog instance from context
 * 2. Calls IRecoveryHandler::isConnectionHealthy() if available
 * 3. Returns OP_STATUS_OK if healthy, error otherwise
 *
 * Implementation in ConnectionWatchdog.cpp
 */
op_status_t coreHealthProbeCallback(void* context);

} // namespace WatchdogAdapter

#endif // WATCHDOG_ADAPTER_H

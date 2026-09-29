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

#include "ConnectionWatchdog.h"
#include "WatchdogAdapter.h"
#include <QDebug>
#include <QtMath>
#include <QThread>
#include <QMetaObject>

// ============================================================================
// Core callback bridge implementations
// ============================================================================

/**
 * @brief Core state change callback - bridges Core state to Qt signals
 *
 * This is called by Core's op_watchdog_t when connection state changes.
 * We retrieve the ConnectionWatchdog instance from user_data and notify it.
 */
void WatchdogAdapter::coreStateChangeCallback(const op_connection_event_t* event) {
    if (event == nullptr || event->user_data == nullptr) {
        return;
    }

    ConnectionWatchdog* watchdog = static_cast<ConnectionWatchdog*>(event->user_data);
    ConnectionState qtNewState = WatchdogAdapter::toQtConnectionState(event->new_state);

    qCDebug(log_core_serial) << "Core state change:"
                             << op_connection_state_label(event->old_state)
                             << "->" << op_connection_state_label(event->new_state)
                             << "(Qt:" << static_cast<int>(qtNewState) << ")"
                             << "errors:" << event->consecutive_errors;

    // Notify the watchdog to handle the state change
    watchdog->onCoreStateChanged(event->new_state, event->last_error);
}

/**
 * @brief Core health probe callback - checks if connection is actually healthy
 *
 * Called by Core during health checks and after recovery attempts.
 * Delegates to IRecoveryHandler::isConnectionHealthy() for chip-specific checks.
 */
op_status_t WatchdogAdapter::coreHealthProbeCallback(void* context) {
    if (context == nullptr) {
        return OP_STATUS_OK;  // No watchdog = assume healthy
    }

    ConnectionWatchdog* watchdog = static_cast<ConnectionWatchdog*>(context);
    IRecoveryHandler* handler = watchdog->getRecoveryHandler();

    if (handler == nullptr) {
        return OP_STATUS_OK;  // No handler = assume healthy
    }

    bool healthy = handler->isConnectionHealthy();
    qCDebug(log_core_serial) << "Health probe:" << (healthy ? "OK" : "FAILED");
    return healthy ? OP_STATUS_OK : OP_STATUS_IO_ERROR;
}

// ============================================================================
// ConnectionWatchdog Implementation
// ============================================================================

ConnectionWatchdog::ConnectionWatchdog(QObject *parent)
    : QObject(parent)
{
    // Initialize stub transport for Core watchdog
    WatchdogAdapter::initStubTransport(&m_stubTransport);

    // Timers are created lazily in start() to ensure correct thread affinity
    m_watchdogTimer = nullptr;
    m_recoveryTimer = nullptr;

    // Initialize elapsed timers (no thread affinity)
    m_lastSuccessfulCommand.start();
    m_uptimeTimer.start();
    m_errorRateTimer.start();

    qCDebug(log_core_serial) << "ConnectionWatchdog initialized (Core delegation)";
}

ConnectionWatchdog::~ConnectionWatchdog()
{
    stop();
    destroyCoreWatchdog();
    qCDebug(log_core_serial) << "ConnectionWatchdog destroyed";
}

// ========== Configuration ==========

void ConnectionWatchdog::setConfig(const WatchdogConfig& config)
{
    m_config = config;
    qCDebug(log_core_serial) << "Watchdog config updated:"
                          << "interval=" << config.watchdogIntervalMs << "ms"
                          << "maxErrors=" << config.maxConsecutiveErrors
                          << "maxRetries=" << config.maxRetryAttempts
                          << "autoRecovery=" << config.autoRecoveryEnabled;

    // Reconfigure Core watchdog if it exists
    if (m_coreWatchdog != nullptr) {
        op_watchdog_backoff_config_t backoffConfig;
        backoffConfig.base_interval_ms = static_cast<uint32_t>(config.baseRetryDelayMs);
        backoffConfig.max_interval_ms = static_cast<uint32_t>(config.maxRetryDelayMs);
        backoffConfig.multiplier = 2.0f;
        op_watchdog_configure_backoff(m_coreWatchdog, &backoffConfig);
    }
}

void ConnectionWatchdog::setRecoveryHandler(IRecoveryHandler* handler)
{
    m_recoveryHandler = handler;
    qCDebug(log_core_serial) << "Recovery handler set:" << (handler ? "valid" : "null");
}

void ConnectionWatchdog::setAutoRecoveryEnabled(bool enabled)
{
    m_config.autoRecoveryEnabled = enabled;
    qCDebug(log_core_serial) << "Auto recovery" << (enabled ? "enabled" : "disabled");
}

void ConnectionWatchdog::setMaxRetryAttempts(int maxRetries)
{
    m_config.maxRetryAttempts = maxRetries;
    qCDebug(log_core_serial) << "Max retry attempts set to" << maxRetries;
    // Note: Core's max_recovery_attempts is set at create time
    // Runtime change requires recreating the watchdog (done in start())
}

void ConnectionWatchdog::setMaxConsecutiveErrors(int maxErrors)
{
    m_config.maxConsecutiveErrors = maxErrors;
    qCDebug(log_core_serial) << "Max consecutive errors set to" << maxErrors;
    // Note: Core's thresholds are set at create time
}

// ========== Lifecycle ==========

void ConnectionWatchdog::start()
{
    if (m_isRunning) {
        qCDebug(log_core_serial) << "Watchdog already running";
        return;
    }

    m_isRunning = true;
    m_isShuttingDown = false;
    m_uptimeTimer.restart();
    m_lastSuccessfulCommand.restart();

    // Ensure timers are created in this object's current thread (thread-safe)
    QMetaObject::invokeMethod(this, [this]() {
        if (m_isShuttingDown) {
            return;
        }

        // Create Core watchdog
        createCoreWatchdog();

        if (!m_watchdogTimer) {
            m_watchdogTimer = new QTimer(this);
            m_watchdogTimer->setSingleShot(false);  // periodic tick
            connect(m_watchdogTimer, &QTimer::timeout, this, [this]() {
                // Drive Core's state machine with periodic tick
                if (m_coreWatchdog != nullptr) {
                    op_watchdog_tick(m_coreWatchdog, 100);  // 100ms tick interval
                }
            });
        }

        if (!m_recoveryTimer) {
            m_recoveryTimer = new QTimer(this);
            m_recoveryTimer->setSingleShot(true);
            connect(m_recoveryTimer, &QTimer::timeout, this, &ConnectionWatchdog::executeRecovery);
        }

        // Start watchdog timer - 100ms tick for Core
        m_watchdogTimer->start(100);

        setConnectionState(ConnectionState::Connected);
        qCInfo(log_core_serial) << "Watchdog started with 100ms tick (Core delegation)";
    }, Qt::QueuedConnection);
}

void ConnectionWatchdog::stop()
{
    if (!m_isRunning) {
        return;
    }

    m_isRunning = false;
    m_isShuttingDown = true;

    // Stop timers safely
    if (m_watchdogTimer && m_watchdogTimer->isActive()) {
        m_watchdogTimer->stop();
    }

    if (m_recoveryTimer && m_recoveryTimer->isActive()) {
        m_recoveryTimer->stop();
    }

    // Destroy Core watchdog
    destroyCoreWatchdog();

    setConnectionState(ConnectionState::Disconnected);
    qCInfo(log_core_serial) << "Watchdog stopped";
}

bool ConnectionWatchdog::isRunning() const
{
    return m_isRunning;
}

void ConnectionWatchdog::setShuttingDown(bool shuttingDown)
{
    m_isShuttingDown = shuttingDown;
    if (shuttingDown) {
        stop();
    }
}

// ========== Error Tracking ==========

void ConnectionWatchdog::recordSuccess()
{
    m_lastSuccessfulCommand.restart();

    // Delegate to Core
    if (m_coreWatchdog != nullptr) {
        op_watchdog_report_ok(m_coreWatchdog);

        // Sync state from Core
        m_consecutiveErrors = op_watchdog_consecutive_errors(m_coreWatchdog);
        m_totalErrors = op_watchdog_total_errors(m_coreWatchdog);
    } else {
        // Fallback: reset local counters if Core not available
        m_consecutiveErrors = 0;
    }

    // If we were in unstable state, return to connected
    if (m_connectionState == ConnectionState::Unstable) {
        setConnectionState(ConnectionState::Connected);
    }

    // If we were recovering, mark as successful
    if (m_connectionState == ConnectionState::Recovering) {
        m_successfulRecoveries++;
        setConnectionState(ConnectionState::Connected);
        emit recoverySucceeded();

        if (m_recoveryHandler) {
            m_recoveryHandler->onRecoverySuccess();
        }

        qCInfo(log_core_serial) << "Recovery successful after" << m_retryAttemptCount.load() << "attempts";
        m_retryAttemptCount = 0;
    }
}

void ConnectionWatchdog::recordError()
{
    // Delegate to Core
    if (m_coreWatchdog != nullptr) {
        op_watchdog_report_error(m_coreWatchdog, OP_STATUS_IO_ERROR);

        // Sync state from Core
        m_consecutiveErrors = op_watchdog_consecutive_errors(m_coreWatchdog);
        m_totalErrors = op_watchdog_total_errors(m_coreWatchdog);
    } else {
        // Fallback: increment local counters if Core not available
        m_consecutiveErrors++;
        m_totalErrors++;
    }

    m_errorsInWindow++;
    updateErrorRate();

    qCDebug(log_core_serial) << "Error recorded. Consecutive:" << m_consecutiveErrors.load()
                          << "Total:" << m_totalErrors.load();

    // Check if we should transition to unstable state (Qt-side state for UI)
    if (m_connectionState == ConnectionState::Connected &&
        m_consecutiveErrors >= m_config.maxConsecutiveErrors / 2) {
        setConnectionState(ConnectionState::Unstable);
    }

    // Core handles recovery triggering via state_cb
    // We don't need to schedule recovery here - Core's state change callback will do it
}

void ConnectionWatchdog::resetCounters()
{
    // Reset Core state
    if (m_coreWatchdog != nullptr) {
        op_watchdog_reset_backoff(m_coreWatchdog);
    }

    m_consecutiveErrors = 0;
    m_retryAttemptCount = 0;
    m_errorsInWindow = 0;
    m_errorRateTimer.restart();

    qCDebug(log_core_serial) << "Error counters reset";
}

bool ConnectionWatchdog::isRecoveryNeeded() const
{
    return m_config.autoRecoveryEnabled &&
           m_consecutiveErrors >= m_config.maxConsecutiveErrors &&
           m_retryAttemptCount < m_config.maxRetryAttempts;
}

ConnectionStats ConnectionWatchdog::getStats() const
{
    ConnectionStats stats;

    // Query from Core if available
    if (m_coreWatchdog != nullptr) {
        stats.consecutiveErrors = op_watchdog_consecutive_errors(m_coreWatchdog);
        stats.totalErrors = op_watchdog_total_errors(m_coreWatchdog);
        stats.recoveryAttempts = op_watchdog_recovery_attempts(m_coreWatchdog);
    } else {
        stats.consecutiveErrors = m_consecutiveErrors.load();
        stats.totalErrors = m_totalErrors.load();
        stats.recoveryAttempts = m_retryAttemptCount.load();
    }

    stats.successfulRecoveries = m_successfulRecoveries.load();
    stats.lastSuccessfulCommandMs = m_lastSuccessfulCommand.elapsed();
    stats.uptimeMs = m_uptimeTimer.elapsed();

    // Calculate error rate
    if (m_errorRateTimer.elapsed() > 0) {
        stats.errorRate = static_cast<double>(m_errorsInWindow) * 1000.0 / m_errorRateTimer.elapsed();
    }

    return stats;
}

bool ConnectionWatchdog::isConnectionStable() const
{
    // Use Core state if available
    if (m_coreWatchdog != nullptr) {
        op_connection_state_t coreState = op_watchdog_get_state(m_coreWatchdog);
        return coreState == OP_CONN_STATE_CONNECTED &&
               m_lastSuccessfulCommand.elapsed() < m_config.communicationTimeoutMs;
    }

    return m_connectionState == ConnectionState::Connected &&
           m_consecutiveErrors < m_config.maxConsecutiveErrors / 2 &&
           m_lastSuccessfulCommand.elapsed() < m_config.communicationTimeoutMs;
}

// ========== Manual Recovery ==========

void ConnectionWatchdog::forceRecovery()
{
    qCInfo(log_core_serial) << "Force recovery requested";

    // Delegate to Core
    if (m_coreWatchdog != nullptr) {
        op_watchdog_force_reconnect(m_coreWatchdog);
    } else {
        // Fallback: force error threshold and schedule
        m_consecutiveErrors = m_config.maxConsecutiveErrors;
        scheduleRecovery();
    }
}

// ========== Private Slots ==========

void ConnectionWatchdog::onWatchdogTimeout()
{
    if (m_isShuttingDown || !m_isRunning) {
        return;
    }

    qCDebug(log_core_serial) << "Watchdog check - last success:"
                          << m_lastSuccessfulCommand.elapsed() << "ms ago";

    // Check if we haven't had successful communication
    if (m_lastSuccessfulCommand.elapsed() > m_config.communicationTimeoutMs) {
        qCWarning(log_core_serial) << "Watchdog triggered - no communication for"
                                << m_config.communicationTimeoutMs << "ms";

        emit watchdogTimeout();
        emit statusUpdate(QString("No communication for %1 seconds")
                         .arg(m_config.communicationTimeoutMs / 1000));

        // Report error to Core for state machine handling
        // Always report to Core when it exists - let Core's state machine decide
        // (autoRecoveryEnabled is checked in onCoreStateChanged before scheduling recovery)
        if (m_coreWatchdog != nullptr) {
            op_watchdog_report_error(m_coreWatchdog, OP_STATUS_TIMEOUT);
        } else if (m_retryAttemptCount < m_config.maxRetryAttempts) {
            // Fallback: schedule recovery directly (no Core watchdog)
            m_consecutiveErrors = m_config.maxConsecutiveErrors;
            scheduleRecovery();
        }
    }
}

void ConnectionWatchdog::executeRecovery()
{
    if (m_isShuttingDown) {
        return;
    }

    m_retryAttemptCount++;

    qCInfo(log_core_serial) << "Executing recovery attempt" << m_retryAttemptCount.load()
                         << "of" << m_config.maxRetryAttempts;

    emit recoveryStarted(m_retryAttemptCount.load());
    emit statusUpdate(QString("Recovery attempt %1 of %2")
                     .arg(m_retryAttemptCount.load())
                     .arg(m_config.maxRetryAttempts));

    setConnectionState(ConnectionState::Recovering);

    bool success = false;

    if (m_recoveryHandler) {
        success = m_recoveryHandler->performRecovery(m_retryAttemptCount.load());
    } else {
        qCWarning(log_core_serial) << "No recovery handler set - cannot perform recovery";
    }

    if (success) {
        recordSuccess();
    } else {
        qCWarning(log_core_serial) << "Recovery attempt" << m_retryAttemptCount.load() << "failed";

        // Report failure to Core
        if (m_coreWatchdog != nullptr) {
            op_watchdog_report_error(m_coreWatchdog, OP_STATUS_IO_ERROR);
        }

        if (m_retryAttemptCount >= m_config.maxRetryAttempts) {
            qCCritical(log_core_serial) << "Maximum retry attempts reached. Recovery failed.";
            setConnectionState(ConnectionState::Failed);
            emit recoveryFailed();
            emit statusUpdate("Recovery failed - max retries exceeded");

            if (m_recoveryHandler) {
                m_recoveryHandler->onRecoveryFailed();
            }
        } else {
            // Clear Recovering state before scheduling the retry timer
            setConnectionState(ConnectionState::Connected);
            scheduleRecovery();
        }
    }
}

// ========== Private Methods ==========

void ConnectionWatchdog::setConnectionState(ConnectionState state)
{
    if (m_connectionState != state) {
        ConnectionState oldState = m_connectionState;
        m_connectionState = state;

        qCDebug(log_core_serial) << "Connection state changed from"
                              << static_cast<int>(oldState) << "to" << static_cast<int>(state);

        emit connectionStateChanged(state);
    }
}

void ConnectionWatchdog::scheduleRecovery()
{
    if (m_isShuttingDown || m_connectionState == ConnectionState::Recovering) {
        return;
    }

    if (m_retryAttemptCount >= m_config.maxRetryAttempts) {
        qCWarning(log_core_serial) << "Cannot schedule recovery - max attempts reached";
        return;
    }

    int delay = calculateRetryDelay();

    // Avoid scheduling if a recovery is already scheduled
    bool alreadyScheduled = false;
    if (QThread::currentThread() == thread()) {
        if (m_recoveryTimer && m_recoveryTimer->isActive()) {
            alreadyScheduled = true;
        }
    } else {
        QMetaObject::invokeMethod(this, "isRecoveryScheduled", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, alreadyScheduled));
    }

    if (alreadyScheduled) {
        qCDebug(log_core_serial) << "Recovery already scheduled, skipping duplicate schedule";
        return;
    }

    qCInfo(log_core_serial) << "Scheduling recovery in" << delay << "ms"
                         << "(attempt" << (m_retryAttemptCount.load() + 1) << ")";

    QMetaObject::invokeMethod(this, [this, delay]() {
        if (m_isShuttingDown || !m_isRunning) {
            return;
        }

        if (m_connectionState == ConnectionState::Recovering) {
            return;
        }

        if (!m_recoveryTimer) {
            qCWarning(log_core_serial) << "Recovery timer is null - cannot schedule recovery";
            return;
        }

        if (m_recoveryTimer->isActive()) {
            qCDebug(log_core_serial) << "(invoke) Recovery already scheduled, skipping";
            return;
        }

        m_recoveryTimer->stop();
        m_recoveryTimer->setInterval(delay);
        m_recoveryTimer->start();
    }, Qt::QueuedConnection);
}

int ConnectionWatchdog::calculateRetryDelay() const
{
    // Use Core's backoff calculation if available
    if (m_coreWatchdog != nullptr) {
        uint32_t coreBackoff = op_watchdog_current_backoff_interval(m_coreWatchdog);
        if (coreBackoff > 0) {
            return static_cast<int>(coreBackoff);
        }
    }

    // Fallback: Qt's exponential backoff
    int exponent = qMin(m_retryAttemptCount.load(), 10);
    int delay = m_config.baseRetryDelayMs * (1 << exponent);
    return qMin(delay, m_config.maxRetryDelayMs);
}

void ConnectionWatchdog::updateErrorRate()
{
    // Reset error window if more than the window time has passed
    if (m_errorRateTimer.elapsed() > ERROR_RATE_WINDOW_MS) {
        m_errorsInWindow = 1;
        m_errorRateTimer.restart();
    }
}

bool ConnectionWatchdog::isRecoveryScheduled() const
{
    return m_recoveryTimer && m_recoveryTimer->isActive();
}

// ========== Core Watchdog Integration ==========

void ConnectionWatchdog::createCoreWatchdog()
{
    if (m_coreWatchdog != nullptr) {
        destroyCoreWatchdog();
    }

    // Build Core config from Qt config
    op_watchdog_config_t coreConfig = WatchdogAdapter::toCoreWatchdogConfig(m_config, &m_stubTransport);

    // Set callbacks
    coreConfig.state_cb = WatchdogAdapter::coreStateChangeCallback;
    coreConfig.state_cb_user_data = this;
    coreConfig.health_probe = WatchdogAdapter::coreHealthProbeCallback;
    coreConfig.health_probe_context = this;

    // Create Core watchdog
    op_status_t status = op_watchdog_create(&coreConfig, &m_coreWatchdog);
    if (status != OP_STATUS_OK) {
        qCWarning(log_core_serial) << "Failed to create Core watchdog:" << status;
        m_coreWatchdog = nullptr;
    } else {
        qCInfo(log_core_serial) << "Core watchdog created (Phase 4)";
    }
}

void ConnectionWatchdog::destroyCoreWatchdog()
{
    if (m_coreWatchdog != nullptr) {
        op_watchdog_destroy(m_coreWatchdog);
        m_coreWatchdog = nullptr;
        qCDebug(log_core_serial) << "Core watchdog destroyed";
    }
}

void ConnectionWatchdog::onCoreStateChanged(op_connection_state_t newState, op_status_t lastError)
{
    Q_UNUSED(lastError);

    // Map Core state to Qt state
    ConnectionState qtState = WatchdogAdapter::toQtConnectionState(newState);

    // Sync counters from Core
    if (m_coreWatchdog != nullptr) {
        m_consecutiveErrors = op_watchdog_consecutive_errors(m_coreWatchdog);
        m_totalErrors = op_watchdog_total_errors(m_coreWatchdog);
        m_retryAttemptCount = op_watchdog_recovery_attempts(m_coreWatchdog);
    }

    // Handle state-specific actions
    switch (newState) {
        case OP_CONN_STATE_CONNECTED:
            setConnectionState(ConnectionState::Connected);
            // Reset Qt-side recovery counter when Core reports connected
            m_retryAttemptCount = 0;
            break;

        case OP_CONN_STATE_DEGRADED:
            setConnectionState(ConnectionState::Unstable);
            emit errorThresholdReached(m_consecutiveErrors.load());
            break;

        case OP_CONN_STATE_RECOVERING:
            // Core wants to recover - trigger Qt-side recovery
            // Schedule recovery BEFORE setting state (scheduleRecovery guards against Recovering state)
            if (m_config.autoRecoveryEnabled && m_recoveryHandler) {
                scheduleRecovery();
            }
            setConnectionState(ConnectionState::Recovering);
            break;

        case OP_CONN_STATE_DISCONNECTED:
            // Core gave up - notify Qt
            setConnectionState(ConnectionState::Failed);
            emit recoveryFailed();
            emit statusUpdate("Recovery failed - max retries exceeded");
            if (m_recoveryHandler) {
                m_recoveryHandler->onRecoveryFailed();
            }
            break;
    }
}

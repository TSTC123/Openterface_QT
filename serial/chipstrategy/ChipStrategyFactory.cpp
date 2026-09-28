/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
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

#include "ChipStrategyFactory.h"
#include "openterface/serial_chip_types.h"
#include <QDebug>

// Declare the unified serial logging category (defined in SerialPortManager.cpp)
Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

// Compile-time verification: Qt VID:PID constants match Core definitions
static_assert(static_cast<uint32_t>(ChipTypeId::CH9329) ==
              ((uint32_t)OP_SERIAL_CHIP_CH9329_VID << 16 | OP_SERIAL_CHIP_CH9329_PID),
              "CH9329 VID:PID mismatch between Qt and Core");
static_assert(static_cast<uint32_t>(ChipTypeId::CH32V208) ==
              ((uint32_t)OP_SERIAL_CHIP_CH32V208_VID << 16 | OP_SERIAL_CHIP_CH32V208_PID),
              "CH32V208 VID:PID mismatch between Qt and Core");

// ── Internal conversion helpers ────────────────────────────────────────

static op_serial_chip_type_t toCoreType(ChipTypeId qtType)
{
    switch (qtType) {
        case ChipTypeId::CH9329:    return OP_SERIAL_CHIP_CH9329;
        case ChipTypeId::CH32V208:  return OP_SERIAL_CHIP_CH32V208;
        case ChipTypeId::Unknown:
        default:                    return OP_SERIAL_CHIP_UNKNOWN;
    }
}

static ChipTypeId fromCoreType(op_serial_chip_type_t coreType)
{
    switch (coreType) {
        case OP_SERIAL_CHIP_CH9329:    return ChipTypeId::CH9329;
        case OP_SERIAL_CHIP_CH32V208:  return ChipTypeId::CH32V208;
        case OP_SERIAL_CHIP_UNKNOWN:
        default:                       return ChipTypeId::Unknown;
    }
}

// ── Detection (delegates to Core) ──────────────────────────────────────

ChipTypeId ChipStrategyFactory::detectChipType(const QString& portName)
{
    QList<QSerialPortInfo> availablePorts = QSerialPortInfo::availablePorts();

    for (const QSerialPortInfo& portInfo : availablePorts) {
        if (portName.indexOf(portInfo.portName()) >= 0) {
            uint16_t vid = portInfo.vendorIdentifier();
            uint16_t pid = portInfo.productIdentifier();

            QString vidStr = QString("%1").arg(vid, 4, 16, QChar('0')).toUpper();
            QString pidStr = QString("%1").arg(pid, 4, 16, QChar('0')).toUpper();

            qCDebug(log_core_serial) << "Detected VID:PID =" << vidStr << ":" << pidStr
                                      << "for port" << portName;

            // Delegate detection to Core
            op_serial_chip_type_t coreType = op_serial_chip_detect(vid, pid);
            ChipTypeId chipType = fromCoreType(coreType);

            if (chipType != ChipTypeId::Unknown) {
                qCInfo(log_core_serial) << "Detected" << op_serial_chip_name(coreType)
                                        << "chip for port" << portName;
            } else {
                qCWarning(log_core_serial) << "Unknown chip type for port" << portName;
            }
            return chipType;
        }
    }

    qCWarning(log_core_serial) << "Unknown chip type for port" << portName;
    return ChipTypeId::Unknown;
}

std::unique_ptr<IChipStrategy> ChipStrategyFactory::createStrategy(ChipTypeId chipType)
{
    switch (chipType) {
        case ChipTypeId::CH9329:
            qCDebug(log_core_serial) << "Creating CH9329 strategy";
            return std::make_unique<CH9329Strategy>();
            
        case ChipTypeId::CH32V208:
            qCDebug(log_core_serial) << "Creating CH32V208 strategy";
            return std::make_unique<CH32V208Strategy>();
            
        case ChipTypeId::Unknown:
        default:
            // Default to CH9329 strategy for unknown chips (backward compatible)
            qCWarning(log_core_serial) << "Unknown chip type, using CH9329 strategy as fallback";
            return std::make_unique<CH9329Strategy>();
    }
}

std::unique_ptr<IChipStrategy> ChipStrategyFactory::createStrategyForPort(const QString& portName)
{
    ChipTypeId chipType = detectChipType(portName);
    return createStrategy(chipType);
}

QString ChipStrategyFactory::chipTypeName(ChipTypeId chipType)
{
    return QString::fromUtf8(op_serial_chip_name(toCoreType(chipType)));
}

bool ChipStrategyFactory::supportsCommands(ChipTypeId chipType)
{
    return op_serial_chip_supports_command_config(toCoreType(chipType));
}

bool ChipStrategyFactory::supportsUsbSwitch(ChipTypeId chipType)
{
    return op_serial_chip_supports_usb_switch(toCoreType(chipType));
}

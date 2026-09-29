#include "DeviceInfo.h"
#include "openterface/profile.h"
#include <cstring>

DeviceInfo::DeviceInfo(const QString& portChain)
    : portChain(portChain)
    , hasCompanionDevice(false)
    , lastSeen(QDateTime::currentDateTime())
{
}

QVariantMap DeviceInfo::toMap() const
{
    QVariantMap map;
    map["portChain"] = portChain;
    map["deviceInstanceId"] = deviceInstanceId;
    map["vid"] = vid;
    map["pid"] = pid;
    map["hidVid"] = hidVid;
    map["hidPid"] = hidPid;
    map["companionPortChain"] = companionPortChain;
    map["hasCompanionDevice"] = hasCompanionDevice;
    map["serialPortPath"] = serialPortPath;
    map["hidDevicePath"] = hidDevicePath;
    map["cameraDevicePath"] = cameraDevicePath;
    map["audioDevicePath"] = audioDevicePath;
    map["serialPortId"] = serialPortId;
    map["hidDeviceId"] = hidDeviceId;
    map["cameraDeviceId"] = cameraDeviceId;
    map["audioDeviceId"] = audioDeviceId;
    map["platformSpecific"] = platformSpecific;
    map["lastSeen"] = lastSeen;
    return map;
}

void DeviceInfo::fromMap(const QVariantMap& map)
{
    portChain = map.value("portChain").toString();
    deviceInstanceId = map.value("deviceInstanceId").toString();
    vid = map.value("vid").toString();
    pid = map.value("pid").toString();
    hidVid = map.value("hidVid").toString();
    hidPid = map.value("hidPid").toString();
    companionPortChain = map.value("companionPortChain").toString();
    hasCompanionDevice = map.value("hasCompanionDevice", false).toBool();
    serialPortPath = map.value("serialPortPath").toString();
    hidDevicePath = map.value("hidDevicePath").toString();
    cameraDevicePath = map.value("cameraDevicePath").toString();
    audioDevicePath = map.value("audioDevicePath").toString();
    serialPortId = map.value("serialPortId").toString();
    hidDeviceId = map.value("hidDeviceId").toString();
    cameraDeviceId = map.value("cameraDeviceId").toString();
    audioDeviceId = map.value("audioDeviceId").toString();
    platformSpecific = map.value("platformSpecific").toMap();
    lastSeen = map.value("lastSeen").toDateTime();
}

QString DeviceInfo::getUniqueKey() const
{
    if (!portChain.isEmpty()) {
        return portChain;
    }
    if (!deviceInstanceId.isEmpty()) {
        return deviceInstanceId;
    }
    return serialPortPath + "|" + hidDevicePath;
}

bool DeviceInfo::isValid() const
{
    return !portChain.isEmpty() || !deviceInstanceId.isEmpty() || 
           !serialPortPath.isEmpty() || !hidDevicePath.isEmpty();
}

bool DeviceInfo::operator==(const DeviceInfo& other) const
{
    return portChain == other.portChain &&
           deviceInstanceId == other.deviceInstanceId &&
           vid == other.vid &&
           pid == other.pid &&
           hidVid == other.hidVid &&
           hidPid == other.hidPid &&
           companionPortChain == other.companionPortChain &&
           hasCompanionDevice == other.hasCompanionDevice &&
           serialPortPath == other.serialPortPath &&
           serialPortId == other.serialPortId &&
           hidDevicePath == other.hidDevicePath &&
           hidDeviceId == other.hidDeviceId &&
           cameraDevicePath == other.cameraDevicePath &&
           cameraDeviceId == other.cameraDeviceId &&
           audioDevicePath == other.audioDevicePath &&
           audioDeviceId == other.audioDeviceId &&
           platformSpecific == other.platformSpecific;
}

bool DeviceInfo::operator!=(const DeviceInfo& other) const
{
    return !(*this == other);
}

// ── Core integration: device info, profile, and capability ──
//
// These methods bridge Qt's DeviceInfo with Core's device management APIs.
// Core serves as the single source of truth for device identification,
// profile matching, and capability queries, while Qt handles UI and
// platform-specific backends.

op_device_info_t DeviceInfo::toCoreDeviceInfo() const
{
    // Convert Qt DeviceInfo to Core's op_device_info_t format.
    // Populates device identification, interface paths, and matches
    // the device profile to retrieve capabilities, default baudrate,
    // and protocol flags from Core's profile database.
    op_device_info_t coreDevice;
    op_device_info_init(&coreDevice);

    // Copy device identification
    if (!deviceInstanceId.isEmpty()) {
        QByteArray id = deviceInstanceId.toLocal8Bit();
        strncpy(coreDevice.device_id, id.constData(), OP_DEVICE_ID_CAP - 1);
        coreDevice.device_id[OP_DEVICE_ID_CAP - 1] = '\0';
    }

    if (!portChain.isEmpty()) {
        QByteArray chain = portChain.toLocal8Bit();
        strncpy(coreDevice.port_chain, chain.constData(), OP_PORT_CHAIN_CAP - 1);
        coreDevice.port_chain[OP_PORT_CHAIN_CAP - 1] = '\0';
    }

    // Convert VID/PID from hex string to uint16_t
    if (!vid.isEmpty()) {
        bool ok;
        uint16_t v = vid.toUInt(&ok, 16);
        if (ok) coreDevice.vendor_id = v;
    }

    if (!pid.isEmpty()) {
        bool ok;
        uint16_t p = pid.toUInt(&ok, 16);
        if (ok) coreDevice.product_id = p;
    }

    // Copy interface paths
    if (!serialPortPath.isEmpty()) {
        QByteArray path = serialPortPath.toLocal8Bit();
        strncpy(coreDevice.serial_path, path.constData(), OP_DEVICE_PATH_CAP - 1);
        coreDevice.serial_path[OP_DEVICE_PATH_CAP - 1] = '\0';
        coreDevice.interface_flags |= OP_DEVICE_IF_SERIAL;
    }

    if (!hidDevicePath.isEmpty()) {
        QByteArray path = hidDevicePath.toLocal8Bit();
        strncpy(coreDevice.hid_path, path.constData(), OP_DEVICE_PATH_CAP - 1);
        coreDevice.hid_path[OP_DEVICE_PATH_CAP - 1] = '\0';
        coreDevice.interface_flags |= OP_DEVICE_IF_HID;
    }

    if (!cameraDevicePath.isEmpty()) {
        QByteArray path = cameraDevicePath.toLocal8Bit();
        strncpy(coreDevice.camera_path, path.constData(), OP_DEVICE_PATH_CAP - 1);
        coreDevice.camera_path[OP_DEVICE_PATH_CAP - 1] = '\0';
        coreDevice.interface_flags |= OP_DEVICE_IF_CAMERA;
    }

    if (!audioDevicePath.isEmpty()) {
        QByteArray path = audioDevicePath.toLocal8Bit();
        strncpy(coreDevice.audio_path, path.constData(), OP_DEVICE_PATH_CAP - 1);
        coreDevice.audio_path[OP_DEVICE_PATH_CAP - 1] = '\0';
        coreDevice.interface_flags |= OP_DEVICE_IF_AUDIO;
    }

    // Try to match profile to populate capabilities
    const op_device_profile_t* profile = op_profile_match(
        coreDevice.vendor_id,
        coreDevice.product_id,
        coreDevice.interface_flags
    );

    if (profile) {
        strncpy(coreDevice.profile_id, profile->profile_id, OP_DEVICE_ID_CAP - 1);
        coreDevice.profile_id[OP_DEVICE_ID_CAP - 1] = '\0';
        coreDevice.capabilities = profile->capabilities;
        coreDevice.default_baudrate = profile->default_baudrate;
        coreDevice.protocol_flags = profile->protocol_flags;
        coreDevice.chip_hint = profile->chip_hint;
    }

    return coreDevice;
}

// Query device capability via Core's op_device_info_has_capability() API
bool DeviceInfo::hasCapability(op_capability_id_t capability) const
{
    op_device_info_t coreDevice = toCoreDeviceInfo();
    return op_device_info_has_capability(&coreDevice, capability) != 0;
}

// Get device capabilities from matched profile
op_capability_flags_t DeviceInfo::getCapabilities() const
{
    op_device_info_t coreDevice = toCoreDeviceInfo();
    return coreDevice.capabilities;
}

// Match device to Core profile by VID/PID/interface flags
// Returns profile ID if found, empty string otherwise
QString DeviceInfo::matchProfile() const
{
    op_device_info_t coreDevice = toCoreDeviceInfo();
    const op_device_profile_t* profile = op_profile_match(
        coreDevice.vendor_id,
        coreDevice.product_id,
        coreDevice.interface_flags
    );

    if (profile) {
        return QString::fromUtf8(profile->profile_id);
    }
    return QString();
}

// Check if device matches a specific profile ID
bool DeviceInfo::matchesProfile(const QString& profileId) const
{
    QString matchedProfile = matchProfile();
    return !matchedProfile.isEmpty() && matchedProfile == profileId;
}

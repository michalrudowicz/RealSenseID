// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 Intel Corporation. All Rights Reserved.

#pragma once

#include "RealSenseID/RealSenseIDExports.h"
#include "RealSenseID/Status.h"
#ifdef ANDROID
#include "RealSenseID/AndroidSerialConfig.h"
#endif

#include <string>
#include <vector>

namespace RealSenseID
{
/**
 * FwUpdater class.
 * Handles firmware update operations.
 */
class RSID_API FwUpdater
{
public:
    /**
     * Firmware update related settings.
     */
    struct Settings
    {
        const char* port = nullptr; // serial port to perform the update on
        bool force_full = false;    // if true update all modules and blocks regardless of crc checks
#ifdef ANDROID
        AndroidSerialConfig android_config;
#endif
    };

    /**
     * User defined callback for firmware update events.
     * Callback will be used to provide feedback to the client.
     */
    struct EventHandler
    {
        virtual ~EventHandler() = default;

        /**
         * Called to inform the client of the overall firmware update progress.
         *
         * @param[in] progress Current firmware update progress, range: 0.0f - 1.0f.
         */
        virtual void OnProgress(float progress) = 0;
    };

    FwUpdater() = default;
    ~FwUpdater() = default;

    /**
     * Extracts the firmware and recognition version from the firmware package, as well as all the modules names.
     *
     * @param[in] binPath Path to the firmware binary file.
     * @param[out] outFwVersion Operational firmware (OPFW) version string.
     * @param[out] outRecognitionVersion Recognition model version string.
     * @param[out] moduleNames Names of modules found in the binary file.
     * @return True if extraction succeeded and false otherwise.
     */
    bool ExtractFwInformation(const char* binPath, std::string& outFwVersion, std::string& outRecognitionVersion, std::vector<std::string>& moduleNames) const;
    
    /**
     * Check encryption used in the binary file and answer whether a device with given serial number can decrypt it.
     *
	 * @param[in] binPath Path to the firmware binary file.
     * @param[in] deviceSerialNumber The device serial number as it was extracted prior to calling this function.
	 */
    bool IsEncryptionSupported(const char* binPath, const std::string& deviceSerialNumber);

    /**
     * Performs a firmware update for the modules listed in moduleNames
     *
     * @param[in] handler Responsible for handling events triggered during the update.
     * @param[in] Settings Firmware update settings.
     * @param[in] binPath Path to the firmware binary file.
     * @param[in] moduleNames list of module names to update.
     * @return OK if update succeeded matching error status if it failed.
     */
    Status UpdateModules(EventHandler* handler, Settings settings, const char* binPath, const std::vector<std::string>& moduleNames) const;
};
} // namespace RealSenseID

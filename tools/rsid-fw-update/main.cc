// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 Intel Corporation. All Rights Reserved.
#include "RealSenseID/FwUpdater.h"
#include "RealSenseID/DeviceController.h"
#include "RealSenseID/DiscoverDevices.h"
#include "RealSenseID/Version.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <memory>
#include <exception>
#include <cstring>
#include <utility>
#include <vector>
#include <thread>
#include <chrono>


static constexpr int SUCCESS_MAIN = 0;
static constexpr int FAILURE_MAIN = 1;
static constexpr int MIN_WAIT_FOR_DEVICE = 6;
static constexpr int MAX_WAIT_FOR_DEVICE = 30;
static const std::string OPFW = "OPFW";
static const std::string RECOG = "RECOG";

struct DeviceMetadata
{
    std::string serial_number = "Unknown";
    std::string fw_version = "Unknown";
    std::string recognition_version = "Unknown";
};

struct FullDeviceInfo
{
    std::unique_ptr<DeviceMetadata> metadata;
    std::unique_ptr<RealSenseID::DeviceInfo> config;
};

/* User interaction */
static int UserDeviceSelection(const std::vector<FullDeviceInfo>& devices)
{
    std::cout << "Detected devices:\n";
    for (int i = 0; i < devices.size(); ++i)
    {
        const auto& device = devices.at(i);
        std::cout << " " << i + 1 << ") S/N: " << device.metadata->serial_number << " "
                  << "FW: " << device.metadata->fw_version << " "
                  << "Port: " << device.config->serialPort << "\n";
    }

    int device_index = -1;

    while (device_index < 1 || device_index > devices.size())
    {
        std::cout << "> ";

        std::string line;
        std::getline(std::cin, line);

        try
        {
            device_index = std::stoi(line);
        }
        catch (...)
        {
            device_index = -1;
        }
    }

    std::cout << "\n";

    return device_index - 1;
}

static bool UserApproval()
{
    char key = '0';
    while (key != 'y' && key != 'n')
    {
        std::cout << "> ";
        std::cin >> key;
    }

    return key == 'y';
}

/* Misc */

static std::string ExtractModuleFromVersion(const std::string& module_name, const std::string& full_version)
{
    std::stringstream version_stream(full_version);
    std::string section;
    while (std::getline(version_stream, section, '|'))
    {
        if (section.find(module_name) != section.npos)
        {
            auto pos = section.find(":");
            auto sub = section.substr(pos + 1, section.npos);
            return sub;
        }
    }

    return "Unknown";
}

static std::string ParseFirmwareVersion(const std::string& full_version)
{
    return ExtractModuleFromVersion("OPFW:", full_version);
}

static std::string ParseRecognitionVersion(const std::string& full_version)
{
    return ExtractModuleFromVersion("RECOG:", full_version);
}

/* Command line arguments */

struct CommandLineArgs
{
    bool is_valid = false;        // was parsing successful
    bool force_version = false;   // force non-compatible versions
    bool force_full = false;      // force update of all modules even if already exist in the fw
    bool is_interactive = false;  // ask user for confirmation before starting
    bool auto_approve = false;    // automatically approve all (use default params)
    std::string fw_file = "";     // path to firmware update binary
    std::string serial_port = ""; // serial port
};

static CommandLineArgs ParseCommandLineArgs(int argc, char* argv[])
{
    CommandLineArgs args;

    if (argc < 2)
    {
        std::cout << "usage: " << argv[0]
                  << " --file <bin path> [--port <COM#>] [--force-version] [--force-full] [--interactive / --auto-approve]\n";
        return args;
    }

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--file") == 0)
        {
            if (i + 1 < argc)
                args.fw_file = argv[++i];
        }
        else if (strcmp(argv[i], "--port") == 0)
        {
            if (i + 1 < argc)
                args.serial_port = argv[++i];
        }
        else if (strcmp(argv[i], "--force-full") == 0)
        {
            args.force_full = true;
        }
        else if (strcmp(argv[i], "--force-version") == 0)
        {
            args.force_version = true;
        }
        else if (strcmp(argv[i], "--interactive") == 0)
        {
            args.is_interactive = true;
        }
        else if (strcmp(argv[i], "--auto-approve") == 0)
        {
            args.auto_approve = true;
        }
    }

    // Make sure all required options are available.
    if (args.fw_file.empty())
        return args;
    
    if (args.is_interactive && args.auto_approve)
    {
        std::cout << "--is-interactive and --auto-approve flags do not co-exist. Choose either or none." << std::endl;
        return args;
    }
    args.is_valid = true;
    return args;
}

static DeviceMetadata QueryDeviceMetadata(const RealSenseID::SerialConfig& serial_config)
{
    DeviceMetadata metadata;

    RealSenseID::DeviceController device_controller;

    device_controller.Connect(serial_config);

    std::string fw_version;
    device_controller.QueryFirmwareVersion(fw_version);
    if (!fw_version.empty())
    {
        metadata.fw_version = ParseFirmwareVersion(fw_version);
        metadata.recognition_version = ParseRecognitionVersion(fw_version);
    }

    std::string serial_number;
    device_controller.QuerySerialNumber(serial_number);
    if (!serial_number.empty())
        metadata.serial_number = serial_number;

    device_controller.Disconnect();

    return metadata;
}

static bool IsDeviceAlive(const RealSenseID::SerialConfig& serial_config)
{
    RealSenseID::DeviceController device_controller;
    device_controller.Connect(serial_config);

    RealSenseID::Status status = device_controller.Ping();

    device_controller.Disconnect();

    return status == RealSenseID::Status::Ok;
}

static bool WaitForDevice(int min_wait_seconds, int max_wait_seconds, const char* port)
{
    bool isDeviceAlive = false;
    std::this_thread::sleep_for(std::chrono::seconds(min_wait_seconds));
    int waitCounter = min_wait_seconds;
    while (!isDeviceAlive && waitCounter < max_wait_seconds)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        waitCounter++;
        std::cout << "Waited for device to become available for " << waitCounter << " seconds" << std::endl;
        isDeviceAlive = IsDeviceAlive(RealSenseID::SerialConfig {port});
    }
    return isDeviceAlive;
}

struct FwUpdaterCliEventHandler : public RealSenseID::FwUpdater::EventHandler
{
public:
    FwUpdaterCliEventHandler(float minValue, float maxValue) : m_minValue(minValue), m_maxValue(maxValue)
    {}

    virtual void OnProgress(float progress) override
    {
        float adjustedProgress = m_minValue + progress * (m_maxValue - m_minValue);
        constexpr int progress_bars = 80;
        std::cout << "[";
        int progress_marker = static_cast<int>(progress_bars * adjustedProgress);
        for (int bar = 0; bar < progress_bars; ++bar)
        {
            char to_print = bar < progress_marker ? ':' : ' ';
            std::cout << to_print;
        }
        std::cout << "] " << static_cast<int>(adjustedProgress * 100) << " %\r";
        std::cout.flush();
    }

private:
    float m_minValue, m_maxValue;
};

int main(int argc, char* argv[])
{
    // parse cli args
    auto args = ParseCommandLineArgs(argc, argv);
    if (!args.is_valid)
        return FAILURE_MAIN;

    // populate device list
    std::vector<FullDeviceInfo> devices_info;
    bool auto_detect = args.serial_port.empty();
    if (auto_detect)
    {
        std::cout << "Using device auto detection...\n\n";

        auto detected_devices = RealSenseID::DiscoverDevices();

        for (const auto& detected_device : detected_devices)
        {
            auto metadata = QueryDeviceMetadata(RealSenseID::SerialConfig {detected_device.serialPort});

            FullDeviceInfo device {std::make_unique<DeviceMetadata>(metadata),
                                   std::make_unique<RealSenseID::DeviceInfo>(detected_device)};

            devices_info.push_back(std::move(device));
        }
    }
    else
    {
        std::cout << "Using manual device selection...\n\n";

        auto metadata = QueryDeviceMetadata(RealSenseID::SerialConfig {args.serial_port.c_str()});

        auto device_info = std::make_unique<RealSenseID::DeviceInfo>();
        ::strncpy(device_info->serialPort, args.serial_port.data(), args.serial_port.size());

        FullDeviceInfo device {std::make_unique<DeviceMetadata>(metadata), std::move(device_info)};

        devices_info.push_back(std::move(device));
    }

    if (devices_info.empty())
    {
        std::cout << "No devices found!\n";
        return FAILURE_MAIN;
    }

    // if more than one device exists - ask user to select
    auto id = devices_info.size() == 1 ? 0 : UserDeviceSelection(devices_info);
    const auto& selected_device = devices_info.at(id);

    // extract fw version from update file
    RealSenseID::FwUpdater fw_updater;

    const auto& bin_path = args.fw_file.c_str();

    std::string new_fw_version;
    std::string new_recognition_version;
    std::vector<std::string> moduleNames;
    auto is_valid = fw_updater.ExtractFwInformation(bin_path, new_fw_version, new_recognition_version, moduleNames);

    if (!is_valid)
    {
        std::cout << "Invalid firmware file !\n";
        return FAILURE_MAIN;
    }

    if (!fw_updater.IsEncryptionSupported(bin_path, selected_device.metadata->serial_number))
    {
        std::cout << "Device does not support the encryption applied on the firmware. Replace firmware binary.\n";
        return FAILURE_MAIN;
    }

    // check compatibility with host
    const auto& current_fw_version = selected_device.metadata->fw_version;
    const auto current_compatible = RealSenseID::IsFwCompatibleWithHost(current_fw_version);
    const auto new_compatible = RealSenseID::IsFwCompatibleWithHost(new_fw_version);

    const auto& current_recognition_version = selected_device.metadata->recognition_version;
    const auto is_database_compatible = current_recognition_version == new_recognition_version;

    // show summary to user - update path, compatibility checks
    std::cout << "\n";
    std::cout << "Summary:\n";
    std::cout << " * Serial number: " << selected_device.metadata->serial_number << "\n";
    std::cout << " * Serial port: " << selected_device.config->serialPort << "\n";
    std::cout << " * " << (current_compatible ? "Compatible" : "Incompatible") << " with current device firmware\n";
    std::cout << " * " << (new_compatible ? "Compatible" : "Incompatible") << " with new device firmware\n";
    std::cout << " * Firmware update path:\n";
    std::cout << "     * OPFW: " << current_fw_version << " -> " << new_fw_version << "\n";
    std::cout << "     * RECOG: " << current_recognition_version << " -> " << new_recognition_version << "\n";
    std::cout << "\n";

    // ask user for approval if interactive
    if (args.is_interactive)
    {
        std::cout << "Proceed with update? (y/n)\n";
        bool user_agreed = UserApproval();
        if (!user_agreed)
            return FAILURE_MAIN;
        std::cout << "\n";
    }

    // allow bypass of incompatible version if forced
    if (!new_compatible && !args.force_version)
    {
        std::cout << "Version is incompatible with the current host version!\n";
        std::cout << "Use --force-version to force the update.\n ";
        return FAILURE_MAIN;
    }

    bool update_recognition = is_database_compatible || args.auto_approve ? true : false;
    if (!is_database_compatible)
    {
        std::cout << "Clear faceprints database and update the recognition module? (y/n)\n";
        if (args.auto_approve)
            std::cout << "Auto approve: (y)" << std::endl;
        else 
            update_recognition = UserApproval();
        std::cout << "\n";
    }

    if (!update_recognition)
    {
        moduleNames.erase(std::remove_if(moduleNames.begin(), moduleNames.end(),
                                         [](const std::string& moduleName) { return moduleName.compare(RECOG) == 0; }),
                          moduleNames.end());
    }
    auto numberOfModules = moduleNames.size();
    // create fw-updater settings and progress callback
    auto event_handler = std::make_unique<FwUpdaterCliEventHandler>(0.f, 1.f / numberOfModules);
    RealSenseID::FwUpdater::Settings settings;
    settings.port = selected_device.config->serialPort;
    settings.force_full = args.force_full;

    // Temprorarily disable two step installing. Flash all modules sequentially.
    /**
    // attempt firmware update and return success/failure according to result
    std::vector<std::string> modulesVector;
    modulesVector.push_back(OPFW);
    auto success = fw_updater.UpdateModules(event_handler.get(), settings, args.fw_file.c_str(), modulesVector) ==
                   RealSenseID::Status::Ok;
    if (success)
    {
        moduleNames.erase(std::remove_if(moduleNames.begin(), moduleNames.end(),
                                         [](const std::string& moduleName) { return moduleName.compare(OPFW) == 0; }),
                          moduleNames.end());
        bool isDeviceAlive = WaitForDevice(MIN_WAIT_FOR_DEVICE, MAX_WAIT_FOR_DEVICE, selected_device.config->serialPort);
        if (isDeviceAlive)
        {
            event_handler = std::make_unique<FwUpdaterCliEventHandler>(1.f / numberOfModules, 1.f);
            RealSenseID::Status status = fw_updater.UpdateModules(event_handler.get(), settings, args.fw_file.c_str(), moduleNames);
            success = status == RealSenseID::Status::Ok;
        }
    }
    **/
    event_handler = std::make_unique<FwUpdaterCliEventHandler>(0.f, 1.f);
    RealSenseID::Status status =
        fw_updater.UpdateModules(event_handler.get(), settings, args.fw_file.c_str(), moduleNames);
    auto success = status == RealSenseID::Status::Ok;

    std::cout << "\n\n";
    std::cout << "Firmware update" << (success ? " finished successfully " : " failed ") << "\n";

    return success ? SUCCESS_MAIN : FAILURE_MAIN;
}

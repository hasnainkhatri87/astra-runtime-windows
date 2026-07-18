#pragma once

#include "astra/package_store.hpp"

#include <filesystem>
#include <string>

namespace astra {

// Astra keeps the optional Android runtime separate from the portable host.
// The runtime consists only of user-downloaded, official SDK components.
struct AndroidVmStatus {
    bool firmware_virtualization_ready{false};
    bool sdk_ready{false};
    bool avd_ready{false};
    bool acceleration_ready{false};
    bool device_ready{false};
    std::filesystem::path runtime_root;
    std::filesystem::path sdk_root;
    std::filesystem::path avd_home;
    std::wstring message;
};

struct AndroidVmResult {
    bool ok{false};
    std::wstring message;
};

std::filesystem::path AndroidVmRoot();
std::filesystem::path AndroidSdkRoot();
std::filesystem::path AndroidAvdHome();

AndroidVmStatus InspectAndroidVm();
AndroidVmResult StartAndroidVm();
AndroidVmResult InstallAndRunInAndroidVm(const InstalledApp& app);

}  // namespace astra

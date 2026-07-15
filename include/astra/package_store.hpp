#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace astra {

struct InstalledApp {
    std::wstring id;
    std::wstring display_name;
    std::wstring package_name;
    std::wstring launcher_activity;
    std::filesystem::path package_path;
    std::filesystem::path data_path;
    std::wstring container_sha256;
};

struct InstallResult {
    bool ok{false};
    std::wstring message;
    InstalledApp app;
};

std::filesystem::path AppStoreRoot();
bool IsSupportedPackageExtension(const std::filesystem::path& path);
bool HasZipContainerSignature(const std::filesystem::path& path);
std::wstring SanitizeId(std::wstring value);

InstallResult InstallPackage(const std::filesystem::path& source);
std::vector<InstalledApp> ListInstalledApps();

}  // namespace astra

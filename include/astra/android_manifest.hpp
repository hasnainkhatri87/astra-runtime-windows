#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace astra {

struct AndroidManifestInfo {
    bool ok{false};
    std::wstring message;
    std::wstring package_name;
    std::wstring version_name;
    std::wstring application_class;
    std::wstring launcher_component;
    std::wstring launcher_activity;
    std::uint32_t version_code{};
    std::uint32_t min_sdk{};
    std::uint32_t target_sdk{};
    std::size_t activity_count{};
    std::size_t launcher_count{};
    std::filesystem::path binary_manifest_path;
};

AndroidManifestInfo ParseAndroidBinaryManifest(
    const std::filesystem::path& binary_manifest);

AndroidManifestInfo ReadAndroidManifestFromApk(
    const std::filesystem::path& verified_base_apk,
    const std::filesystem::path& app_root);

}  // namespace astra

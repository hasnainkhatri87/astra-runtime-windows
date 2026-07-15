#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace astra {

struct ProvisionResult {
    bool ok{false};
    std::wstring message;
    std::filesystem::path base_apk;
    std::vector<std::filesystem::path> selected_apks;
};

ProvisionResult ProvisionPackageSplits(
    const std::filesystem::path& source,
    const std::filesystem::path& app_root);

}  // namespace astra

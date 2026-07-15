#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace astra {

struct ProvisionResult {
    bool ok{false};
    std::wstring message;
    std::vector<std::filesystem::path> selected_apks;
};

ProvisionResult ProvisionPackageSplits(
    const std::filesystem::path& source,
    const std::filesystem::path& app_root);

}  // namespace astra


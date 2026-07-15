#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace astra {

struct DexExecutionResult {
    bool ok{false};
    std::int32_t value{};
    std::uint64_t instructions_executed{};
    std::wstring message;
};

// Executes a checksum-verified, static, no-argument DEX method returning int.
// This is the first managed-execution proof, not a replacement for full ART.
DexExecutionResult ExecuteStaticIntMethod(
    const std::filesystem::path& dex_path,
    const std::string& class_descriptor,
    const std::string& method_name);

}  // namespace astra

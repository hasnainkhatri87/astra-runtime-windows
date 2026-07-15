#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace astra {

struct DexInfo {
    std::filesystem::path path;
    std::wstring source_entry;
    std::wstring version;
    std::uint32_t file_size{};
    std::uint32_t string_ids{};
    std::uint32_t type_ids{};
    std::uint32_t method_ids{};
    std::uint32_t class_defs{};
};

struct DexCatalogResult {
    bool ok{false};
    std::wstring message;
    std::vector<DexInfo> dex_files;
};

DexCatalogResult CatalogDexPayloads(
    const std::vector<std::filesystem::path>& verified_apks,
    const std::filesystem::path& app_root);

}  // namespace astra

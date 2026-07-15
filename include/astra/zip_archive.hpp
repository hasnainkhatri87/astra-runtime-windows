#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace astra {

struct ZipEntry {
    std::wstring name;
    std::uint16_t flags{};
    std::uint16_t method{};
    std::uint32_t crc32{};
    std::uint32_t compressed_size{};
    std::uint32_t uncompressed_size{};
    std::uint32_t local_header_offset{};
    bool is_directory{};
};

class ZipArchive {
public:
    static ZipArchive Open(const std::filesystem::path& path);

    const std::filesystem::path& path() const noexcept { return path_; }
    const std::vector<ZipEntry>& entries() const noexcept { return entries_; }

    void Extract(const ZipEntry& entry,
                 const std::filesystem::path& destination) const;

private:
    std::filesystem::path path_;
    std::uint64_t file_size_{};
    std::uint64_t central_directory_offset_{};
    std::vector<ZipEntry> entries_;
};

}  // namespace astra


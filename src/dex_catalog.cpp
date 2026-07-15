#include "astra/dex_catalog.hpp"

#include "astra/zip_archive.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace astra {
namespace {

constexpr std::size_t kDexHeaderSize = 0x70;

std::uint16_t U16(const unsigned char* value) {
    return static_cast<std::uint16_t>(value[0]) |
           (static_cast<std::uint16_t>(value[1]) << 8U);
}

std::uint32_t U32(const unsigned char* value) {
    return static_cast<std::uint32_t>(value[0]) |
           (static_cast<std::uint32_t>(value[1]) << 8U) |
           (static_cast<std::uint32_t>(value[2]) << 16U) |
           (static_cast<std::uint32_t>(value[3]) << 24U);
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool IsDexEntry(const ZipEntry& entry) {
    if (entry.is_directory || entry.name.find(L'/') != std::wstring::npos) {
        return false;
    }
    const auto base = Lower(entry.name);
    if (base == L"classes.dex") {
        return true;
    }
    if (base.size() <= 11 || base.rfind(L"classes", 0) != 0 ||
        base.substr(base.size() - 4) != L".dex") {
        return false;
    }
    return std::all_of(base.begin() + 7, base.end() - 4,
                       [](wchar_t c) { return std::iswdigit(c) != 0; });
}

void ReadExact(std::ifstream& input, void* output, std::size_t size) {
    input.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("Unexpected end of DEX file");
    }
}

struct AlgorithmCloser {
    void operator()(void* handle) const noexcept {
        if (handle) {
            BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(handle), 0);
        }
    }
};

struct HashCloser {
    void operator()(void* handle) const noexcept {
        if (handle) {
            BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(handle));
        }
    }
};

void CheckNt(NTSTATUS status, const char* message) {
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error(message);
    }
}

void CheckSection(std::uint32_t count, std::uint32_t offset,
                  std::uint32_t item_size, std::uint32_t section_limit,
                  const char* name) {
    if (count == 0) {
        if (offset != 0) {
            throw std::runtime_error(std::string(name) + " has offset without items");
        }
        return;
    }
    const std::uint64_t end = static_cast<std::uint64_t>(offset) +
                              static_cast<std::uint64_t>(count) * item_size;
    if (offset < kDexHeaderSize || (offset & 3U) != 0 || end > section_limit) {
        throw std::runtime_error(std::string(name) + " range exceeds DEX file");
    }
}

void ValidateMap(std::ifstream& input,
                 const std::array<unsigned char, kDexHeaderSize>& header,
                 std::uint32_t file_size, std::uint32_t data_offset,
                 std::uint32_t map_offset) {
    if ((map_offset & 3U) != 0 || map_offset < data_offset ||
        static_cast<std::uint64_t>(map_offset) + 4 > file_size) {
        throw std::runtime_error("DEX map offset is invalid");
    }

    input.clear();
    input.seekg(static_cast<std::streamoff>(map_offset));
    std::array<unsigned char, 4> count_bytes{};
    ReadExact(input, count_bytes.data(), count_bytes.size());
    const std::uint32_t count = U32(count_bytes.data());
    if (count == 0 || count > 8192 ||
        static_cast<std::uint64_t>(map_offset) + 4ULL +
                static_cast<std::uint64_t>(count) * 12ULL >
            file_size) {
        throw std::runtime_error("DEX map list range is invalid");
    }

    struct HeaderSection {
        std::uint16_t type;
        std::uint32_t count;
        std::uint32_t offset;
    };
    const std::array<HeaderSection, 7> expected{{
        {0x0000U, 1U, 0U},
        {0x0001U, U32(header.data() + 0x38), U32(header.data() + 0x3c)},
        {0x0002U, U32(header.data() + 0x40), U32(header.data() + 0x44)},
        {0x0003U, U32(header.data() + 0x48), U32(header.data() + 0x4c)},
        {0x0004U, U32(header.data() + 0x50), U32(header.data() + 0x54)},
        {0x0005U, U32(header.data() + 0x58), U32(header.data() + 0x5c)},
        {0x0006U, U32(header.data() + 0x60), U32(header.data() + 0x64)},
    }};

    std::set<std::uint16_t> seen_types;
    std::uint32_t previous_offset = 0;
    bool saw_map = false;
    std::array<bool, 7> saw_expected{};
    for (std::uint32_t index = 0; index < count; ++index) {
        std::array<unsigned char, 12> item{};
        ReadExact(input, item.data(), item.size());
        const std::uint16_t type = U16(item.data());
        const std::uint16_t unused = U16(item.data() + 2);
        const std::uint32_t size = U32(item.data() + 4);
        const std::uint32_t offset = U32(item.data() + 8);
        if (unused != 0 || size == 0 || offset >= file_size ||
            (index != 0 && offset <= previous_offset) ||
            !seen_types.insert(type).second) {
            throw std::runtime_error("DEX map item is invalid or out of order");
        }
        previous_offset = offset;
        if (type == 0x1000U) {
            if (size != 1 || offset != map_offset) {
                throw std::runtime_error("DEX map-list item does not match map_off");
            }
            saw_map = true;
        }
        for (std::size_t expected_index = 0;
             expected_index < expected.size(); ++expected_index) {
            const auto& section = expected[expected_index];
            if (type == section.type) {
                if (section.count == 0 || size != section.count ||
                    offset != section.offset) {
                    throw std::runtime_error(
                        "DEX map item does not match header section");
                }
                saw_expected[expected_index] = true;
            }
        }
    }
    if (!saw_map || !saw_expected[0]) {
        throw std::runtime_error("DEX map omits the header or map-list item");
    }
    for (std::size_t index = 1; index < expected.size(); ++index) {
        if ((expected[index].count != 0) != saw_expected[index]) {
            throw std::runtime_error("DEX map omits or invents a header section");
        }
    }
}

DexInfo ValidateDex(const std::filesystem::path& path,
                    const std::wstring& source_entry) {
    const auto actual_size = std::filesystem::file_size(path);
    if (actual_size < kDexHeaderSize ||
        actual_size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("DEX file size is invalid");
    }

    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, kDexHeaderSize> header{};
    ReadExact(input, header.data(), header.size());
    if (std::memcmp(header.data(), "dex\n", 4) != 0 || header[7] != 0 ||
        !std::isdigit(header[4]) || !std::isdigit(header[5]) ||
        !std::isdigit(header[6])) {
        throw std::runtime_error("DEX magic or version is invalid");
    }

    const std::string version(reinterpret_cast<const char*>(header.data() + 4), 3);
    static const std::set<std::string> supported_versions = {
        "035", "037", "038", "039", "040"};
    if (!supported_versions.contains(version)) {
        throw std::runtime_error(
            "DEX version is unsupported (v041 containers are not enabled yet)");
    }

    const std::uint32_t declared_size = U32(header.data() + 0x20);
    const std::uint32_t declared_header = U32(header.data() + 0x24);
    const std::uint32_t endian_tag = U32(header.data() + 0x28);
    if (declared_size != actual_size || declared_header != kDexHeaderSize ||
        endian_tag != 0x12345678U) {
        throw std::runtime_error("DEX header size or endian tag is invalid");
    }

    const std::uint32_t link_size = U32(header.data() + 0x2c);
    const std::uint32_t link_offset = U32(header.data() + 0x30);
    if ((link_size == 0) != (link_offset == 0) ||
        (link_size != 0 &&
         (link_offset < kDexHeaderSize ||
          static_cast<std::uint64_t>(link_offset) + link_size > declared_size))) {
        throw std::runtime_error("DEX link section range is invalid");
    }

    const std::uint32_t data_size = U32(header.data() + 0x68);
    const std::uint32_t data_offset = U32(header.data() + 0x6c);
    if (data_size == 0 || (data_size & 3U) != 0 ||
        data_offset < kDexHeaderSize || (data_offset & 3U) != 0 ||
        static_cast<std::uint64_t>(data_offset) + data_size != declared_size) {
        throw std::runtime_error("DEX data section range is invalid");
    }

    CheckSection(U32(header.data() + 0x38), U32(header.data() + 0x3c), 4,
                 data_offset, "string_ids");
    CheckSection(U32(header.data() + 0x40), U32(header.data() + 0x44), 4,
                 data_offset, "type_ids");
    CheckSection(U32(header.data() + 0x48), U32(header.data() + 0x4c), 12,
                 data_offset, "proto_ids");
    CheckSection(U32(header.data() + 0x50), U32(header.data() + 0x54), 8,
                 data_offset, "field_ids");
    CheckSection(U32(header.data() + 0x58), U32(header.data() + 0x5c), 8,
                 data_offset, "method_ids");
    CheckSection(U32(header.data() + 0x60), U32(header.data() + 0x64), 32,
                 data_offset, "class_defs");
    if (U32(header.data() + 0x40) > 65535U ||
        U32(header.data() + 0x48) > 65535U) {
        throw std::runtime_error("DEX type or prototype table exceeds its limit");
    }
    ValidateMap(input, header, declared_size, data_offset,
                U32(header.data() + 0x34));

    BCRYPT_ALG_HANDLE raw_algorithm = nullptr;
    CheckNt(BCryptOpenAlgorithmProvider(&raw_algorithm, BCRYPT_SHA1_ALGORITHM,
                                        nullptr, 0),
            "Unable to open SHA-1 provider");
    std::unique_ptr<void, AlgorithmCloser> algorithm(raw_algorithm);
    DWORD object_size = 0;
    DWORD written = 0;
    CheckNt(BCryptGetProperty(raw_algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size),
                              sizeof(object_size), &written, 0),
            "Unable to get SHA-1 object size");
    std::vector<unsigned char> hash_object(object_size);
    BCRYPT_HASH_HANDLE raw_hash = nullptr;
    CheckNt(BCryptCreateHash(raw_algorithm, &raw_hash, hash_object.data(),
                             object_size, nullptr, 0, 0),
            "Unable to create SHA-1 hash");
    std::unique_ptr<void, HashCloser> hash(raw_hash);

    input.clear();
    input.seekg(12);
    std::vector<unsigned char> buffer(1024 * 1024);
    std::uint64_t remaining = actual_size - 12;
    uLong adler = adler32(0L, Z_NULL, 0);
    bool sha_started = false;
    std::uint64_t position = 12;
    while (remaining != 0) {
        const auto amount = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        ReadExact(input, buffer.data(), amount);
        adler = adler32(adler, buffer.data(), static_cast<uInt>(amount));

        std::size_t sha_offset = 0;
        if (!sha_started && position < 32) {
            sha_offset = static_cast<std::size_t>(
                std::min<std::uint64_t>(32 - position, amount));
            if (position + amount >= 32) {
                sha_started = true;
            }
        }
        if (amount > sha_offset) {
            CheckNt(BCryptHashData(raw_hash, buffer.data() + sha_offset,
                                   static_cast<ULONG>(amount - sha_offset), 0),
                    "Unable to hash DEX payload");
        }
        position += amount;
        remaining -= amount;
    }

    std::array<unsigned char, 20> sha1{};
    CheckNt(BCryptFinishHash(raw_hash, sha1.data(),
                             static_cast<ULONG>(sha1.size()), 0),
            "Unable to finish DEX SHA-1");
    if (U32(header.data() + 8) != static_cast<std::uint32_t>(adler) ||
        !std::equal(sha1.begin(), sha1.end(), header.begin() + 12)) {
        throw std::runtime_error("DEX SHA-1 or Adler-32 checksum mismatch");
    }

    DexInfo info;
    info.path = path;
    info.source_entry = source_entry;
    info.version.assign(version.begin(), version.end());
    info.file_size = declared_size;
    info.string_ids = U32(header.data() + 0x38);
    info.type_ids = U32(header.data() + 0x40);
    info.method_ids = U32(header.data() + 0x58);
    info.class_defs = U32(header.data() + 0x60);
    return info;
}

}  // namespace

DexInfo ValidateDexFile(const std::filesystem::path& path,
                        const std::wstring& source_entry) {
    return ValidateDex(path, source_entry);
}

DexCatalogResult CatalogDexPayloads(
    const std::vector<std::filesystem::path>& verified_apks,
    const std::filesystem::path& app_root) {
    DexCatalogResult result;
    try {
        const auto dex_root = app_root / L"runtime" / L"dex";
        std::filesystem::create_directories(dex_root);
        std::ofstream catalog(dex_root / L"catalog.tsv",
                              std::ios::binary | std::ios::trunc);
        if (!catalog) {
            throw std::runtime_error("Unable to create DEX catalog");
        }
        catalog << "apk_index\tdex_name\tversion\tfile_size\tstring_ids\t"
                   "type_ids\tmethod_ids\tclass_defs\r\n";

        for (std::size_t apk_index = 0; apk_index < verified_apks.size(); ++apk_index) {
            const auto archive = ZipArchive::Open(verified_apks[apk_index]);
            for (const auto& entry : archive.entries()) {
                if (!IsDexEntry(entry)) {
                    continue;
                }
                const auto output_name = std::to_wstring(apk_index) + L"-" +
                                         entry.name;
                const auto destination = dex_root / output_name;
                archive.Extract(entry, destination);
                auto info = ValidateDex(destination, entry.name);
                result.dex_files.push_back(info);

                const std::string line =
                    std::to_string(apk_index) + "\t" +
                    std::string(output_name.begin(), output_name.end()) + "\t" +
                    std::string(info.version.begin(), info.version.end()) + "\t" +
                    std::to_string(info.file_size) + "\t" +
                    std::to_string(info.string_ids) + "\t" +
                    std::to_string(info.type_ids) + "\t" +
                    std::to_string(info.method_ids) + "\t" +
                    std::to_string(info.class_defs) + "\r\n";
                catalog.write(line.data(), static_cast<std::streamsize>(line.size()));
            }
        }
        catalog.flush();
        if (!catalog) {
            throw std::runtime_error("Unable to save DEX catalog");
        }
        if (result.dex_files.empty()) {
            throw std::runtime_error("Selected APKs contain no root classes*.dex payload");
        }
        result.ok = true;
        result.message = L"Validated " + std::to_wstring(result.dex_files.size()) +
                         L" DEX payload(s).";
    } catch (const std::exception& error) {
        const int needed = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1,
                                               nullptr, 0);
        std::wstring detail;
        if (needed > 1) {
            detail.resize(static_cast<std::size_t>(needed));
            MultiByteToWideChar(CP_UTF8, 0, error.what(), -1,
                                detail.data(), needed);
            detail.resize(static_cast<std::size_t>(needed - 1));
        }
        result.message = L"DEX validation failed: " + detail;
    }
    return result;
}

}  // namespace astra

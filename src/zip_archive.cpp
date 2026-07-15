#include "astra/zip_archive.hpp"

#include <windows.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astra {
namespace {

constexpr std::uint32_t kLocalHeader = 0x04034b50U;
constexpr std::uint32_t kCentralHeader = 0x02014b50U;
constexpr std::uint32_t kEndOfCentralDirectory = 0x06054b50U;
constexpr std::uint64_t kMaximumEntryBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumTotalBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumEntries = 8192;
constexpr std::uint64_t kMaximumExpansionRatio = 300;

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

void ReadExact(std::ifstream& input, void* buffer, std::size_t size) {
    input.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("Unexpected end of ZIP container");
    }
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            value.data(),
                                            static_cast<int>(value.size()),
                                            nullptr, 0);
    if (needed <= 0) {
        throw std::runtime_error("ZIP entry name is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(),
                            needed) != needed) {
        throw std::runtime_error("Unable to decode ZIP entry name");
    }
    return result;
}

std::wstring NormalizeEntryName(std::wstring name) {
    std::replace(name.begin(), name.end(), L'\\', L'/');
    if (name.empty() || name.front() == L'/' || name.find(L'\0') != std::wstring::npos ||
        name.find(L':') != std::wstring::npos) {
        throw std::runtime_error("Unsafe ZIP entry path");
    }

    std::size_t start = 0;
    while (start < name.size()) {
        const std::size_t end = name.find(L'/', start);
        const std::wstring part = name.substr(
            start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (part.empty() || part == L"." || part == L"..") {
            if (!(part.empty() && end == name.size() - 1)) {
                throw std::runtime_error("Unsafe ZIP entry path component");
            }
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return name;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

class PartialFileGuard {
public:
    explicit PartialFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
    ~PartialFileGuard() {
        if (!committed_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }
    void Commit() noexcept { committed_ = true; }

private:
    std::filesystem::path path_;
    bool committed_{false};
};

}  // namespace

ZipArchive ZipArchive::Open(const std::filesystem::path& path) {
    ZipArchive archive;
    archive.path_ = path;
    archive.file_size_ = std::filesystem::file_size(path);
    if (archive.file_size_ < 22) {
        throw std::runtime_error("ZIP container is too small");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open ZIP container");
    }

    const std::uint64_t tail_size =
        std::min<std::uint64_t>(archive.file_size_, 22ULL + 65535ULL);
    std::vector<unsigned char> tail(static_cast<std::size_t>(tail_size));
    input.seekg(static_cast<std::streamoff>(archive.file_size_ - tail_size));
    ReadExact(input, tail.data(), tail.size());

    std::size_t eocd_in_tail = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = tail.size() - 22;; --index) {
        if (U32(tail.data() + index) == kEndOfCentralDirectory) {
            const std::uint16_t comment_length = U16(tail.data() + index + 20);
            if (index + 22ULL + comment_length == tail.size()) {
                eocd_in_tail = index;
                break;
            }
        }
        if (index == 0) {
            break;
        }
    }
    if (eocd_in_tail == std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("ZIP central directory footer is missing");
    }

    const unsigned char* eocd = tail.data() + eocd_in_tail;
    const std::uint16_t disk = U16(eocd + 4);
    const std::uint16_t central_disk = U16(eocd + 6);
    const std::uint16_t entries_on_disk = U16(eocd + 8);
    const std::uint16_t entry_count = U16(eocd + 10);
    const std::uint32_t central_size = U32(eocd + 12);
    const std::uint32_t central_offset = U32(eocd + 16);
    if (disk != 0 || central_disk != 0 || entries_on_disk != entry_count) {
        throw std::runtime_error("Multi-disk ZIP containers are unsupported");
    }
    if (entry_count == 0xffffU || central_size == 0xffffffffU ||
        central_offset == 0xffffffffU) {
        throw std::runtime_error("ZIP64 containers are not accepted by this build");
    }
    if (entry_count > kMaximumEntries) {
        throw std::runtime_error("ZIP container contains too many entries");
    }

    const std::uint64_t eocd_offset = archive.file_size_ - tail_size + eocd_in_tail;
    if (static_cast<std::uint64_t>(central_offset) + central_size > eocd_offset) {
        throw std::runtime_error("ZIP central directory range is invalid");
    }
    archive.central_directory_offset_ = central_offset;

    input.clear();
    input.seekg(static_cast<std::streamoff>(central_offset));
    std::set<std::wstring> names;
    std::uint64_t total_uncompressed = 0;
    for (std::uint16_t index = 0; index < entry_count; ++index) {
        std::array<unsigned char, 46> header{};
        ReadExact(input, header.data(), header.size());
        if (U32(header.data()) != kCentralHeader) {
            throw std::runtime_error("Invalid ZIP central-directory entry");
        }

        ZipEntry entry;
        entry.flags = U16(header.data() + 8);
        entry.method = U16(header.data() + 10);
        entry.crc32 = U32(header.data() + 16);
        entry.compressed_size = U32(header.data() + 20);
        entry.uncompressed_size = U32(header.data() + 24);
        const std::uint16_t name_length = U16(header.data() + 28);
        const std::uint16_t extra_length = U16(header.data() + 30);
        const std::uint16_t comment_length = U16(header.data() + 32);
        const std::uint16_t starting_disk = U16(header.data() + 34);
        entry.local_header_offset = U32(header.data() + 42);

        if (starting_disk != 0 || (entry.flags & 0x0001U) != 0) {
            throw std::runtime_error("Encrypted or multi-disk ZIP entry rejected");
        }
        if (name_length == 0) {
            throw std::runtime_error("ZIP entry has an empty name");
        }

        std::string raw_name(name_length, '\0');
        ReadExact(input, raw_name.data(), raw_name.size());
        entry.name = NormalizeEntryName(Utf8ToWide(raw_name));
        entry.is_directory = !entry.name.empty() && entry.name.back() == L'/';

        input.seekg(static_cast<std::streamoff>(extra_length) + comment_length,
                    std::ios::cur);
        if (!input) {
            throw std::runtime_error("Invalid ZIP entry metadata length");
        }

        if (!names.insert(Lower(entry.name)).second) {
            throw std::runtime_error("Duplicate ZIP entry name rejected");
        }
        if (!entry.is_directory && entry.method != 0 && entry.method != 8) {
            throw std::runtime_error("Unsupported ZIP compression method");
        }
        if (entry.uncompressed_size > kMaximumEntryBytes) {
            throw std::runtime_error("ZIP entry exceeds extraction limit");
        }
        if (entry.compressed_size == 0 && entry.uncompressed_size != 0) {
            throw std::runtime_error("Invalid zero-length compressed ZIP entry");
        }
        if (entry.compressed_size != 0 &&
            entry.uncompressed_size / entry.compressed_size > kMaximumExpansionRatio) {
            throw std::runtime_error("ZIP entry expansion ratio rejected");
        }
        total_uncompressed += entry.uncompressed_size;
        if (total_uncompressed > kMaximumTotalBytes) {
            throw std::runtime_error("ZIP container exceeds total extraction limit");
        }
        if (entry.local_header_offset >= central_offset) {
            throw std::runtime_error("ZIP local-header offset is invalid");
        }
        archive.entries_.push_back(std::move(entry));
    }
    return archive;
}

void ZipArchive::Extract(const ZipEntry& entry,
                         const std::filesystem::path& destination) const {
    if (entry.is_directory) {
        throw std::runtime_error("Directory entries cannot be extracted as files");
    }
    std::ifstream input(path_, std::ios::binary);
    input.seekg(static_cast<std::streamoff>(entry.local_header_offset));
    std::array<unsigned char, 30> local{};
    ReadExact(input, local.data(), local.size());
    if (U32(local.data()) != kLocalHeader) {
        throw std::runtime_error("ZIP local header is invalid");
    }
    const std::uint16_t name_length = U16(local.data() + 26);
    const std::uint16_t extra_length = U16(local.data() + 28);
    const std::uint64_t data_offset = static_cast<std::uint64_t>(entry.local_header_offset) +
                                      local.size() + name_length + extra_length;
    if (data_offset + entry.compressed_size > central_directory_offset_) {
        throw std::runtime_error("ZIP entry data range is invalid");
    }
    input.seekg(static_cast<std::streamoff>(data_offset));

    std::filesystem::create_directories(destination.parent_path());
    const auto partial = destination.wstring() + L".partial";
    PartialFileGuard guard(partial);
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to create extracted APK");
    }

    std::vector<unsigned char> input_buffer(256 * 1024);
    std::vector<unsigned char> output_buffer(256 * 1024);
    std::uint64_t compressed_remaining = entry.compressed_size;
    std::uint64_t produced = 0;
    uLong crc = crc32(0L, Z_NULL, 0);

    if (entry.method == 0) {
        while (compressed_remaining != 0) {
            const auto amount = static_cast<std::size_t>(
                std::min<std::uint64_t>(compressed_remaining, input_buffer.size()));
            ReadExact(input, input_buffer.data(), amount);
            output.write(reinterpret_cast<const char*>(input_buffer.data()),
                         static_cast<std::streamsize>(amount));
            crc = crc32(crc, input_buffer.data(), static_cast<uInt>(amount));
            compressed_remaining -= amount;
            produced += amount;
        }
    } else {
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            throw std::runtime_error("Unable to initialize ZIP decompressor");
        }
        int status = Z_OK;
        while (status != Z_STREAM_END) {
            if (stream.avail_in == 0 && compressed_remaining != 0) {
                const auto amount = static_cast<std::size_t>(
                    std::min<std::uint64_t>(compressed_remaining, input_buffer.size()));
                ReadExact(input, input_buffer.data(), amount);
                stream.next_in = input_buffer.data();
                stream.avail_in = static_cast<uInt>(amount);
                compressed_remaining -= amount;
            }
            stream.next_out = output_buffer.data();
            stream.avail_out = static_cast<uInt>(output_buffer.size());
            status = inflate(&stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END) {
                inflateEnd(&stream);
                throw std::runtime_error("Invalid deflate stream in ZIP entry");
            }
            const std::size_t amount = output_buffer.size() - stream.avail_out;
            if (produced + amount > entry.uncompressed_size) {
                inflateEnd(&stream);
                throw std::runtime_error("ZIP entry exceeded declared output size");
            }
            if (amount != 0) {
                output.write(reinterpret_cast<const char*>(output_buffer.data()),
                             static_cast<std::streamsize>(amount));
                crc = crc32(crc, output_buffer.data(), static_cast<uInt>(amount));
                produced += amount;
            }
            if (stream.avail_in == 0 && compressed_remaining == 0 &&
                status != Z_STREAM_END && amount == 0) {
                inflateEnd(&stream);
                throw std::runtime_error("Truncated deflate stream in ZIP entry");
            }
        }
        inflateEnd(&stream);
        if (compressed_remaining != 0) {
            throw std::runtime_error("ZIP entry ended before its compressed range");
        }
    }

    output.flush();
    if (!output || produced != entry.uncompressed_size ||
        static_cast<std::uint32_t>(crc) != entry.crc32) {
        throw std::runtime_error("Extracted APK failed size or CRC verification");
    }
    output.close();
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
    std::filesystem::rename(partial, destination);
    guard.Commit();
}

}  // namespace astra

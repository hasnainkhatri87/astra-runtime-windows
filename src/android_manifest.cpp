#include "astra/android_manifest.hpp"

#include "astra/zip_archive.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace astra {
namespace {

constexpr std::uint16_t kXmlType = 0x0003;
constexpr std::uint16_t kStringPoolType = 0x0001;
constexpr std::uint16_t kStartNamespaceType = 0x0100;
constexpr std::uint16_t kEndNamespaceType = 0x0101;
constexpr std::uint16_t kStartElementType = 0x0102;
constexpr std::uint16_t kEndElementType = 0x0103;
constexpr std::uint16_t kCdataType = 0x0104;
constexpr std::uint16_t kResourceMapType = 0x0180;
constexpr std::uint32_t kNoIndex = 0xffffffffU;
constexpr std::uint32_t kUtf8Flag = 1U << 8U;
constexpr std::uint8_t kTypeString = 0x03;
constexpr std::uint8_t kTypeFirstInt = 0x10;
constexpr std::uint8_t kTypeLastInt = 0x1f;
constexpr std::uint32_t kAttrName = 0x01010003U;
constexpr std::uint32_t kAttrEnabled = 0x0101000eU;
constexpr std::uint32_t kAttrTargetActivity = 0x01010202U;
constexpr std::uint32_t kAttrMinSdk = 0x0101020cU;
constexpr std::uint32_t kAttrVersionCode = 0x0101021bU;
constexpr std::uint32_t kAttrVersionName = 0x0101021cU;
constexpr std::uint32_t kAttrTargetSdk = 0x01010270U;
constexpr std::uint64_t kMaximumManifestBytes = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumStrings = 250000;
constexpr std::uint32_t kMaximumAttributes = 4096;

struct Bytes {
    std::vector<unsigned char> value;

    bool Contains(std::size_t offset, std::size_t size) const noexcept {
        return offset <= value.size() && size <= value.size() - offset;
    }

    std::uint16_t U16(std::size_t offset) const {
        if (!Contains(offset, 2)) {
            throw std::runtime_error("Binary manifest 16-bit read is out of range");
        }
        return static_cast<std::uint16_t>(value[offset]) |
               (static_cast<std::uint16_t>(value[offset + 1]) << 8U);
    }

    std::uint32_t U32(std::size_t offset) const {
        if (!Contains(offset, 4)) {
            throw std::runtime_error("Binary manifest 32-bit read is out of range");
        }
        return static_cast<std::uint32_t>(value[offset]) |
               (static_cast<std::uint32_t>(value[offset + 1]) << 8U) |
               (static_cast<std::uint32_t>(value[offset + 2]) << 16U) |
               (static_cast<std::uint32_t>(value[offset + 3]) << 24U);
    }
};

struct Chunk {
    std::uint16_t type{};
    std::uint16_t header_size{};
    std::uint32_t size{};
    std::size_t offset{};
    std::size_t end{};
};

Chunk ReadChunk(const Bytes& bytes, std::size_t offset,
                std::size_t container_end) {
    if (!bytes.Contains(offset, 8) || offset + 8 > container_end) {
        throw std::runtime_error("Binary manifest chunk header is truncated");
    }
    Chunk chunk;
    chunk.type = bytes.U16(offset);
    chunk.header_size = bytes.U16(offset + 2);
    chunk.size = bytes.U32(offset + 4);
    chunk.offset = offset;
    if (chunk.header_size < 8 || chunk.size < chunk.header_size ||
        (chunk.size & 3U) != 0 ||
        static_cast<std::uint64_t>(offset) + chunk.size > container_end) {
        throw std::runtime_error("Binary manifest chunk range is invalid");
    }
    chunk.end = offset + chunk.size;
    return chunk;
}

std::uint32_t ReadLength8(const Bytes& bytes, std::size_t& position,
                          std::size_t end) {
    if (position >= end) {
        throw std::runtime_error("UTF-8 string length is truncated");
    }
    const std::uint8_t first = bytes.value[position++];
    if ((first & 0x80U) == 0) {
        return first;
    }
    if (position >= end) {
        throw std::runtime_error("UTF-8 string length continuation is truncated");
    }
    return (static_cast<std::uint32_t>(first & 0x7fU) << 8U) |
           bytes.value[position++];
}

std::uint32_t ReadLength16(const Bytes& bytes, std::size_t& position,
                           std::size_t end) {
    if (position > end || end - position < 2) {
        throw std::runtime_error("UTF-16 string length is truncated");
    }
    const std::uint16_t first = bytes.U16(position);
    position += 2;
    if ((first & 0x8000U) == 0) {
        return first;
    }
    if (position > end || end - position < 2) {
        throw std::runtime_error("UTF-16 string length continuation is truncated");
    }
    const std::uint16_t second = bytes.U16(position);
    position += 2;
    return (static_cast<std::uint32_t>(first & 0x7fffU) << 16U) | second;
}

class StringPool {
public:
    static StringPool Parse(const Bytes& bytes, const Chunk& chunk) {
        if (chunk.header_size < 28) {
            throw std::runtime_error("Binary manifest string-pool header is invalid");
        }
        const std::uint32_t string_count = bytes.U32(chunk.offset + 8);
        const std::uint32_t style_count = bytes.U32(chunk.offset + 12);
        const std::uint32_t flags = bytes.U32(chunk.offset + 16);
        const std::uint32_t strings_start = bytes.U32(chunk.offset + 20);
        const std::uint32_t styles_start = bytes.U32(chunk.offset + 24);
        if (string_count > kMaximumStrings || style_count > kMaximumStrings) {
            throw std::runtime_error("Binary manifest string pool is excessive");
        }
        const std::uint64_t indices_end =
            static_cast<std::uint64_t>(chunk.header_size) +
            static_cast<std::uint64_t>(string_count + style_count) * 4ULL;
        if (indices_end > chunk.size || strings_start < indices_end ||
            strings_start >= chunk.size ||
            (styles_start != 0 &&
             (styles_start < strings_start || styles_start >= chunk.size))) {
            throw std::runtime_error("Binary manifest string-pool ranges are invalid");
        }

        const std::size_t string_data_start = chunk.offset + strings_start;
        const std::size_t string_data_end =
            styles_start == 0 ? chunk.end : chunk.offset + styles_start;
        StringPool pool;
        pool.strings_.reserve(string_count);
        for (std::uint32_t index = 0; index < string_count; ++index) {
            const std::uint32_t relative = bytes.U32(
                chunk.offset + chunk.header_size +
                static_cast<std::size_t>(index) * 4);
            if (relative >= string_data_end - string_data_start) {
                throw std::runtime_error("Binary manifest string offset is invalid");
            }
            std::size_t position = string_data_start + relative;
            if ((flags & kUtf8Flag) != 0) {
                const std::uint32_t utf16_length =
                    ReadLength8(bytes, position, string_data_end);
                const std::uint32_t utf8_length =
                    ReadLength8(bytes, position, string_data_end);
                if (utf8_length > static_cast<std::uint32_t>(INT_MAX) ||
                    position > string_data_end ||
                    static_cast<std::uint64_t>(position) + utf8_length + 1ULL >
                        string_data_end ||
                    bytes.value[position + utf8_length] != 0) {
                    throw std::runtime_error("Binary manifest UTF-8 string is invalid");
                }
                std::wstring decoded;
                if (utf8_length != 0) {
                    const int needed = MultiByteToWideChar(
                        CP_UTF8, MB_ERR_INVALID_CHARS,
                        reinterpret_cast<const char*>(bytes.value.data() + position),
                        static_cast<int>(utf8_length), nullptr, 0);
                    if (needed <= 0 ||
                        static_cast<std::uint32_t>(needed) != utf16_length) {
                        throw std::runtime_error(
                            "Binary manifest UTF-8 string cannot be decoded");
                    }
                    decoded.resize(static_cast<std::size_t>(needed));
                    if (MultiByteToWideChar(
                            CP_UTF8, MB_ERR_INVALID_CHARS,
                            reinterpret_cast<const char*>(bytes.value.data() + position),
                            static_cast<int>(utf8_length), decoded.data(), needed) !=
                        needed) {
                        throw std::runtime_error(
                            "Binary manifest UTF-8 conversion failed");
                    }
                } else if (utf16_length != 0) {
                    throw std::runtime_error("Binary manifest empty string length disagrees");
                }
                pool.strings_.push_back(std::move(decoded));
            } else {
                const std::uint32_t utf16_length =
                    ReadLength16(bytes, position, string_data_end);
                const std::uint64_t byte_count =
                    static_cast<std::uint64_t>(utf16_length) * 2ULL;
                if (static_cast<std::uint64_t>(position) + byte_count + 2ULL >
                        string_data_end ||
                    bytes.U16(position + static_cast<std::size_t>(byte_count)) != 0) {
                    throw std::runtime_error("Binary manifest UTF-16 string is invalid");
                }
                std::wstring decoded;
                decoded.reserve(utf16_length);
                for (std::uint32_t unit = 0; unit < utf16_length; ++unit) {
                    decoded.push_back(static_cast<wchar_t>(
                        bytes.U16(position + static_cast<std::size_t>(unit) * 2)));
                }
                pool.strings_.push_back(std::move(decoded));
            }
        }
        return pool;
    }

    const std::wstring& At(std::uint32_t index) const {
        if (index >= strings_.size()) {
            throw std::runtime_error("Binary manifest string index is invalid");
        }
        return strings_[index];
    }

    std::size_t size() const noexcept { return strings_.size(); }

private:
    std::vector<std::wstring> strings_;
};

struct Attribute {
    std::uint32_t resource_id{};
    std::wstring name;
    std::uint32_t raw_value{kNoIndex};
    std::uint8_t data_type{};
    std::uint32_t data{};
};

struct StartElement {
    std::wstring name;
    std::vector<Attribute> attributes;
};

StartElement ParseStartElement(const Bytes& bytes, const Chunk& chunk,
                               const StringPool& strings,
                               const std::vector<std::uint32_t>& resource_map) {
    if (chunk.header_size < 16 ||
        static_cast<std::uint64_t>(chunk.offset) + chunk.header_size + 20ULL >
            chunk.end) {
        throw std::runtime_error("Binary manifest start element is truncated");
    }
    const std::size_t extension = chunk.offset + chunk.header_size;
    const std::uint32_t name_index = bytes.U32(extension + 4);
    const std::uint16_t attribute_start = bytes.U16(extension + 8);
    const std::uint16_t attribute_size = bytes.U16(extension + 10);
    const std::uint16_t attribute_count = bytes.U16(extension + 12);
    const std::uint16_t id_index = bytes.U16(extension + 14);
    const std::uint16_t class_index = bytes.U16(extension + 16);
    const std::uint16_t style_index = bytes.U16(extension + 18);
    if (attribute_count > kMaximumAttributes || attribute_size < 20 ||
        attribute_start < 20 || id_index > attribute_count ||
        class_index > attribute_count || style_index > attribute_count) {
        throw std::runtime_error("Binary manifest attribute header is invalid");
    }
    const std::uint64_t attributes_end =
        static_cast<std::uint64_t>(extension) + attribute_start +
        static_cast<std::uint64_t>(attribute_count) * attribute_size;
    if (attributes_end > chunk.end) {
        throw std::runtime_error("Binary manifest attribute array is out of range");
    }

    StartElement element;
    element.name = strings.At(name_index);
    element.attributes.reserve(attribute_count);
    for (std::uint16_t index = 0; index < attribute_count; ++index) {
        const std::size_t offset = extension + attribute_start +
                                   static_cast<std::size_t>(index) * attribute_size;
        const std::uint32_t attribute_name = bytes.U32(offset + 4);
        const std::uint16_t value_size = bytes.U16(offset + 12);
        const std::uint8_t value_zero = bytes.value[offset + 14];
        if (value_size != 8 || value_zero != 0) {
            throw std::runtime_error("Binary manifest typed attribute is invalid");
        }
        Attribute attribute;
        attribute.name = strings.At(attribute_name);
        attribute.raw_value = bytes.U32(offset + 8);
        attribute.data_type = bytes.value[offset + 15];
        attribute.data = bytes.U32(offset + 16);
        if (attribute_name < resource_map.size()) {
            attribute.resource_id = resource_map[attribute_name];
        }
        element.attributes.push_back(std::move(attribute));
    }
    return element;
}

const Attribute* FindAttribute(const StartElement& element,
                               std::uint32_t resource_id,
                               const wchar_t* fallback_name) {
    for (const auto& attribute : element.attributes) {
        if (resource_id != 0 && attribute.resource_id == resource_id) {
            return &attribute;
        }
    }
    for (const auto& attribute : element.attributes) {
        if (attribute.name == fallback_name) {
            return &attribute;
        }
    }
    return nullptr;
}

std::optional<std::wstring> StringValue(const Attribute* attribute,
                                        const StringPool& strings) {
    if (!attribute) {
        return std::nullopt;
    }
    if (attribute->raw_value != kNoIndex) {
        return strings.At(attribute->raw_value);
    }
    if (attribute->data_type == kTypeString) {
        return strings.At(attribute->data);
    }
    return std::nullopt;
}

std::optional<std::uint32_t> IntegerValue(const Attribute* attribute,
                                          const StringPool& strings) {
    if (!attribute) {
        return std::nullopt;
    }
    if (attribute->data_type >= kTypeFirstInt &&
        attribute->data_type <= kTypeLastInt) {
        return attribute->data;
    }
    const auto text = StringValue(attribute, strings);
    if (!text || text->empty()) {
        return std::nullopt;
    }
    std::size_t consumed = 0;
    try {
        const unsigned long parsed = std::stoul(*text, &consumed, 0);
        if (consumed == text->size() &&
            parsed <= std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(parsed);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<bool> BooleanValue(const Attribute* attribute,
                                 const StringPool& strings) {
    const auto integer = IntegerValue(attribute, strings);
    if (integer) {
        return *integer != 0;
    }
    const auto text = StringValue(attribute, strings);
    if (!text) {
        return std::nullopt;
    }
    std::wstring lower = *text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t value) {
                       return static_cast<wchar_t>(std::towlower(value));
                   });
    if (lower == L"true") {
        return true;
    }
    if (lower == L"false") {
        return false;
    }
    return std::nullopt;
}

std::wstring ResolveClassName(const std::wstring& package_name,
                              const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    if (value.front() == L'.') {
        return package_name + value;
    }
    if (value.find(L'.') == std::wstring::npos) {
        return package_name + L"." + value;
    }
    return value;
}

bool IsValidPackageName(const std::wstring& value) {
    if (value.empty() || value.size() > 512 || value.front() == L'.' ||
        value.back() == L'.') {
        return false;
    }
    bool at_segment_start = true;
    for (const wchar_t character : value) {
        if (character == L'.') {
            if (at_segment_start) {
                return false;
            }
            at_segment_start = true;
            continue;
        }
        if (!(std::iswalnum(character) || character == L'_')) {
            return false;
        }
        if (at_segment_start && std::iswdigit(character)) {
            return false;
        }
        at_segment_start = false;
    }
    return !at_segment_start;
}

std::wstring ErrorToWide(const std::exception& error) {
    const int needed = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1,
                                           nullptr, 0);
    if (needed <= 1) {
        return L"unknown parser error";
    }
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, result.data(), needed);
    result.resize(static_cast<std::size_t>(needed - 1));
    return result;
}

struct ComponentState {
    std::wstring component_name;
    std::wstring target_activity;
    std::wstring tag;
    std::size_t depth{};
    bool enabled{true};
    bool has_launcher_filter{false};
};

struct FilterState {
    std::size_t depth{};
    bool has_main{false};
    bool has_launcher{false};
};

Bytes ReadManifestBytes(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size < 8 || size > kMaximumManifestBytes) {
        throw std::runtime_error("Binary manifest size is invalid");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open binary manifest");
    }
    Bytes bytes;
    bytes.value.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.value.data()),
               static_cast<std::streamsize>(bytes.value.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.value.size())) {
        throw std::runtime_error("Unable to read complete binary manifest");
    }
    return bytes;
}

}  // namespace

AndroidManifestInfo ParseAndroidBinaryManifest(
    const std::filesystem::path& binary_manifest) {
    AndroidManifestInfo result;
    result.binary_manifest_path = binary_manifest;
    try {
        const Bytes bytes = ReadManifestBytes(binary_manifest);
        const Chunk root = ReadChunk(bytes, 0, bytes.value.size());
        if (root.type != kXmlType || root.size != bytes.value.size()) {
            throw std::runtime_error("File is not one complete Android binary XML tree");
        }

        std::optional<StringPool> strings;
        std::vector<std::uint32_t> resource_map;
        std::vector<std::wstring> element_stack;
        std::optional<ComponentState> component;
        std::optional<FilterState> filter;
        bool saw_manifest = false;
        bool closed_manifest = false;
        bool has_min_sdk = false;
        bool has_target_sdk = false;

        std::size_t cursor = root.header_size;
        while (cursor < root.end) {
            const Chunk chunk = ReadChunk(bytes, cursor, root.end);
            if (chunk.type == kStringPoolType) {
                if (strings || !element_stack.empty()) {
                    throw std::runtime_error(
                        "Binary manifest has a duplicate or late string pool");
                }
                strings = StringPool::Parse(bytes, chunk);
            } else if (chunk.type == kResourceMapType) {
                if (!strings || !resource_map.empty() || chunk.header_size < 8 ||
                    ((chunk.size - chunk.header_size) & 3U) != 0) {
                    throw std::runtime_error("Binary manifest resource map is invalid");
                }
                const std::size_t count =
                    (chunk.size - chunk.header_size) / sizeof(std::uint32_t);
                if (count > strings->size()) {
                    throw std::runtime_error(
                        "Binary manifest resource map exceeds string pool");
                }
                resource_map.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    resource_map.push_back(bytes.U32(
                        chunk.offset + chunk.header_size + index * 4));
                }
            } else if (chunk.type == kStartElementType) {
                if (!strings || closed_manifest) {
                    throw std::runtime_error(
                        "Binary manifest element appears outside the document root");
                }
                const StartElement element = ParseStartElement(
                    bytes, chunk, *strings, resource_map);
                const std::size_t depth = element_stack.size();
                if (element.name == L"manifest") {
                    if (depth != 0 || saw_manifest) {
                        throw std::runtime_error("Binary manifest root element is invalid");
                    }
                    saw_manifest = true;
                    const auto package = StringValue(
                        FindAttribute(element, 0, L"package"), *strings);
                    if (!package || !IsValidPackageName(*package)) {
                        throw std::runtime_error("Manifest package name is missing or invalid");
                    }
                    result.package_name = *package;
                    if (const auto value = IntegerValue(
                            FindAttribute(element, kAttrVersionCode, L"versionCode"),
                            *strings)) {
                        result.version_code = *value;
                    }
                    if (const auto value = StringValue(
                            FindAttribute(element, kAttrVersionName, L"versionName"),
                            *strings)) {
                        result.version_name = *value;
                    }
                } else if (!saw_manifest || depth == 0) {
                    throw std::runtime_error(
                        "Binary manifest does not begin with a manifest element");
                } else if (element.name == L"uses-sdk") {
                    if (const auto value = IntegerValue(
                            FindAttribute(element, kAttrMinSdk, L"minSdkVersion"),
                            *strings)) {
                        result.min_sdk = *value;
                        has_min_sdk = true;
                    }
                    if (const auto value = IntegerValue(
                            FindAttribute(element, kAttrTargetSdk, L"targetSdkVersion"),
                            *strings)) {
                        result.target_sdk = *value;
                        has_target_sdk = true;
                    }
                } else if (element.name == L"application") {
                    if (const auto value = StringValue(
                            FindAttribute(element, kAttrName, L"name"), *strings)) {
                        result.application_class =
                            ResolveClassName(result.package_name, *value);
                    }
                } else if (element.name == L"activity" ||
                           element.name == L"activity-alias") {
                    if (component) {
                        throw std::runtime_error("Manifest contains nested activities");
                    }
                    ComponentState state;
                    state.tag = element.name;
                    state.depth = depth;
                    const auto name = StringValue(
                        FindAttribute(element, kAttrName, L"name"), *strings);
                    if (!name || name->empty()) {
                        throw std::runtime_error("Manifest activity has no class name");
                    }
                    state.component_name =
                        ResolveClassName(result.package_name, *name);
                    if (element.name == L"activity-alias") {
                        const auto target = StringValue(
                            FindAttribute(element, kAttrTargetActivity,
                                          L"targetActivity"),
                            *strings);
                        if (!target || target->empty()) {
                            throw std::runtime_error(
                                "Manifest activity alias has no targetActivity");
                        }
                        state.target_activity =
                            ResolveClassName(result.package_name, *target);
                    }
                    if (const auto enabled = BooleanValue(
                            FindAttribute(element, kAttrEnabled, L"enabled"),
                            *strings)) {
                        state.enabled = *enabled;
                    }
                    component = std::move(state);
                    ++result.activity_count;
                } else if (element.name == L"intent-filter" && component &&
                           depth == component->depth + 1) {
                    if (filter) {
                        throw std::runtime_error("Manifest contains nested intent filters");
                    }
                    filter = FilterState{depth, false, false};
                } else if (element.name == L"action" && filter &&
                           depth == filter->depth + 1) {
                    const auto name = StringValue(
                        FindAttribute(element, kAttrName, L"name"), *strings);
                    if (name && *name == L"android.intent.action.MAIN") {
                        filter->has_main = true;
                    }
                } else if (element.name == L"category" && filter &&
                           depth == filter->depth + 1) {
                    const auto name = StringValue(
                        FindAttribute(element, kAttrName, L"name"), *strings);
                    if (name && *name == L"android.intent.category.LAUNCHER") {
                        filter->has_launcher = true;
                    }
                }
                element_stack.push_back(element.name);
            } else if (chunk.type == kEndElementType) {
                if (!strings || chunk.header_size < 16 ||
                    static_cast<std::uint64_t>(chunk.offset) +
                            chunk.header_size + 8ULL >
                        chunk.end ||
                    element_stack.empty()) {
                    throw std::runtime_error("Binary manifest end element is invalid");
                }
                const std::wstring& name = strings->At(
                    bytes.U32(chunk.offset + chunk.header_size + 4));
                if (element_stack.back() != name) {
                    throw std::runtime_error("Binary manifest element nesting is invalid");
                }
                const std::size_t depth = element_stack.size() - 1;
                if (name == L"intent-filter" && filter &&
                    filter->depth == depth) {
                    if (component && filter->has_main && filter->has_launcher) {
                        component->has_launcher_filter = true;
                    }
                    filter.reset();
                }
                if ((name == L"activity" || name == L"activity-alias") &&
                    component && component->depth == depth) {
                    if (component->enabled && component->has_launcher_filter) {
                        ++result.launcher_count;
                        if (result.launcher_component.empty()) {
                            result.launcher_component = component->component_name;
                            result.launcher_activity =
                                component->tag == L"activity-alias"
                                    ? component->target_activity
                                    : component->component_name;
                        }
                    }
                    component.reset();
                }
                element_stack.pop_back();
                if (name == L"manifest") {
                    if (!element_stack.empty()) {
                        throw std::runtime_error(
                            "Manifest root closed before nested elements");
                    }
                    closed_manifest = true;
                }
            } else if (chunk.type == kStartNamespaceType ||
                       chunk.type == kEndNamespaceType ||
                       chunk.type == kCdataType) {
                if (!strings || chunk.header_size < 16) {
                    throw std::runtime_error(
                        "Binary manifest namespace or text node is invalid");
                }
            } else {
                throw std::runtime_error("Unsupported binary manifest chunk type");
            }
            cursor = chunk.end;
        }

        if (!strings || !saw_manifest || !closed_manifest ||
            !element_stack.empty() || component || filter) {
            throw std::runtime_error("Binary manifest document is incomplete");
        }
        if (!has_min_sdk) {
            result.min_sdk = 1;
        }
        if (!has_target_sdk) {
            result.target_sdk = result.min_sdk;
        }
        if (result.target_sdk < result.min_sdk) {
            throw std::runtime_error("Manifest target SDK is below its minimum SDK");
        }

        result.ok = true;
        result.message = L"Parsed signed manifest for " + result.package_name +
                         L" (SDK " + std::to_wstring(result.min_sdk) + L"-" +
                         std::to_wstring(result.target_sdk) + L").";
        if (!result.launcher_activity.empty()) {
            result.message += L" Launcher: " + result.launcher_activity + L".";
        } else {
            result.message += L" No launcher Activity is declared.";
        }
    } catch (const std::exception& error) {
        result.message = L"Android manifest validation failed: " + ErrorToWide(error);
    }
    return result;
}

AndroidManifestInfo ReadAndroidManifestFromApk(
    const std::filesystem::path& verified_base_apk,
    const std::filesystem::path& app_root) {
    AndroidManifestInfo result;
    try {
        const auto archive = ZipArchive::Open(verified_base_apk);
        const ZipEntry* manifest = nullptr;
        for (const auto& entry : archive.entries()) {
            if (!entry.is_directory && entry.name == L"AndroidManifest.xml") {
                if (manifest) {
                    throw std::runtime_error("APK contains duplicate binary manifests");
                }
                manifest = &entry;
            }
        }
        if (!manifest || manifest->uncompressed_size == 0 ||
            manifest->uncompressed_size > kMaximumManifestBytes) {
            throw std::runtime_error("APK has no bounded root AndroidManifest.xml");
        }
        const auto manifest_root = app_root / L"runtime" / L"manifest";
        const auto destination =
            manifest_root /
            (verified_base_apk.filename().wstring() + L".AndroidManifest.xml");
        archive.Extract(*manifest, destination);
        result = ParseAndroidBinaryManifest(destination);
    } catch (const std::exception& error) {
        result.message = L"Android manifest extraction failed: " + ErrorToWide(error);
    }
    return result;
}

}  // namespace astra

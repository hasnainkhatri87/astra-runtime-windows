#include "astra/google_services.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace astra {
namespace {

constexpr std::uint64_t kMaximumScannedDexBytes =
    256ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumStringBytes = 4U * 1024U * 1024U;

std::uint32_t U32(const std::vector<unsigned char>& value,
                  std::size_t offset) {
    if (offset > value.size() || value.size() - offset < 4) {
        throw std::runtime_error("DEX service scan read is out of range");
    }
    return static_cast<std::uint32_t>(value[offset]) |
           (static_cast<std::uint32_t>(value[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(value[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(value[offset + 3]) << 24U);
}

std::uint32_t ReadUleb128(const std::vector<unsigned char>& value,
                          std::size_t& position) {
    std::uint32_t result = 0;
    for (std::uint32_t byte_index = 0; byte_index < 5; ++byte_index) {
        if (position >= value.size()) {
            throw std::runtime_error("DEX string length is truncated");
        }
        const std::uint8_t byte = value[position++];
        if (byte_index == 4 && (byte & 0xf0U) != 0) {
            throw std::runtime_error("DEX string length overflows 32 bits");
        }
        result |= static_cast<std::uint32_t>(byte & 0x7fU)
                  << (byte_index * 7U);
        if ((byte & 0x80U) == 0) {
            return result;
        }
    }
    throw std::runtime_error("DEX string length uses excessive LEB128 bytes");
}

bool Contains(const std::string& value, const char* token) {
    return value.find(token) != std::string::npos;
}

void Classify(const std::string& value,
              GoogleServiceRequirements& required) {
    if (Contains(value, "Lcom/google/android/gms/") ||
        Contains(value, "com.google.android.gms")) {
        required.core = true;
    }
    if (Contains(value, "Lcom/google/android/gms/maps/") ||
        Contains(value, "com.google.android.geo.API_KEY")) {
        required.maps = true;
    }
    if (Contains(value, "Lcom/google/android/gms/location/") ||
        Contains(value, "Lcom/google/android/libraries/places/")) {
        required.location = true;
    }
    if (Contains(value, "Lcom/google/android/gms/auth/") ||
        Contains(value, "Lcom/google/firebase/auth/")) {
        required.authentication = true;
    }
    if (Contains(value, "Lcom/google/firebase/messaging/") ||
        Contains(value, "Lcom/google/android/gms/gcm/") ||
        Contains(value, "FirebaseMessagingService")) {
        required.cloud_messaging = true;
    }
    if (Contains(value, "Lcom/android/billingclient/") ||
        Contains(value, "com.android.vending.billing")) {
        required.billing = true;
    }
    if (Contains(value, "Lcom/google/android/play/core/integrity/") ||
        Contains(value, "Lcom/google/android/play/integrity/") ||
        Contains(value, "Lcom/google/android/gms/safetynet/") ||
        Contains(value, "IntegrityManagerFactory")) {
        required.play_integrity = true;
    }
}

void ScanDex(const std::filesystem::path& path,
             GoogleServiceRequirements& required) {
    const auto file_size = std::filesystem::file_size(path);
    if (file_size < 0x70 || file_size > kMaximumScannedDexBytes) {
        throw std::runtime_error("DEX is outside the bounded service-scan size");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open verified DEX for service scan");
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(file_size));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        bytes[0] != 'd' || bytes[1] != 'e' || bytes[2] != 'x' ||
        bytes[3] != '\n') {
        throw std::runtime_error("Verified DEX could not be read for service scan");
    }

    const std::uint32_t string_count = U32(bytes, 0x38);
    const std::uint32_t string_ids_offset = U32(bytes, 0x3c);
    const std::uint64_t table_end =
        static_cast<std::uint64_t>(string_ids_offset) +
        static_cast<std::uint64_t>(string_count) * 4ULL;
    if (string_count > 1000000U || string_ids_offset < 0x70 ||
        table_end > bytes.size()) {
        throw std::runtime_error("DEX string identifier table is invalid");
    }

    for (std::uint32_t index = 0; index < string_count; ++index) {
        std::size_t position = U32(
            bytes, string_ids_offset + static_cast<std::size_t>(index) * 4);
        if (position >= bytes.size()) {
            throw std::runtime_error("DEX string data offset is invalid");
        }
        (void)ReadUleb128(bytes, position);
        const std::size_t begin = position;
        while (position < bytes.size() && bytes[position] != 0 &&
               position - begin <= kMaximumStringBytes) {
            ++position;
        }
        if (position >= bytes.size() || position - begin > kMaximumStringBytes) {
            throw std::runtime_error("DEX string data is unterminated or excessive");
        }
        const std::string value(
            reinterpret_cast<const char*>(bytes.data() + begin),
            position - begin);
        Classify(value, required);
        ++required.strings_inspected;
    }
}

void AddName(std::wstring& value, const wchar_t* name) {
    if (!value.empty()) {
        value += L",";
    }
    value += name;
}

std::wstring RequiredNames(const GoogleServiceRequirements& required) {
    std::wstring result;
    if (required.core) {
        AddName(result, L"core");
    }
    if (required.maps) {
        AddName(result, L"maps");
    }
    if (required.location) {
        AddName(result, L"location");
    }
    if (required.authentication) {
        AddName(result, L"authentication");
    }
    if (required.cloud_messaging) {
        AddName(result, L"cloud-messaging");
    }
    if (required.billing) {
        AddName(result, L"billing");
    }
    if (required.play_integrity) {
        AddName(result, L"play-integrity");
    }
    if (result.empty()) {
        result = L"none-detected";
    }
    return result;
}

}  // namespace

GoogleServicesPlan AnalyzeGoogleServiceRequirements(
    const std::vector<DexInfo>& verified_dex_files) {
    GoogleServicesPlan plan;
    try {
        for (const auto& dex : verified_dex_files) {
            ScanDex(dex.path, plan.required);
        }
        plan.required.analysis_ok = true;
        plan.required_services = RequiredNames(plan.required);
        const bool any_required = plan.required_services != L"none-detected";
        plan.provider_id = any_required ? L"open-services" : L"none";
        plan.provider_state = any_required ? L"runtime-bridge-pending" :
                                             L"not-required";
        if (any_required) {
            plan.message = L"Detected Google service APIs: " +
                           plan.required_services +
                           L". Open Services provider selected; runtime bridge pending.";
        } else {
            plan.message = L"No Google service API requirement was detected.";
        }
        if (plan.required.play_integrity) {
            plan.message +=
                L" Play Integrity cannot be asserted without a genuine certified environment.";
        }
    } catch (const std::exception&) {
        plan.required_services = L"analysis-unavailable";
        plan.provider_id = L"open-services";
        plan.provider_state = L"requirement-analysis-failed";
        plan.message =
            L"Google service requirement analysis failed safely; no capability was claimed.";
    }
    return plan;
}

std::wstring GoogleServicesPolicySummary() {
    return
        L"Astra Google-service provider policy\n"
        L"  open-services: personal/open compatibility target for core, Maps, "
        L"location, auth, and cloud messaging; framework bridge is pending\n"
        L"  licensed-gms: plug-in boundary only for Google-authorized binaries; "
        L"not distributed by Astra\n"
        L"  Play Integrity: never forged; certified-device verdicts are unavailable "
        L"on this Windows compatibility runtime\n";
}

}  // namespace astra

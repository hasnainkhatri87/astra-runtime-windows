#include "astra/google_services.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Store32(std::vector<unsigned char>& value, std::size_t offset,
             std::uint32_t number) {
    value[offset] = static_cast<unsigned char>(number);
    value[offset + 1] = static_cast<unsigned char>(number >> 8U);
    value[offset + 2] = static_cast<unsigned char>(number >> 16U);
    value[offset + 3] = static_cast<unsigned char>(number >> 24U);
}

void AppendUleb128(std::vector<unsigned char>& output, std::uint32_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0) {
            byte |= 0x80U;
        }
        output.push_back(byte);
    } while (value != 0);
}

std::vector<unsigned char> BuildDexWithStrings(
    const std::vector<std::string>& strings) {
    const std::size_t ids_offset = 0x70;
    std::vector<unsigned char> dex(ids_offset + strings.size() * 4, 0);
    dex[0] = 'd';
    dex[1] = 'e';
    dex[2] = 'x';
    dex[3] = '\n';
    dex[4] = '0';
    dex[5] = '3';
    dex[6] = '9';
    Store32(dex, 0x38, static_cast<std::uint32_t>(strings.size()));
    Store32(dex, 0x3c, static_cast<std::uint32_t>(ids_offset));
    for (std::size_t index = 0; index < strings.size(); ++index) {
        Store32(dex, ids_offset + index * 4,
                static_cast<std::uint32_t>(dex.size()));
        AppendUleb128(dex, static_cast<std::uint32_t>(strings[index].size()));
        dex.insert(dex.end(), strings[index].begin(), strings[index].end());
        dex.push_back(0);
    }
    Store32(dex, 0x20, static_cast<std::uint32_t>(dex.size()));
    return dex;
}

void WriteBytes(const std::filesystem::path& path,
                const std::vector<unsigned char>& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(value.data()),
                 static_cast<std::streamsize>(value.size()));
}

void Expect(bool condition, const char* message, int& failures) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    int failures = 0;
    const auto root = std::filesystem::temp_directory_path() /
                      L"astra-google-services-tests";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const auto service_dex = root / L"services.dex";
    WriteBytes(service_dex, BuildDexWithStrings({
        "Lcom/google/android/gms/common/GoogleApiAvailability;",
        "Lcom/google/android/gms/maps/GoogleMap;",
        "Lcom/google/android/gms/location/FusedLocationProviderClient;",
        "Lcom/google/android/gms/auth/api/signin/GoogleSignIn;",
        "Lcom/google/firebase/messaging/FirebaseMessagingService;",
        "Lcom/android/billingclient/api/BillingClient;",
        "Lcom/google/android/play/core/integrity/IntegrityManagerFactory;",
    }));
    astra::DexInfo info;
    info.path = service_dex;
    const auto plan = astra::AnalyzeGoogleServiceRequirements({info});
    Expect(plan.required.analysis_ok && plan.required.core &&
               plan.required.maps && plan.required.location &&
               plan.required.authentication && plan.required.cloud_messaging &&
               plan.required.billing && plan.required.play_integrity,
           "known Google service descriptors were not classified", failures);
    Expect(plan.provider_id == L"open-services" &&
               plan.provider_state == L"runtime-bridge-pending" &&
               !plan.play_integrity_available,
           "open provider or Integrity policy is incorrect", failures);

    const auto plain_dex = root / L"plain.dex";
    WriteBytes(plain_dex, BuildDexWithStrings({"Lcom/example/Plain;"}));
    info.path = plain_dex;
    const auto plain = astra::AnalyzeGoogleServiceRequirements({info});
    Expect(plain.required.analysis_ok && plain.provider_id == L"none" &&
               plain.provider_state == L"not-required",
           "plain DEX incorrectly required a Google provider", failures);

    std::filesystem::remove_all(root, ignored);
    if (failures == 0) {
        std::cout << "All Astra Google service requirement tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}

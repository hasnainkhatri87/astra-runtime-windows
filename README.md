# Astra Runtime

Astra is an experimental, portable Windows compatibility runtime for
user-provided Android application packages. Foodpanda is the first validation
target; no application-specific behavior is built into Astra.

## Verified executable milestone

The current native Windows build provides this real workflow:

1. Run `astra.exe`.
2. Click **Install package**.
3. Choose an `.apk`, `.apkm`, `.apks`, or `.xapk` file you are authorized to use.
4. Astra safely indexes the ZIP container, selects x86-64/English/xhdpi splits,
   and extracts them with bounded DEFLATE and CRC verification.
5. Every selected APK must pass APK Signature Scheme v2 signature and signed
   content verification, and all selected splits must have the same signer set.
6. Root `classes*.dex` payloads are freshly extracted and checked for recognized
   DEX format, bounded header/table/map ranges, SHA-1 signature, and Adler-32
   checksum.
7. Verified DEX string tables are scanned for Google core, Maps, location,
   authentication, cloud messaging, billing, and Play Integrity dependencies;
   the required provider state is saved per app.
8. The installed package appears in the native launcher. **Run** currently opens
   the honest native runtime boundary; it does not yet execute an Android
   Activity.

The supplied Foodpanda 26.27.0 APKM passed this flow locally on 2026-07-15:

- 4 compatible APK splits selected and CRC-verified;
- all 4 v2 signatures and signed content digests verified with one signer set;
- signed manifest resolved `com.deliveryhero.applaunch.launcher.LauncherActivity`;
- 8 DEX v039 payloads structurally and integrity-validated after signed extraction;
- Google API profile detected as core, Maps, location, authentication, cloud
  messaging, and billing, with `open-services:runtime-bridge-pending`;
- portable CLI exited successfully without uploading the APKM.

Exact evidence is recorded in
[docs/FOODPANDA_M2_RESULTS.md](docs/FOODPANDA_M2_RESULTS.md). The Google-service
provider contract and its legal/security limits are documented in
[docs/GOOGLE_SERVICES.md](docs/GOOGLE_SERVICES.md).

This includes a real managed-code proof: Astra resolves a checksum-valid DEX
static `()I` method and executes a bounded integer/branch opcode subset, returning
`42` in CI. It is not a completed Android execution engine. Full ART semantics,
Android framework APIs, ELF/Bionic loading, graphics, and app lifecycle services
are still pending. The project will not display a fake Foodpanda screen or claim
its launcher Activity ran before that is true.

## Use the portable tool

The cloud artifact contains three small native executables:

- `astra.exe`: graphical package installer and application list;
- `astra-cli.exe`: command-line installer and diagnostics;
- `astra-runtime.exe`: per-application Windows host process.

Command-line installation is also available:

```powershell
.\astra-cli.exe install "C:\Path\application.apkm"
.\astra-cli.exe list
.\astra-cli.exe services
.\astra-cli.exe dex-execute "C:\Path\probe.dex" "Lexample/Probe;" "answer"
```

## No large local builder

The GitHub Actions workflow compiles and tests Astra on a temporary Windows
runner and publishes `AstraRuntime-win-x64.zip`. A user needs only the portable
ZIP; Android Studio and the Android SDK are not required.

The workflow never uploads an APK. Android package formats are ignored by Git,
rejected if accidentally tracked, and checked again before artifact publishing.

Local development is optional for contributors who already have Visual Studio
2022 C++ and CMake:

```powershell
cmake --preset win-x64-release
cmake --build --preset win-x64-release --parallel 4
ctest --preset win-x64-release --output-on-failure
cmake --install out/build/win-x64-release --config Release
```

## Current compatibility limits

- APK Signature Scheme v2 is verified. V3-only and V4-only packages are not yet
  accepted.
- Standard APK DEX versions 035 and 037-040 are accepted. Experimental DEX v041
  containers are explicitly rejected until their container rules are supported.
- The managed executor currently supports only verified static no-argument
  integer methods and a bounded integer/branch opcode subset. It is an M2 proof,
  not enough to start an Android Activity or run Foodpanda yet.
- Direct native CPU execution requires an ABI matching the Windows host. ARM-only
  applications need a separately labelled instruction translator or cannot run
  in strict direct mode.
- Google API requirements and provider state are now detected and reported.
  The open-services framework bridge is not runnable yet. Proprietary Google
  Play Store/GMS binaries are not included; Google says GMS requires a license
  and Android compatibility approval.
- Play Integrity is reported unavailable on this Windows compatibility runtime;
  Astra never fabricates an integrity verdict or certified-device identity.
- Passing package intake does not imply that an app can render or use network,
  location, WebView, notifications, login, or payments yet.

## Performance boundary

On the first Foodpanda test PC, the idle native launcher used 10.13 MiB working
set and the idle runtime host used 8.50 MiB; both measured 0.000% machine CPU
over the sample interval. These are host-only measurements. The eventual ART,
framework, graphics, and application memory must be measured separately.

## Foodpanda test policy

The supplied APKM remains outside this repository. The local interoperability
test stops before login, ordering, payment, or account automation. Astra must not
bypass TLS pinning, Play Integrity, fraud controls, identity checks, or any app
security policy.

Astra is not affiliated with or endorsed by Google, Foodpanda, or Delivery Hero.
"Android" is used only as a descriptive trademark of Google LLC.

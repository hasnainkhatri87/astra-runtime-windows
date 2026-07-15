# Astra Runtime

Astra is an experimental, portable Windows compatibility runtime for
user-provided Android application packages. Foodpanda is the first validation
target; application-specific logic is not built into Astra.

## Current executable milestone

The bootstrap build provides a real native Windows workflow:

1. Run `astra.exe`.
2. Click **Install package**.
3. Choose an `.apk`, `.apkm`, `.apks`, or `.xapk` file you are authorized to use.
4. Astra validates the container type, streams a SHA-256 hash, copies the package
   into an isolated local application directory, and displays it in the library.
5. Select the installed package and click **Run**.
6. `astra-runtime.exe` opens the package's native Windows host surface and private
   data boundary.

This is milestone M0, not yet a completed Android execution engine. The host
window explicitly reports that ART/DEX and Android framework execution are not
connected. The project will not display a fake application screen or claim an
Android activity is running before that is true.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the gates required to reach
Foodpanda's first rendered frame and broader APK compatibility.

## No large local builder

The repository includes a GitHub Actions workflow that compiles and tests Astra
on a temporary Windows cloud runner, then uploads `AstraRuntime-win-x64.zip`.
The user's PC needs only the downloaded portable ZIP.

The workflow never uploads an APK. Package formats are ignored by Git, rejected
if accidentally tracked, and checked again before publishing the ZIP artifact.

## Local development (optional)

If a developer already has Visual Studio 2022 with C++ and CMake:

```powershell
cmake --preset win-x64-release
cmake --build --preset win-x64-release --parallel 4
ctest --preset win-x64-release --output-on-failure
cmake --install out/build/win-x64-release --config Release
```

No Android Studio or Android SDK is used by the bootstrap build.

## Foodpanda test package

The supplied Foodpanda APKM remains outside this repository. The recorded local
test profile is `policies/foodpanda-26.27.0.test-profile.json`. It identifies the
x86-64 split, which permits direct CPU execution on x86-64 Windows once the ELF,
Bionic, JNI, and ART layers are implemented.

The initial test sequence is deliberately limited to package installation,
offline cold launch, and first-frame rendering. It must not automate login,
orders, payments, or bypass TLS pinning, fraud controls, identity checks, or
application security policy.

## Product and licensing status

- Google Play Store and Google Mobile Services are not included.
- User-provided packages and application data stay local.
- Packages are preserved unmodified.
- The current importer checks the outer ZIP signature and SHA-256 only. Android
  APK Signature Scheme v2/v3 verification and split extraction are M1 work.
- Astra is not affiliated with or endorsed by Google, Foodpanda, or Delivery Hero.

“Android” is used only as a descriptive trademark of Google LLC.

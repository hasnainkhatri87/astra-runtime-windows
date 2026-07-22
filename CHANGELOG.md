# Changelog

All notable project changes are recorded here. The project follows Semantic
Versioning once a stable public API exists; prerelease versions may change
incompatibly.

## [0.1.0-alpha.1] - 2026-07-22

### Added

- Portable native Windows launcher, CLI, and per-application host process.
- Bounded APK/APKM/APKS/XAPK container intake with CRC and path checks.
- APK Signature Scheme v2 signed-content and common-signer verification.
- Android binary manifest parsing and package/launcher metadata extraction.
- DEX integrity catalog and a deliberately bounded integer-method execution
  proof.
- Google-service dependency detection and explicit provider-state reporting.
- Experimental Android framework/view/input foundation.
- Optional, license-gated Android 16 Google Play virtualization backend.
- Windows CI builds, tests, portable ZIP packaging, and package-leak checks.

### Security

- User-provided Android packages are excluded from source control and CI
  artifacts.
- Astra does not forge device certification, Play Integrity, authentication, or
  payment state.

### Known limitations

- The native runtime is not a complete ART, Android framework, Bionic/JNI, or
  graphics implementation and cannot run arbitrary Android applications.
- The virtualization backend requires separately accepted third-party terms and
  hardware virtualization.

[0.1.0-alpha.1]: https://github.com/hasnainkhatri87/astra-runtime-windows/releases/tag/v0.1.0-alpha.1

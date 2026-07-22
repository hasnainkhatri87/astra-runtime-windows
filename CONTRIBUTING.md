# Contributing to Astra Runtime

Thank you for helping improve Astra. The project is experimental and values
small, reviewable changes backed by reproducible evidence.

## Before you start

- Search existing issues before opening a new one.
- Open an issue before a large architectural change.
- Use only packages, accounts, and systems you own or are authorized to test.
- Never commit APK/APKM/APKS/XAPK/AAB files, credentials, proprietary Google
  binaries, account sessions, private keys, or personal data.
- Report vulnerabilities privately according to [SECURITY.md](SECURITY.md).

## Build and test on Windows

Requirements are Visual Studio 2022 with the C++ workload, CMake 3.24 or newer,
Git, and network access for the pinned zlib dependency.

```powershell
cmake --preset win-x64-release
cmake --build --preset win-x64-release --parallel 4
ctest --preset win-x64-release --output-on-failure
```

Run `git diff --check` before submitting. New behavior should include tests, and
security-sensitive parsing changes should include malformed and boundary cases.

## Pull requests

Keep each pull request focused. Explain the problem, the chosen approach,
security implications, and verification performed. Update user-facing docs when
behavior or limitations change. Do not claim Android compatibility that has not
been demonstrated by a reproducible test.

By contributing, you agree that your contribution is licensed under the
Apache License 2.0 that covers this repository.

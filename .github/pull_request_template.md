## Summary

Describe the problem and the scoped change.

## Verification

- [ ] `cmake --preset win-x64-release`
- [ ] `cmake --build --preset win-x64-release --parallel 4`
- [ ] `ctest --preset win-x64-release --output-on-failure`
- [ ] `git diff --check`

List any additional tests and attach only sanitized, redistributable evidence.

## Security and licensing

- [ ] No APK/APKM/APKS/XAPK/AAB, credential, token, private key, account session, proprietary Google binary, or personal data is included.
- [ ] New dependencies are pinned, license-reviewed, and documented in `THIRD_PARTY_NOTICES.md`.
- [ ] The change does not bypass integrity, authentication, fraud, identity, or payment controls.
- [ ] User-facing behavior and compatibility limits are documented honestly.

# Foodpanda signed-package, manifest, DEX, and service-profile result

Test date: 2026-07-15
Package: `com.global.foodpanda.android` 26.27.0 (`262700094`)
Test boundary: local, offline package intake only

## Proven result

The warning-clean portable Windows CLI built from commit `974bdcc` completed
successfully:

```text
Selected and CRC-verified 4 APK split(s) for x86-64 Windows.
All selected APK v2 signatures and signed content digests verified.
Parsed signed manifest for com.global.foodpanda.android (SDK 29-35).
Launcher: com.deliveryhero.applaunch.launcher.LauncherActivity.
Validated 8 DEX payload(s).
Detected Google service APIs: core,maps,location,authentication,cloud-messaging,billing.
Open Services provider selected; runtime bridge pending.
```

- GitHub Actions run: `29441998296` (run 14), success, 6/6 tests
- Astra MSVC warnings: none
- Portable ZIP SHA-256:
  `fe44db96bab97aa014da33caed320dc8052e3910be5bc873df8dd4510717d120`
- APKM SHA-256:
  `317be58c4ed6d65f49e60d62eb4b5aca70b870d35c92fe41e711b0eb72f2112b`
- APK signer SHA-256:
  `af472590adc7f390d8aa24bdb2b049591efdf35b0a0837582890aa7d2276c663`
- Selected splits: base, x86_64, xhdpi, English

## Verified DEX catalog

| File | Version | Bytes | String IDs | Type IDs | Method IDs | Class defs |
|---|---:|---:|---:|---:|---:|---:|
| classes.dex | 039 | 9,394,748 | 57,649 | 21,127 | 65,526 | 8,772 |
| classes2.dex | 039 | 868 | 10 | 4 | 2 | 1 |
| classes3.dex | 039 | 9,600,480 | 60,256 | 22,294 | 65,534 | 9,736 |
| classes4.dex | 039 | 10,574,024 | 60,092 | 27,557 | 65,522 | 13,358 |
| classes5.dex | 039 | 11,287,728 | 60,777 | 28,271 | 65,434 | 14,839 |
| classes6.dex | 039 | 11,491,096 | 60,707 | 27,636 | 65,536 | 11,696 |
| classes7.dex | 039 | 8,620,784 | 52,853 | 13,035 | 65,500 | 8,651 |
| classes8.dex | 039 | 8,995,296 | 49,867 | 12,824 | 60,467 | 9,353 |

For every file, Astra checked DEX magic/version, declared size and endianness,
fixed table bounds, data/map bounds and consistency, SHA-1 signature, and
Adler-32 checksum after CRC-verified extraction from a signed APK.

## Resolved application and service profile

- Package: `com.global.foodpanda.android`
- Launcher: `com.deliveryhero.applaunch.launcher.LauncherActivity`
- SDK range: 29-35
- Required APIs: core, Maps, location, authentication, cloud messaging, billing
- Provider: `open-services`
- Provider state: `runtime-bridge-pending`
- Play Integrity available: `false`

The detector did not find a direct Play Integrity dependency in these eight DEX
files. Astra still never claims or fabricates a certified-device verdict.

## Managed execution proof

The same run passed a checksum-valid DEX v039 fixture through the production
SHA-1/Adler-32 verifier, resolved `LAstraProbe;->answer()I`, executed real
`const/16`, `add-int/lit8`, and `return` instructions, and obtained `42`.
Conditional branches, division-by-zero rejection, unsupported-opcode rejection,
checksum tampering, and an infinite-loop step limit also passed.

This proves a bounded DEX interpreter path; it does not prove Foodpanda execution.

## Native idle footprint

Measured over a three-second idle sample on the first Windows 11 test PC:

| Process | Working set | Private memory | Average machine CPU |
|---|---:|---:|---:|
| Launcher | 10.13 MiB | 1.64 MiB | 0.000% |
| Runtime host | 8.50 MiB | 1.32 MiB | 0.000% |

These figures cover only the current native host. They do not predict the later
ART, framework, graphics, WebView, or Foodpanda application footprint.

## Not yet proven

No Foodpanda DEX method or launcher Activity executed in this milestone. There
was no login, network transaction, order, payment, integrity bypass, TLS bypass,
or simulated Foodpanda UI. The next gate is a real Android test Activity frame,
followed by Foodpanda cold launch and callable framework/open-service bridges.

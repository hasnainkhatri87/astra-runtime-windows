# PandaDirect: Foodpanda 26.27.0 feasibility report

Date: 2026-07-15

This report covers read-only interoperability analysis of the supplied APKM. It does not authorize redistribution, bypass app security, or claim that the application is already executable on Windows.

## Artifact identity and provenance

- Package: `com.global.foodpanda.android`
- Version: `26.27.0`
- Version code: `262700094`
- APKM size: `99,281,094` bytes
- APKM SHA-256: `317BE58C4ED6D65F49E60D62EB4B5ACA70B870D35C92FE41E711B0EB72F2112B`
- Base APK SHA-256: `72479BF23F30693763AED31C9CE034E81AC2F418DE0184C7B57D9CF897099EFE`
- x86-64 split SHA-256: `D4077E4F28E2303C2F64B3427424281A217C78049304429E36C9DF3A087E6A04`

All 35 embedded APKs have valid v2/v3 signatures and share this signer fingerprint:

`AF:47:25:90:AD:C7:F3:90:D8:AA:24:BD:B2:B0:49:59:1E:FD:F3:5B:0A:08:37:58:28:90:AA:7D:22:76:C6:63`

That fingerprint is also published for `com.global.foodpanda.android` by Foodpanda-controlled Android App Links files:

- https://www.foodpanda.pk/.well-known/assetlinks.json
- https://www.foodpanda.sg/.well-known/assetlinks.json

This establishes signer continuity with Foodpanda-controlled domains. The supplied APKM is nevertheless a third-party APKMirror acquisition and must not be treated as a redistribution license.

## Android contract

- Minimum SDK: Android 10 / API 29
- Target SDK: Android 15 / API 35
- Compile SDK: API 35
- Host goal: Android 17 / API 37 while preserving target-35 compatibility behavior
- Application class: `com.deliveryhero.app.App`
- Launcher target: `com.deliveryhero.applaunch.launcher.LauncherActivity`
- Application requests `largeHeap=true`
- Native libraries are stored for direct APK mapping (`extractNativeLibs=false`)
- Eight DEX files
- 194 activities, 1 activity alias, 23 services, 21 receivers, and 16 providers

Required or relevant device contracts include OpenGL ES 2.0, camera, location, network, notifications, WebView, microphone, optional NFC, sensors, contacts/calendar integration, and screen-capture detection.

## CPU and native-code result

The bundle contains `arm64-v8a`, `x86`, and `x86_64` splits. Intel/AMD Windows can therefore execute Foodpanda's x86-64 instructions without ARM translation or virtual-CPU emulation.

Each ABI split contains the same nine native libraries:

- `libandroidx.graphics.path.so`
- `libcronet.145.0.7582.0.so`
- `libdatastore_shared_counter.so`
- `libimage_processing_util_jni.so`
- `liblanguage_id_l2c_jni.so`
- `libsigner.so`
- `libsurface_util_jni.so`
- `libtensorflowlite_jni.so`
- `libtranslate_jni.so`

The x86-64 ELF files depend on Android/Bionic libraries including `libc.so`, `libm.so`, `libdl.so`, `liblog.so`, `libandroid.so`, and `libjnigraphics.so`. PandaDirect therefore needs an x86-64 ELF loader plus Bionic/Android ABI shims even though it does not need CPU emulation.

## Confirmed dependency surface

Static references confirm these important runtime requirements:

- Google Play services client APIs
- Google Maps and fused location
- Firebase Cloud Messaging and Firebase initialization/analytics
- Google and Facebook authentication paths
- Android WebView and Custom Tabs
- Chromium Cronet 145 networking
- Android Keystore and Google Tink cryptography
- Certificate pinning
- Braze notifications/content
- Adjust analytics/attribution
- Sentry diagnostics
- Klarna mobile SDK and WebView payment flows
- Adyen endpoints and other regional payment/deep-link schemes
- Incognia device/fraud signals
- Onfido identity, camera, document, and environment-integrity flows
- TensorFlow Lite and ML Kit

No direct reference to the Google Play Integrity API or SafetyNet was found. Generic integrity references belong to other SDKs, especially Onfido, Facebook, WebView, and Amazon Privacy Pass. This does not guarantee that Foodpanda's backend will accept an uncertified Windows compatibility environment.

## Strict non-emulated architecture

The shipping design must contain no Hyper-V guest, virtual Android device, guest Linux kernel, virtual CPU, Android launcher, or full Android system image.

1. `PandaPackage` selects the signed base, x86-64, language, and density splits without altering their signatures.
2. `ArtNT` executes DEX through a Windows port of ART and preserves Android target-SDK behavior.
3. `PandaELF` maps x86-64 ELF libraries and resolves Bionic/Android symbols to C++ compatibility shims.
4. `BinderNT` maps Android Binder semantics to Windows ALPC/shared memory.
5. `ActivityNT` maps Activities, services, jobs, broadcasts, permissions, and lifecycle events to Windows processes and windows.
6. `SurfaceNT` maps HWUI/Skia/OpenGL ES surfaces to Direct3D and DirectComposition.
7. `DeviceNT` maps camera, location, input, audio, sensors, clipboard, and notifications to Windows APIs.
8. `WebNT` implements Android WebView/Custom Tabs behavior on WebView2 without changing payment or OAuth security boundaries.
9. `TrustNT` maps Android Keystore to CNG/TPM/DPAPI and preserves TLS certificate pinning.
10. `GmsContract` supplies only licensed/authorized Maps, location, authentication, and notification contracts. It must not imitate protected Google services without permission.

## Compatibility matrix

| Area | Status | Reason |
|---|---|---|
| x86-64 CPU execution | Green | A native x86-64 split is supplied |
| Package/split selection | Green | Split layout and signer are known |
| ELF loading | Yellow | Nine libraries, but Bionic relocations/JNI must be implemented |
| DEX execution | Red | A production-quality ART-on-NT runtime does not yet exist |
| Android framework | Red | Large Activity/service/API surface must be reproduced |
| Graphics/input | Yellow | Technically bridgeable to Direct3D/Windows input |
| WebView/payment flows | Red | Exact WebView, OAuth, cookie, redirect, and 3-D Secure behavior is required |
| Google Maps/location | Red | Requires licensed Google contracts or Foodpanda cooperation |
| Push notifications | Red | FCM normally depends on Google services; WNS relay needs Foodpanda cooperation |
| TLS/Keystore | Yellow | Must preserve Android trust and pinning semantics exactly |
| Fraud/KYC acceptance | Red | Incognia/Onfido/backend policy may reject an unknown environment; do not bypass |
| Low idle resource use | Green | The runtime can terminate/hibernate fully when Foodpanda is closed |
| Low active memory | Yellow | `largeHeap=true`, WebView, Maps, ML, and security SDKs make sub-400 MB unreliable |

## First executable proof

The first legitimate proof should not attempt checkout or modify the APK.

1. Validate the APKM and select `base.apk`, `split_config.x86_64.apk`, one language split, and one density split.
2. Build `PandaELF` and load the simplest native library while resolving `libc`, `libm`, and `libdl` through Windows shims.
3. Bring up ART-on-NT and load the eight DEX files without running the application class.
4. Initialize Android resources and instantiate the signed launcher Activity.
5. Render the first frame in a Windows window with no network login.
6. Add input, WebView, networking, manual address entry, and restaurant browsing.
7. Add location, Maps, notifications, authentication, and payments only through licensed services and test systems.

Go/no-go gate: if Foodpanda, Incognia, Onfido, Google services, or the backend requires a certified Android environment, PandaDirect must obtain partner approval. It must not spoof device integrity or bypass anti-fraud controls.

## Resource targets

- Closed: 0% CPU and no application runtime process
- Open but idle: below 1% average CPU
- Warm launch after compiled-code cache: below 2 seconds
- Minimal selected APK payload: roughly 130 MB before PandaDirect runtime files
- Active RAM planning budget: 500-800 MB for early builds; measure before promising a lower number

The application itself opts into Android's large heap and includes WebView, Maps, Cronet, ML, fraud, identity, and payment SDKs. A sub-400 MB active-RAM guarantee is not credible until a working build is profiled.

## Decision

Foodpanda is a viable research target for strict non-emulated execution because it includes x86-64 native code. It is not a small or easy first application. The hardest problems are ART/framework compatibility, licensed Google-service contracts, WebView/payment fidelity, and truthful acceptance by fraud/KYC systems—not CPU instruction execution.

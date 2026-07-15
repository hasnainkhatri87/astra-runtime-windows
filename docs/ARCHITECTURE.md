# Astra Runtime architecture

## Product boundary

Astra is an experimental Windows compatibility runtime for Android application
packages. It is not a virtual phone, a virtual machine, or a complete Android
system image. Each application is intended to appear as an independent Windows
application.

The compatibility goal is broad application coverage. Literal support for every
application is not technically possible under a strict no-CPU-translation rule:
an ARM-only native library cannot execute on an x86-64 processor. Astra's core
mode therefore executes matching host instructions directly. An optional
instruction translator, if ever added, must be labelled separately.

## User flow

```text
User-owned APK/APKM/APKS/XAPK
              |
              v
    Package validation and split selection
              |
              v
  Local per-app package and private data store
              |
              v
       Astra Windows launcher
              |
              v
   Native runtime host + ART/DEX engine
              |
              v
 Android APIs mapped to Windows services
```

Packages, account details, and application data remain on the user's computer.
They are excluded from source control and cloud build artifacts.

## Runtime modules

- **Package store:** container safety checks, APK Signature Scheme validation,
  manifest/resource parsing, and split selection.
- **ART host:** DEX interpreter/JIT/AOT integration and JNI boundary.
- **Native loader:** x86/x86-64 ELF loading plus Bionic/NDK symbol mapping.
- **Framework services:** activity, package, permission, content, Binder-like IPC,
  lifecycle, storage, notifications, location, and intent routing.
- **Graphics:** Surface/Canvas/OpenGL ES translated through ANGLE to Direct3D.
- **Web:** Android WebView behavior backed by a versioned Chromium/WebView layer;
  WebView2 can be used only where behavior is compatible.
- **Security:** restricted Windows token/AppContainer research, Job Objects,
  per-app storage, permission prompts, and no host filesystem access by default.
- **GMS contract:** licensed integration boundary only. Google Play and Google
  Mobile Services are not AOSP and cannot be bundled without permission.

## Milestones and truthful gates

| Gate | Required evidence |
|---|---|
| M0 Windows host | Install package, display library, open isolated host window |
| M1 Package trust | Parse AXML, verify APK v2/v3, select signed splits |
| M2 Managed execution | ART loads DEX and calls a synthetic Activity |
| M3 First frame | Android View/Compose renders through the Windows surface |
| M4 Foodpanda cold launch | Unmodified launcher activity renders without network |
| M5 Platform services | Input, storage, network, WebView, location, notifications |
| M6 Broader apps | Compatibility database driven by legal user-supplied packages |
| M7 Google services | Only after written licensing and compatibility approval |

The bootstrap repository completes M0. It must not claim M2 or later until an
unmodified activity actually executes.

## Performance policy

The launcher uses a blocking Windows message loop and therefore consumes no
busy-loop CPU while idle. Package hashing is streamed in 1 MiB chunks. The
runtime will be loaded on demand and terminated with the application.

Runtime overhead and application memory must be measured independently. A large
application can still require substantial memory even if Astra itself is small.

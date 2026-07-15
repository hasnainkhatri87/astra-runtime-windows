# Astra Runtime architecture

## Product boundary

Astra is a Windows userspace compatibility runtime for Android application
packages. It is not a virtual phone, virtual machine, or complete Android system
image. The intended result is one independent Windows process tree per app.

"Direct" means that matching x86/x86-64 native instructions execute on the host
CPU and Android OS services are mapped to Windows services. DEX is managed
bytecode and still requires an Android-compatible runtime such as a Windows port
of ART or a compatible execution engine. An ARM ELF library cannot execute on an
x86-64 CPU without instruction translation.

```text
User-owned APK/APKM/APKS/XAPK
              |
              v
  Bounded parser + signed split verification
              |
              v
    Verified DEX + native library inventory
              |
              v
     Per-app Windows process and data root
              |
              v
      ART/DEX execution + JNI boundary
              |
              v
 Android APIs mapped to Windows services
              |
              v
  ANGLE/Direct3D surface in a Windows window
```

Packages, account details, and application data remain on the user's computer.
They are excluded from source control and cloud build artifacts.

## Runtime modules

- **Package trust:** bounded ZIP parsing, split selection, APK signing
  verification, AXML/resources, and immutable package identity.
- **Managed runtime:** DEX verifier, class linker, garbage collector,
  interpreter/JIT/AOT, exceptions, reflection, and Java/JNI contracts. Reusing a
  pinned, audited ART revision is preferred to inventing incompatible Java
  semantics.
- **Native loader:** x86/x86-64 ELF mapping, relocations, TLS, and Bionic/NDK
  symbol compatibility on Windows.
- **Framework services:** Activity lifecycle, package and permission services,
  storage, intents, Binder-like IPC, notifications, location, and input.
- **Graphics:** Surface/Canvas/OpenGL ES translation through ANGLE to Direct3D.
- **Web:** a versioned Chromium/WebView compatibility layer. WebView2 is usable
  only where observable Android WebView behavior remains compatible.
- **Security:** Job Objects, restricted tokens/AppContainer research, per-app
  storage, permission prompts, and no arbitrary host filesystem access.
- **Google-service providers:** verified DEX requirements are mapped either to
  an open-services provider target (microG-compatible framework contracts) or
  to a separately supplied, authorized GMS plug-in. Astra does not bundle
  proprietary Play Store/GMS code and never forges Play Integrity.

## Truthful milestone gates

| Gate | State | Required evidence |
|---|---|---|
| M0 Windows host | Complete | Install/list a package and open a separate native host process |
| M1A package structure | Complete for tested APKM | Bounded container parser, split selection, CRC extraction |
| M1B package trust | Partial | V2 signed content/common signer and signed binary manifest parsing complete; v3 pending |
| M1C DEX intake | Complete for Foodpanda | Recognized DEX, bounded tables/map, SHA-1 and Adler-32; 8 real payloads pass |
| M2 managed execution | Pending | Execute a synthetic unmodified DEX entry under the Windows runtime and prove its result |
| M3 first frame | Pending | An Android View/Compose test Activity renders through the Windows surface |
| M4 Foodpanda cold launch | Pending | Unmodified launcher Activity reaches a rendered offline frame |
| M5 platform services | Pending | Input, storage, network, WebView, location, and notifications |
| M6 broader apps | Pending | Compatibility database based on legal user-supplied packages |
| M7 open services | Detection complete; bridge pending | Detect requirements, route provider calls, and pass Maps/location/auth/push probes |
| M8 licensed GMS | Authorization gate | Written licensing and compatibility approval before any proprietary binary is accepted |

See [GOOGLE_SERVICES.md](GOOGLE_SERVICES.md) for the provider contract and
Play Integrity boundary.

The next execution proof must run real DEX instructions. A painted imitation of
an Android screen does not satisfy M2.

## Performance policy

The launcher and host use blocking Windows message loops and no idle polling.
Package hashing and integrity validation are streamed in bounded buffers. Runtime
components load on demand and terminate with the app.

Host overhead and app memory are reported separately. Foodpanda's eight DEX
files occupy about 69 MiB on disk after verified extraction; they are not all
loaded into RAM by the current intake step.

## Upstream and licensing policy

Every reused runtime component must be pinned, license-audited, reproducibly
built, and listed in `THIRD_PARTY_NOTICES.md`. AOSP/ART is Apache-2.0 overall but
its dependency graph must be reviewed component by component. Google Play/GMS
bits are outside this open-source runtime boundary.

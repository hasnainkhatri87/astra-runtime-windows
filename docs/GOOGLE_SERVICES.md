# Google-service compatibility boundary

Foodpanda and many other Android apps call APIs supplied by Google Play
services. Astra therefore treats Google-service support as a provider contract,
not as an assumption hidden inside the runtime.

## Provider modes

### Open Services

The personal-use default is an open compatibility provider targeting core Play
services APIs, Maps, fused location, account authentication, and cloud
messaging. The intended upstream is
[microG GmsCore](https://github.com/microg/GmsCore), an Apache-2.0 free
implementation of Play services.

Astra does not yet bundle microG. GmsCore normally expects a functioning Android
framework, package manager, Binder, account service, permissions, and background
service lifecycle. Astra must implement those contracts before its APK can run
correctly. The provider will be pinned and audited before distribution.

### Licensed GMS

Google Mobile Services is not part of AOSP and is available only under a Google
license. Astra reserves a provider plug-in boundary for a party that has written
authorization to use those binaries. Astra itself does not download,
redistribute, or silently copy Google Play Store/GMS packages.

Google's current compatibility program requires an Android-compatible
implementation before GMS licensing can be considered:
[AOSP compatibility overview](https://source.android.com/docs/compatibility/overview)
and [Google's GMS description](https://www.android.com/gms/).

## Play Integrity

Play Integrity is not a normal API shim. Its device verdict can rely on a genuine
certified Android image and hardware-backed boot signals. Astra will report this
capability as unavailable unless Google provides an authorized Windows path. It
will not forge a token, certified-device label, Play entitlement, package
signature, or hardware attestation. See the
[official Play Integrity overview](https://developer.android.com/google/play/integrity/overview).

## Current implementation state

| Part | State |
|---|---|
| Scan verified DEX string tables for service API requirements | Implemented |
| Select `open-services` for apps that require Google APIs | Implemented |
| Persist per-app requirements/provider status | Implemented |
| Android framework/Binder provider ABI | Pending |
| Maps/location/auth/cloud-messaging behavior | Pending |
| Run pinned microG provider | Pending |
| Licensed GMS plug-in | Requires authorization |
| Play Integrity certified verdict | Unavailable; never forged |

Passing requirement detection does not mean the service is already callable.
The runtime reports `runtime-bridge-pending` until an end-to-end API call passes.

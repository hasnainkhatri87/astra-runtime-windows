# Astra Android virtualization backend

Astra's direct native-runtime research is retained, but full Android compatibility is
provided by an optional Android virtual device. The backend uses the official Android
Emulator, Windows Hypervisor Platform (WHPX), and a signed Android 16 x86_64 Google Play
system image. It does not require Android Studio.

This is the practical route for Foodpanda and other applications: Android's real ART,
framework, graphics stack, JNI/Bionic environment, package manager, Google Play services,
and Play Store run inside the guest. Astra only validates the local package, selects its
compatible splits, starts the guest, and sends `adb install-multiple` plus the declared
launcher Activity.

## What is and is not included

The portable Astra ZIP does **not** include the Android SDK, Android emulator, Google Play
image, Play Store, GMS, Java runtime, user credentials, or application packages. The user
downloads these components directly from their licensors by running the bootstrap with an
explicit terms flag. This keeps proprietary components out of the repository and artifact.

The official Android tools page supplies command-line tools without Android Studio. Google
documents that `google_apis` images include Play services while `google_apis_playstore`
images also include Play Store. The latter is the only image Astra accepts.

## Install once

1. Enable firmware virtualization (Intel VT-x/AMD-V) in BIOS/UEFI.
2. In an elevated PowerShell window, run:

   ```powershell
   .\enable-windows-hypervisor.ps1 -EnableWindowsHypervisorPlatform
   ```

3. Restart Windows.
4. From the `tools` folder, run the bootstrap. It requires an existing Java 17+ runtime,
   or an explicit portable Temurin JRE download:

   ```powershell
   .\bootstrap-android-vm.ps1 -AcceptThirdPartyTerms -InstallPortableJre
   ```

5. In Astra's portable folder, check and run an installed app:

   ```powershell
   .\astra-cli.exe vm-status
   .\astra-cli.exe vm-run <installed-app-id>
   ```

The profile is capped at 2 vCPUs and 1536 MiB guest RAM. It will still need multiple GB of
disk space and cannot be called low-RAM on an 8 GB computer. Astra never falls back to an
unaccelerated VM, because that would be too slow to be useful.

## Foodpanda test boundary

Foodpanda's verified x86_64-compatible split set is installed with `adb install-multiple`,
then Astra invokes its declared launcher. The app, Play Store login, maps/location, and all
network behavior are handled by Android and Google components in the guest.

Foodpanda can still decide not to run on a virtual or uncertified device. Astra does not
forge device certification, Play Integrity results, Google account state, payment flows,
or app-security checks. Login and ordering are manual user actions.

## Sources

- [Android command-line tools](https://developer.android.com/tools)
- [Android Emulator command line](https://developer.android.com/studio/run/emulator-commandline)
- [Windows acceleration via WHPX](https://developer.android.com/studio/run/emulator-acceleration)
- [Google Play system image behavior](https://developer.android.com/studio/run/managing-avds)
- [Temurin download API](https://adoptium.net/installation/ci-scripts/)

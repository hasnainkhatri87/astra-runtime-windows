# Optional MuMuPlayer helper

`tools/open-playstore-package-mumu.ps1` is an optional interoperability helper
for users who already installed MuMuPlayer themselves. MuMuPlayer is third-party
software and is not downloaded, bundled, licensed, or endorsed by Astra.

The helper discovers MuMuPlayer through its Windows uninstall metadata, starts
one selected VM, waits for Android and ADB, and opens a package's official Google
Play listing. It never stores, copies, or submits Google credentials or account
sessions.

```powershell
# Open Foodpanda's listing in VM 4.
.\tools\open-playstore-package-mumu.ps1 `
  -VmIndex 4 `
  -PackageName com.global.foodpanda.android

# Open Android's normal account-add UI; authenticate manually in MuMu.
.\tools\open-playstore-package-mumu.ps1 -VmIndex 4 -OpenGoogleSignIn

# Wait for a manual Play Store installation and then request normal app launch.
.\tools\open-playstore-package-mumu.ps1 `
  -VmIndex 4 `
  -PackageName com.global.foodpanda.android `
  -WaitForInstall
```

Availability, app compatibility, Play certification, integrity verdicts, and
account authentication remain controlled by Google, MuMuPlayer, and the app
publisher. The helper does not bypass those controls.

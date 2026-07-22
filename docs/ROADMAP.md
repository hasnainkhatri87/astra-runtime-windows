# Roadmap

Astra is developed through evidence-based milestones. Dates are intentionally
not promised; a milestone moves only when its acceptance evidence is public and
reproducible.

## Near term

- Expand malformed-input and boundary tests for ZIP, APK signatures, AXML, and
  DEX structures.
- Add fuzzing harnesses and a documented seed corpus containing only synthetic
  or redistributable fixtures.
- Support APK Signature Scheme v3 without weakening v2 verification.
- Improve launcher-intent resolution and Android VM diagnostics.
- Add reproducible release provenance and dependency update automation.

## Native compatibility research

- Define a small, testable class/object/runtime contract beyond the bounded DEX
  arithmetic proof.
- Render a real redistributable Android test Activity through a Windows surface.
- Map lifecycle, input, storage, networking, WebView, location, and notification
  contracts incrementally.
- Research x86-64 ELF/Bionic loading and JNI boundaries before considering ARM
  instruction translation.

## Platform services

- Implement an open-services provider contract using only appropriately
  licensed components.
- Keep proprietary Google Play/GMS binaries outside the repository and require
  explicit authorization for any licensed provider integration.
- Never emulate or forge Play Integrity, certified-device, fraud, identity, or
  payment verdicts.

## Project health

- Triage issues and review pull requests with documented acceptance criteria.
- Publish signed, checksummed prereleases from Windows CI.
- Maintain `SECURITY.md`, dependency notices, and compatibility evidence.

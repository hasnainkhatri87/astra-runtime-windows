# Security policy

Astra parses complex, user-supplied application containers, so package parsing,
signature verification, path containment, process isolation, and dependency
integrity are security-sensitive parts of the project.

## Supported versions

| Version | Supported |
|---|---|
| Latest tagged prerelease | Yes |
| `main` | Best effort |
| Older snapshots | No |

This experimental project does not yet provide a production security SLA.

## Report a vulnerability

Use GitHub's **Report a vulnerability** option in the Security tab. This creates
a private security advisory visible only to the reporter and maintainers. If
private reporting is unavailable, contact
[@hasnainkhatri87](https://github.com/hasnainkhatri87) through GitHub and request
a private reporting channel.

Please include the affected commit or version, impact, reproduction steps, and
the smallest safe proof of concept. Do not attach proprietary APKs, account
credentials, access tokens, personal information, or third-party user data.

Please do not open a public issue for an unpatched vulnerability. The maintainer
will acknowledge a complete report when practical, investigate it, coordinate a
fix and disclosure, and credit reporters who want attribution.

## Scope boundaries

Astra does not authorize testing of third-party applications or services. Only
test packages, systems, and accounts that you own or are explicitly authorized
to assess. Reports that require bypassing Play Integrity, fraud controls,
authentication, payments, or another service's security policies are out of
scope.

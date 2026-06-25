# Security Policy

## Supported Versions

Only the latest commit on `main` receives security fixes.

## Reporting a Vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Email the maintainer at the address listed in your GitHub profile, or open a
[GitHub Security Advisory](../../security/advisories/new) (private disclosure).

Include:
- A clear description of the vulnerability
- Steps to reproduce
- Potential impact
- Any suggested remediation

You will receive an acknowledgement within 72 hours. If the vulnerability is
confirmed, a fix will be issued as soon as practical and credited to the
reporter unless anonymity is requested.

## Out of Scope

- Vulnerabilities in dependencies (nanopb, ESP-IDF) should be reported upstream.
- Physical hardware attacks (JTAG, UART, SPI sniffing) are by design accessible
  on ESP32 developer boards and outside the scope of this policy.

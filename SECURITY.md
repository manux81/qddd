# Security Policy

## Supported Versions

qddd is currently under active development.

Security fixes are generally applied to the latest version on the `main` branch and to the most recent release when practical.

| Version        | Supported |
| -------------- | --------- |
| Latest release | ✅         |
| `main` branch  | ✅         |
| Older releases | ❌         |

## Reporting a Vulnerability

Please do **not** report security vulnerabilities through a public GitHub issue.

If you believe you have found a security issue in qddd, please use GitHub's **Private Vulnerability Reporting** feature for this repository, if available.

When reporting a vulnerability, please include:

* a clear description of the issue;
* the affected qddd version or commit;
* operating system and debugger backend used;
* steps to reproduce the problem;
* expected and actual behavior;
* any proof-of-concept code or logs that help reproduce the issue.

Please avoid including sensitive information such as API keys, passwords, private source code, or confidential target data.

## Scope

Examples of security issues that are relevant to qddd include:

* unintended command execution through debugger input;
* unsafe handling of executable paths or debugger-server arguments;
* credential or API-key leakage;
* sensitive information written to logs;
* unsafe parsing of debugger output;
* crashes or memory-safety issues caused by malformed debugger data;
* vulnerabilities involving remote GDB servers or hardware-debug configurations.

General bugs, UI issues, debugger compatibility problems, and feature requests should be reported through normal GitHub Issues.

## API Keys and Credentials

qddd may use external AI providers.

API keys and authorization credentials should never be included in bug reports, screenshots, logs, or example configuration files.

Environment variables should be preferred for credentials where supported.

## Disclosure

Please allow reasonable time for investigation and remediation before publicly disclosing a vulnerability.

Once a fix is available, the issue may be documented in the release notes or through a GitHub Security Advisory when appropriate.

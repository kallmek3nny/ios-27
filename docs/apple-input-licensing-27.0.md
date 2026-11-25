# Apple input licensing inventory (iOS 27.0, build 24A434)

**Status:** documentation only. For each Apple input that
[Apple runtime inputs](apple-runtime-inputs.md) lists, this file records the
license the repository cites for it, the kind of host that acquired and
processed it, and the clause or written permission that covers each use. It
is not a legal opinion and draws no conclusion beyond the published text.
The clause quotes and the analysis are in
[feasibility review section 7](feasibility-review-27.0.md#7-licensing); this
file links to them instead of repeating them. Anything the repository does
not record is marked **unresolved**.

## Rule

Every Apple component stays parse-only. None of it may run on the Android
host until an entry below names a clause or a written permission from Apple
that covers execution there. No entry does so today, and the
[sanitized inventory](apple-runtime-inventory-27.0.json) records
`apple_code_executed` as `false`.

## Reviewed agreement

| Field | Value |
| --- | --- |
| Title | Xcode and Apple SDKs Agreement |
| URL | [xcode.pdf](https://www.apple.com/legal/sla/docs/xcode.pdf) |
| Printed version | EA2002 |
| Printed date | 06/08/2026, as printed |
| Copy reviewed | Fetched 2026-11-08 for [section 7](feasibility-review-27.0.md#7-licensing) |
| Clauses section 7 quotes | Notice before section 1; 2.2.A and 2.2.A(iii); 2.5; 2.7 |

Section 7 quotes the agreement's definition of the Apple SDKs, which names
simulators labeled as part of the iOS SDK and included in the Xcode
Developer Tools package. This runtime was fetched directly from Apple's CDN
by `tools/apple-runtime-source.py`, not installed through Xcode. Whether the
agreement governs a runtime obtained that way, or other terms do, is
**unresolved**. No repository document records a license file inside the
runtime archive or its disk image, and no other Apple license has been
reviewed for these inputs.

## Acquisition and processing hosts

Line numbers refer to `docs/apple-runtime-inputs.md` at commit `4c0f4cb`.

| Step | Tool | Lines | Host kind |
| --- | --- | --- | --- |
| Index and asset metadata requests | `tools/apple-runtime-source.py` | 15-17 | Unresolved |
| Apple PKI root fetch for the asset service | `tools/apple-runtime-source.py` | 19-22 | Unresolved |
| Archive download and SHA-256 check | `tools/apple-runtime-source.py` | 27-29 | Unresolved |
| Decryption with the server-provided key | ipsw v3.1.742 | 39-41 | Unresolved |
| Extraction from the AppleArchive and APFS disk image | 7-Zip 24.09 | 41-43 | Unresolved |
| Header and import inspection | `audit-ios` | 66 | Unresolved |
| Cache and binding audit | `tools/apple-cache-audit.py` | See input 9 note | Unresolved |

The only host evidence is the example command at line 34, which uses the
Windows directory `C:/AppleRuntimeInputs`. An example path does not record
which host ran a step. No repository document records a macOS host,
Apple-branded hardware or written permission from Apple for any step. ipsw
and 7-Zip are third-party tools, not Apple inputs, so their licenses are
outside this inventory.

## Inputs

The license column names the agreement the repository cites for the input.
The last column names a grant that covers the uses in [Uses](#uses); none is
recorded for any input.

| # | Input | Lines | License cited | Covering clause or permission |
| --- | --- | --- | --- | --- |
| 1 | Simulator download index (`index2.dvtdownloadableindex`) | 15-17 | None reviewed | Unresolved |
| 2 | Asset service response, including archive key material kept out of Git | 15-17, 38-39 | None reviewed | Unresolved |
| 3 | Apple PKI root, loaded only into the request's SSL context | 19-22 | None reviewed | Unresolved |
| 4 | Simulator runtime archive, 8,053,063,680 bytes, SHA-256 at line 28 | 27-29 | Xcode and Apple SDKs Agreement, EA2002, 06/08/2026; whether it governs this download is unresolved | Unresolved |
| 5 | Decrypted AppleArchive and the APFS simulator disk image inside it | 39-43 | As input 4 | Unresolved |
| 6 | `SpringBoard.app/SpringBoard` executable | 55-56 | As input 4 | Unresolved |
| 7 | `SpringBoard.framework/SpringBoard` dylib | 57 | As input 4 | Unresolved |
| 8 | Shared-cache family, including UIKitCore, Foundation, libobjc and libSystem | 58-62 | As input 4 | Unresolved |
| 9 | 66 further standalone dylibs from the same disk image (the RuntimeRoot) | See note | As input 4 | Unresolved |

Input 9 and `tools/apple-cache-audit.py` are described in the section
SpringBoard dependency and binding audit of `docs/apple-runtime-inputs.md`,
which was not committed when this file was written. Add its commit here once
it lands.

## Uses

The clause column cites agreement text, quoted in section 7, whose words
name the act. It records the words only, not how they apply.

| Use | Inputs | Clause text that names the act | Covering clause or permission | Status |
| --- | --- | --- | --- | --- |
| Download outside Xcode | 1-4 | Notice before section 1 (any other download or use) | Unresolved | Done (line 29) |
| Installation and storage on the host | 4-9 | 2.2.A (installation on Apple-branded computers) | Unresolved; host kind not recorded | Done (line 43) |
| Decryption | 4-5 | 2.7 (decrypt) | Unresolved | Done (lines 39-41) |
| Copying and extraction outside the checkout | 5-9 | 2.7 (copy, except as expressly permitted) | Unresolved | Done (lines 41-43) |
| Static parsing of headers, imports and fixups | 6-9 | 2.7 (reverse engineer, disassemble); whether parsing is either act is unresolved | Unresolved | Done (line 66; see input 9 note) |
| Redistribution, hosting or packaging in the APK | None | 2.7 (upload, host, redistribute) | None recorded | Not done; lines 6-7 and [HANDOFF.md](../HANDOFF.md) forbid it |
| Execution on non-Apple-branded hardware, including the Android host | None | Notice before section 1; 2.5; 2.7 (install, use or run) | None recorded | Not done; the rule above forbids it |

## Exceptions

Section 7 quotes that the section 2.7 restrictions give way only "to the
extent any foregoing restriction is prohibited by applicable law", or where
the licensing terms of included open-source components or sample code
permit. The repository records no legal review that applies either exception
to these inputs or uses, so both are **unresolved**.

## Open items

To resolve an entry, record here:

1. The kind of host, its operating system and hardware brand, that ran
   each step in
   [Acquisition and processing hosts](#acquisition-and-processing-hosts).
2. The published terms Apple attaches to a runtime fetched directly from its
   CDN, if they differ from the reviewed agreement.
3. The terms, if any, that govern Apple's download index, asset service
   response and PKI root.
4. Any written permission from Apple, or a legal review of the exceptions
   above, with the uses and hosts it covers.
5. The licenses for iPhone device software and App Store applications, which
   section 7 did not examine.

The rule above holds until item 4 names a clause or permission that covers
execution on the Android host.

## Scope

This file adds no Apple bytes and no names, hashes or counts beyond those in
the linked documents. Section 7's next test proposed adding these fields to
the sanitized inventory. They are kept here instead, and the inventory is
unchanged.

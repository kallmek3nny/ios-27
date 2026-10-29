# Phase 20–22 engineering delivery

![Agent engineering delivery percentage](phase-delivery.svg)

**Meaning:** percentage of 20 explicitly scoped engineering workstreams with source-bearing PRs **merged into main**. This is not a measurement of iOS, Apple ABI, or real-device compatibility. A merged PR is not guaranteed to implement its entire original requirement.

**55.0% delivered** · 11/20 merged · 9 open source-code PRs · 0 without a tracked PR

| phase | merged | active | delivered |
| --- | ---: | ---: | ---: |
| 20 Objective-C / libSystem prototypes | 5/5 | 0 | 100.0% |
| 21 Darwin pthread and synchronization | 5/10 | 5 | 50.0% |
| 22 Guest VM, BSD, dyld, ObjC, Mach IPC | 1/5 | 4 | 20.0% |

## Evidence by workstream

| scope | state | source PR |
| --- | --- | --- |
| 20a | merged | [#65](https://github.com/kallmek3nny/ios-27/pull/65) |
| 20b | merged | [#63](https://github.com/kallmek3nny/ios-27/pull/63) |
| 20c | merged | [#67](https://github.com/kallmek3nny/ios-27/pull/67) |
| 20d | merged | [#64](https://github.com/kallmek3nny/ios-27/pull/64) |
| 20e | merged | [#62](https://github.com/kallmek3nny/ios-27/pull/62) |
| 21a | active | [#69](https://github.com/kallmek3nny/ios-27/pull/69) |
| 21b | merged | [#71](https://github.com/kallmek3nny/ios-27/pull/71) |
| 21c | merged | [#70](https://github.com/kallmek3nny/ios-27/pull/70) |
| 21d | active | [#73](https://github.com/kallmek3nny/ios-27/pull/73) |
| 21e | merged | [#75](https://github.com/kallmek3nny/ios-27/pull/75) |
| 21f | active | [#78](https://github.com/kallmek3nny/ios-27/pull/78) |
| 21g | merged | [#81](https://github.com/kallmek3nny/ios-27/pull/81) |
| 21h | merged | [#79](https://github.com/kallmek3nny/ios-27/pull/79) |
| 21i | active | [#80](https://github.com/kallmek3nny/ios-27/pull/80) |
| 21j | active | [#77](https://github.com/kallmek3nny/ios-27/pull/77) |
| 22a | active | [#85](https://github.com/kallmek3nny/ios-27/pull/85) |
| 22b | active | [#86](https://github.com/kallmek3nny/ios-27/pull/86) |
| 22c | active | [#83](https://github.com/kallmek3nny/ios-27/pull/83) |
| 22d | merged | [#84](https://github.com/kallmek3nny/ios-27/pull/84) |
| 22e | active | [#87](https://github.com/kallmek3nny/ios-27/pull/87) |

**Source:** [machine-readable phase ledger](phase-delivery.json), [GitHub source-diff scan](branch-activity.json) and [merged PR provenance](repo-status.json).

The older [compatibility gate percentages](PROGRESS.md) are a separate, manually reviewed fixture inventory and do not automatically advance on PR merges.


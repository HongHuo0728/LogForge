# LogForge 1.1.1 (26923D): defect audit

Scope: native Windows x64, discovery/trust, process lifecycle, media parsing,
publication safety and UI state. No Apple Log/HLG equations or numerical limits
were relaxed. Tests use generated media and disposable profiles only.

## Confirmed defects and corrections

Severity describes the consequence, not the source file. Major means an
application hang/crash or uncontrolled resource consumption; medium means a
supported workflow is blocked or reliable state is lost.

| ID | Severity | Reproduction / root cause | Correction | Regression evidence |
| --- | --- | --- | --- | --- |
| LF111-01 | Medium | Startup found an unapproved executable but continued into whole-drive enumeration. | Three-second Quick discovery, explicit-only Deep mode, no execution during discovery. | Deep-only fixture absent in Quick, present in Deep; executable canary never runs. |
| LF111-02 | Medium, UI | Cancelled discovery left download/manual controls hidden because visibility depended on completed traversal. | Recovery controls depend on idle/unready state; candidates survive timeout/cancel. | Native UI completion/error handlers and actual search cancellation. |
| LF111-03 | Minor, UI | Another search could retain previous directory/candidate counters and progress. | Clear counters, report, candidates and progress at task start. | Native retry from deliberately stale counters. |
| LF111-04 | Medium | WinGet discovery used the app data override as a Windows installation root; common paths assumed drive C. | Known Folder APIs plus actual package environment variables and drive roots. | PATH, custom SCOOP/Chocolatey, isolated App Paths REG_EXPAND_SZ; known-folder code review. |
| LF111-05 | Major | Root child exited but a descendant retained stdout/stderr. Readers blocked after the root-only watcher stopped, defeating timeout/cancel. | Watch until both readers finish; terminate the job before joins on error/timeout/cancel. | Owned child/grandchild verifies deadline, cancellation and no surviving descendant. |
| LF111-06 | Major | Unterminated callback/progress lines grew without a bound; reader exceptions could escape a thread. | Bound lines, capture reader exceptions, stop sibling processes and report cause. | 8 MiB stdout/stderr fixtures and throwing callback; actual codec regressions. |
| LF111-07 | Medium | Invalid CLI names/argument counts launched tool discovery before reporting an error. | Validate command shape/options first. | Invalid commands return CLICommand within the deadline. |
| LF111-08 | Minor | Mixed-case .mOv was refused. | Case-insensitive extension comparison. | Real Unicode-path conversion to mixed-case MOV. |
| LF111-09 | Medium | Numeric fields were cast without bounds; frame count passed through double. | Checked integers, exact int64 frame parsing, checked space estimate from verified packet count. | Negative/fractional/overflow rejection; 2^53+1 frame count retained exactly. |
| LF111-10 | Minor | Failed trust-store replacement left a process-specific temporary file. | Scoped cleanup on every save exit. | Forced replacement failure preserves the original and removes temporary data. |
| LF111-11 | Major | Creative buffer allocation before the worker exception boundary could terminate the application. | Allocate tiles on the caller before starting workers. | Code-path audit; exact scalar/parallel regressions in both modes. System-wide OOM is not simulated. |
| LF111-12 | Minor, UI | Appearance did not refresh when Windows system colors changed while windows were open. | Handle system color/settings notifications in main, Settings and candidate windows. | Handler review and normal theme matrix; live OS high-contrast switching is not claimed. |
| LF111-13 | Minor, UI | Candidate list horizontal extent could remain at the previous DPI. | Recalculate text extent on DPI/appearance changes; cap minimum size to the monitor. | Candidate select/cancel/incomplete pair and long paths in the DPI matrix. |
| LF111-14 | Medium, UI | Multiline candidate/details text used LF alone; native Edit controls joined labels together, confirmed in the captured candidate window. | Normalize to CRLF at the native Edit boundary without duplicating existing CR. | Mixed-newline assertion and inspected window captures. |
| LF111-15 | Medium, UI | At high DPI a short viewport allowed keyboard focus to move to an offscreen output/settings control. | Reveal focused controls by scrolling their parent; keep combo dropdown extent separate. | Focus output field and return to Open; assert both are inside the viewport at each tested DPI. |
| LF111-16 | Medium, UI | If background thread creation threw, busy state had already been set and could strand controls. | Restore idle state if startup fails before the worker exists. | Exception-path review; successful and failed job UI regressions. Forced OS thread exhaustion is not simulated. |
| LF111-17 | Medium | Long discovery paths failed when Windows canonicalization removed the extended path prefix and MAX_PATH policy was disabled. | Restore the extended local path form at filesystem calls while still excluding network paths. | Actual greater-than-260-character paired-file fixture passes Quick discovery without execution. |

The candidate dialog is new in 1.1.1; LF111-13 was caught during implementation.
No confirmed overwrite, wrong-output-success or unauthorized-execution regression
was found. Existing publication, hash-change, canary and validation checks remain.

## Verification record

Actual run results and timings are in [VALIDATION](VALIDATION.md). Root-cause
regressions retain existing tolerances and validators; errors remain errors.

## Remaining limits / not established

- Quick discovery is intentionally incomplete. Nonstandard deeply nested tools
  require explicit Deep search or manual selection. Windows Search may omit all
  executables; its failure cannot prove that a tool is absent.
- Known folders and App Paths are exercised on this host plus isolated fixtures.
  The complete matrix of package-manager releases, enterprise policies, detached
  disks and permission configurations is not certified.
- The parent enforces deadlines with 40 ms polling; scheduling/teardown overhead
  can make observed time slightly exceed 3 s. Hashing and numerical verification
  are separately measured work.
- Shared-library FFmpeg builds require trusting their DLL dependencies. Pair
  approval does not attest every DLL or protect a compromised user account.
- Native UI tests use injected 96/144/192 DPI layout values, not physical monitor
  hot-plug at every scaling factor. Live OS high contrast and assistive technology
  combinations are not fully exercised.
- Download-failure UI is injected without an unnecessary network download. The
  pinned provider/installer protocol is unchanged; proxy/TLS configurations vary.
- Resolve coverage is limited to the installed version and tested color-managed
  configuration. Premiere, FCP and other Resolve releases remain unverified.
- System-wide memory exhaustion, forced disk removal and every malformed codec
  input are not exhaustively proven. This audit does not claim zero potential bugs.

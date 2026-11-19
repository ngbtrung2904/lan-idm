# Data Collector — Architecture & Implementation Decisions

| # | Decision | Reasoning |
|---|---|---|
| D1 | SSH with key auth + rsync, no agent/service on PCs | Specified in guide; zero footprint on headless PCs. |
| D2 | Qt 6 (QML UI, C++17 core), CMake, Linux only, Qt 6.2 APIs | Specified in guide; compatible with Ubuntu 22.04 and 24.04. |
| D3 | Private `ssh-agent -D` owned by app, unlocked via `ssh-add` + `SSH_ASKPASS_REQUIRE=force` | One passphrase prompt per session; agent dies on sign out/exit/crash. |
| D4 | `authorized_keys` forces `rrsync -ro /home/user` with `restrict` | Read-only isolation; no remote shell commands possible. |
| D5 | Auto-detected USB drive in header dropdown | Eliminates extra wizard screen; supports none/one/multiple drives. |
| D6 | Desktop-side time filtering on `rsync --list-only --no-human-readable`, copy via `--files-from --from0` | Compatible with `rrsync -ro`; safe for arbitrary filenames. |
| D7 | Default concurrency `maxParallel = 2` | Avoids thrashing USB write throughput while keeping pipeline busy. |
| D8 | Session folder `<usb>/collect_<yyyyMMdd_hhmmss>/<pcId>/{logs,screenshots}/` + atomic `manifest.json` | Clean audit trail and prevents collisions across collections. |
| D9 | No passwords anywhere; password auth disabled on PCs | Enforces key-only security model. |
| D10 | Include `-i <sshKeyPath>` alongside `-o IdentitiesOnly=yes -o IdentityAgent=<sock>` | Spike S1 proved OpenSSH rejects agent keys under `IdentitiesOnly=yes` unless `-i` specifies the key path. |
| D11 | Use `rsync -r --list-only --no-human-readable` (not `-n --out-format`) | Spike S1 proved cleaner output without dummy destination dir or symlink skip noise; 100k files in 1.4 s. |
| D12 | Use `prctl(PR_SET_PDEATHSIG, SIGTERM)` in `KeyAgent` child modifier | Guarantees private `ssh-agent` dies even if desktop app crashes with `SIGKILL`. |
| D13 | Project lives at repository root (`/home/trungnb/workspace/my-work/aesa-idm2`) | Matches existing `CMakePresets.json` at repository root. |
| D14 | Support `--demo` CLI flag to run app with stub controllers | Lets reviewers/testers inspect all GUI states (including mock-up data) without a live fleet. |
| D15 | Support `DATA_COLLECTOR_CONFIG` env var / `--config <path>` override | Enables integration tests and fake-fleet runs without touching `~/.config/DataCollector/config.json`. |


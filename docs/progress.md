# Data Collector — Progress Log

| Phase | Task | Owner | Status | Notes / Blockers |
|---|---|---|---|---|
| Phase 0 | Spikes S1–S4 (`docs/spikes.md`) | developer | Done | All 4 GO; S1 found `-i <key>` required with `IdentitiesOnly=yes` |
| Phase 1 | Foundation (CMake, interfaces, decisions, config.example, Theme.qml) | orchestrator / writer | Done | Builds clean with `gcc-debug` |
| Phase 2A | Core remote (`ConfigStore`, `SshOptions`, `KeyAgent`, `HostProbe`, `RsyncLister`, `TimeFilter`, `SpaceEstimator`, `PathSanitizer`) | developer | Done | 19 unit tests passing, 0 warnings |
| Phase 2B | USB (`UsbMonitor` + eject) | developer | Done | 10 unit tests passing, 0 warnings |
| Phase 2C | QML UI + stub controllers (matching all 5 `mock-up-gui/` PNGs + Finished/UsbRemoved) | frontend-developer | Done | `qmllint` clean, offscreen launch clean |
| Phase 2D | Test harness (`scripts/fake-fleet/`) | tester | Done | All 9 verification checks passing |
| Phase 2E | Provisioning (`scripts/provision-pc.sh`, `scripts/setup-desktop.sh`, `docs/provisioning.md`) | writer | Done | `bash -n` and `--dry-run` verified |
| Phase 2 Gate | Build, unit tests, reviewer pass on 2A & 2B | reviewer | Done | PASS (0 High) |
| Phase 3A | Phase 2 review fixes + `Manifest` + `TransferEngine` + unit tests | developer | Done | 3/3 unit test suites passing |
| Phase 3B | Real controllers (`AuthController`, `SourcesController`, `TransferController`) + `ConfigErrorWindow` + `main.cpp` | developer | Done | 0 warnings, all 3 launch modes verified |
| Phase 3–4 Tests | Fake-fleet integration tests, failure injection, GUI smoke test, 300k soak test | tester | Done | 8/8 integration + 3/3 GUI smoke passing; 300k soak in 1.06 s; 0 orphan processes |
| Phase 4 Review | Full security & hardening checklist review + re-verification | reviewer | Done | PASS (0 High-severity items remaining; all Medium & Low fixed) |
| Phase 5 | CMake install, CPack DEB, `.desktop`, `packaging/install.sh`, `docs/user-guide.md` | writer | Done | Staged install + `desktop-file-validate` + CTest all green |

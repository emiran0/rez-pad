# Versioning and releases

## One version for everything

Host app, firmware, web editor and installer ship together in one release zip and share one version number. It is stored once, in the repo's [`VERSION`](../VERSION) file:

- the host reads it in `host/Directory.Build.props` (exe file version; the product version adds the git commit, e.g. `0.2.0+913c346`);
- the firmware gets it from `firmware/scripts/version.py`, which generates `src/version_info.cpp` (not in git) with the version, the commit and `.dirty` for uncommitted changes (e.g. `0.2.0+913c346`), plus the USB `bcdDevice` (0x0020 for 0.2.0); the device reports it in HELLO_ACK `fw_ver` and the console's `info`;
- `tools/package.ps1` names the zip and the firmware image after it.

Never write a version number anywhere else.

## Numbering: MAJOR.MINOR.PATCH ([Semantic Versioning](https://semver.org/))

| Part | When | Example |
|---|---|---|
| **MINOR** | A big update: a finished roadmap phase or a notable set of features (new widgets, an integration, new protocol messages, new config sections). | 0.2.0 to 0.3.0 |
| **PATCH** | Fixes and small polish only: no new features, no new config keys, nothing the user has to learn. | 0.2.0 to 0.2.1 |
| **MAJOR** | After 1.0 only: a breaking change. Firmware and host must be updated together (`PROTOCOL_VERSION` goes up), or an old config cannot be carried over by the host's migration. | 1.4.2 to 2.0.0 |

- **0.x** until the first public release on GitHub, which becomes **1.0.0**. While on 0.x a breaking change is a MINOR bump (and says so in the changelog).
- **Between releases nothing is bumped.** Commits on `main` build as the last released version plus the commit (`0.2.0+abc1234`, `.dirty` with local changes), so a dev build is never mistaken for the release.
- The protocol has its own version (`PROTOCOL_VERSION`, docs/protocol.md): additive messages and fields keep it; only incompatible changes raise it (and the release's MAJOR).
- The config has its own `schema` number (docs/config-schema.md): a released schema change raises it and comes with a host migration.

## Making a release

1. Make sure `main` builds and every test passes (`pio test -e native`, `dotnet test --project tests/RezPad.Protocol.Tests`).
2. Choose the number with the table above and write it into `VERSION`.
3. Add the release to [`CHANGELOG.md`](../CHANGELOG.md): date, what is new, what changed, what was fixed, and whether the firmware must be flashed again.
4. Commit (`Release X.Y.Z`) and tag it: `git tag -a vX.Y.Z -m "rez-pad X.Y.Z"`.
5. Build the zip: `powershell -ExecutionPolicy Bypass -File tools\package.ps1`. It warns when the tree has uncommitted changes or HEAD is not tagged `vX.Y.Z`.
6. On GitHub (once public): a release for the tag with the changelog entry and the zip.

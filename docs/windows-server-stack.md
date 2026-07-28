# Windows server stack from recovered source

`tools/build_windows_servers_from_source.ps1` turns an external DBSrv/TMSrv
source-and-data bundle into an ignored, auditable Debug/Win32 comparison build.
No recovered source or binary is committed.

## Input and build

Stage the external material below the ignored default path:

```text
artifacts/server-stack/input/
  Source/Code/DBSrv/DBSrv.vcxproj
  Source/Code/TMSrv/TMSrv.vcxproj
  Server/Common/
  Server/DBSrv/run/
  Server/TMSrv/run/
```

Then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/build_windows_servers_from_source.ps1
```

An external bundle may be used without staging it:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/build_windows_servers_from_source.ps1 `
  -ServerSourceRoot C:\external\Servidor `
  -Clean
```

When the argument points directly at `...\Servidor\Source`, also pass
`-RuntimeDataRoot ...\Servidor\Server`.

The tool:

- copies only text source/project extensions into its working source;
- reads the two `vcxproj` files only to obtain their `.cpp` lists;
- applies the loopback identity fallback only to the copied DBSrv source and
  guards it with `_DEBUG && OPENWYD_COMPARE`;
- removes the unused missing `sqlite3.h` include from the copied source;
- excludes the absent stale `Nyerds.cpp` entry and the unreferenced MFC-only
  `DialogConfigExtra.cpp`;
- records the intentional exclusion of GUI-only `.rc` icon/menu resources;
- compiles every remaining `.cpp` with MSVC v142 for x86 and
  `OPENWYD_COMPARE=1`;
- links only against `winmm`, `ws2_32`, `kernel32`, `user32`, and `gdi32` from
  the local MSVC/Windows SDK;
- rejects unexpected DLL imports and any precompiled residue in the runtime;
- rewrites only known server address/configuration files to `127.0.0.1`.

The default output is `artifacts/server-stack/source-build/`. Its manifests
record every copied or excluded source/data file, every compiled `.cpp`, input
and output SHA-256 hashes, source adaptations, linker inputs, PE architecture,
and runtime DLL dependencies. Build logs and response files are retained there.

`-Clean` is accepted only when the exact output contains the safety sentinel
created by this tool. It moves the prior generated tree into
`rebuild-backups/` instead of deleting it. Source input is never changed or
deleted. Run the path safety tests with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/tests/test_build_windows_servers_from_source.ps1
```

## Persistence backend

This recovered server revision uses the original flat-file backend. Active
`CFileDB.cpp` paths read and write:

- `DBSrv/run/account/<first-letter>/<account>`;
- `DBSrv/run/char/<first-letter>/<character>`;
- `DBSrv/run/capsule/<index>`.

The SQLite calls in `Sqlite_Connect.cpp` are commented out. The validated
source-built executables import no SQLite, MySQL, ODBC, or SQL Server runtime.
Recovered `libmysql.dll`, `sqlite3.dll`, server EXEs, and editor utilities are
explicitly excluded. Therefore this revision does not require installing or
configuring an external SQL service or DSN. If later server sources introduce
an active database adapter, treat that as a new migration stage rather than
silently substituting a database here.

## Two equivalent local accounts

The build creates `CMPNATIVE` and `CMPWASM`, both with the local-only password
`compare123`, before taking the baseline snapshot. These are deliberately
public test credentials, not secrets, and must never be reused outside this
isolated comparison environment.

This is not a byte-copy of `account/A/admin`. The build extracts the exact
`BASE_GetFirstKey`, `CFileDB::{AddAccount,DBWriteAccount,DBReadAccount}`, and
constructor/destructor definitions from the copied official sources into a
narrow temporary translation unit, then compiles it against the same official
headers. The extraction source hashes and generated-source hash are recorded.
The utility calls `CFileDB::AddAccount`, which initializes a fresh
`STRUCT_ACCOUNTFILE` and persists it through `CFileDB::DBWriteAccount`. It then
reads both records with `CFileDB::DBReadAccount` and requires the complete
structures to be byte-identical after normalizing only `AccountName`. Keeping
only these exact definitions avoids linking unrelated server globals or
inventing stubs for the rest of DBSrv.

Both accounts begin with no characters, making them equivalent for the
official create-character flow. Create matching characters through that real
client/TMSrv/DBSrv flow rather than manufacturing character records. Override
the local identifiers/password with `-TestAccountOne`, `-TestAccountTwo`, and
`-TestAccountPassword` if needed; the values must fit the official 16-byte
account and 12-byte password fields.

The ignored `manifests/test-accounts.local.json` maps each account to the
native or WASM role. The general build manifest stores account file hashes,
the password hash, `sizeof(STRUCT_ACCOUNTFILE)`, and equivalence evidence, but
not a private credential.

## Runtime order and readiness

Start the complete stack, optionally restoring the deterministic baseline
first, with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/run_windows_servers.ps1 `
  -Action Start `
  -RestoreBaseline
```

`run_windows_servers.ps1` accepts `-BuildRoot` for a non-default artifact. It
verifies the sentinel, source-build hashes, and both CFileDB account hashes
before launch. It starts DBSrv first, waits for 7514/8895, starts TMSrv, waits
up to 180 seconds for 8281, and requires an established loopback connection
from the TMSrv PID to DBSrv:7514. PIDs and evidence are written to
`manifests/server-process-state.json`.

Expected loopback topology:

| Process | Listener | Purpose |
| --- | ---: | --- |
| DBSrv | `0.0.0.0:7514` | TMSrv database connection |
| DBSrv | `0.0.0.0:8895` | admin endpoint |
| TMSrv | `127.0.0.1:8281` | official client connection |

Start DBSrv first, wait for 7514 and 8895, then start TMSrv and wait for 8281.
A healthy stack also has an established loopback TCP connection from TMSrv to
DBSrv:7514. The native client connects to TMSrv:8281. The browser proxy must
forward bytes transparently to the same endpoint.

Inspect and cleanly stop the stack with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/run_windows_servers.ps1 -Action Status

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/run_windows_servers.ps1 -Action Stop
```

Stop sends `WM_CLOSE` to TMSrv, finds its official Yes/No dialog, sends
`IDYES`, and waits for exit code 0. It then sends `WM_CLOSE` to DBSrv and
requires all three ports to close. `-ForceOnFailure` is an explicit
failure-cleanup fallback and is recorded in `manifests/last-stop.json`.

The source-driven comparison controller is documented in
`tools/openwyd_compare/README.md`. Point its explicit DBSrv/TMSrv commands and
working directories at this generated runtime; never point it at recovered
executables.

## Baseline snapshot and restore

Every successful build creates a pre-run snapshot under:

```text
artifacts/server-stack/source-build/snapshots/baseline/DBSrv/run/
  account/
  char/
  capsule/
```

The state manifest contains file sizes and SHA-256 hashes. Stop both servers and
restore deterministically with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/build_windows_servers_from_source.ps1 `
  -RestoreBaseline
```

Restore refuses to run while a server process or port is active. It moves the
current state to a timestamped `restore-backups/` directory before copying the
baseline, so a mistaken restore remains recoverable.

# Agent Guide: aribcap-dump

## What this project does

`aribcap-dump` is a C++20 command-line tool that reads an ARIB MPEG-TS stream from a file or
standard input and emits JSONL records for:

- Captions: decoded ARIB caption and superimpose text records with timing and display metadata
- EIT: parsed present/following EPG event records from TSDuck EIT tables
- Diagnostics: tagged diagnostic records for decode, clock, and stream-structure issues

The stream processing pipeline is:

1. `aribcap-dump` reads bytes from a file or standard input and frames them into 188-byte MPEG-TS
   packets. The framer confirms alignment by finding four `0x47` sync bytes spaced 188 bytes apart
   and repeats that search if sync is lost.
2. TSDuck demuxers read those packets and deliver PAT, PMT, TOT, EIT tables, and caption PES
   packets. TSDuck handles section/PES demuxing and PSI/SI table and descriptor parsing.
3. `aribcap-dump` tracks the requested `--sid`, follows PAT/PMT changes, classifies caption and
   superimpose streams, tracks PCR/TOT, and converts caption PTS values to Unix timestamps.
4. libaribcaption decodes caption PES payloads. `aribcap-dump` turns decoded captions, EIT
   present/following events, and diagnostics into JSONL through `OutputRecordSink`.

Do not add custom byte parsers for formats that TSDuck or libaribcaption already parses.

## Build

The project expects the **Nix** development environment and uses **CMake + Ninja** for the build.

### Dev shell policy

Do not run `nix develop` unless the user explicitly asks. It needs access to the Nix daemon socket
and usually requires running outside the sandbox. Use the current environment first. If a requested
task cannot proceed without the dev shell, ask the user whether to run:

```sh
nix develop
```

### Build and test commands

Build and test commands, once the needed tools are available:

```sh
# Configure and build (from project root)
cmake -G Ninja -B build
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

The build directory is `build/`. `compile_commands.json` at the project root is a symlink into
`build/` and is kept up to date.

### Vendored TSDuck build

TSDuck lives in `vendor/tsduck` as a git submodule. CMake builds it by calling TSDuck's own
Makefile through `ExternalProject`; see `cmake/BuildTSDuck.cmake`.

CMake tracks the vendored TSDuck source files and build scripts listed in
`cmake/BuildTSDuck.cmake`. When one of those tracked inputs changes, CMake reruns TSDuck's
external build step, but it does not clean the object and dependency files TSDuck already generated
under `build/_vendor/tsduck/build`.

After changing `vendor/tsduck` to point at a different commit, the next build may still reuse
object and dependency files from the previous TSDuck commit. If the build then fails in a way that
points at stale TSDuck build files, ask the user before deleting that build tree and rebuilding.
TSDuck submodule updates are rare, so the extra confirmation is usually worth it. One stale-file
failure looks like:

```text
No rule to make target ...
```

If the user approves, fix it with:

```sh
rm -rf build/_vendor/tsduck/build
cmake --build build
```

### Deleting the build directory

Before deleting the entire `build/` directory, ask the user. This is broader than the targeted
TSDuck cleanup and may discard local CMake configuration or expensive build artifacts.

## Format / lint / test

Use the configured command for the job:

- `nixpkgs-fmt [files...]` for Nix formatting
- `./scripts/mega-linter.sh` for MegaLinter and C++ formatting

For MegaLinter and C++ formatting, use `./scripts/mega-linter.sh`. It pins the Docker image and
passes the repository's run defaults. Do not run `mega-linter-runner`, `clang-format`, or
`npx mega-linter-runner` directly; direct runs can use different tool versions.

### Linting and formatting commands

```sh
# Nix formatting. Pass the Nix files changed by the task.
nixpkgs-fmt [files...]

# Full MegaLinter run, including project-mode linters.
./scripts/mega-linter.sh

# C++ formatting through MegaLinter's pinned clang-format.
# Run after C++ changes. Pass files after the command to format only those files.
APPLY_FIXES=all ENABLE_LINTERS=CPP_CLANG_FORMAT ./scripts/mega-linter.sh [files...]
```

`ENABLE_LINTERS` chooses which MegaLinter linters run.

Run `./scripts/mega-linter.sh` with no file arguments for a full project check. Add file paths only
when you want a file-only run. File-only runs are faster, but they skip MegaLinter checks that need
the whole project.

### Running commands outside the sandbox

Run `./scripts/mega-linter.sh` outside the sandbox. It uses Docker and needs access to the Docker
socket.

### Running tests

After code changes, format with the commands above, then build and run tests:

```sh
cmake --build build
cd build && ctest --output-on-failure
```

### Git pre-commit hooks

If pre-commit hooks are not installed, warn the user before starting code changes. Do not install
hooks unless the user asks.

Hooks check staged files. They may edit files, so re-stage fixed files before committing.

### Fixing MegaLinter errors

When a user asks to fix an error shown in MegaLinter logs, inspect the logs but do not
run the underlying linter tool directly. The full MegaLinter log is
`megalinter-reports/megalinter.log`; individual linter logs are in
`megalinter-reports/linters_logs/`. Always rerun checks through `./scripts/mega-linter.sh`:

```sh
# Run all MegaLinter linters enabled by .mega-linter.yml.
./scripts/mega-linter.sh

# Run one MegaLinter linter. Pass files to check only those files.
ENABLE_LINTERS=<LINTER_ID> ./scripts/mega-linter.sh [files...]
```

## Source layout

```text
src/              — implementation of the `aribcap-dump` executable
  core/           — main program logic kept out of `src/main.cc`
tests/            — Catch2 unit tests
docs/             — contributor docs, including linting/pre-commit notes
scripts/          — local helper scripts, including the MegaLinter wrapper
cmake/            — CMake scripts that build and install vendored TSDuck
pkgs/             — Nix derivations for aribcap-dump and its vendored dependencies
vendor/           — vendored third-party dependencies
```

Files to check before changing behavior:

- `src/core/caption_classifier.*`: decides which PMT streams count as ARIB captions or
  superimpose streams.
- `src/core/eit_parser.*`: parses EIT sections into present/following EPG events.
- `src/core/output_record.*`: defines the JSONL record types and serialization.
- `src/core/output_record_sink.*`: writes output records to stdout or test sinks.
- `src/core/caption_dumper.*`: connects TSDuck demuxer callbacks to caption/EIT handling.

## Dependencies

| Library            | Purpose                                      | Headers                                                            | Source                    |
| ------------------ | -------------------------------------------- | ------------------------------------------------------------------ | ------------------------- |
| **TSDuck**         | MPEG-TS demuxing and PSI/SI table support    | `build/_vendor/tsduck/install/usr/include` after configure/build   | `vendor/tsduck`           |
| **libaribcaption** | ARIB STD-B24 caption decode (no renderer)    | `vendor/libaribcaption/include`                                    | `vendor/libaribcaption`   |
| **jsoncons**       | JSON/JSONL serialisation (header-only)       | `vendor/jsoncons/include`                                          | `vendor/jsoncons`         |
| **CLI11**          | Command-line parsing (header-only)           | `vendor/CLI11/include`                                             | `vendor/CLI11`            |
| **Catch2**         | Unit test framework                          | `vendor/Catch2/src`, `vendor/Catch2/extras`                        | `vendor/Catch2`           |

### Reading dependency source/headers

Dependencies are vendored as git submodules. Read dependency source directly from `vendor/`;
for generated TSDuck aggregate headers and installed development headers, read them from the
build-tree install prefix after `cmake --build build` has run:

```sh
# List TSDuck installed public headers
ls build/_vendor/tsduck/install/usr/include/tsduck/
ls build/_vendor/tsduck/install/usr/include/tscore/

# Read a specific TSDuck header
cat build/_vendor/tsduck/install/usr/include/tsduck/tsSectionDemux.h

# Browse libaribcaption upstream source
ls vendor/libaribcaption/src/

```

If you need to verify the exact compiler search path, inspect `compile_commands.json` at the
project root.

## Never Traverse `/nix/store`

Never search or traverse `/nix/store` directly for discovery, whether you are looking
for sources, headers, libraries, tools, binaries, or helper scripts. It contains
thousands of unrelated paths and is too slow/noisy for agent workflows. In particular,
do not run:

- `find /nix/store ...`
- `rg ... /nix/store`, `grep -R ... /nix/store`, or similar recursive searches
- shell globs such as `/nix/store/*tsduck*`, `/nix/store/**`, or `/nix/store/*/include`

Instead:

- For dependency sources or headers, start from the vendored roots above, the TSDuck build-tree
  install prefix, or a path from `compile_commands.json`, then inspect only that specific subtree.
- For tools or binaries, ask the shell or build system for the resolved command, such as
  with `command -v <tool>`, `type -a <tool>`, CMake cache entries, or project scripts.

## Key design constraints

These constraints reflect current architecture choices — don't change them without understanding
the rationale.

1. **TSDuck owns TS demux and PSI/SI parsing.** Do not introduce hand-written PSI/SI
   byte-level parsing for PAT/PMT/EIT/TDT/TOT; use TSDuck's demuxers and descriptor/table types.
   This constraint covers parsing production input, not building synthetic packets for test fixtures;
   TSDuck exposes no general-purpose packet builder, so test helpers such as
   `tests/pes_test_utils.hpp` hand-assemble bytes where TSDuck does not provide a suitable helper.

2. **All JSONL output goes through `OutputRecordSink`.** Production uses `JsonlOutputRecordSink`;
   tests can replace it with `VectorOutputRecordSink` and compare emitted records without reading
   process stdout.

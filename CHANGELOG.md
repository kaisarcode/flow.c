# CHANGELOG

## v2.1.0

- Added data-driven configuration with table-driven environment variable loading.
- Added `kc_flow_options_default()`, `kc_flow_options_load_env()`, and `kc_flow_options_free()` to the public API.
- Refactored `kc_flow_open()` to take `kc_flow_options_t`.
- CLI is now decoupled from `libflow`; configuration is initialized through options, then overridden by flags.
- Env vars: `KC_FLOW_FILE`, `KC_FLOW_DIR`, and `KC_FLOW_*` overlay variables.
- Added signal listener lifecycle: `kc_flow_on_signal()`, `kc_flow_raise_signal()`, `kc_flow_listen_signals()`, `kc_flow_listen_signal()`, and `kc_flow_signal_listener()`.

## v2.0.1

- Resolved structural capture bug in `_WIN32` branch by replacing `_popen()` with native `CreateProcessA` standard handles to ensure correct `stdin` data propagation.
- Resolved CLI lockup bug on Windows by adding `_isatty()` check.

## v2.0.0

- Removed `--workers` CLI flag and `kc_flow_set_workers()` API. Execution is and will remain sequential; the hint was never functional and added surface area.

## v1.1.1

- Renamed `--entry` CLI flag to `--link`.

## v1.1.0

- Renamed reserved key `node.{ref}.file` to `node.{ref}.import`.
- Bumped version to 1.1.0.
- Added beta notice and contact info to README.

## v1.0.0

- Published the stable baseline release.
- Provided flat dotted-document workflow execution with shell-pipe branches.
- Supported node definitions with `exec`, `use`, `import`, and `link` fields.
- Supported function definitions with `<func.name arg>` call syntax and template expansion.
- Supported computed links via heredoc on `node.link`.
- Supported shell-aware template expansion with quote-state tracking (single, double, backslash).
- Supported builtin commands (`echo`, `cat`, `mkdir`) and fork+exec with environment variable exports.
- Supported overlay records via CLI flags (`--set`, `--unset`, `--entry`).
- Supported cross-platform builds (POSIX fork+exec, Windows `_popen`).

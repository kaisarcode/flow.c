# flow.c — Branch Flow Runtime

## Overview

`flow.c` executes flat dotted-document workflow files as independent shell-pipe branches. A `.flow` file declares nodes, each node can execute a shell command, expand a child flow, or inherit behavior from another node. Nodes fan out through links; branches stay independent and never merge. Leaf output is concatenated to stdout.

Real-world: runs [kaisarcode.com](https://kaisarcode.com) — a 981-line Flow DAG that listens via `netl`, parses HTTP through `http`, routes requests, serves static assets, renders Markdown pages through `mdp` + `tpl`, generates RSS, and handles search — all composed in shell pipelines without a single line of application C code.

---

## Architecture: Three Phases

```
Read → Model → Execute
```

1. **Read**: parse `.flow` file into flat key-value records (heredocs resolved)
2. **Model**: validate keys, build node/function graph with stores and links
3. **Execute**: resolve entries → walk graph → collect data → expand templates → run commands → fan-out

---

## Directory Layout

| Path | Contents |
|------|----------|
| `src/flow.c` | CLI entry: arg parsing, stdin read, delegates to API |
| `src/libflow.c` | Full runtime implementation (3549 lines) |
| `src/flow.h` | Public C API (121 lines) |
| `etc/site.flow` | Example: tiny static-site request pipeline |
| `etc/page.flow` | Example: reusable page renderer child flow |
| `Makefile` | Cross-compilation builder (17 targets, CMake + Ninja) |
| `CMakeLists.txt` | CMake project: static lib + shared lib + CLI |
| `test.sh` | 28 tests: CLI, runtime, C API |
| `DOCUMENTATION.md` | Legacy (replaced by this file) |
| `README.md` | CLI usage reference |
| `.kcsignore` | Excludes `.build/` from validation |

---

## Data Model

### Internal Structures

| Symbol | Type | Role |
|--------|------|------|
| `kc_flow_t` | struct | Context: overlays array, worker hint, error buffer |
| `kc_flow_model_t` | struct | Parsed flow: id, flow data store, entries links, nodes[], funcs[] |
| `kc_flow_node_t` | struct | One node/func: ref, use, file, exec, data store, links |
| `kc_flow_record_t` | struct | One key-value pair with value kind (literal or exec) |
| `kc_flow_store_t` | struct | Ordered array of records (key → first match wins on get) |
| `kc_flow_links_t` | struct | Ordered array of link records (multiple links = fan-out) |
| `kc_flow_branch_t` | struct | One data buffer (stdin passthrough from parent) |
| `kc_flow_branches_t` | struct | Ordered array of branches (fan-out children) |
| `kc_flow_overlay_t` | struct | CLI `--set`/`--unset` operation |

### Value Kinds

- `KC_FLOW_VALUE_LITERAL` — plain text, template-expanded at resolution
- `KC_FLOW_VALUE_EXEC` — heredoc body, template-expanded then executed as shell

### Template Modes

- `KC_FLOW_TEMPLATE_TEXT` — plain text interpolation
- `KC_FLOW_TEMPLATE_SHELL` — shell-aware: tracks `'`/`"`/`\` quote state, escapes values correctly

### Hard Limits

| Limit | Value | Symbol |
|-------|-------|--------|
| Records per store | 2048 | `KC_FLOW_MAX_RECORDS` |
| Nodes per model | 512 | `KC_FLOW_MAX_NODES` |
| Functions per model | 512 | `KC_FLOW_MAX_NODES` |
| Branches in flight | 2048 | `KC_FLOW_MAX_BRANCHES` |
| Key length | 256 | `KC_FLOW_MAX_KEY` |
| Path length | 4096 | `KC_FLOW_MAX_PATH` |
| Error buffer | 512 | `KC_FLOW_ERROR_SIZE` |
| Recursion depth | 64 | hardcoded in guards |

---

## Document Format (`.flow`)

### Namespaces

| Prefix | Scope |
|--------|-------|
| `flow.*` | Global flow data |
| `node.{ref}.*` | Per-node data, exec, file, use, link |
| `func.{name}.*` | Reusable function data and exec |

### Reserved Keys

| Key | Applies To | Behaviour |
|-----|------------|-----------|
| `flow.id` | flow | Identifier, exported as `KC_FLOW_ID` |
| `flow.link` | flow | Entry node reference (can repeat for multiple entries) |
| `flow.*` | flow | Arbitrary global data keys |
| `node.{ref}.exec` | node | Shell command to run when node fires |
| `node.{ref}.import` | node | Import a child flow to execute in place |
| `node.{ref}.use` | node | Inherit exec/file/data from another node |
| `node.{ref}.link` | node | Downstream node(s) to trigger (can repeat) |
| `node.{ref}.meta.*` | node | Convention: metadata (no runtime behaviour) |
| `node.{ref}.*` | node | Arbitrary node-local data keys |
| `func.{name}.exec` | func | Function body (template with `<arg.*>` placeholders) |
| `func.{name}.use` | func | Inherit from another function |
| `func.{name}.*` | func | Arbitrary function data keys |

### Syntax

```
key=value              # literal value
key=<<MARKER          # heredoc (executable value)
...lines...
MARKER
# comment              # full-line comment (starts with #)
```

### Key Validation Rules

- Must start with `flow.`, `node.`, or `func.`
- `node.{ref}.` requires non-empty alphanumeric/`_`/`-` ref
- `func.{name}.` same rules as node refs
- Tail segments (after first dot) must each be non-empty alphanumeric/`_`/`-`
- Empty segments (`node..exec`) are rejected
- Overlays (`--set`/`--unset`) enforce same validation

### Heredoc Mechanics

```
node.x.exec=<<EOF
printf "%s" "<node.msg>"
EOF
```

- Reads lines until trimmed marker line
- Body is template-expanded then executed via shell
- stdout becomes the resolved value (one trailing newline removed)
- Works on any key: `node.{ref}.exec`, `node.{ref}.link`, `flow.*`, etc.
- Computed `node.link` values drive dynamic routing
- Computed values are memoized per cache key

A heredoc on `node.exec` runs as the node action; on `node.link` it computes the downstream node name; on other keys it computes the parameter value.

---

## Execution Pipeline

### 1. Load (`kc_flow_load`)
- `stat()` the flow file
- `kc_flow_read_records()` — parse file line by line
- `kc_flow_apply_overlays()` — apply context `--set`/`--unset`
- `kc_flow_parse_model()` — build node/func graph
- `kc_flow_validate_model()` — DFS cycle detection + node ref checks

### 2. Resolve Entries
- For each entry in `flow.link`:
    - Resolve (template-expand) the link value
    - Look up target node
    - If no entries exist (with `--entry`), use the explicit entry

### 3. Run Node (`kc_flow_run_node`)
For each node in the execution trace:

a. **Resolve behavior** via `use` chain (recursive, max depth 64)
b. **Collect node data** — walk `use` chain, resolve all data records (template-expand each, memoized)
c. **Copy input branch** — each incoming branch is the node's stdin
d. **If `import`** — resolve path relative to flow file, run child flow, replace active branches with child output
e. **If `exec`** — template-expand the command (shell mode), run via shell/builtin, replace active branches with command output
f. **If `links`** — for each active branch × each link:
    - Resolve link value (template-expand, possibly heredoc-exec)
    - Look up target node
    - Recurse into target node with the current branch as input
g. **If no links** — copy active branches to output (leaf nodes)

### 4. Collect Output
All leaf branch outputs are concatenated in order to produce the final stdout.

---

## Template System

### Syntax
`<name>` — resolves `name` and substitutes the value.

### Namespace Resolution

| Tag | Resolution |
|-----|------------|
| `<node.key>` | `key` in current node's data |
| `<node.ref.key>` | `key` in node `ref`'s data |
| `<flow.key>` | Global flow data key |
| `<flow.id>` | Flow identifier string |
| `<func.name>` | Function node's `exec` value |
| `<func.name.key>` | Data key in function node |
| `<func.name arg>` | Expand function with arg node data |

### Recursive Resolution
- Tags in values are resolved recursively (max depth 64)
- Inner `<...>` in a resolved value are expanded again before use

### Shell-Aware Mode
In `KC_FLOW_TEMPLATE_SHELL` mode (used for `exec` values):
- Tracks single-quote (`'...'`), double-quote (`"..."`), and backslash escapes
- Values inserted into double-quoted context are escaped (`"`, `\`, `$`, `` ` ``)
- Values inserted into single-quoted context handle embedded single quotes via `'\''`
- Bare (unquoted) values are inserted verbatim

### Function Calls

```
func.greet.exec=printf "Hello <arg.name>"
node.x.msg=<func.greet x>
```

- `func.{name}.exec` defines the function body template
- `<func.name arg_node>` expands the body with `<arg.*>` resolved from `arg_node.*`
- `<func.name node.prefix>` resolves `<arg.key>` from `node.prefix.key`
- Functions can be called from any template context (including inside other data values)

### Inline Recursion
- `<func.log info.startup>` — calls `func.log` with `info.startup` as arg prefix
- Inside `func.log`: `<arg.level>` → `info.startup.level`, `<arg.msg>` → `info.startup.msg`

---

## Command Execution

### Builtins
Simple commands (no shell metacharacters) are parsed into argv and checked against builtins:

| Builtin | Behaviour |
|---------|-----------|
| `echo` | Prints args space-separated with newline |
| `cat` | With args: reads files relative to flow dir. No args: copies stdin to stdout |
| `mkdir` | Creates directories relative to flow dir (no flags supported) |

If not a builtin, falls through to fork+exec.

### Fork+Exec (POSIX)
- Creates temp files for stdin and stdout
- Sets working directory to flow file's directory
- Exports environment variables:
    - `KC_FLOW_FILE`, `KC_FLOW_DIR`, `FLOW_FILE`, `FLOW_DIR`
    - `FLOW_<KEY>` for all flow data keys (uppercased, dots → underscores)
    - `NODE_<REF>_<KEY>` for all literal node data keys
    - `KC_FLOW_FLOW_<KEY>`, `KC_FLOW_NODE_<KEY>`, `KC_FLOW_<KEY>` for all node data
- If simple command: `execvp(argv[0], argv)` (no shell)
- If complex (has `|&;<>()$`\`"'*?#~[]`): `execl("/bin/sh", "sh", "-c", command, NULL)`

### Popen (Windows)
- Uses `_popen(command, "r")` for command execution
- No environment variable export on Windows

### Error Handling
- Non-zero exit status → node fails → flow fails (propagated up)
- `node.{ref}.ignore_error=1` skips exit code check

---

## Branch Model

- Each `node.link` creates independent branches (fan-out)
- A node with M links and N active branches produces M×N branches
- Each branch carries its own stdin data buffer
- Branches never merge — leaf output is concatenated
- Branch data persists through the entire downstream chain

### Branch Lifecycle

```
input branch (stdin)
  → node (file/exec replaces branch data)
  → links (fan-out: each active branch × each link)
  → each child node receives one branch as stdin
  → leaf nodes copy branch data to output collection
```

---

## Public API

```c
#include "flow.h"

kc_flow_t *ctx = kc_flow_open();

kc_flow_set(ctx, "flow.hello", "Hello");
kc_flow_set(ctx, "node.greet.exec", "printf '<flow.hello> World'");

char *out = NULL;
size_t out_size = 0;
kc_flow_exec(ctx, "file.flow", NULL, 0, &out, &out_size);

// or: kc_flow_exec_entry(ctx, "file.flow", "entry-name", NULL, 0, &out, &out_size);

printf("%.*s", (int)out_size, out);
kc_flow_free(out);
kc_flow_close(ctx);
```

| Function | Purpose |
|----------|---------|
| `kc_flow_open()` | Allocate context |
| `kc_flow_set(ctx, key, val)` | Append `--set` overlay |
| `kc_flow_unset(ctx, key)` | Append `--unset` overlay |
| `kc_flow_set_workers(ctx, n)` | Worker hint (stored but no parallel execution yet) |
| `kc_flow_exec(ctx, path, input, input_size, &out, &out_size)` | Execute from flow entries |
| `kc_flow_exec_entry(ctx, path, entry, input, input_size, &out, &out_size)` | Execute from one entry |
| `kc_flow_free(ptr)` | Free output buffer |
| `kc_flow_close(ctx)` | Release context |
| `kc_flow_strerror(ctx)` | Last error message |

Return values: `KC_FLOW_OK` (0) or `KC_FLOW_ERROR` (-1).

---

## CLI

```
flow file.flow [options]
```

### Options

| Flag | Description |
|------|-------------|
| `--entry <name>` | Execute one explicit entry node |
| `--set key=value` | Append one overlay record |
| `--unset <key>` | Remove prior records for one exact key |
| `--workers <n>` | Set worker count hint |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version (0.1.0) |

Stdin is available to all nodes as the root branch. Leaf output concatenated to stdout. Diagnostics to stderr.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (shown with message on stderr) |

---

## Build

```
make          # native build for host
make all      # build full cross-compilation matrix
make x86_64/linux   # specific target
sh test.sh    # run tests (requires built binary)
```

Artefacts: `bin/{arch}/{platform}/flow` (CLI), `libflow.a` (static lib), `libflow.so`/`libflow.dll` (shared lib).

Requires: CMake ≥ 3.14, Ninja, C11 compiler.

---

## Real-World Patterns

From the kaisarcode.com production flow:

### Asset Template Pattern

```flow
node.asset.exec=<<EOF
status="<node.status>"
mime="<node.mime>"
cache="<node.cache>"
cat "<node.source>" | <node.pipe> | <func.http-response http>
EOF

node.robots.use=asset
node.robots.source=robots.txt
node.robots.pipe=cat
node.robots.status=200
node.robots.mime=text/plain
node.robots.cache=public, max-age=3600
```

A generic behavior node (`asset`) defines the exec template. Leaf nodes inherit via `use` and override only the data fields (source, pipe, mime, cache). No exec duplication.

### Computed Route Dispatch

```flow
node.dispatch.link=<<EOF
input=$(cat)
path=$(<func.request-path dispatch>)
case "$path" in
    "/robots.txt") printf robots ;;
    "/favicon.ico") printf favicon ;;
    "/styles.css") printf styles ;;
    *) printf doc ;;
esac
EOF
```

A heredoc on `node.link` reads the active branch stdin and `printf`s the next node name. This is the flow runtime's native router — no regex, no config file, just a case statement in a computed link.

### Function Macros for Data Access

```flow
func.request-path.exec=case "$input" in request.path=*) printf '%s\n' "$input" \
    | awk 'BEGIN { FS = "=" } $1 == "request.path" { sub(/^[^=]*=/, ""); print; exit }' ;; \
    *) printf '%s' "$input" ;; esac
```

Functions extract fields from structured data (key=value lines in branch stdin). Called as `<func.request-path node_name>` from anywhere.

### Multi-Stage Validation Pipeline

```flow
node.font.link=font-name         # strip prefix
node.font-name.link=font-extension  # validate chars
node.font-extension.link=font-file  # check extension
node.font-file.link=font-serve      # check file exists
node.font-serve.exec=...            # serve file
```

Each stage is a separate node — simple, testable, replaceable.

### Fan-Out for Side Effects

```flow
node.audit.link=emit
node.audit.link=word-count
node.audit.link=checksum
```

Three branches from one node — each independently processes the same input. All three outputs appear in the final concatenated result.

---

## Error Handling

Errors propagate up the call chain. The context stores one error string:

| Error Message | Trigger |
|---------------|---------|
| `flow file not found` | Path doesn't exist or isn't a regular file |
| `unable to read flow file` | Parse error or I/O failure |
| `unable to parse flow file` | Key validation failure or structural error |
| `invalid flow structure` | Cycle detected or unknown node reference in link |
| `invalid structural key` | `--set`/`--unset` key fails validation |
| `invalid node reference` | Entry/link target doesn't exist |
| `unknown entry` | `--entry` target doesn't exist |
| `unable to resolve node data` | Template expansion failure in data collection |
| `invalid child flow path` | `node.import` resolves to invalid path |
| `child flow execution failed` | Error in child flow expansion |
| `computed link target not found` | Heredoc link resolved to a node that doesn't exist |
| `computed link resolved to empty target` | Heredoc link produced empty string |
| `unable to create branch` | Branch buffer allocation failure |
| `too many branches` | Exceeded `KC_FLOW_MAX_BRANCHES` |
| `maximum flow depth exceeded` | Execution recursion > 64 |
| `invalid node use` | `node.use` references a non-existent node |
| `node command failed` | Executed command exited non-zero |
| `computed value exited with non-zero status` | Heredoc execution failed |
| `template expansion failed inside computed value` | Template error in heredoc body |
| `out of memory` | Allocation failure |
| `invalid argument` | API called with NULL pointers |

---

## Constraints

- No parallel execution (workers hint stored, sequential execution only)
- No branch merging (all leaf output concatenated)
- Single flow file per invocation (child flows load separate files)
- Functions cannot contain nested function calls in arg position (`<func.a <func.b x>>` not supported)
- No built-in HTTP, JSON, or structured data handling — shell composition model
- Environment variable names are generated by uppercasing and replacing dots with underscores — may collide
- Cross-platform: POSIX fork+exec and Windows `_popen` paths with `#ifndef _WIN32`

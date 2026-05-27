# flow.c - Complete Guide

## Table of Contents

1. [Overview](#overview)
2. [Installation](#installation)
3. [Document Format](#document-format)
4. [CLI Reference](#cli-reference)
5. [Execution Model](#execution-model)
6. [Templates](#templates)
7. [Functions](#functions)
8. [Heredocs (Executable Values)](#heredocs-executable-values)
9. [Node Behavior](#node-behavior)
10. [Overlays](#overlays)
11. [Builtins vs Fork+Exec](#builtins-vs-forkexec)
12. [Environment Variables](#environment-variables)
13. [Public C API](#public-c-api)
14. [Error Handling](#error-handling)
15. [Patterns and Recipes](#patterns-and-recipes)

---

## Overview

`flow.c` is a workflow runner for chaining shell commands and reusable child
flows. A `.flow` file declares a set of **nodes** - each node can execute a
shell command, expand a child flow, or inherit behavior from another node.
Nodes fan out through **links**; branches stay independent and never merge.
Leaf output is concatenated to stdout.

The model is **branch-oriented**: the flow starts at its declared entry nodes,
passes stdin through each branch, and collects all leaf outputs.

### What it is not

flow.c is not a general-purpose programming language, not a build system, not a
CI/CD runner, not a pipeline framework with merging, and not a dataflow engine.
It is a shell-pipe composition tool: one file describes a DAG of shell commands,
and execution walks it depth-first, one branch at a time.

---

## Installation

### Build from source

```bash
git clone https://github.com/kaisarcode/flow.c
cd flow.c
make
```

Builds for the host architecture. Artifacts go to `bin/{arch}/{platform}/flow`.

### Multiarch

```bash
make all                       # full cross-compilation matrix
make x86_64/linux              # specific target
make aarch64/linux
make riscv64/linux
# see Makefile for full target list
```

### Requirements

- CMake ≥ 3.14
- Ninja
- C11 compiler
- POSIX environment (fork+exec) or Windows (`_popen`)

---

## Document Format

A `.flow` file is a flat key-value document. One key per line, with optional
heredoc values.

### Syntax

```
key=value                  # literal value
key=<<MARKER               # heredoc (executable value)
...lines...
MARKER
# comment                  # full-line comments start with #
```

### Key namespaces

| Prefix | Scope |
|--------|-------|
| `flow.*` | Global flow data |
| `node.{ref}.*` | Per-node data |
| `func.{name}.*` | Reusable function data |

Key rules:
- Must start with `flow.`, `node.`, or `func.`
- `node.{ref}.` requires non-empty alphanumeric/`_`/`-` ref
- `func.{name}.` same rules as refs
- Tail segments must be non-empty alphanumeric/`_`/`-`
- No empty segments (`node..exec`)
- Overlays (`--set`/`--unset`) enforce same rules

### Reserved keys

| Key | Behaviour |
|-----|-----------|
| `flow.id` | Flow identifier, exported as `KC_FLOW_ID` |
| `flow.link` | Entry node reference (repeatable for multiple entries) |
| `node.{ref}.exec` | Shell command to run when the node fires |
| `node.{ref}.import` | Import a child flow to execute in place |
| `node.{ref}.use` | Inherit exec/file/data from another node |
| `node.{ref}.link` | Downstream node(s) to trigger (repeatable) |
| `node.{ref}.ignore_error` | If `1`, skip non-zero exit check |
| `node.{ref}.meta.*` | Metadata (no runtime behaviour - convention only) |
| `func.{name}.exec` | Function body template |
| `func.{name}.use` | Inherit from another function |

All other keys are arbitrary data, accessible via `<node.ref.key>` template
tags.

### Example: minimal flow

```flow
flow.link=upper

node.upper.exec=tr '[:lower:]' '[:upper:]'
```

```bash
printf "hello" | flow minimal.flow
# Output: HELLO
```

---

## CLI Reference

### Synopsis

```
flow file.flow [options] [< stdin]
```

Stdin becomes the root branch input. All leaf outputs are concatenated to
stdout. Diagnostics go to stderr.

### Flags

| Flag | Description |
|------|-------------|
| `--link <name>` | Execute one explicit entry node |
| `--set key=value` | Append one overlay record |
| `--unset <key>` | Remove prior records for one key |
| `--workers <n>` | Worker count hint (stored but unused - sequential only) |
| `-h`, `--help` | Show help |
| `-v`, `--version` | Show version |

### Examples

```bash
# Basic execution
flow my.flow

# Pipe input
printf "hello world" | flow my.flow

# Override entry node
flow my.flow --link build

# Override values
flow my.flow --set node.port.exec='printf 8080'

# Unset and replace entries
flow my.flow --unset flow.link --set flow.link=server

# Multiple overlays
flow my.flow \
    --set flow.env=production \
    --set node.db.host=10.0.0.1
```

### Exit codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (message on stderr) |

---

## Execution Model

### Phases

```
Read → Model → Execute
```

1. **Read**: parse `.flow` into flat key-value records, resolve heredocs
2. **Model**: validate keys, build node/func graph with stores and links
3. **Execute**: resolve entries → walk graph → collect data → expand templates → run commands → fan-out

### Branches

- Every node receives **active branches** (data buffers) as stdin.
- After execution, if the node has links, each active branch fans out to each link.
- A node with M links and N active branches produces **M × N** branches.
- Branches never merge.
- Leaf nodes (no links) copy their branch data to the output collection.

```
stdin → node A → [link X, link Y]
                      ↓        ↓
                  node B    node C
                      ↓
               node D (leaf) → output
```

### Node lifecycle

1. **Resolve `use`** - walk the inheritance chain (max depth 64)
2. **Collect data** - walk `use` chain, template-expand all data records (memoized)
3. **Copy input branch** - each incoming branch becomes the node's stdin
4. **If `import`** - resolve path, run child flow, replace branches with child output
5. **If `exec`** - template-expand command, run via shell/builtin, replace branches with command output
6. **If `links`** - for each branch × each link, resolve link target and recurse
7. **If no links** - copy branches to output (leaf)

### Fan-out example

```flow
flow.link=source

node.source.exec=printf "data"
node.source.link=compress
node.source.link=checksum

node.compress.exec=gzip
node.checksum.exec=cksum
```

Both `compress` and `checksum` receive `"data"` independently. Both outputs
appear in the final result.

---

## Templates

### Syntax

```
<name>
```

Resolves `name` and substitutes the value. Templates are resolved recursively
(max depth 64) - inner `<...>` in a resolved value are expanded again before
use.

### Resolution namespaces

| Tag | Resolves to |
|-----|-------------|
| `<node.key>` | `key` in the **current** node's data |
| `<node.ref.key>` | `key` in node `ref`'s data |
| `<flow.key>` | Global flow data key |
| `<flow.id>` | Flow identifier |
| `<func.name>` | Function node's `exec` body |
| `<func.name.key>` | `key` in function's data |
| `<func.name node.ref>` | Call function with `node.ref.*` as args |

### Shell-aware mode

In `exec` values (shell mode), the template engine tracks quote state:

- Inside `'...'` - inserted verbatim (single quotes are escaped via `'\''`)
- Inside `"..."` - special chars (`"`, `\`, `$`, `` ` ``) are escaped
- Bare (unquoted) - inserted as-is

This prevents shell injection from resolved values.

### Recursion guard

Template expansion stops at 64 levels of recursion. Beyond that it is a
hard error.

### Examples

```flow
flow.greeting=Hello

node.main.exec=printf "<flow.greeting> World"
# Output: Hello World

node.data.name=Alice
node.data.email=alice@example.com
node.main.exec=printf "User: <node.data.name> <<node.data.email>>"
# Output: User: <alice@example.com>
# (inner <> not expanded - it is literal text at template time)

node.outer.inner=42
node.main.exec=printf "<node.outer.inner>"
# Output: 42
```

---

## Functions

Functions are reusable template expansions defined in `func.*` keys.

### Definition

```flow
func.greet.exec=printf "Hello, <arg.name>!"
```

`<arg.name>` is resolved from the calling context's arg data.

### Calling

```flow
node.main.msg=<func.greet main.data>
node.main.data.name=World

node.main.exec=printf "<node.main.msg>"
# Output: Hello, World!
```

The call `<func.name node.prefix>` resolves `<arg.key>` from `node.prefix.key`.

### Nested calls

Functions can be called from any template context, including inside other data
keys:

```flow
func.wrapper.exec=printf "[<arg.inner>]"

node.x.msg=<func.wrapper x.inner>
node.x.inner=hello

node.main.exec=printf "<node.x.msg>"
# Output: [hello]
```

### Function with use

```flow
func.base.exec=printf "base: <arg.val>"
func.derived.use=base
func.derived.exec=printf "derived: <arg.val>"

node.x.val=42
node.main.exec=printf "<func.derived x>"
# Output: derived: 42
```

---

## Heredocs (Executable Values)

A heredoc turns any key into an executable value. The body is
template-expanded and executed via shell. Its stdout (minus one trailing
newline) becomes the resolved value.

### Syntax

```flow
node.x.port=<<EOF
case "$ENV" in
    production) printf 80 ;;
    *) printf 8080 ;;
esac
EOF
```

### Heredoc on exec

Runs as the node's action:

```flow
node.build.exec=<<EOF
printf "Building..."
gcc -o output source.c
printf "done"
EOF
```

### Heredoc on link (computed routing)

The link value is computed at runtime by executing the heredoc. Its stdout
names the next node:

```flow
node.router.link=<<EOF
case "<flow.path>" in
    "/") printf home ;;
    "/about") printf about ;;
    *) printf not-found ;;
esac
EOF
```

### Heredoc on any key

Any `flow.*`, `node.*`, or `func.*` key supports heredocs. The computed value
is memoized per cache key (same key + same input → not re-executed).

### Important

- The heredoc body is template-expanded **before** execution.
- Execution runs through the shell (`/bin/sh -c`).
- One trailing newline is removed from stdout.
- Non-zero exit from a heredoc is a hard error (unless `ignore_error=1`).

---

## Node Behavior

### Use (inheritance)

A node can inherit from another node via `node.{ref}.use`:

```flow
node.worker.exec=<<EOF
printf "[<node.label>] Processing <node.file>\n"
cat "<node.file>" | gzip > "<node.file>.gz"
EOF

node.backup.use=worker
node.backup.label=BACKUP
node.backup.file=data.txt
```

The `use` chain is recursive (max depth 64). The effective behavior is the
union of the leaf node's own data plus all inherited data. Own values override
inherited ones.

### Import (child flow)

A node can expand a child `.flow` file in place:

```flow
node.page.import=page.flow
node.page.title=About
node.page.body=About us page content
```

The child flow sees the parent's node data as if it were its own. The child
flow's output replaces the importing node's branch data.

### Ignore error

By default, a non-zero command exit fails the flow:

```flow
node.cleanup.ignore_error=1
node.cleanup.exec=rm -f tempfile
```

---

## Overlays

Overlays modify the flow document at runtime without editing the file.

### --set

Appends one key-value record. If the key already exists, the new value
appends to the store - the first match wins on lookup, so **new overlays
take precedence** over file records. Multiple overlays of the same key
stack: the last one wins.

```bash
flow my.flow --set flow.env=production --set node.port=8080
```

### --unset

Removes all prior records (file + earlier overlays) for one exact key.

```bash
flow my.flow --unset flow.link --set flow.link=server
```

This replaces all entry declarations with a single entry `server`.

### Use case: overriding without editing

```bash
flow my.flow \
    --unset flow.link \
    --set flow.link=deploy \
    --set node.deploy.target=staging \
    --set node.build.exec='make test && make'
```

---

## Builtins vs Fork+Exec

### Builtins

Simple commands (no shell metacharacters) are checked against builtins:

| Builtin | Behaviour |
|---------|-----------|
| `echo` | Prints args space-separated with newline |
| `cat` | With args: reads files relative to flow dir. No args: copies stdin |
| `mkdir` | Creates directories relative to flow dir (no flags) |

### Fork+Exec (POSIX)

If the command is not a builtin and contains no shell metacharacters, it is
executed via `execvp`. If it contains shell metacharacters (`|&;<>()$\`\`"'*?#~[]`),
it runs via `/bin/sh -c`.

The working directory is set to the flow file's directory.

### Popen (Windows)

Uses `_popen(command, "r")`. Environment variables are not exported.

---

## Environment Variables

When a command runs via fork+exec, the following environment variables are set:

| Variable | Source |
|----------|--------|
| `KC_FLOW_FILE` | Absolute path to the flow file |
| `KC_FLOW_DIR` | Directory containing the flow file |
| `FLOW_FILE` | Same as `KC_FLOW_FILE` |
| `FLOW_DIR` | Same as `KC_FLOW_DIR` |
| `FLOW_<KEY>` | For each `flow.*` key (uppercased, dots → underscores) |
| `NODE_<REF>_<KEY>` | For each literal `node.*` key |
| `KC_FLOW_<KEY>` | For each node data key (non-prefixed) |
| `KC_FLOW_FLOW_<KEY>` | For each flow data key (non-prefixed) |
| `KC_FLOW_NODE_<KEY>` | For each node data key (non-prefixed) |

Not exported on Windows.

---

## Public C API

```c
#include "flow.h"
```

### Lifecycle

```c
// Allocate context
kc_flow_t *ctx = kc_flow_open();

// Configure overlays
kc_flow_set(ctx, "flow.key", "value");
kc_flow_unset(ctx, "flow.key");
kc_flow_set_workers(ctx, 4);

// Execute from flow entries
char *out = NULL;
size_t out_size = 0;
int ret = kc_flow_exec(ctx, "file.flow", NULL, 0, &out, &out_size);

// Execute from one entry
ret = kc_flow_exec_entry(ctx, "file.flow", "my-node", "stdin-data", 9, &out, &out_size);

// Use output (owned by library - must free)
printf("%.*s", (int)out_size, out);
kc_flow_free(out);

// Release context
kc_flow_close(ctx);
```

### Functions

| Function | Returns | Purpose |
|----------|---------|---------|
| `kc_flow_open()` | `kc_flow_t *` | Allocate new context |
| `kc_flow_set(ctx, key, val)` | `int` | Append `--set` overlay |
| `kc_flow_unset(ctx, key)` | `int` | Append `--unset` overlay |
| `kc_flow_set_workers(ctx, n)` | `int` | Worker count hint |
| `kc_flow_exec(ctx, path, in, in_size, &out, &out_size)` | `int` | Execute from flow entries |
| `kc_flow_exec_entry(ctx, path, entry, in, in_size, &out, &out_size)` | `int` | Execute from one entry |
| `kc_flow_free(ptr)` | `void` | Free output buffer |
| `kc_flow_close(ctx)` | `int` | Release context |
| `kc_flow_strerror(ctx)` | `const char *` | Last error message |

Return values: `KC_FLOW_OK` (0) or `KC_FLOW_ERROR` (-1).

### Error handling

Every function returns an integer status. On error, `kc_flow_strerror(ctx)`
returns a human-readable message (valid until the next context-modifying call).

---

## Error Handling

### Error messages

| Message | Trigger |
|---------|---------|
| `flow file not found` | Path doesn't exist or isn't a regular file |
| `unable to read flow file` | Parse or I/O failure |
| `unable to parse flow file` | Key validation failure |
| `invalid flow structure` | Cycle detected or unknown node ref in link |
| `invalid structural key` | `--set`/`--unset` key fails validation |
| `invalid node reference` | Entry/link target doesn't exist |
| `unknown entry` | `--link` target doesn't exist |
| `unable to resolve node data` | Template expansion failure |
| `invalid child flow path` | `node.import` resolves to invalid path |
| `child flow execution failed` | Error in child flow |
| `computed link target not found` | Heredoc link resolved to nonexistent node |
| `computed link resolved to empty target` | Heredoc link produced empty string |
| `unable to create branch` | Allocation failure |
| `too many branches` | Exceeded `KC_FLOW_MAX_BRANCHES` (2048) |
| `maximum flow depth exceeded` | Execution recursion > 64 |
| `invalid node use` | `node.use` references non-existent node |
| `node command failed` | Command exited non-zero |
| `computed value exited with non-zero status` | Heredoc execution failed |
| `template expansion failed inside computed value` | Template error in heredoc body |
| `out of memory` | Allocation failure |
| `invalid argument` | NULL pointers passed to API |

### Hard limits

| Limit | Value |
|-------|-------|
| Records per store | 2048 |
| Nodes per model | 512 |
| Functions per model | 512 |
| Branches in flight | 2048 |
| Key length | 256 |
| Path length | 4096 |
| Error buffer | 512 |
| Recursion depth | 64 |

---

## Patterns and Recipes

### Asset template

Define a generic behavior node, then inherit via `use`:

```flow
node.asset.exec=<<EOF
status="<node.status>"
mime="<node.mime>"
cache="<node.cache>"
cat "<node.source>" | <node.pipe> | http build response --status "$status" --header "Content-Type: $mime" --header "Cache-Control: $cache"
EOF

node.robots.use=asset
node.robots.source=robots.txt
node.robots.pipe=cat
node.robots.status=200
node.robots.mime=text/plain
node.robots.cache=public, max-age=3600
```

### Computed router

Use a heredoc on `node.link` to dispatch dynamically:

```flow
node.router.link=<<EOF
case "$(cat)" in
    "/") printf home ;;
    "/api/"*) printf api ;;
    *) printf static ;;
esac
EOF
```

### Multi-stage validation

Chain nodes for pipeline validation:

```flow
node.input.link=sanitize
node.sanitize.link=validate
node.validate.link=process
node.process.exec=...final action...
```

### Function macro for field extraction

```flow
func.field.exec=<<EOF
printf '%s\n' "$input" | awk 'BEGIN { FS = "=" } $1 == "<arg.name>" { sub(/^[^=]*=/, ""); print; exit }'
EOF

node.parse.exec=printf "path=<func.field parse>\nhost=example.com"
node.parse.input=request.path=/home
```

### Fan-out for side effects

```flow
node.work.exec=printf "data"
node.work.link=compress
node.work.link=checksum
node.work.link=count

node.compress.exec=gzip
node.checksum.exec=cksum
node.count.exec=wc -c
```

Three independent branches, three results in output.

### Dynamic port from environment

```flow
node.server.port=<<EOF
[ "$ENV" = "production" ] && printf 80 || printf 8080
EOF

node.server.exec=printf "listening on port <node.server.port>"
```

### Override via CLI for testing

```bash
flow deploy.flow \
    --set flow.link=check \
    --set node.check.exec='printf "dry run OK"'
```

### Nested function calls in templates

```flow
func.format.exec=printf "[<arg.level>] <arg.msg>"
func.log.exec=printf "%s: %s" "$(date +%T)" "<func.format <arg.inner>>"

node.x.msg=startup
node.x.inner.level=INFO
node.x.inner.msg=System started

node.main.exec=printf "<func.log x>"
# Output: 14:30:00 [INFO] System started
```

---

## Conventions

### Metadata namespace

Use `meta` sub-keys for display or documentation data - the runtime does not
give them special behaviour:

```flow
flow.meta.title=My Flow
node.build.meta.summary=Compiles the project
func.clean.meta.description=Remove temporary files
```

### Key ordering

While order does not affect runtime semantics, grouping by function improves
readability:

1. Global data (`flow.*`)
2. Functions (`func.*`)
3. Nodes (`node.*`), grouped by execution phase

### Comment style

```flow
# Single-line comments only
# Use them before each group of related keys
```

No block comments, no inline comments after keys.

---

## Constraints

- **No parallel execution** - workers hint is stored but execution is sequential
- **No branch merging** - all leaf output is simply concatenated
- **Single flow file per invocation** - child flows load separate files
- **No nested function calls in function-arg position** - `<func.a <func.b x>>` is not supported
- **No built-in HTTP, JSON, or structured data** - shell composition model
- **Cross-platform** - POSIX fork+exec on Unix, `_popen` on Windows

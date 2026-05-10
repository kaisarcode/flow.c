# flow.c - Branch Flow Runtime

`flow.c` is a small workflow runner for chaining shell commands and reusable
child flows. It starts at the flow entries, passes stdin through each branch,
and writes the final branch output to stdout.

The model is branch-oriented: a flow declares entry nodes, nodes can execute
commands, expand child flows, and fan out through links. Branches stay
independent and do not merge. `flow.c` is not a visual graph editor and it is
not a contract or port runtime.

---

## CLI

The `flow` command executes one workflow file, accepts optional input from
standard input, and prints the produced branch output to standard output.
Runtime overlays can replace entries, parameters, child files, and commands
without editing the source file.

### Examples

Execute one flow file:

```bash
./bin/x86_64/linux/flow etc/parent.flow
```

Execute one explicit entry:

```bash
./bin/x86_64/linux/flow file.flow --entry build
```

Pipe input through standard input:

```bash
printf "input" | ./bin/x86_64/linux/flow file.flow
```

Write a minimal flow file:

```flow
flow.link=upper

node.upper.exec=tr '[:lower:]' '[:upper:]'
```

Write a flow that passes parameters to a child flow:

```flow
flow.link=greet
flow.param.name=World

node.greet.file=child.flow
node.greet.param.name=<flow.param.name>
node.greet.link=suffix

node.suffix.exec=cat; printf "%s" "!"
```

```flow
flow.link=print
flow.param.name=Friend

node.print.exec=printf "%s" "Hello <flow.param.name>"
```

Overlay one effective flow document:

```bash
./bin/x86_64/linux/flow file.flow \
    --unset flow.link \
    --set flow.link=server \
    --set node.server.exec='printf "%s" "<flow.param.message>"'
```

Run the included parent and child examples:

```bash
./bin/x86_64/linux/flow etc/parent.flow
./bin/x86_64/linux/flow etc/child.flow
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `file.flow` | Execute one flow file |
| `--entry <name>` | Execute one explicit entry node |
| `--set key=value` | Append one overlay record |
| `--unset <key>` | Remove prior records for one exact key |
| `--workers <n>` | Set a positive worker count hint |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

## Public API

```c
#include "flow.h"

kc_flow_t *ctx = kc_flow_open();
char *output = NULL;
size_t output_size = 0;

kc_flow_set(ctx, "flow.param.hello", "Hello");
kc_flow_exec(ctx, "etc/parent.flow", NULL, 0, &output, &output_size);

kc_flow_free(output);
kc_flow_close(ctx);
```

---

## Lifecycle

- `kc_flow_open()` - allocates and returns a new context owned by the caller.
- `kc_flow_set()` - appends one ordered set overlay.
- `kc_flow_unset()` - appends one ordered unset overlay.
- `kc_flow_set_workers()` - validates and stores a worker count hint.
- `kc_flow_exec()` - executes a flow file from its declared entries.
- `kc_flow_exec_entry()` - executes a flow file from one explicit entry node.
- `kc_flow_free()` - releases output data owned by the library.
- `kc_flow_close()` - releases the context and all associated resources.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make clean && make
```

## Multiarch Builds

The project is prepared to build artifacts for multiple architectures under
`bin/{arch}/{platform}/`. A plain `make` builds only the current host
architecture, while the targets below build the full matrix or a specific
target.

```bash
make all
make x86_64/linux
make x86_64/windows
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

---

**Author:** KaisarCode

**Email:** <kaisar@kaisarcode.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode

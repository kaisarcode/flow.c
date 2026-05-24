# flow.c - Branch Flow Runtime

`flow.c` is a small workflow runner for chaining shell commands and reusable
child flows. It starts at the flow entries, passes stdin through each branch,
and writes the final branch output to stdout.

The model is branch-oriented: a flow declares entry nodes, nodes can execute
commands, expand child flows, and fan out through links. Branches stay
independent and do not merge.

---

## CLI

The `flow` command executes one workflow file, accepts optional input from
standard input, and prints the produced branch output to standard output.
Runtime overlays can replace entries, parameters, child imports, and commands
without editing the source file.

### Examples

Execute one flow file:

```bash
./bin/x86_64/linux/flow etc/site.flow
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

The included `etc` directory is a small static-site request pipeline:

```bash
./bin/x86_64/linux/flow etc/site.flow
./bin/x86_64/linux/flow etc/site.flow --set flow.path=/
./bin/x86_64/linux/flow etc/site.flow --set flow.path=/missing
printf "Hello" | ./bin/x86_64/linux/flow etc/page.flow
```

`site.flow` routes a request path, renders a page through `page.flow`, then
fans the rendered page out to emit the page, count words, and compute a
checksum. It uses only default Linux commands such as `printf`, `cat`, `wc`,
`cksum`, `cut`, and shell `case`.

Its main route is an executable heredoc `node.link`:

```flow
flow.id=tiny-site
flow.path=/robots.txt
flow.link=request

node.request.link=<<EOF
case "<flow.path>" in
    "/") printf home ;;
    "/robots.txt") printf robots ;;
    *) printf missing ;;
esac
EOF
```

Each route uses a shared child-flow behavior:

```flow
node.page.import=page.flow
node.page.site=<flow.site>

node.home.use=page
node.home.slug=home
node.home.heading=Home
node.home.body=Welcome to the tiny flow site.
node.home.status=<func.status home.status>
node.home.link=audit
```

Write a flow using `node.use` for behavior inheritance:

```flow
flow.link=backup

node.worker.label=BACKUP
node.worker.exec=<<EOF
printf "[<node.label>] Processing: <node.file>\n"
cat "<node.file>" | gzip > "<node.file>.gz"
EOF

node.backup.use=worker
node.backup.file=data.txt
```

Write a flow using `func` for reusable templates:

```flow
flow.link=main

func.log.exec=<<EOF
printf "[%s] %s: %s" "$(date +%T)" "<arg.level>" "<arg.msg>"
EOF

node.main.exec=<<EOF
printf "%s\n" "<func.log info.startup>"
printf "%s\n" "work complete"
printf "%s\n" "<func.log info.shutdown>"
EOF

node.info.startup.level=INFO
node.info.startup.msg=System starting up

node.info.shutdown.level=INFO
node.info.shutdown.msg=System shutting down
```

Functions can be called anywhere a template tag is supported, including inside other data keys:

```flow
func.greet.exec=<<EOF
printf "Hello %s" "<arg.name>"
EOF

node.data.name=World
node.data.msg=<func.greet data>

node.main.exec=printf "Message: <node.data.msg>"
```

Heredoc values are executable values:

```flow
flow.link=router
flow.env=dev

node.server.port=<<EOF
[ "<flow.env>" = "dev" ] && printf 8080 || printf 80
EOF

node.router.link=<<EOF
printf home
EOF

node.home.exec=printf "port=%s" "<node.server.port>"
```

A heredoc can be used on any `flow.*`, `node.*`, or `func.*` key. The heredoc
body is template-expanded and executed when the field is resolved. Its stdout
becomes the resolved value, with exactly one trailing newline removed when one
is present.

The key determines how the computed value is used:

- `node.exec` runs as the node action, and stdout becomes node output.
- `node.link` computes the downstream node name.
- Other fields compute the final parameter value.

### Metadata Convention

`flow.c` treats unreserved keys as ordinary data. By convention, `meta` is the
namespace for metadata about the flow document, a node, a function, or another
Flow element. This keeps display-oriented data away from runtime fields such as
`title`, `summary`, `status`, or other project-specific values.

```flow
flow.meta.title=Tiny Site Runtime
flow.meta.summary=Routes one request path to a page or asset response.

node.request.meta.title=Request Router
node.request.meta.summary=Maps request paths to route nodes.

func.response.meta.summary=Builds the HTTP response envelope.
```

The runtime does not give `meta` special behavior. Tools may use
`*.meta.title`, `*.meta.summary`, or other `*.meta.*` fields to render graph
labels, tooltips, inspectors, or documentation.

Overlay one effective flow document:

```bash
./bin/x86_64/linux/flow file.flow \
    --unset flow.link \
    --set flow.link=server \
    --set node.server.exec='printf "%s" "<flow.message>"'
```

Run the included cohesive examples:

```bash
./bin/x86_64/linux/flow etc/site.flow
./bin/x86_64/linux/flow etc/site.flow --set flow.path=/
./bin/x86_64/linux/flow etc/site.flow --set flow.path=/missing
./bin/x86_64/linux/flow etc/page.flow --set flow.heading=Preview
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

kc_flow_set(ctx, "flow.hello", "Hello");
kc_flow_exec(ctx, "etc/page.flow", NULL, 0, &output, &output_size);

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

### Multiarch Builds

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

## Visualization

`flow.c` is a strict execution engine dedicated to running system command workflows.
For rendering visual graph representations of `.flow` documents, you can check the
[fldot](https://github.com/kaisarcode/fldot) conversion tool.

---

## Beta Notice

This is a beta project realistically tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**. 

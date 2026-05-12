# flow.c - The Know-How

`flow.c` is more than a workflow runner; it is a **dotted-document orchestration engine**. It follows the Unix philosophy of small, specialized tools working together through pipes, but adds a structured data model and powerful template expansion.

---

## 1. Philosophy: The Orthodox Pipeline

`flow.c` is built on a strict "orthodox" piping model:

- **Independent Branches**: Every link creates a new independent branch.
- **Auto-Piping**: The `stdout` of a node is automatically piped to the `stdin` of all linked downstream nodes.
- **Fan-Out**: A single node can link to multiple nodes, fanning out the data stream.
- **No Implicit Merge**: Branches do not merge back together. If you need to combine data, use a node that reads from multiple sources (e.g., files or network).
- **Leaf-to-Stdout**: The final output of the entire flow is the aggregate of all leaf node outputs.

---

## 2. The Dotted-Document Model

A `.flow` file is an ordered collection of key-value pairs using a dotted namespace.

### Namespaces

| Namespace | Purpose |
| :--- | :--- |
| **`flow.*`** | Global data, flow ID, and initial entry points. |
| **`node.{ref}.*`** | Graph structure (links), behavior (exec/file), and local data. |
| **`func.{name}.*`** | Reusable inline template functions. |

### Reserved Runtime Keys

| Key | Description |
| :--- | :--- |
| `flow.id` | Flow identifier (exported as `KC_FLOW_ID`). |
| `flow.link` | Entry nodes for execution. |
| `node.{ref}.exec` | The command to run (supports heredocs). |
| `node.{ref}.file` | A child flow to execute. |
| `node.{ref}.use` | Inherits behavior/data from another node. |
| `node.{ref}.link` | Downstream nodes to trigger. |

---

## 3. Template Expansion & Functions

`flow.c` uses a template engine that resolves tags in `<...>` before any command is executed or any child flow is loaded.

### What are Tags?

Tags are placeholders that allow for dynamic data injection and code reuse.

- **Syntax**: Any string wrapped in angle brackets, such as `<node.my_key>` or `<node.other.data>`.
- **Where they work**: Tags are resolved in:
    - `node.{ref}.exec`: Inside shell commands and heredocs.
    - `node.{ref}.file`: In child flow file paths.
    - **Any data key**: A value can reference other keys (e.g., `node.A.msg=<flow.id>`).
- **Recursive Resolution**: If a tag resolves to a value that contains other tags, those tags are also resolved (up to a recursion limit, typically 64).

### Resolution Rules

Tags **must** include a namespace (`node`, `flow`, or `func`). Bare tags are not supported.

1.  **`<node.{key}>`**: Resolves `key` within the **current** node.
2.  **`<node.{ref}.{key}>`**: Resolves `key` within the node named `{ref}`.
3.  **`<flow.{key}>`**: Resolves `key` within the global flow data.
4.  **`<func.{name} {arg}>`**: Expands a template function.
5.  **`<func.{name}.{key}>`**: Resolves `key` within the function node named `{name}`.

### Inline Functions (`func`)
Functions are reusable snippets of templates.
- **Definition**: `func.{name}.exec=Hello <arg.name>`
- **Invocation**: `<func.{name} {arg_node}>`
- **Arguments**: `<arg.{key}>` inside a function resolves to `{arg_node}.{key}`.

Functions can be called **anywhere**, including inside other data keys, allowing for recursive data composition.

---

## 4. Behavior Inheritance (`node.use`)

The `use` key allows a node to "borrow" the behavior (`exec` or `file`) and the data of another node.

```flow
node.base-worker.exec=printf "Task: <node.task_name>"
node.base-worker.task_name=Default

node.job1.use=base-worker
node.job1.task_name=Build
```

- `node.job1` will execute the command defined in `base-worker`.
- It will use `Build` as the task name.
- It can still have its own unique `link` nodes.

---

## 5. Visual Representation (Graphing)

The flat dotted structure is perfectly suited for visual graphing:

- **Boxes**: Each `node.{ref}` is a box.
- **Connectors**: `node.{ref}.link={target}` is a solid arrow.
- **Inheritance**: `node.{ref}.use={template}` is a dashed arrow.
- **Functions**: `func.{name}` is a distinct "Trait" or "Tool" (often visualized with a distinct color or grouped in a side toolbox).
- **Function Usage**: A call `<func.{name} {arg}>` is a dynamic expansion relationship, visualized as a link to the function or a label in the attribute.
- **Sub-graphs**: `node.{ref}.file` is a collapsible or nested graph.
- **Data Attributes**: All other keys are listed as attributes inside the box.

---

## 6. Best Practices & Know-How

### Descriptive Naming
Use dots to group data logically:
`node.server.config.port=8080`
`node.server.config.timeout=30s`

### Decoupling Logic from Data
Create "behavior nodes" (templates) and "data nodes".
```flow
node.tpl.exec=process --input <node.source>
node.data1.source=file1.txt
node.task1.use=tpl
node.task1.source=<node.data1.source>
```

### Heredoc Indentation
`flow.c` preserves the relative indentation of heredocs while removing the common leading whitespace, keeping your scripts readable.

### Environment Integration
Every data key is exported to the sub-process environment:
`node.api.key=123` -> `KC_FLOW_NODE_API_KEY=123`

---

## 7. Common Use Cases

### Asset Pipelines
Chaining minifiers, compilers, and uploaders using independent branches for different file types.

### Dynamic Request Routers
Using `func.*` and `node.use` to define a declarative routing table that expands into a single high-performance shell script.

### System Orchestration
Wrapping complex `docker`, `git`, or `rsync` commands into reusable blocks that share global configuration (like paths or credentials).

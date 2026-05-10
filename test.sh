#!/bin/bash
# test.sh - flow.c functional validation.
# Summary: Validates the public CLI and C API for the flow runtime.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU GPL v3.0

set -e

# Print one failure and exit.
# @param $1 Failure message.
# @return Does not return.
kc_test_fail() {
    printf '\033[31m[FAIL]\033[0m %s\n' "$1"
    exit 1
}

# Print one success message.
# @param $1 Success message.
# @return 0.
kc_test_pass() {
    printf '\033[32m[PASS]\033[0m %s\n' "$1"
}

# Resolve the built CLI path.
# @return 0 on success.
setup() {
    ROOT=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
    BIN="$ROOT/bin/$(uname -m)/linux/flow"
    TMP="$ROOT/../../tmp/flow-test"
    rm -rf "$TMP"
    mkdir -p "$TMP"
    [ -x "$BIN" ] || kc_test_fail "flow binary not found"
}

# Assert command output.
# @param $1 Expected output.
# @param $2 Failure message.
# @return 0 on success.
assert_output() {
    expected=$1
    message=$2
    shift 2
    output=$("$@")
    [ "$output" = "$expected" ] || kc_test_fail "$message"
}

# Run CLI behavior tests.
# @return 0 on success.
test_cli() {
    "$BIN" --help | grep -q 'Usage:' || kc_test_fail "help output missing usage"
    "$BIN" -h | grep -q 'Usage:' || kc_test_fail "short help output missing usage"
    kc_test_pass "help output"
    "$BIN" --unknown >/dev/null 2>&1 && kc_test_fail "unknown flag should fail"
    kc_test_pass "unknown flag failure"
    "$BIN" --workers 0 "$ROOT/etc/parent.flow" >/dev/null 2>&1 && kc_test_fail "invalid workers should fail"
    kc_test_pass "invalid workers failure"
    "$BIN" >/dev/null 2>&1 && kc_test_fail "missing flow file should fail"
    kc_test_pass "missing flow file failure"
}

# Write generated flow fixtures.
# @return 0 on success.
write_fixtures() {
    cat > "$TMP/fanout.flow" <<'EOF'
flow.id=fanout
flow.param.greeting=Hi
flow.link=root
node.root.link=left
node.root.link=right
node.left.exec=printf "%s" "<flow.param.greeting> Left"
node.right.exec=printf "%s" "<flow.param.greeting> Right"
EOF
    cat > "$TMP/stdin.flow" <<'EOF'
flow.link=root
node.root.exec=cat
EOF
    cat > "$TMP/overlay.flow" <<'EOF'
flow.id=overlay
flow.link=default
flow.owner=team
node.default.exec=printf "%s" "default"
node.server.exec=printf "%s" "<node.param.msg>"
node.install.exec=printf "%s" "install"
node.child.file=child-switch.flow
node.left.exec=printf "%s" "left"
node.right.exec=printf "%s" "right"
node.meta.foo=bar
EOF
    cat > "$TMP/child-switch.flow" <<'EOF'
flow.link=node-1
node.node-1.exec=printf "%s" "child-default"
EOF
    cat > "$TMP/child-alt.flow" <<'EOF'
flow.link=node-1
node.node-1.exec=printf "%s" "child-alt"
EOF
    cat > "$TMP/heredoc.flow" <<'EOF'
flow.link=root
node.root.exec=<<CMD
printf "%s" "heredoc"
CMD
EOF
    {
        printf '%b\n' '\043 full-line comment'
        printf '%s\n' 'flow.link=root'
        printf '%b\n' '\043 another comment'
        printf '%s\n' 'node.root.exec=printf "%s" "comment-ok"'
    } > "$TMP/comment.flow"
    cat > "$TMP/file-exec.flow" <<'EOF'
flow.link=root
node.root.file=child-switch.flow
node.root.exec=sed 's/default/exec/'
EOF
    cat > "$TMP/cycle.flow" <<'EOF'
flow.link=left
node.left.link=right
node.right.link=left
EOF
}

# Run runtime behavior tests.
# @return 0 on success.
test_runtime() {
    assert_output "Hello WorldHola Mundo" "default parent child execution" "$BIN" "$ROOT/etc/parent.flow"
    kc_test_pass "default parent/child flow execution"
    assert_output "Hello World" "optional flow id failed" "$BIN" "$TMP/child-switch.flow" --set 'node.node-1.exec=printf "%s" "Hello World"'
    kc_test_pass "optional flow.id"
    assert_output "" "optional flow link failed" "$BIN" "$TMP/overlay.flow" --unset flow.link
    kc_test_pass "optional flow.link"
    assert_output "" "metadata-only node failed" "$BIN" "$TMP/overlay.flow" --entry meta
    kc_test_pass "metadata-only nodes"
    assert_output "Salut WorldHola Mundo" "flow param override propagation failed" "$BIN" "$ROOT/etc/parent.flow" --set flow.param.hello=Salut
    kc_test_pass "flow param override propagation"
    assert_output "Hello Planet" "direct child flow execution failed" "$BIN" "$ROOT/etc/child.flow" --set flow.param.world=Planet
    kc_test_pass "direct child flow execution"
    assert_output "Hi LeftHi Right" "fan-out failed" "$BIN" "$TMP/fanout.flow"
    kc_test_pass "fan-out"
    assert_output "child-exec" "child artifact propagation failed" "$BIN" "$TMP/file-exec.flow"
    kc_test_pass "child artifact propagation and file-before-exec"
    output=$(printf "Pipe Input" | "$BIN" "$TMP/stdin.flow")
    [ "$output" = "Pipe Input" ] || kc_test_fail "stdin input failed"
    kc_test_pass "stdin input through kclib model"
    assert_output "Hello" "overlay placeholder resolution failed" "$BIN" "$TMP/overlay.flow" --unset flow.link --set flow.link=server --set flow.param.hello=Hello --set 'node.server.param.msg=<flow.param.hello>'
    kc_test_pass "overlay placeholder resolution"
    assert_output "two" "singular overlay priority failed" "$BIN" "$TMP/overlay.flow" --set 'node.server.exec=printf "%s" "one"' --set 'node.server.exec=printf "%s" "two"' --unset flow.link --set flow.link=server --set node.server.param.msg=x
    kc_test_pass "singular overlay priority"
    assert_output "leftright" "flow.link overlay replacement failed" "$BIN" "$TMP/overlay.flow" --unset flow.link --set flow.link=left --set flow.link=right
    kc_test_pass "flow.link overlay replacement"
    assert_output "leftright" "node.link overlay replacement failed" "$BIN" "$TMP/overlay.flow" --unset flow.link --set flow.link=root --set node.root.link=left --set node.root.link=right
    kc_test_pass "node.link overlay replacement"
    assert_output "meta-ok" "metadata overlays failed" "$BIN" "$TMP/overlay.flow" --set flow.foo=bar --set node.server.foo=baz --unset flow.link --set flow.link=server --set node.server.param.msg=meta-ok
    kc_test_pass "metadata overlays"
    assert_output "apk add curl" "exec overlay failed" "$BIN" "$TMP/overlay.flow" --set 'node.install.exec=printf "%s" "apk add curl"' --unset flow.link --set flow.link=install
    kc_test_pass "exec overlay"
    assert_output "heredoc" "heredoc execution failed" "$BIN" "$TMP/heredoc.flow"
    kc_test_pass "heredoc execution"
    assert_output "comment-ok" "full-line comments failed" "$BIN" "$TMP/comment.flow"
    kc_test_pass "full-line comments"
    assert_output "child-alt" "file overlay failed" "$BIN" "$TMP/overlay.flow" --set node.child.file=child-alt.flow --unset flow.link --set flow.link=child
    kc_test_pass "file overlay"
    assert_output "install" "--unset behavior failed" "$BIN" "$TMP/overlay.flow" --unset flow.link --set flow.link=install
    kc_test_pass "--unset behavior"
    "$BIN" "$TMP/overlay.flow" --set node..exec=bad >/dev/null 2>&1 && kc_test_fail "invalid structural key should fail"
    kc_test_pass "invalid structural key failure"
    "$BIN" "$TMP/overlay.flow" --unset flow.link --set flow.link=missing >/dev/null 2>&1 && kc_test_fail "invalid node reference should fail"
    kc_test_pass "invalid node reference failure"
    "$BIN" "$TMP/cycle.flow" >/dev/null 2>&1 && kc_test_fail "cycle validation should fail"
    kc_test_pass "cycle validation"
}

# Validate public static-library embedding.
# @return 0 on success.
test_api() {
    {
        printf '%b\n' '\043include "flow.h"'
        printf '%b\n' '\043include <stdio.h>'
        printf '%b\n' '\043include <string.h>'
        printf '%s\n' 'int main(int argc, char **argv) {'
        printf '%s\n' '    kc_flow_t *ctx;'
        printf '%s\n' '    char *out = NULL;'
        printf '%s\n' '    size_t out_size = 0;'
        printf '%s\n' '    int rc;'
        printf '%s\n' '    if (argc != 2) return 2;'
        printf '%s\n' '    ctx = kc_flow_open();'
        printf '%s\n' '    if (!ctx) return 3;'
        printf '%s\n' '    rc = kc_flow_exec(ctx, argv[1], NULL, 0, &out, &out_size);'
        printf '%s\n' '    if (rc != KC_FLOW_OK) return 4;'
        printf '%s\n' '    if (out_size != strlen("Hello World") || memcmp(out, "Hello World", out_size) != 0) return 5;'
        printf '%s\n' '    kc_flow_free(out);'
        printf '%s\n' '    kc_flow_close(ctx);'
        printf '%s\n' '    return 0;'
        printf '%s\n' '}'
    } > "$TMP/consumer.c"
    cc -I "$ROOT/src" "$TMP/consumer.c" "$ROOT/bin/$(uname -m)/linux/libflow.a" -o "$TMP/consumer"
    "$TMP/consumer" "$ROOT/etc/child.flow" || kc_test_fail "public C API smoke test failed"
    kc_test_pass "public C API smoke test linked against libflow.a"
}

# Run all tests.
# @return 0 on success.
main() {
    setup
    write_fixtures
    test_cli
    test_runtime
    test_api
    rm -rf "$TMP"
    kc_test_pass "all tests passed"
}

main "$@"

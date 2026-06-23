#!/bin/bash
# Summary: Validation suite for flow CLI and C API runtime behavior.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

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
node.child.import=render-default.flow
node.left.exec=printf "%s" "left"
node.right.exec=printf "%s" "right"
node.meta.foo=bar
EOF
    cat > "$TMP/render-default.flow" <<'EOF'
flow.link=node-1
node.node-1.exec=printf "%s" "child-default"
EOF
    cat > "$TMP/render-alt.flow" <<'EOF'
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
node.root.import=render-default.flow
node.root.exec=sed 's/default/exec/'
EOF
    cat > "$TMP/cycle.flow" <<'EOF'
flow.link=left
node.left.link=right
node.right.link=left
EOF
    cat > "$TMP/heredoc-literal-field.flow" <<'EOF'
flow.link=x
node.x.value=printf hello
node.x.exec=printf "%s" "<node.value>"
EOF
    cat > "$TMP/heredoc-field-exec.flow" <<'EOF'
flow.link=x
node.x.value=<<EOF_VALUE
printf hello
EOF_VALUE
node.x.exec=printf "%s" "<node.value>"
EOF
    cat > "$TMP/heredoc-field-template.flow" <<'EOF'
flow.link=x
flow.env=dev
node.x.port=<<EOF_PORT
[ "<flow.env>" = "dev" ] && printf 8080 || printf 80
EOF_PORT
node.x.exec=printf "%s" "<node.port>"
EOF
    cat > "$TMP/heredoc-exec-node.flow" <<'EOF'
flow.link=x
node.x.exec=<<EOF_EXEC
printf hello
EOF_EXEC
EOF
    cat > "$TMP/heredoc-computed-link.flow" <<'EOF'
flow.link=router
node.router.link=<<EOF_LINK
printf home
EOF_LINK
node.home.exec=printf ok
EOF
    cat > "$TMP/heredoc-multiple-links.flow" <<'EOF'
flow.link=router
node.router.link=<<EOF_LINK_A
printf a
EOF_LINK_A
node.router.link=<<EOF_LINK_B
printf b
EOF_LINK_B
node.a.exec=printf A
node.b.exec=printf B
EOF
    cat > "$TMP/heredoc-link-stdin.flow" <<'EOF'
flow.link=router
node.router.exec=sed -n 's/^request.path=//p'
node.router.link=<<EOF_LINK
case "$(cat)" in
    "/") printf home ;;
    *) printf not_found ;;
esac
EOF_LINK
node.home.exec=printf home
node.not_found.exec=printf missing
EOF
    cat > "$TMP/heredoc-link-missing.flow" <<'EOF'
flow.link=router
node.router.link=<<EOF_LINK
printf missing
EOF_LINK
EOF
    cat > "$TMP/heredoc-link-empty.flow" <<'EOF'
flow.link=router
node.router.link=<<EOF_LINK
printf ""
EOF_LINK
EOF
    cat > "$TMP/heredoc-memo.flow" <<'EOF'
flow.link=x
node.x.value=<<EOF_VALUE
n=$(cat memo-count 2>/dev/null || printf 0)
n=$((n + 1))
printf "%s" "$n" > memo-count
printf "%s" "$n"
EOF_VALUE
node.x.exec=printf "%s\n%s\n" "<node.value>" "<node.value>"
EOF
    cat > "$TMP/heredoc-branch-cache.flow" <<'EOF'
flow.link=left
flow.link=right
node.left.link=x
node.right.link=x
node.x.value=<<EOF_VALUE
n=$(cat branch-count 2>/dev/null || printf 0)
n=$((n + 1))
printf "%s" "$n" > branch-count
printf "%s" "$n"
EOF_VALUE
node.x.exec=printf "%s" "<node.value>"
EOF
    cat > "$TMP/quote.flow" <<'EOF'
flow.link=x
node.x.double=He said "hi" with $HOME and `ticks`
node.x.single=can't stop
node.x.exec=printf "%s\n%s" "<node.x.double>" '<node.x.single>'
EOF
    cat > "$TMP/quote-func.flow" <<'EOF'
flow.link=x
func.echo.exec=printf "%s" "<arg.msg>"
node.x.msg=Function says "hello"
node.x.exec=<func.echo x>
EOF
}

# Run runtime behavior tests.
# @return 0 on success.
test_runtime() {
    site_expected=$(printf '# Tiny Flow Site / Robots\nstatus: 200 OK\nslug: robots\n\nUser-agent: *\n\n-- words: 13\n\n-- checksum: 2916289011')
    assert_output "$site_expected" "default site request failed" "$BIN" "$ROOT/etc/site.flow"
    kc_test_pass "default site request"
    assert_output "Hello World" "optional flow id failed" "$BIN" "$TMP/render-default.flow" --set 'node.node-1.exec=printf "%s" "Hello World"'
    kc_test_pass "optional flow.id"
    assert_output "" "optional flow link failed" "$BIN" "$TMP/overlay.flow" --unset flow.link
    kc_test_pass "optional flow.link"
    assert_output "" "metadata-only node failed" "$BIN" "$TMP/overlay.flow" --link meta
    kc_test_pass "metadata-only nodes"
    site_expected=$(printf '# Tiny Flow Site / Home\nstatus: 200 OK\nslug: home\n\nWelcome to the tiny flow site.\n\n-- words: 17\n\n-- checksum: 902964706')
    assert_output "$site_expected" "site home route failed" "$BIN" "$ROOT/etc/site.flow" --set flow.path=/
    site_expected=$(printf '# Tiny Flow Site / Missing\nstatus: 404 NOT_FOUND\nslug: missing\n\nNothing lives at /missing.\n\n-- words: 15\n\n-- checksum: 3197011564')
    assert_output "$site_expected" "site fallback route failed" "$BIN" "$ROOT/etc/site.flow" --set flow.path=/missing
    kc_test_pass "site conditional routing"
    page_expected=$(printf '# Tiny Flow Site / Welcome\nstatus: 200 OK\nslug: welcome\n\nHello from stdin')
    output=$(printf "Hello from stdin" | "$BIN" "$ROOT/etc/page.flow" --set flow.heading=Welcome --set flow.slug=welcome)
    [ "$output" = "$page_expected" ] || kc_test_fail "page renderer stdin failed"
    kc_test_pass "page renderer child flow"
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
    assert_output "printf hello" "literal field changed behavior" "$BIN" "$TMP/heredoc-literal-field.flow"
    kc_test_pass "literal field remains literal"
    assert_output "hello" "heredoc field execution failed" "$BIN" "$TMP/heredoc-field-exec.flow"
    kc_test_pass "heredoc field execution"
    assert_output "8080" "heredoc field template execution failed" "$BIN" "$TMP/heredoc-field-template.flow"
    kc_test_pass "heredoc field templates before execution"
    assert_output "hello" "heredoc node exec failed" "$BIN" "$TMP/heredoc-exec-node.flow"
    kc_test_pass "heredoc node exec"
    assert_output "ok" "computed link failed" "$BIN" "$TMP/heredoc-computed-link.flow"
    kc_test_pass "computed link"
    assert_output "AB" "computed link ordering failed" "$BIN" "$TMP/heredoc-multiple-links.flow"
    kc_test_pass "computed link ordering"
    output=$(printf 'request.path=/\n' | "$BIN" "$TMP/heredoc-link-stdin.flow")
    [ "$output" = "home" ] || kc_test_fail "computed link stdin routing failed"
    output=$(printf 'request.path=/missing\n' | "$BIN" "$TMP/heredoc-link-stdin.flow")
    [ "$output" = "missing" ] || kc_test_fail "computed link stdin fallback failed"
    kc_test_pass "computed link receives branch stdin"
    "$BIN" "$TMP/heredoc-link-missing.flow" 2>"$TMP/missing.err" >/dev/null && kc_test_fail "missing computed link target should fail"
    grep -q "computed link target not found" "$TMP/missing.err" || kc_test_fail "missing computed link error unclear"
    kc_test_pass "computed link missing target failure"
    "$BIN" "$TMP/heredoc-link-empty.flow" 2>"$TMP/empty.err" >/dev/null && kc_test_fail "empty computed link target should fail"
    grep -q "computed link resolved to empty target" "$TMP/empty.err" || kc_test_fail "empty computed link error unclear"
    kc_test_pass "computed link empty target failure"
    rm -f "$TMP/memo-count"
    output=$("$BIN" "$TMP/heredoc-memo.flow")
    [ "$output" = "$(printf '1\n1')" ] || kc_test_fail "computed field memoization failed"
    kc_test_pass "computed field memoization"
    rm -f "$TMP/branch-count"
    assert_output "12" "branch cache isolation failed" "$BIN" "$TMP/heredoc-branch-cache.flow"
    kc_test_pass "branch cache isolation"
    quote_expected=$(printf '%s\n%s' "He said \"hi\" with \$HOME and \`ticks\`" "can't stop")
    assert_output "$quote_expected" "quoted shell placeholder failed" "$BIN" "$TMP/quote.flow"
    kc_test_pass "quoted shell placeholders"
    assert_output 'Function says "hello"' "quoted function placeholder failed" "$BIN" "$TMP/quote-func.flow"
    kc_test_pass "quoted function placeholders"
    assert_output "comment-ok" "full-line comments failed" "$BIN" "$TMP/comment.flow"
    kc_test_pass "full-line comments"
    assert_output "child-alt" "import overlay failed" "$BIN" "$TMP/overlay.flow" --set node.child.import=render-alt.flow --unset flow.link --set flow.link=child
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
        printf '%s\n' '    kc_flow_options_t opts = kc_flow_options_default();'
        printf '%s\n' '    if (kc_flow_open(&ctx, &opts) != KC_FLOW_OK) return 3;'
        printf '%s\n' '    rc = kc_flow_exec(ctx, argv[1], NULL, 0, &out, &out_size);'
        printf '%s\n' '    if (rc != KC_FLOW_OK) return 4;'
        printf '%s\n' '    const char *expected = "# Tiny Flow Site / Untitled\nstatus: 200 OK\nslug: page\n\nEmpty page\n";'
        printf '%s\n' '    if (out_size != strlen(expected) || memcmp(out, expected, out_size) != 0) return 5;'
        printf '%s\n' '    kc_flow_free(out);'
        printf '%s\n' '    kc_flow_close(ctx);'
        printf '%s\n' '    kc_flow_options_free(&opts);'
        printf '%s\n' '    return 0;'
        printf '%s\n' '}'
    } > "$TMP/consumer.c"
    cc -I "$ROOT/src" "$TMP/consumer.c" "$ROOT/bin/$(uname -m)/linux/libflow.a" -o "$TMP/consumer"
    "$TMP/consumer" "$ROOT/etc/page.flow" || kc_test_fail "public C API smoke test failed"
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

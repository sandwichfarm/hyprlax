#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${HYPRLAX_BIN:-$ROOT/hyprlax}"
IMAGE="${HYPRLAX_SMOKE_IMAGE:-$ROOT/examples/space/bkgd_0.png}"
SUFFIX="${HYPRLAX_SOCKET_SUFFIX:-hotremove-$$}"
LOG="${HYPRLAX_SMOKE_LOG:-/tmp/hyprlax-hotremove-$SUFFIX.log}"
WORKSPACE_EVENT="${HYPRLAX_SMOKE_WORKSPACE_EVENT:-1}"

created_output=""
daemon_pid=""
original_workspace=""
restored_workspace=0

die() {
    echo "FAIL: $*" >&2
    if [ -f "$LOG" ]; then
        echo "--- daemon log: $LOG ---" >&2
        tail -n 120 "$LOG" >&2 || true
    fi
    exit 1
}

cleanup() {
    set +e
    if [ -n "$created_output" ]; then
        hyprctl output remove "$created_output" >/dev/null 2>&1
    fi
    if [ -n "$daemon_pid" ] && kill -0 "$daemon_pid" >/dev/null 2>&1; then
        kill "$daemon_pid" >/dev/null 2>&1
        wait "$daemon_pid" >/dev/null 2>&1
    fi
    if [ "$restored_workspace" -eq 0 ] && [ -n "$original_workspace" ]; then
        hyprctl dispatch workspace "$original_workspace" >/dev/null 2>&1
    fi
}
trap cleanup EXIT

need() {
    command -v "$1" >/dev/null 2>&1 || die "$1 is required"
}

need hyprctl
need jq

[ -x "$BIN" ] || die "hyprlax binary not executable: $BIN"
[ -r "$IMAGE" ] || die "smoke image not readable: $IMAGE"

mapfile -t before_outputs < <(hyprctl monitors -j | jq -r '.[].name' | sort)
original_workspace="$(hyprctl activeworkspace -j | jq -r '.id')"

hyprctl output create headless >/dev/null

for _ in $(seq 1 50); do
    mapfile -t after_outputs < <(hyprctl monitors -j | jq -r '.[].name' | sort)
    created_output="$(
        comm -13 \
            <(printf '%s\n' "${before_outputs[@]}") \
            <(printf '%s\n' "${after_outputs[@]}") | head -n 1
    )"
    [ -n "$created_output" ] && break
    sleep 0.1
done

[ -n "$created_output" ] || die "failed to detect created virtual output"
echo "Created virtual output: $created_output"

HYPRLAX_SOCKET_SUFFIX="$SUFFIX" "$BIN" \
    --fps 30 \
    "$IMAGE" \
    >"$LOG" 2>&1 &
daemon_pid=$!

ctl_status() {
    HYPRLAX_SOCKET_SUFFIX="$SUFFIX" "$BIN" ctl status 2>&1
}

wait_for_ctl() {
    local output
    for _ in $(seq 1 80); do
        if output="$(ctl_status)"; then
            printf '%s\n' "$output"
            return 0
        fi
        if ! kill -0 "$daemon_pid" >/dev/null 2>&1; then
            wait "$daemon_pid" || true
            die "test daemon exited before IPC became ready"
        fi
        sleep 0.1
    done
    die "test daemon IPC did not become ready"
}

status_before="$(wait_for_ctl)"
printf '%s\n' "$status_before" | grep -q 'hyprlax running' || die "unexpected status before removal"
printf '%s\n' "$status_before" | grep -q 'Monitors: 2' || die "test daemon did not see the virtual output"

hyprctl output remove "$created_output" >/dev/null
removed_output="$created_output"
created_output=""

for _ in $(seq 1 50); do
    if ! hyprctl monitors -j | jq -e --arg name "$removed_output" 'map(.name) | index($name)' >/dev/null; then
        break
    fi
    sleep 0.1
done

if hyprctl monitors -j | jq -e --arg name "$removed_output" 'map(.name) | index($name)' >/dev/null; then
    die "virtual output still present after removal: $removed_output"
fi

status_after_remove="$(wait_for_ctl)"
printf '%s\n' "$status_after_remove" | grep -q 'hyprlax running' || die "IPC stopped responding after output removal"
printf '%s\n' "$status_after_remove" | grep -q 'Monitors: 1' || die "test daemon did not drop the removed output"

if [ "$WORKSPACE_EVENT" = "1" ]; then
    target_workspace=$((original_workspace == 1 ? 2 : original_workspace - 1))
    hyprctl dispatch workspace "$target_workspace" >/dev/null
    sleep 0.2
    status_after_workspace="$(wait_for_ctl)"
    printf '%s\n' "$status_after_workspace" | grep -q 'hyprlax running' || die "IPC stopped after workspace event"
    hyprctl dispatch workspace "$original_workspace" >/dev/null
    restored_workspace=1
fi

kill "$daemon_pid" >/dev/null 2>&1
wait "$daemon_pid" >/dev/null 2>&1 || true
daemon_pid=""

echo "PASS: removed $removed_output; IPC stayed responsive; log: $LOG"

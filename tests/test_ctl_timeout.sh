#!/bin/sh
set -eu

runtime_dir=$(mktemp -d)
user_name=hyprlax_ctl_timeout
signature=timeout_test
socket="$runtime_dir/hyprlax-$user_name-$signature.sock"
output="$runtime_dir/output"

cleanup() {
    kill "$server_pid" 2>/dev/null || true
    rm -rf "$runtime_dir"
}

socat "UNIX-LISTEN:$socket,fork" EXEC:'/bin/sleep 5' &
server_pid=$!
trap cleanup EXIT INT TERM

sleep 1
start=$(date +%s)
set +e
USER="$user_name" XDG_RUNTIME_DIR="$runtime_dir" \
HYPRLAND_INSTANCE_SIGNATURE="$signature" \
timeout 3 ./hyprlax ctl status >"$output" 2>&1
status=$?
set -e
elapsed=$(( $(date +%s) - start ))

if [ "$status" -eq 0 ] || [ "$status" -eq 124 ]; then
    cat "$output"
    exit 1
fi
if [ "$elapsed" -ge 3 ]; then
    cat "$output"
    exit 1
fi

grep -q 'Failed to receive response' "$output"

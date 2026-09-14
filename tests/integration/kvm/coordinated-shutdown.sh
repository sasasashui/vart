#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 VART SPIN-GUEST" >&2
    exit 2
fi

log=$(mktemp /tmp/vart-signal-shutdown-XXXXXX)
trap 'rm -f "$log"' EXIT

run_signal_test()
{
    signame=$1
    expected=$2

    "$3" --kernel "$4" --memory 64M </dev/null >"$log" 2>&1 &
    pid=$!
    ready=false
    count=0
    while [ "$count" -lt 200 ]; do
        if [ -d "/proc/$pid/task" ] &&
           [ "$(find "/proc/$pid/task" -mindepth 1 -maxdepth 1 | wc -l)" \
             -ge 2 ]; then
            ready=true
            break
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
        count=$((count + 1))
        sleep 0.01
    done
    if [ "$ready" != true ]; then
        wait "$pid" || true
        cat "$log" >&2
        echo "not ok - VART did not start a vCPU" >&2
        exit 1
    fi

    kill "-$signame" "$pid"
    status=0
    wait "$pid" || status=$?
    if [ "$status" -ne "$expected" ] ||
       ! grep -F "vart: terminated by signal $((expected - 128))" \
           "$log" >/dev/null; then
        cat "$log" >&2
        echo "not ok - $signame shutdown exited with status $status" >&2
        exit 1
    fi
}

run_signal_test INT 130 "$1" "$2"
run_signal_test TERM 143 "$1" "$2"
echo "ok - host signals coordinate vCPU shutdown"

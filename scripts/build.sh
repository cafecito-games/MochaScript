#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

if [[ -r /sys/fs/cgroup/cpu.max ]]; then
    read -r quota period < /sys/fs/cgroup/cpu.max
else
    quota=max
    period=100000
fi
if [[ "$quota" == "max" ]]; then
    detected_jobs=$(getconf _NPROCESSORS_ONLN)
else
    detected_jobs=$((quota / period))
fi
if (( detected_jobs < 1 )); then
    detected_jobs=1
fi

# The godot-cpp binding build is memory-heavy. Four is the measured-safe bootstrap
# ceiling for this workspace; callers may only lower it through MOCHA_JOBS.
jobs=${MOCHA_JOBS:-4}
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "MOCHA_JOBS must be a positive integer" >&2
    exit 2
fi
if (( jobs > detected_jobs )); then
    jobs=$detected_jobs
fi
if (( jobs > 4 )); then
    jobs=4
fi

for argument in "$@"; do
    case "$argument" in
        -j*|--jobs|--jobs=*|jobs=*|num_jobs=*)
            echo "Build job count is controlled only by the clamped MOCHA_JOBS value" >&2
            exit 2
            ;;
    esac
done

exec scons -C "$repo_root" target=template_debug -j"$jobs" "$@"

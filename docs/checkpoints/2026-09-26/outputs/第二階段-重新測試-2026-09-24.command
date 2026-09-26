#!/bin/bash
set -euo pipefail
TASK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$TASK_ROOT"
python3 work/lifetime-stage2-20260924/build_probe.py optimized
python3 work/lifetime-stage2-20260924/build_probe.py asan
python3 work/lifetime-stage2-20260924/run_probes.py

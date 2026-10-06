#!/usr/bin/env bash
# MicroRT-Lab sim API dev launcher — serves app.main:app on 127.0.0.1:3031.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 -m uvicorn app.main:app --host 127.0.0.1 --port 3031 --reload

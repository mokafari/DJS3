#!/bin/bash
# Quick development script wrapper for dev.py
# Usage: ./dev.sh [options]

cd "$(dirname "$0")"
python3 dev.py "$@"


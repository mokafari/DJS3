#!/bin/bash
# Simple wrapper script for integration test runner
# Usage: ./run_test.sh [test_app_name] [options]

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
python3 "$SCRIPT_DIR/run_integration_test.py" "$@"

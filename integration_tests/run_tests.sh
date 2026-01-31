#!/bin/bash
# Run all integration tests

set -e

echo "Building and running integration tests..."

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Build all test apps
for app in test_apps/*/; do
    if [ -d "$app" ] && [ -f "$app/CMakeLists.txt" ]; then
        echo "Building $app"
        cd "$app"
        idf.py build
        cd "$SCRIPT_DIR"
    fi
done

# Run pytest tests
echo "Running pytest tests..."
pytest host_tests/ -v --tb=short


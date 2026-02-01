#!/usr/bin/env python3
"""
Integration Test Runner
Builds, flashes, monitors, and runs integration tests for ESP32-S3 DJ controller

Usage:
    python run_integration_test.py <test_app_name> [options]
    
Examples:
    python run_integration_test.py audio_integration
    python run_integration_test.py e2e_workflows --port /dev/cu.usbmodem2101
    python run_integration_test.py all --build-only
    python run_integration_test.py audio_integration --flash-only --monitor
"""

import os
import sys
import subprocess
import argparse
import time
import json
from pathlib import Path
from datetime import datetime

# Colors for output
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

def print_header(msg):
    print(f"\n{Colors.HEADER}{Colors.BOLD}{'='*60}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{msg}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{'='*60}{Colors.ENDC}\n")

def print_step(msg):
    print(f"{Colors.OKCYAN}▶ {msg}{Colors.ENDC}")

def print_success(msg):
    print(f"{Colors.OKGREEN}✓ {msg}{Colors.ENDC}")

def print_error(msg):
    print(f"{Colors.FAIL}✗ {msg}{Colors.ENDC}")

def print_warning(msg):
    print(f"{Colors.WARNING}⚠ {msg}{Colors.ENDC}")

def get_project_root():
    """Get the project root directory"""
    script_dir = Path(__file__).parent
    return script_dir.parent

def get_test_apps():
    """Get list of available test apps"""
    test_apps_dir = Path(__file__).parent / "test_apps"
    apps = []
    if test_apps_dir.exists():
        for item in test_apps_dir.iterdir():
            if item.is_dir() and (item / "CMakeLists.txt").exists():
                apps.append(item.name)
    return sorted(apps)

def check_idf_environment():
    """Check if ESP-IDF environment is set up"""
    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        print_error("IDF_PATH not set. Please run: . $HOME/esp/esp-idf/export.sh")
        return False
    
    if not os.path.exists(idf_path):
        print_error(f"IDF_PATH points to non-existent directory: {idf_path}")
        return False
    
    print_success(f"ESP-IDF found at: {idf_path}")
    return True

def find_serial_port():
    """Try to find ESP32 serial port"""
    import glob
    
    # Common port patterns
    patterns = [
        '/dev/cu.usbmodem*',  # macOS
        '/dev/ttyUSB*',       # Linux
        '/dev/ttyACM*',       # Linux
        'COM*',               # Windows
    ]
    
    for pattern in patterns:
        ports = glob.glob(pattern)
        if ports:
            return sorted(ports)[0]
    
    return None

def run_command(cmd, cwd=None, check=True, capture_output=False):
    """Run a shell command"""
    if isinstance(cmd, str):
        cmd = cmd.split()
    
    print_step(f"Running: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            check=check,
            capture_output=capture_output,
            text=True
        )
        if capture_output:
            return result.stdout, result.stderr, result.returncode
        return result.returncode == 0
    except subprocess.CalledProcessError as e:
        if not check:
            return False
        print_error(f"Command failed: {' '.join(cmd)}")
        if e.stdout:
            print(e.stdout)
        if e.stderr:
            print(e.stderr)
        raise
    except FileNotFoundError:
        print_error(f"Command not found: {cmd[0]}")
        print("Make sure ESP-IDF is properly set up and idf.py is in PATH")
        raise

def build_test_app(app_name, project_root):
    """Build a test app"""
    app_dir = project_root / "integration_tests" / "test_apps" / app_name
    
    if not app_dir.exists():
        print_error(f"Test app not found: {app_name}")
        return False
    
    print_header(f"Building {app_name}")
    
    # Set target if not already set
    os.chdir(app_dir)
    
    # Build
    success = run_command(["idf.py", "build"], cwd=app_dir, check=False)
    
    if success:
        print_success(f"{app_name} built successfully")
        return True
    else:
        print_error(f"Failed to build {app_name}")
        return False

def flash_test_app(app_name, project_root, port=None):
    """Flash a test app to device"""
    app_dir = project_root / "integration_tests" / "test_apps" / app_name
    
    if not app_dir.exists():
        print_error(f"Test app not found: {app_name}")
        return False
    
    print_header(f"Flashing {app_name}")
    
    if not port:
        port = find_serial_port()
        if not port:
            print_error("No serial port found. Please specify with --port")
            return False
    
    print_step(f"Using serial port: {port}")
    
    # Flash
    cmd = ["idf.py", "-p", port, "flash"]
    success = run_command(cmd, cwd=app_dir, check=False)
    
    if success:
        print_success(f"{app_name} flashed successfully")
        return True
    else:
        print_error(f"Failed to flash {app_name}")
        return False

def monitor_serial(port=None, timeout=30):
    """Monitor serial output"""
    if not port:
        port = find_serial_port()
        if not port:
            print_error("No serial port found. Please specify with --port")
            return False
    
    print_header(f"Serial Monitor (port: {port})")
    print_step("Press Ctrl+] to exit monitor")
    
    try:
        # Use idf.py monitor for proper ESP-IDF integration
        cmd = ["idf.py", "-p", port, "monitor"]
        subprocess.run(cmd, check=False)
    except KeyboardInterrupt:
        print("\n" + Colors.WARNING + "Monitor interrupted by user" + Colors.ENDC)
    except Exception as e:
        print_error(f"Monitor error: {e}")

def run_pytest_tests(test_app_name=None, port=None, verbose=True):
    """Run pytest tests"""
    project_root = get_project_root()
    tests_dir = project_root / "integration_tests"
    
    print_header("Running pytest tests")
    
    # Determine which test file to run
    test_files = {
        "audio_integration": "test_audio_playback.py",
        "display_integration": "test_display_rendering.py",
        "storage_integration": "test_storage_access.py",
        "controls_integration": "test_controls_input.py",
        "dsp_integration": "test_dsp_pipeline.py",
        "e2e_workflows": "test_e2e_workflows.py",
    }
    
    if test_app_name and test_app_name in test_files:
        test_file = test_files[test_app_name]
        test_path = tests_dir / "host_tests" / test_file
    else:
        # Run all tests
        test_path = tests_dir / "host_tests"
    
    if not test_path.exists():
        print_error(f"Test file not found: {test_path}")
        return False
    
    # Build pytest command
    cmd = ["pytest", str(test_path), "-v", "--tb=short"]
    
    if port:
        cmd.extend(["--embedded-services-args", f"esp.port={port}"])
    
    if verbose:
        cmd.append("-v")
    
    # Add JSON report
    results_dir = tests_dir / "results"
    results_dir.mkdir(exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    json_report = results_dir / f"test_results_{timestamp}.json"
    html_report = results_dir / f"test_results_{timestamp}.html"
    
    cmd.extend(["--json-report", "--json-report-file", str(json_report)])
    
    print_step(f"Running pytest: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, cwd=tests_dir, check=False)
        
        # Generate HTML report if pytest-html is available
        try:
            subprocess.run(
                ["pytest", str(test_path), "--html", str(html_report), "--self-contained-html"],
                cwd=tests_dir,
                check=False,
                capture_output=True
            )
            print_success(f"HTML report: {html_report}")
        except:
            pass
        
        if result.returncode == 0:
            print_success("All tests passed!")
            return True
        else:
            print_warning(f"Some tests failed (exit code: {result.returncode})")
            return False
    except FileNotFoundError:
        print_error("pytest not found. Install with: pip install pytest pytest-embedded pytest-embedded-serial-esp")
        return False

def collect_test_results(project_root):
    """Collect and summarize test results"""
    results_dir = project_root / "integration_tests" / "results"
    
    if not results_dir.exists():
        return None
    
    json_files = sorted(results_dir.glob("test_results_*.json"), reverse=True)
    
    if not json_files:
        return None
    
    latest = json_files[0]
    
    try:
        with open(latest, 'r') as f:
            data = json.load(f)
            return data
    except:
        return None

def print_test_summary(results):
    """Print test results summary"""
    if not results:
        return
    
    print_header("Test Results Summary")
    
    if 'exitcode' in results:
        exitcode = results['exitcode']
        if exitcode == 0:
            print_success("All tests passed!")
        else:
            print_error(f"Tests failed (exit code: {exitcode})")
    
    if 'summary' in results:
        summary = results['summary']
        total = summary.get('total', 0)
        passed = summary.get('passed', 0)
        failed = summary.get('failed', 0)
        skipped = summary.get('skipped', 0)
        
        print(f"\nTotal: {total}")
        print(f"{Colors.OKGREEN}Passed: {passed}{Colors.ENDC}")
        print(f"{Colors.FAIL}Failed: {failed}{Colors.ENDC}")
        if skipped > 0:
            print(f"{Colors.WARNING}Skipped: {skipped}{Colors.ENDC}")

def main():
    parser = argparse.ArgumentParser(
        description="Integration test runner for ESP32-S3 DJ controller",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument(
        "test_app",
        nargs="?",
        default="all",
        choices=["all"] + get_test_apps(),
        help="Test app to build/flash/run (default: all)"
    )
    
    parser.add_argument(
        "--port", "-p",
        help="Serial port (e.g., /dev/cu.usbmodem2101)"
    )
    
    parser.add_argument(
        "--build-only",
        action="store_true",
        help="Only build, don't flash or run tests"
    )
    
    parser.add_argument(
        "--flash-only",
        action="store_true",
        help="Only flash, don't build or run tests"
    )
    
    parser.add_argument(
        "--monitor",
        "-m",
        action="store_true",
        help="Start serial monitor after flashing"
    )
    
    parser.add_argument(
        "--no-pytest",
        action="store_true",
        help="Skip pytest execution"
    )
    
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip build step (assume already built)"
    )
    
    parser.add_argument(
        "--skip-flash",
        action="store_true",
        help="Skip flash step (assume already flashed)"
    )
    
    args = parser.parse_args()
    
    # Check ESP-IDF environment
    if not check_idf_environment():
        sys.exit(1)
    
    project_root = get_project_root()
    test_apps = get_test_apps()
    
    if args.test_app == "all" and not args.build_only and not args.flash_only:
        print_header("Running all integration tests")
        
        # Build all apps
        if not args.skip_build:
            for app in test_apps:
                if not build_test_app(app, project_root):
                    print_error(f"Failed to build {app}")
                    sys.exit(1)
        
        # Flash and test each app
        for app in test_apps:
            if not args.skip_flash:
                if not flash_test_app(app, project_root, args.port):
                    print_warning(f"Failed to flash {app}, skipping")
                    continue
            
            if args.monitor:
                monitor_serial(args.port)
            
            if not args.no_pytest:
                run_pytest_tests(app, args.port)
        
        # Collect and print summary
        results = collect_test_results(project_root)
        print_test_summary(results)
        
    else:
        # Single test app workflow
        app_name = args.test_app
        
        if app_name not in test_apps and app_name != "all":
            print_error(f"Unknown test app: {app_name}")
            print(f"Available apps: {', '.join(test_apps)}")
            sys.exit(1)
        
        # Build
        if not args.skip_build and not args.flash_only:
            if not build_test_app(app_name, project_root):
                sys.exit(1)
        
        # Flash
        if not args.skip_flash and not args.build_only:
            if not flash_test_app(app_name, project_root, args.port):
                sys.exit(1)
        
        # Monitor
        if args.monitor:
            monitor_serial(args.port)
        
        # Run pytest
        if not args.no_pytest and not args.build_only and not args.flash_only:
            success = run_pytest_tests(app_name, args.port)
            
            # Collect and print summary
            results = collect_test_results(project_root)
            print_test_summary(results)
            
            if not success:
                sys.exit(1)

if __name__ == "__main__":
    main()

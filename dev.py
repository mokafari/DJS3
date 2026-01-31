#!/usr/bin/env python3
"""
PT1210 ESP32-S3 Development Script
Automates build, flash, monitor, and reset operations for continuous development.

Usage:
    python3 dev.py                    # Build, flash, and monitor
    python3 dev.py --build-only       # Only build
    python3 dev.py --flash-only       # Only flash (requires previous build)
    python3 dev.py --monitor-only     # Only monitor serial output
    python3 dev.py --reset            # Reset device before monitoring
    python3 dev.py --clean            # Clean build directory
    python3 dev.py --menuconfig       # Open menuconfig
    python3 dev.py --test             # Build and run test suite (unit tests)
    python3 dev.py --test-hw          # Build and run hardware tests on device
    python3 dev.py -p /dev/ttyUSB0    # Use different serial port
    python3 dev.py --no-monitor       # Build and flash without monitoring
"""

import os
import sys
import subprocess
import argparse
import time
import shutil
import re
from pathlib import Path

# Try to import pyserial for direct serial monitoring
try:
    import serial
    import serial.tools.list_ports
    PYSERIAL_AVAILABLE = True
except ImportError:
    PYSERIAL_AVAILABLE = False

# Configuration
PROJECT_DIR = Path(__file__).parent.absolute()
IDF_PATH = Path("/Users/gustav/.espressif/v5.5.2/esp-idf")
SERIAL_PORT = "/dev/cu.usbmodem2101"
BAUD_RATE = 115200

# Colors for terminal output
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

def print_header(text):
    print(f"\n{Colors.HEADER}{Colors.BOLD}{'='*60}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{text:^60}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{'='*60}{Colors.ENDC}\n")

def print_step(text):
    print(f"{Colors.OKCYAN}▶ {text}{Colors.ENDC}")

def print_success(text):
    print(f"{Colors.OKGREEN}✓ {text}{Colors.ENDC}")

def print_error(text):
    print(f"{Colors.FAIL}✗ {text}{Colors.ENDC}")

def print_warning(text):
    print(f"{Colors.WARNING}⚠ {text}{Colors.ENDC}")

def run_command(cmd, cwd=None, check=True, capture_output=False, env=None):
    """Run a shell command and return the result."""
    print_step(f"Running: {' '.join(cmd)}")
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd or PROJECT_DIR,
            check=check,
            capture_output=capture_output,
            text=True,
            env=env
        )
        return result
    except subprocess.CalledProcessError as e:
        print_error(f"Command failed: {e}")
        if e.stdout:
            print(e.stdout)
        if e.stderr:
            print(e.stderr)
        sys.exit(1)

def setup_idf_env():
    """Set up ESP-IDF environment."""
    print_step("Setting up ESP-IDF environment...")
    export_script = IDF_PATH / "export.sh"
    if not export_script.exists():
        print_error(f"ESP-IDF not found at {IDF_PATH}")
        sys.exit(1)
    
    # Source the export script and get the environment
    env_cmd = f"source {export_script} && env"
    result = subprocess.run(
        env_cmd,
        shell=True,
        executable="/bin/zsh",
        capture_output=True,
        text=True,
        cwd=PROJECT_DIR
    )
    
    # Parse environment variables
    env = os.environ.copy()
    for line in result.stdout.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            env[key] = value
    
    return env

def build_project(env):
    """Build the ESP-IDF project."""
    print_header("BUILDING PROJECT")
    cmd = ["idf.py", "build"]
    try:
        result = subprocess.run(
            cmd,
            cwd=PROJECT_DIR,
            env=env,
            capture_output=True,
            text=True
        )
        
        # Print output
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)
        
        if result.returncode == 0:
            print_success("Build completed successfully!")
            # Extract binary size info if available
            if result.stdout:
                for line in result.stdout.splitlines():
                    if "binary size" in line.lower():
                        print(f"  {line.strip()}")
            return True
        else:
            print_error("Build failed!")
            return False
    except Exception as e:
        print_error(f"Build error: {e}")
        return False

def flash_project(env, port=None, force_download=False):
    """Flash the project to the device.
    
    Args:
        env: Environment variables
        port: Serial port
        force_download: If True, attempt to force device into download mode first
    """
    print_header("FLASHING PROJECT")
    port = port or SERIAL_PORT
    
    # Check if port exists
    if not check_port_available(port):
        print_error(f"Port {port} not found!")
        print(f"{Colors.WARNING}Please connect the ESP32-S3 device{Colors.ENDC}")
        print(f"{Colors.WARNING}If device is in bootloop, try:{Colors.ENDC}")
        print(f"{Colors.WARNING}  1. Hold BOOT button, press RESET, release BOOT{Colors.ENDC}")
        print(f"{Colors.WARNING}  2. Or run: python3 dev.py --force-download{Colors.ENDC}")
        return False
    
    # If device might be in bootloop, try to force download mode
    if force_download:
        print_step("Attempting to force device into download mode...")
        if not force_download_mode(port):
            print_warning("Could not force download mode, attempting flash anyway...")
        time.sleep(1)
    
    cmd = ["idf.py", "-p", port, "flash"]
    
    try:
        result = subprocess.run(
            cmd,
            cwd=PROJECT_DIR,
            env=env,
            text=True
        )
        
        if result.returncode == 0:
            print_success(f"Flash completed successfully to {port}!")
            # Wait a moment for flash to complete
            time.sleep(1)
            return True
        else:
            print_error(f"Flash failed to {port}!")
            print(f"{Colors.WARNING}If device is stuck in bootloop:{Colors.ENDC}")
            print(f"{Colors.WARNING}  1. Hold BOOT button (GPIO0){Colors.ENDC}")
            print(f"{Colors.WARNING}  2. Press and release RESET button{Colors.ENDC}")
            print(f"{Colors.WARNING}  3. Release BOOT button{Colors.ENDC}")
            print(f"{Colors.WARNING}  4. Run: python3 dev.py --flash-only --force-download{Colors.ENDC}")
            return False
    except KeyboardInterrupt:
        print_error("Flash interrupted by user")
        return False
    except Exception as e:
        print_error(f"Flash error: {e}")
        return False

def monitor_serial_direct(port=None, baud=115200, auto_exit=False, wait_for_reset=False):
    """Monitor serial output directly using pyserial (works without TTY).
    
    Args:
        port: Serial port path
        baud: Baud rate
        auto_exit: If True, exit on errors/crashes
        wait_for_reset: If True, wait for user to reset device before displaying logs
    
    Returns:
        True on graceful exit (Ctrl+C), False on error/connection loss
    """
    if not PYSERIAL_AVAILABLE:
        print_error("pyserial not available. Install with: pip install pyserial")
        return False
    
    port = port or SERIAL_PORT
    
    # Wait for port to be available (device might be rebooting)
    max_retries = 10
    retry_count = 0
    while retry_count < max_retries:
        if os.path.exists(port):
            break
        print_step(f"Waiting for port {port}... ({retry_count + 1}/{max_retries})")
        time.sleep(1)
        retry_count += 1
    
    if not os.path.exists(port):
        print_error(f"Port {port} not found after {max_retries} retries")
        print_warning("Device might be disconnected or in bootloop")
        print_warning("Try: Hold BOOT button, press RESET, release BOOT, then retry")
        return False
    
    print_step(f"Opening serial port {port} at {baud} baud...")
    
    try:
        # Try to open port with retries (device might be initializing)
        ser = None
        for attempt in range(5):
            try:
                ser = serial.Serial(port, baud, timeout=1)
                break
            except serial.SerialException as e:
                if attempt < 4:
                    print_step(f"Port busy, retrying... ({attempt + 1}/5)")
                    time.sleep(1)
                else:
                    raise
        
        if ser is None:
            raise serial.SerialException("Failed to open serial port after retries")
            
        print_success(f"Serial port opened: {port}")
        
        # If waiting for reset, wait for serial output to start
        if wait_for_reset:
            print(f"\n{Colors.OKCYAN}{'='*60}{Colors.ENDC}")
            print(f"{Colors.OKCYAN}Serial logging ready. Please reset the device.{Colors.ENDC}")
            print(f"{Colors.OKCYAN}Waiting for device output...{Colors.ENDC}")
            print(f"{Colors.WARNING}Press Ctrl+C to cancel{Colors.ENDC}")
            print(f"{Colors.OKCYAN}{'='*60}{Colors.ENDC}\n")
            
            # Wait for first output (up to 30 seconds)
            # Allow port to disappear during reboot and wait for it to reappear
            wait_start = time.time()
            wait_timeout = 30
            port_reappear_timeout = 10  # Max time to wait for port to reappear after disconnect
            got_output = False
            port_disconnected = False
            
            try:
                while (time.time() - wait_start) < wait_timeout:
                    # Check if port still exists
                    if not os.path.exists(port):
                        if not port_disconnected:
                            # First time port disappears - device is rebooting
                            print_step(f"Port {port} disconnected (device rebooting)...")
                            try:
                                ser.close()
                            except:
                                pass  # Already closed or error
                            port_disconnected = True
                            port_disconnect_time = time.time()
                        
                        # Wait for port to reappear
                        if (time.time() - port_disconnect_time) > port_reappear_timeout:
                            print_error(f"Port {port} did not reappear after {port_reappear_timeout} seconds")
                            return False
                        
                        time.sleep(0.5)
                        continue
                    
                    # Port exists - if we were disconnected, reopen connection
                    if port_disconnected:
                        print_step(f"Port {port} reappeared, reopening connection...")
                        try:
                            ser = serial.Serial(port, baud, timeout=1)
                            print_success("Connection reopened")
                            port_disconnected = False
                        except serial.SerialException as e:
                            print_warning(f"Failed to reopen port: {e}, retrying...")
                            time.sleep(0.5)
                            continue
                    
                    # Check for serial output
                    try:
                        if ser.in_waiting > 0:
                            # Got output, device has reset
                            got_output = True
                            print(f"{Colors.OKGREEN}Device output detected, starting log display...{Colors.ENDC}\n")
                            break
                    except (serial.SerialException, OSError) as e:
                        # Connection error - might be rebooting, close and wait for reconnect
                        if not port_disconnected:
                            print_step(f"Serial connection lost (device may be rebooting): {e}")
                            try:
                                ser.close()
                            except:
                                pass  # Already closed or error
                            port_disconnected = True
                            port_disconnect_time = time.time()
                        time.sleep(0.5)
                        continue
                    
                    time.sleep(0.1)
            except KeyboardInterrupt:
                print(f"\n{Colors.OKGREEN}Graceful exit requested by user (Ctrl+C){Colors.ENDC}")
                try:
                    ser.close()
                except:
                    pass
                return True
            
            if not got_output:
                print_warning(f"No output detected after {wait_timeout} seconds")
                print_warning("Device might not be responding. Press Ctrl+C to cancel or wait longer.")
                # Continue anyway - user might want to see what happens
        
        print(f"{Colors.WARNING}Press Ctrl+C for graceful exit{Colors.ENDC}\n")
        
        error_keywords = [
            "Guru Meditation",
            "abort()",
            "assert failed",
            "PANIC",
            "Fatal exception",
            "CORRUPT",
            "Stack overflow",
            "***ERROR***",
        ]
        
        # Separate list for warnings that should be logged but not trigger auto-exit
        # Note: "E (" is ESP-IDF's standard error logging prefix (e.g., "E (1234) component: message")
        # and is normal logging output, not a warning condition, so we don't flag it
        warning_keywords = [
            # Add any actual warning patterns here if needed
        ]
        
        last_output_time = time.time()
        timeout_seconds = 60
        connection_check_interval = 5  # Check connection every 5 seconds
        
        try:
            error_count = 0
            last_connection_check = time.time()
            
            while True:
                # Periodically check if port still exists (connection loss detection)
                if time.time() - last_connection_check > connection_check_interval:
                    if not os.path.exists(port):
                        print_error(f"\n{Colors.FAIL}Connection lost: Port {port} no longer exists{Colors.ENDC}")
                        print_error("Device may have crashed, rebooted, or disconnected")
                        ser.close()
                        return False
                    last_connection_check = time.time()
                
                try:
                    if ser.in_waiting > 0:
                        line = ser.readline().decode('utf-8', errors='ignore')
                        if line.strip():
                            print(line, end='', flush=True)
                            last_output_time = time.time()
                            
                            # Convert to lowercase for keyword matching
                            line_lower = line.lower()
                            
                            # Check for warning keywords (log but never exit)
                            for keyword in warning_keywords:
                                if keyword.lower() in line_lower:
                                    error_count += 1
                                    print(f"\n{Colors.FAIL}⚠ [{error_count}] Warning detected: '{keyword}'{Colors.ENDC}")
                                    print(f"{Colors.WARNING}Continuing monitoring... (use Ctrl+C to exit){Colors.ENDC}")
                                    break
                            
                            # Check for error keywords (log but don't exit unless auto_exit is True)
                            for keyword in error_keywords:
                                if keyword.lower() in line_lower:
                                    error_count += 1
                                    print(f"\n{Colors.FAIL}⚠ [{error_count}] Error detected: '{keyword}'{Colors.ENDC}")
                                    if auto_exit:
                                        print(f"{Colors.WARNING}Stopping monitor due to detected error{Colors.ENDC}")
                                        ser.close()
                                        return False  # Error exit
                                    else:
                                        print(f"{Colors.WARNING}Continuing monitoring... (use Ctrl+C to exit){Colors.ENDC}")
                                    break
                    else:
                        # Check for timeout (only if auto_exit)
                        if auto_exit and (time.time() - last_output_time > timeout_seconds):
                            print(f"\n{Colors.WARNING}⚠ No output for {timeout_seconds} seconds - possible freeze{Colors.ENDC}")
                            ser.close()
                            return False  # Error exit
                        time.sleep(0.01)  # Small delay
                        
                except (serial.SerialException, OSError) as e:
                    # Connection lost during read
                    print_error(f"\n{Colors.FAIL}Connection lost during read: {e}{Colors.ENDC}")
                    print_error("Device may have crashed, rebooted, or disconnected")
                    ser.close()
                    return False
                    
        except KeyboardInterrupt:
            print(f"\n{Colors.OKGREEN}Graceful exit requested by user (Ctrl+C){Colors.ENDC}")
            if 'error_count' in locals() and error_count > 0:
                print(f"{Colors.WARNING}Total errors detected: {error_count}{Colors.ENDC}")
            ser.close()
            return True  # Graceful exit
            
    except serial.SerialException as e:
        print_error(f"Failed to open serial port: {e}")
        return False
    except Exception as e:
        print_error(f"Serial monitor error: {e}")
        return False

def monitor_project(env, port=None, reset=False, auto_exit=False, wait_for_reset=False):
    """Monitor serial output from the device.
    
    Args:
        env: Environment variables
        port: Serial port
        reset: Whether to reset device before monitoring
        auto_exit: If True, monitor until error/crash detected, then exit
        wait_for_reset: If True, wait for user to reset device before displaying logs
    """
    print_header("MONITORING SERIAL OUTPUT")
    port = port or SERIAL_PORT
    
    if reset:
        print_step("Resetting device...")
        reset_device(port)
        time.sleep(2)  # Give device time to reset
    
    print_step(f"Starting monitor on {port} (baud: {BAUD_RATE})")
    if auto_exit:
        print(f"{Colors.WARNING}Monitoring until error/crash detected...{Colors.ENDC}")
        print(f"{Colors.WARNING}Press Ctrl+C to exit early{Colors.ENDC}\n")
    else:
        print(f"{Colors.WARNING}Press Ctrl+C for graceful exit{Colors.ENDC}\n")
    
    # Use direct serial monitoring (works without TTY, better for continuous monitoring)
    if PYSERIAL_AVAILABLE:
        print_step("Using direct serial monitoring (pyserial)...")
        result = monitor_serial_direct(port, BAUD_RATE, auto_exit, wait_for_reset)
        if not result:
            # Connection lost or error - exit with error code
            sys.exit(1)
        return result
    
    # Fallback to idf_monitor for interactive mode (requires TTY)
    cmd = ["idf.py", "-p", port, "monitor"]
    
    try:
        if not auto_exit:
            # Interactive mode - just run it
            subprocess.run(cmd, env=env, cwd=PROJECT_DIR)
        else:
            # Auto-exit mode but pyserial not available
            if not PYSERIAL_AVAILABLE:
                print_warning("pyserial not available, idf_monitor requires TTY")
                print_warning("Install pyserial for non-TTY monitoring: pip install pyserial")
                print_warning("Or run monitor in an interactive terminal")
                return False
            # Should have been handled above, but just in case
            result = monitor_serial_direct(port, BAUD_RATE, auto_exit, wait_for_reset)
            if not result:
                sys.exit(1)
            return result
                
    except KeyboardInterrupt:
        print(f"\n{Colors.OKGREEN}Graceful exit requested by user (Ctrl+C){Colors.ENDC}")
        return True
    except Exception as e:
        print_error(f"Monitor error: {e}")
        # Try fallback to direct serial
        if PYSERIAL_AVAILABLE:
            print_step("Falling back to direct serial monitoring...")
            result = monitor_serial_direct(port, BAUD_RATE, auto_exit, wait_for_reset)
            if not result:
                sys.exit(1)
            return result
        return False
    
    return True

def reset_device(port=None, wait_after=True):
    """Reset the device via esptool.
    
    Args:
        port: Serial port
        wait_after: If True, wait a bit after reset for device to stabilize
    """
    port = port or SERIAL_PORT
    print_step(f"Resetting device on {port}...")
    
    cmd = [
        "python", "-m", "esptool",
        "--chip", "esp32s3",
        "-p", port,
        "--before", "no_reset",  # Don't reset before (we're doing it manually)
        "--after", "no_reset",   # Don't reset after (avoid double reset)
        "chip_reset"
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, timeout=5, text=True)
        if result.returncode == 0:
            print_success("Device reset")
            if wait_after:
                time.sleep(2)  # Give device time to stabilize
            return True
        else:
            # Reset might fail if device is in bootloader, that's OK
            print_warning(f"Reset command returned non-zero: {result.stderr}")
            if wait_after:
                time.sleep(2)
            return False
    except subprocess.TimeoutExpired:
        print_warning("Reset timeout (device might be rebooting)")
        if wait_after:
            time.sleep(2)
        return False
    except Exception as e:
        print_warning(f"Reset warning: {e} (device might be rebooting)")
        if wait_after:
            time.sleep(2)
        return False

def force_download_mode(port=None):
    """Force device into download/flashing mode.
    
    For ESP32-S3, this attempts multiple methods to enter download mode:
    1. Try esptool reset with no_reset flags
    2. Try to catch device during boot and send download command
    
    Note: If device is stuck in bootloop, you may need to manually:
    - Hold BOOT button (GPIO0), press RESET, release BOOT
    """
    port = port or SERIAL_PORT
    print_step(f"Forcing device into download mode on {port}...")
    
    # Method 1: Try esptool with no_reset to catch device in boot
    print_step("Attempting method 1: Reset into download mode...")
    cmd1 = [
        "python", "-m", "esptool",
        "--chip", "esp32s3",
        "-p", port,
        "--baud", "115200",
        "--before", "no_reset",
        "--after", "no_reset",
        "chip_reset"
    ]
    
    try:
        result = subprocess.run(
            cmd1,
            capture_output=True,
            text=True,
            timeout=3
        )
        if result.returncode == 0:
            print_success("Device reset successful")
            time.sleep(1)
            return True
    except:
        pass
    
    # Method 2: Try to connect and immediately flash (esptool will handle download mode)
    print_step("Attempting method 2: Direct connection attempt...")
    cmd2 = [
        "python", "-m", "esptool",
        "--chip", "esp32s3",
        "-p", port,
        "--baud", "115200",
        "chip_id"
    ]
    
    try:
        result = subprocess.run(
            cmd2,
            capture_output=True,
            text=True,
            timeout=3
        )
        if result.returncode == 0:
            print_success("Device is accessible")
            return True
    except:
        pass
    
    print_warning("Could not automatically enter download mode")
    print(f"{Colors.WARNING}Manual method required:{Colors.ENDC}")
    print(f"{Colors.WARNING}  1. Hold BOOT button (GPIO0) on ESP32-S3{Colors.ENDC}")
    print(f"{Colors.WARNING}  2. Press and release RESET button{Colors.ENDC}")
    print(f"{Colors.WARNING}  3. Release BOOT button{Colors.ENDC}")
    print(f"{Colors.WARNING}  4. Device should now be in download mode{Colors.ENDC}")
    print(f"{Colors.WARNING}  5. Run flash command again{Colors.ENDC}")
    
    return False

def check_port_available(port=None):
    """Check if serial port is available."""
    port = port or SERIAL_PORT
    if os.path.exists(port):
        return True
    return False

def clean_project(env):
    """Clean the build directory."""
    print_header("CLEANING PROJECT")
    cmd = ["idf.py", "fullclean"]
    run_command(cmd, env=env)
    print_success("Project cleaned")

def set_target(env):
    """Set the target to esp32s3."""
    print_step("Setting target to esp32s3...")
    cmd = ["idf.py", "set-target", "esp32s3"]
    run_command(cmd, env=env, check=False)
    print_success("Target set")

def run_tests(env, port):
    """Build and run the test suite."""
    print_header("RUNNING TEST SUITE")
    
    test_app_dir = PROJECT_DIR / "test_app"
    
    if not test_app_dir.exists():
        print_error("Test app directory not found: test_app/")
        print("Create test files in test_app/main/ first.")
        return False
    
    print_step(f"Building test app in {test_app_dir}...")
    
    # Build test app
    cmd = ["idf.py", "-C", str(test_app_dir), "build"]
    result = subprocess.run(cmd, env=env, cwd=PROJECT_DIR)
    
    if result.returncode != 0:
        print_error("Test app build failed!")
        return False
    
    print_success("Test app built successfully")
    
    # Flash test app
    print_step("Flashing test app...")
    cmd = ["idf.py", "-C", str(test_app_dir), "-p", port, "flash"]
    result = subprocess.run(cmd, env=env, cwd=PROJECT_DIR)
    
    if result.returncode != 0:
        print_error("Test app flash failed!")
        return False
    
    print_success("Test app flashed successfully")
    
    # Monitor test output
    print_step("Monitoring test output...")
    print_header("TEST OUTPUT")
    cmd = ["idf.py", "-C", str(test_app_dir), "-p", port, "monitor"]
    subprocess.run(cmd, env=env, cwd=PROJECT_DIR)
    
    return True

def run_hw_tests(env, port):
    """Build and run hardware tests on actual device.
    
    This builds the main app with CONFIG_HW_TEST_MODE=y, flashes it,
    monitors the output for test results, then restores normal config.
    """
    print_header("HARDWARE TEST SUITE")
    
    sdkconfig_test = PROJECT_DIR / "sdkconfig.test"
    sdkconfig_normal = PROJECT_DIR / "sdkconfig.normal"
    sdkconfig = PROJECT_DIR / "sdkconfig"
    
    # Check test config exists
    if not sdkconfig_test.exists():
        print_error("sdkconfig.test not found!")
        print("Create sdkconfig.test with CONFIG_HW_TEST_MODE=y")
        return False
    
    # Backup current sdkconfig if not already backed up
    if sdkconfig.exists() and not sdkconfig_normal.exists():
        print_step("Backing up current sdkconfig...")
        shutil.copy(sdkconfig, sdkconfig_normal)
    
    try:
        # Copy test config
        print_step("Applying test configuration...")
        shutil.copy(sdkconfig_test, sdkconfig)
        
        # Clean and rebuild with test config
        print_step("Cleaning build directory...")
        clean_project(env)
        
        print_step("Building with hardware test mode...")
        if not build_project(env):
            print_error("Build failed!")
            return False
        
        print_success("Test firmware built successfully")
        
        # Flash test firmware
        print_step("Flashing test firmware...")
        if not flash_project(env, port):
            print_error("Flash failed!")
            return False
        
        print_success("Test firmware flashed successfully")
        
        # Monitor and capture test output
        print_header("HARDWARE TEST OUTPUT")
        print_step("Monitoring test output (timeout: 120s)...")
        print("Press Ctrl+C to abort\n")
        
        test_output = capture_hw_test_output(port, timeout=120)
        
        # Parse and display results
        return parse_hw_test_results(test_output)
        
    finally:
        # Restore normal config
        if sdkconfig_normal.exists():
            print_step("Restoring normal configuration...")
            shutil.copy(sdkconfig_normal, sdkconfig)
            print_success("Configuration restored")

def capture_hw_test_output(port, timeout=120):
    """Capture hardware test output from serial port."""
    if not PYSERIAL_AVAILABLE:
        print_error("pyserial not available - install with: pip install pyserial")
        return []
    
    output_lines = []
    start_time = time.time()
    test_started = False
    test_completed = False
    
    try:
        # Wait for port to be available
        for i in range(10):
            try:
                ser = serial.Serial(port, BAUD_RATE, timeout=1)
                break
            except serial.SerialException:
                if i < 9:
                    print_step(f"Waiting for port {port}... ({i+1}/10)")
                    time.sleep(1)
                else:
                    print_error(f"Could not open port {port}")
                    return []
        
        print_success(f"Connected to {port}")
        
        while time.time() - start_time < timeout:
            try:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    print(line)
                    output_lines.append(line)
                    
                    # Check for test markers
                    if "HW_TEST_START" in line:
                        test_started = True
                    if "HW_TEST_SUMMARY" in line:
                        test_completed = True
                    if "TEST MODE COMPLETE" in line:
                        break
                        
            except Exception as e:
                if "Device not configured" in str(e):
                    time.sleep(0.5)
                    continue
                print_warning(f"Read error: {e}")
                
        ser.close()
        
    except KeyboardInterrupt:
        print_warning("\nTest monitoring interrupted by user")
    except Exception as e:
        print_error(f"Serial error: {e}")
    
    if not test_started:
        print_warning("Test start marker not found - device may not have test mode enabled")
    if not test_completed:
        print_warning("Test completion marker not found - tests may not have finished")
        
    return output_lines

def parse_hw_test_results(output_lines):
    """Parse hardware test output and display summary."""
    passed = 0
    failed = 0
    results = []
    
    for line in output_lines:
        # Match test results: TEST_PASS|name|message or TEST_FAIL|name|message
        match = re.match(r'TEST_(PASS|FAIL)\|([^|]+)\|(.+)', line)
        if match:
            status, name, message = match.groups()
            results.append({
                'status': status,
                'name': name,
                'message': message
            })
            if status == 'PASS':
                passed += 1
            else:
                failed += 1
        
        # Also parse summary line: HW_TEST_SUMMARY|passed=X|failed=Y
        summary_match = re.match(r'.*HW_TEST_SUMMARY\|passed=(\d+)\|failed=(\d+)', line)
        if summary_match:
            passed = int(summary_match.group(1))
            failed = int(summary_match.group(2))
    
    # Display summary
    print("\n")
    print_header("HARDWARE TEST RESULTS")
    
    total = passed + failed
    if total == 0:
        print_warning("No test results captured!")
        return False
    
    # Show individual results
    for r in results:
        if r['status'] == 'PASS':
            print(f"{Colors.OKGREEN}✓ {r['name']}: {r['message']}{Colors.ENDC}")
        else:
            print(f"{Colors.FAIL}✗ {r['name']}: {r['message']}{Colors.ENDC}")
    
    print("\n" + "="*60)
    print(f"Total: {total} tests")
    print(f"{Colors.OKGREEN}Passed: {passed}{Colors.ENDC}")
    if failed > 0:
        print(f"{Colors.FAIL}Failed: {failed}{Colors.ENDC}")
    else:
        print(f"Failed: {failed}")
    print("="*60)
    
    if failed == 0:
        print_success(f"ALL {passed} HARDWARE TESTS PASSED!")
        return True
    else:
        print_error(f"{failed} HARDWARE TEST(S) FAILED!")
        return False

def menuconfig(env):
    """Open menuconfig."""
    print_header("OPENING MENUCONFIG")
    cmd = ["idf.py", "menuconfig"]
    subprocess.run(cmd, env=env, cwd=PROJECT_DIR)

def main():
    parser = argparse.ArgumentParser(
        description="PT1210 ESP32-S3 Development Script",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                    # Build, flash, and monitor
  %(prog)s --build-only       # Only build
  %(prog)s --flash-only       # Only flash
  %(prog)s --monitor-only     # Only monitor
  %(prog)s --reset            # Reset device
  %(prog)s --clean            # Clean build
  %(prog)s --menuconfig       # Open menuconfig
  %(prog)s -p /dev/ttyUSB0    # Use different port
        """
    )
    
    parser.add_argument(
        "-p", "--port",
        default=SERIAL_PORT,
        help=f"Serial port (default: {SERIAL_PORT})"
    )
    
    parser.add_argument(
        "--build-only",
        action="store_true",
        help="Only build, don't flash or monitor"
    )
    
    parser.add_argument(
        "--flash-only",
        action="store_true",
        help="Only flash, don't build or monitor"
    )
    
    parser.add_argument(
        "--monitor-only",
        action="store_true",
        help="Only monitor, don't build or flash"
    )
    
    parser.add_argument(
        "--no-auto-exit",
        action="store_true",
        help="Don't auto-exit monitor on error (default: auto-exit enabled in build/flash workflow)"
    )
    
    parser.add_argument(
        "--reset",
        action="store_true",
        help="Reset device before monitoring"
    )
    
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean build directory before building"
    )
    
    parser.add_argument(
        "--menuconfig",
        action="store_true",
        help="Open menuconfig"
    )
    
    parser.add_argument(
        "--no-monitor",
        action="store_true",
        help="Don't start monitor after flashing"
    )
    
    parser.add_argument(
        "--force-download",
        action="store_true",
        help="Force device into download mode before flashing (useful for bootloop recovery)"
    )
    
    parser.add_argument(
        "--test",
        action="store_true",
        help="Build and run test suite (uses test_app/)"
    )
    
    parser.add_argument(
        "--test-hw",
        action="store_true",
        help="Build and run hardware tests on device (tests actual components)"
    )
    
    args = parser.parse_args()
    
    # Change to project directory
    os.chdir(PROJECT_DIR)
    
    # Set up ESP-IDF environment
    env = setup_idf_env()
    
    # Handle menuconfig
    if args.menuconfig:
        menuconfig(env)
        return
    
    # Handle test
    if args.test:
        run_tests(env, args.port)
        return
    
    # Handle hardware test
    if args.test_hw:
        run_hw_tests(env, args.port)
        return
    
    # Handle clean
    if args.clean:
        clean_project(env)
    
    # Handle monitor-only
    if args.monitor_only:
        auto_exit = not args.no_auto_exit  # Default: enabled unless --no-auto-exit
        monitor_project(env, args.port, args.reset, auto_exit=auto_exit)
        return
    
    # Handle flash-only
    if args.flash_only:
        if flash_project(env, args.port, force_download=args.force_download):
            if not args.no_monitor:
                print_step("Setting up serial logging...")
                time.sleep(1)  # Brief pause after flash
                auto_exit = not args.no_auto_exit  # Default: enabled unless --no-auto-exit
                result = monitor_project(env, args.port, reset=False, auto_exit=auto_exit, wait_for_reset=True)
                if not result:
                    # Connection lost or error occurred
                    sys.exit(1)
        return
    
    # Handle build-only
    if args.build_only:
        build_project(env)
        return
    
    # Default: build, flash, and monitor
    print_header("PT1210 ESP32-S3 DEVELOPMENT WORKFLOW")
    print(f"Project: {PROJECT_DIR}")
    print(f"Port: {args.port}")
    print(f"ESP-IDF: {IDF_PATH}\n")
    
    # Build
    if not build_project(env):
        print_error("Build failed!")
        sys.exit(1)
    
    # Flash
    if not flash_project(env, args.port, force_download=args.force_download):
        print_error("Flash failed!")
        print(f"{Colors.WARNING}Note: Make sure no other process is using {args.port}{Colors.ENDC}")
        print(f"{Colors.WARNING}Close any serial monitors or other tools using the port{Colors.ENDC}")
        sys.exit(1)
    
    # Monitor
    if not args.no_monitor:
        print_step("Setting up serial logging...")
        time.sleep(1)  # Brief pause after flash
        
        # Start monitoring - wait for user to reset device, then display logs
        # Connection loss (crash, reboot, etc.) will exit with error
        # Ctrl+C will exit gracefully
        result = monitor_project(env, args.port, reset=False, auto_exit=False, wait_for_reset=True)
        if not result:
            # Connection lost or error occurred
            sys.exit(1)

if __name__ == "__main__":
    main()


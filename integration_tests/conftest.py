"""
Pytest configuration and fixtures for ESP-IDF integration tests
"""
import pytest
from pytest_embedded import Dut
import os

@pytest.fixture(scope='session')
def dut(request):
    """ESP32-S3 device under test fixture"""
    return Dut(request.config)

@pytest.fixture
def audio_test_file():
    """Path to test audio file"""
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base_dir, "integration_tests", "fixtures", "test_audio_files", "test_tone_1khz.mp3")

@pytest.fixture
def test_fixtures_dir():
    """Path to test fixtures directory"""
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base_dir, "integration_tests", "fixtures")


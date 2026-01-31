"""
Host-side tests for end-to-end user workflows
This module provides comprehensive workflow testing
"""
import pytest
import time

# Import individual workflow tests
from test_track_loading import (
    test_full_system_initialization,
    test_track_loading_workflow,
    test_playback_workflow,
    test_cue_point_workflow,
    test_loop_control_workflow,
    test_pitch_control_workflow,
    test_ui_integration,
    test_complete_playback_sequence,
    test_e2e_test_suite_complete
)

# Re-export for convenience
__all__ = [
    'test_full_system_initialization',
    'test_track_loading_workflow',
    'test_playback_workflow',
    'test_cue_point_workflow',
    'test_loop_control_workflow',
    'test_pitch_control_workflow',
    'test_ui_integration',
    'test_complete_playback_sequence',
    'test_e2e_test_suite_complete'
]

def test_workflow_integration(dut):
    """Integration test for complete workflow"""
    # This test verifies that all components work together
    dut.expect("End-to-End Workflows Test Suite", timeout=10)
    
    # Wait for all tests to complete
    dut.expect("End-to-End Workflows Test Suite Complete", timeout=120)


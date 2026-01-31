# Test Application CMakeLists.txt
# Build and flash: idf.py -C test_app build flash monitor
#
# This is a STANDALONE test app that doesn't require the full main component.
# It tests pure logic functions that don't need hardware.

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(cdj_test_app)


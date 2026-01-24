#!/bin/bash
# Build and flash script for ESP32-S3 DJ Deck project

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}ESP32-S3 DJ Deck Build & Flash Script${NC}"
echo "=========================================="

# Check if ESP-IDF is set up
if [ -z "$IDF_PATH" ]; then
    echo -e "${YELLOW}Warning: IDF_PATH not set. Attempting to source ESP-IDF...${NC}"
    if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        source "$HOME/esp/esp-idf/export.sh"
    else
        echo -e "${RED}Error: ESP-IDF not found. Please set up ESP-IDF first.${NC}"
        echo "Run: . \$HOME/esp/esp-idf/export.sh"
        exit 1
    fi
fi

# Set target
echo -e "${GREEN}Setting target to ESP32-S3...${NC}"
idf.py set-target esp32s3

# Build
echo -e "${GREEN}Building project...${NC}"
idf.py build

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Build successful!${NC}"
    
    # Check if port is specified
    PORT=${1:-/dev/cu.usbserial-* /dev/ttyUSB* /dev/ttyACM*}
    
    # Find available port
    if [ -e /dev/cu.usbserial-* ] || [ -e /dev/ttyUSB* ] || [ -e /dev/ttyACM* ]; then
        echo -e "${GREEN}Flashing to device...${NC}"
        idf.py -p $(ls /dev/cu.usbserial-* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1) flash
        
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}Flash successful!${NC}"
            echo -e "${GREEN}Starting monitor...${NC}"
            idf.py -p $(ls /dev/cu.usbserial-* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1) monitor
        else
            echo -e "${RED}Flash failed!${NC}"
            exit 1
        fi
    else
        echo -e "${YELLOW}No serial port found. Build complete.${NC}"
        echo "To flash manually, run: idf.py -p <PORT> flash monitor"
    fi
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi


#!/usr/bin/env python3
"""
Test script to send Improv WiFi protocol commands to ESP32 via Serial
Based on Improv WiFi Serial Protocol specification
"""

import serial
import time
import struct

# Improv WiFi Protocol Constants
IMPROV_SERIAL_VERSION = 1
IMPROV_CMD_GET_CURRENT_STATE = 0x01
IMPROV_CMD_GET_DEVICE_INFO = 0x02
IMPROV_CMD_GET_WIFI_NETWORKS = 0x03
IMPROV_CMD_SET_WIFI = 0x04

IMPROV_STATE_STOPPED = 0x01
IMPROV_STATE_AWAITING_AUTHORIZATION = 0x02
IMPROV_STATE_AUTHORIZED = 0x03
IMPROV_STATE_PROVISIONING = 0x04
IMPROV_STATE_PROVISIONED = 0x05

def calculate_crc(data):
    """Calculate CRC-8 for Improv WiFi protocol"""
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ 0x31
            else:
                crc <<= 1
            crc &= 0xFF
    return crc

def create_improv_packet(command, data=b''):
    """Create an Improv WiFi Serial protocol packet
    Format: IM (2 bytes) + Version (1) + Command (1) + Length (1) + Data (N) + CRC (1)
    """
    packet = bytearray([ord('I'), ord('M'), IMPROV_SERIAL_VERSION, command, len(data)])
    packet.extend(data)
    crc = calculate_crc(packet)
    packet.append(crc)
    return bytes(packet)

def send_command(ser, command, data=b''):
    """Send an Improv WiFi command and return response"""
    # Clear any pending serial data first
    ser.reset_input_buffer()
    time.sleep(0.1)
    
    packet = create_improv_packet(command, data)
    print(f"Sending command 0x{command:02x}: {packet.hex()}")
    print(f"  Packet bytes: {[hex(b) for b in packet]}")
    
    ser.write(packet)
    ser.flush()
    
    # Wait longer for response and check multiple times
    response_data = bytearray()
    for i in range(20):  # Wait up to 2 seconds
        time.sleep(0.1)
        if ser.in_waiting > 0:
            chunk = ser.read(ser.in_waiting)
            response_data.extend(chunk)
            print(f"  Received {len(chunk)} bytes after {i*0.1:.1f}s (total: {len(response_data)} bytes)")
    
    if len(response_data) > 0:
        print(f"  Full response: {response_data.hex()}")
        print(f"  Response bytes: {[hex(b) for b in response_data]}")
        
        # Try to decode as text to see if it's debug output
        try:
            text = response_data.decode('utf-8', errors='ignore')
            if text.strip():
                print(f"  Response as text: {repr(text)}")
        except:
            pass
        
        # Check for Improv WiFi response format
        if len(response_data) >= 2 and response_data[0:2] == b'IM':
            print("  ✓ Valid Improv response header detected!")
            return bytes(response_data)
        else:
            print("  ⚠ Response doesn't start with 'IM' - might be debug output or wrong format")
            return bytes(response_data)
    else:
        print("  ✗ No response received after 2 seconds")
        return None

def main():
    # Serial port configuration
    port = '/dev/cu.usbmodem14101'  # Update this to your port
    baudrate = 115200
    
    print(f"Connecting to {port} at {baudrate} baud...")
    
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print("Connected! Waiting for device to be ready...")
        time.sleep(2)  # Wait for device to be ready
        
        # Check for any initial Serial output
        if ser.in_waiting > 0:
            initial_data = ser.read(ser.in_waiting)
            print(f"\nInitial Serial output from device:")
            print(f"  Hex: {initial_data.hex()}")
            try:
                text = initial_data.decode('utf-8', errors='ignore')
                if text.strip():
                    print(f"  Text: {repr(text)}")
            except:
                pass
            print()
        
        print("\n=== Testing Improv WiFi Protocol ===\n")
        
        # Test 1: Get Current State
        print("1. Testing GET_CURRENT_STATE (0x01)...")
        response = send_command(ser, IMPROV_CMD_GET_CURRENT_STATE)
        if response:
            print(f"   State response: {response.hex()}")
        else:
            print("   No response received")
        
        time.sleep(0.5)
        
        # Test 2: Get Device Info
        print("\n2. Testing GET_DEVICE_INFO (0x02)...")
        response = send_command(ser, IMPROV_CMD_GET_DEVICE_INFO)
        if response:
            print(f"   Device info response: {response.hex()}")
            # Try to parse device info
            if len(response) >= 5 and response[0:2] == b'IM':
                print("   ✓ Valid Improv response header detected!")
        else:
            print("   No response received")
        
        time.sleep(0.5)
        
        # Test 3: Get WiFi Networks
        print("\n3. Testing GET_WIFI_NETWORKS (0x03)...")
        response = send_command(ser, IMPROV_CMD_GET_WIFI_NETWORKS)
        if response:
            print(f"   WiFi networks response: {response.hex()}")
        else:
            print("   No response received")
        
        print("\n=== Test Complete ===")
        print("\nIf you see valid Improv responses (starting with 'IM'),")
        print("the device is correctly implementing Improv WiFi protocol.")
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        print("\nMake sure:")
        print("1. The ESP32 is connected")
        print("2. No other program is using the serial port")
        print("3. The port name is correct")
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        if 'ser' in locals():
            ser.close()

if __name__ == '__main__':
    main()

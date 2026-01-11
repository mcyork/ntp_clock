#!/usr/bin/env python3
"""
Test script to send Improv WiFi protocol commands to ESP32 via Serial
Based on Improv WiFi Serial Protocol specification
"""

import serial
import time
import struct

# Improv WiFi Protocol Constants (from ImprovTypes.h)
IMPROV_SERIAL_VERSION = 1
IMPROV_TYPE_RPC = 0x03  # RPC Command type
IMPROV_CMD_GET_CURRENT_STATE = 0x02  # Note: 0x02, not 0x01!
IMPROV_CMD_GET_DEVICE_INFO = 0x03
IMPROV_CMD_GET_WIFI_NETWORKS = 0x04
IMPROV_CMD_SET_WIFI = 0x01

def calculate_checksum(data):
    """Calculate checksum for Improv WiFi protocol - simple sum of all bytes"""
    checksum = 0
    for byte in data:
        checksum = (checksum + byte) & 0xFF
    return checksum

def create_improv_packet(command, data=b''):
    """Create an Improv WiFi Serial protocol packet
    Format: IMPROV (6 bytes) + Version (1) + Type (1) + Length (1) + Data (N) + Checksum (1)
    Data format: Command (1) + DataLength (1) + Data (N)
    """
    # Build RPC data: command byte + data length byte + data bytes
    rpc_data = bytearray([command, len(data)])
    rpc_data.extend(data)
    
    # Build packet: IMPROV header + version + type + length + data
    packet = bytearray([ord('I'), ord('M'), ord('P'), ord('R'), ord('O'), ord('V')])
    packet.append(IMPROV_SERIAL_VERSION)
    packet.append(IMPROV_TYPE_RPC)
    packet.append(len(rpc_data))  # Length of RPC data
    packet.extend(rpc_data)
    
    # Calculate checksum (simple sum of all bytes)
    checksum = calculate_checksum(packet)
    packet.append(checksum)
    
    return bytes(packet)

def send_command(ser, command, data=b''):
    """Send an Improv WiFi command and return response"""
    # Clear any pending serial data first
    ser.reset_input_buffer()
    time.sleep(0.2)  # Give device time to process
    
    packet = create_improv_packet(command, data)
    print(f"Sending command 0x{command:02x}: {packet.hex()}")
    print(f"  Packet bytes: {[hex(b) for b in packet]}")
    print(f"  Packet length: {len(packet)} bytes")
    
    ser.write(packet)
    ser.flush()
    print("  Packet sent, waiting for response...")
    
    # Wait longer for response and check multiple times
    response_data = bytearray()
    for i in range(30):  # Wait up to 3 seconds
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
        if len(response_data) >= 6 and response_data[0:6] == b'IMPROV':
            print("  ✓ Valid Improv response header detected!")
            return bytes(response_data)
        elif len(response_data) >= 2 and response_data[0:2] == b'IM':
            print("  ✓ Partial Improv response header detected!")
            return bytes(response_data)
        else:
            print("  ⚠ Response doesn't start with 'IMPROV' - might be debug output or wrong format")
            return bytes(response_data)
    else:
        print("  ✗ No response received after 3 seconds")
        # Try reading one more time after a longer delay
        time.sleep(0.5)
        if ser.in_waiting > 0:
            chunk = ser.read(ser.in_waiting)
            print(f"  Late response: {chunk.hex()}")
            return bytes(chunk)
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

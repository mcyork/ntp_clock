# NTP Clock - TODO / Future Enhancements

## Segment Test Mode

Segment test code for debugging display segment mapping. This code cycles through each segment individually to help identify which bit corresponds to which physical segment.

**Note:** Segments are REVERSED on this hardware:
- bit0 → G (middle)
- bit1 → F (top-left)
- bit2 → E (bottom-left)
- bit3 → D (bottom)
- bit4 → C (bottom-right)
- bit5 → B (top-right)
- bit6 → A (top)
- bit7 → DP (decimal point)

```cpp
// === SEGMENT TEST MODE: Cycle through each segment individually ===
// This helps map which bit corresponds to which physical segment
lc.setDecodeMode(0x00);  // Raw mode for segment testing
Serial.println("Segment test mode - press any button to skip");

// Segment bit positions (REVERSED): A=bit6, B=bit5, C=bit4, D=bit3, E=bit2, F=bit1, G=bit0, DP=bit7
// Display in forward order: A, B, C, D, E, F, G, DP
uint8_t segments[] = {
  0x40,  // A (bit6)
  0x20,  // B (bit5)
  0x10,  // C (bit4)
  0x08,  // D (bit3)
  0x04,  // E (bit2)
  0x02,  // F (bit1)
  0x01,  // G (bit0)
  0x80   // DP (bit7)
};
const char* segmentNames[] = {"A", "B", "C", "D", "E", "F", "G", "DP"};

bool testDone = false;
while (!testDone) {
  for (int seg = 0; seg < 8; seg++) {
    // Check for button press to exit
    if (digitalRead(PIN_BTN_MODE) == LOW || 
        digitalRead(PIN_BTN_UP) == LOW || 
        digitalRead(PIN_BTN_DOWN) == LOW) {
      testDone = true;
      break;
    }
    
    // Display this segment on all 4 digits
    Serial.print("Showing segment: ");
    Serial.println(segmentNames[seg]);
    for (int digit = 0; digit < 4; digit++) {
      lc.writeRawSegment(0, digit, segments[seg]);
    }
    
    delay(1000);  // 1 second interval
  }
  
  // Clear display between cycles
  if (!testDone) {
    lc.clearDisplay(0);
    delay(500);
  }
}

// Clear display after test
lc.clearDisplay(0);
delay(100);
Serial.println("Segment test complete, continuing...");
```

## Future Enhancements

- Add automatic DST detection/calculation
- Add more timezone presets
- Add display brightness auto-adjust based on ambient light
- Add alarm functionality
- Add temperature display option
- Add network status indicator

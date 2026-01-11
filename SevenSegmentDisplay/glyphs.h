#ifndef GLYPHS_H
#define GLYPHS_H

#include <stdint.h>

// 7-segment bit mapping (reversed from standard):
// Bit 0 = G, Bit 1 = F, Bit 2 = E, Bit 3 = D, Bit 4 = C, Bit 5 = B, Bit 6 = A, Bit 7 = DP
// This matches the physical display wiring

inline uint8_t charToSegment(char c) {
  switch (c) {
    case '0': return 0b01111110; // A B C D E F
    case '1': return 0b00110000; // B C
    case '2': return 0b11011010; // A B D E G
    case '3': return 0b11110010; // A B C D G
    case '4': return 0b10110110; // B C F G
    case '5': return 0b11100110; // A C D F G
    case '6': return 0b11101110; // A C D E F G
    case '7': return 0b00110010; // A B C
    case '8': return 0b11111110; // A B C D E F G
    case '9': return 0b11110110; // A B C D F G
    case 'A': case 'a': return 0b11110110; // A B C E F G
    case 'B': case 'b': return 0b11101110; // C D E F G
    case 'C': case 'c': return 0b11001100; // A D E F
    case 'D': case 'd': return 0b11111000; // B C D E G
    case 'E': case 'e': return 0b11001110; // A D E F G
    case 'F': case 'f': return 0b11000110; // A E F G
    case 'G': case 'g': return 0b11111100; // A C D E F
    case 'H': case 'h': return 0b10110110; // B C E F G
    case 'I': case 'i': return 0b00110000; // B C
    case 'J': case 'j': return 0b01111000; // B C D E
    case 'K': case 'k': return 0b10110110; // B C E F G (same as H)
    case 'L': case 'l': return 0b01001100; // D E F
    case 'M': case 'm': return 0b11110110; // A B C E F G (same as A)
    case 'N': case 'n': return 0b10110000; // B C E
    case 'O': case 'o': return 0b11111000; // C D E G
    case 'P': case 'p': return 0b11010110; // A B E F G
    case 'Q': case 'q': return 0b11110110; // A B C F G
    case 'R': case 'r': return 0b10010000; // E G
    case 'S': case 's': return 0b11100110; // A C D F G (same as 5)
    case 'T': case 't': return 0b01001110; // D E F G
    case 'U': case 'u': return 0b01111000; // B C D E F
    case 'V': case 'v': return 0b01111000; // B C D E F (same as U)
    case 'W': case 'w': return 0b01111110; // A B C D E F (same as 0)
    case 'X': case 'x': return 0b10110110; // B C E F G (same as H)
    case 'Y': case 'y': return 0b11110100; // B C D F G
    case 'Z': case 'z': return 0b11011010; // A B D E G (same as 2)
    case '-': return 0b00000010; // G
    case '_': return 0b00001000; // D
    case '=': return 0b00010010; // D G
    case ' ': return 0b00000000; // Blank
    default: return 0b00000000; // Blank for unknown characters
  }
}

inline bool isCodeBCompatible(char value) {
  // MAX7219 Code-B decode mode supports: 0-9, -, E, H, L, P, blank
  return (value >= '0' && value <= '9') || 
         value == '-' || 
         value == 'E' || value == 'H' || value == 'L' || value == 'P' || 
         value == ' ';
}

#endif // GLYPHS_H

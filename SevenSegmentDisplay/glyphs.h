#ifndef GLYPHS_H
#define GLYPHS_H

#include <stdint.h>

// MAX7219 no-decode mapping: bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G, bit7=DP
// This is the standard MAX7219 segment bit mapping

inline uint8_t charToSegment(char c) {
  switch (c) {
    // digits
    case '0': return 0x3F; // A B C D E F
    case '1': return 0x06; // B C
    case '2': return 0x5B; // A B D E G
    case '3': return 0x4F; // A B C D G
    case '4': return 0x66; // B C F G
    case '5': return 0x6D; // A C D F G
    case '6': return 0x7D; // A C D E F G
    case '7': return 0x07; // A B C
    case '8': return 0x7F; // A B C D E F G
    case '9': return 0x6F; // A B C D F G

    // letters (approx)
    case 'A': case 'a': return 0x77; // A B C E F G
    case 'B': case 'b': return 0x7C; // C D E F G
    case 'C': case 'c': return 0x39; // A D E F
    case 'D': case 'd': return 0x5E; // B C D E G
    case 'E': case 'e': return 0x79; // A D E F G
    case 'F': case 'f': return 0x71; // A E F G
    case 'G': case 'g': return 0x3D; // A C D E F
    case 'H': case 'h': return 0x76; // B C E F G
    case 'I': case 'i': return 0x06; // B C
    case 'J': case 'j': return 0x1E; // B C D E
    case 'K': case 'k': return 0x76; // B C E F G (same as H)
    case 'L': case 'l': return 0x38; // D E F
    case 'M': case 'm': return 0x77; // A B C E F G (same as A)
    case 'N': case 'n': return 0x54; // C E G (approx)
    case 'O': case 'o': return 0x5C; // C D E G
    case 'P': case 'p': return 0x73; // A B E F G
    case 'Q': case 'q': return 0x67; // A B C F G
    case 'R': case 'r': return 0x50; // E G
    case 'S': case 's': return 0x6D; // A C D F G (same as 5)
    case 'T': case 't': return 0x78; // D E F G
    case 'U': case 'u': return 0x3E; // B C D E F
    case 'V': case 'v': return 0x3E; // B C D E F (same as U)
    case 'W': case 'w': return 0x3F; // A B C D E F (same as 0)
    case 'X': case 'x': return 0x76; // B C E F G (same as H)
    case 'Y': case 'y': return 0x6E; // B C D F G
    case 'Z': case 'z': return 0x5B; // A B D E G (same as 2)
    case '-': return 0x40; // G
    case '_': return 0x08; // D
    case '=': return 0x48; // D G
    case ' ': return 0x00; // Blank
    case '.': return 0x80; // DP only
    default: return 0x00; // Blank for unknown characters
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

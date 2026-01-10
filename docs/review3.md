Alright — with this full code, we can *actually* pin it down. There are **two independent, concrete bugs** that together produce *exactly* what you’re seeing:

1. **Version/AP/Conn never show** (or show as blank)
2. **Time still works** and the blinking “colon” still works

That combination is the giveaway.

---

# What’s really happening

## 1) Your “version for 5 seconds” logic can’t work (as written)

You set:

```cpp
showingVersion = true;
versionStartTime = millis();
lc.setDigit(...); // draw 2.00
```

…but then **`setup()` keeps going** and immediately does one of these:

* Writes `"AP"` (AP mode)
* Writes `"Conn"` (STA mode)
* Writes `"Err"`

So the version gets overwritten **inside `setup()`**, long before `loop()` ever runs. The `showingVersion` guard in `loop()` never gets a chance to protect anything.

So even if the version display *was correct*, you still wouldn’t see it.

✅ Fix: if you want “show version before anything else touches the display”, you must either:

* **block inside `setup()`** for 5 seconds, *or*
* move Wi-Fi/AP decision into `loop()` via a boot state machine.

Given you want to “really figure it out,” I’ll give you the **smallest blocking fix** first (it proves the display path).

---

## 2) Your hybrid decode/raw driver is internally inconsistent

You configured the MAX7219 like this:

```cpp
writeRegister(0x09, 0x0F); // decode digits 0-3
```

That means **digits 0–3 are in Code-B decode mode**.

But then you do this for “Conn”:

```cpp
lc.setChar(0, 0, 'C', false);
lc.setChar(0, 1, 'o', false);
lc.setChar(0, 2, 'n', false);
lc.setChar(0, 3, 'n', false);
```

Inside `setChar()`, because `C/o/n` aren’t Code-B compatible, you fall into the “raw segments” path and write segment bitmasks to digit registers **while decode is ON for those digits**.

When decode is ON, the MAX7219 does **not** interpret your byte as segments. It interprets it as a Code-B character code (0–15). Your “raw” values become “undefined / blank-ish”.

That’s why:

* **Time works** (uses Code-B digits)
* **Version can work** (digits)
* **Conn/AP/Err/IP scrolling never shows** (raw bytes being fed into decode digits)

✅ Fix: whenever you want raw segment output on digits 0–3, you must set decode mode for those digits to **OFF** (mask bit 0).

---

## 3) Bonus: your raw segment bit order comment is wrong

Your comment says:

> DP G F E D C B A

But the MAX7219 no-decode bit mapping is:

> **DP G F E D C B A** *on many shift registers*…
> …but on the MAX7219 it’s actually **DP, then A..G (A is bit0, G is bit6)**.

If you “fix decode” but keep these raw constants as-is, letters still won’t look right.

So we’ll fix that too.

---

# The “deep dive” fix: make the driver *actually* support decode switching cleanly

## ✅ Patch 1 — fix the MAX7219 class (decode mask + correct raw encoding)

Replace your `SimpleMAX7219` class with this version (same name, minimal interface changes):

```cpp
class SimpleMAX7219 {
private:
  int csPin;
  uint8_t decodeMask = 0x00;  // bit0=digit0 ... bit7=digit7

  void writeRegister(uint8_t address, uint8_t value) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(csPin, LOW);
    SPI.transfer(address);
    SPI.transfer(value);
    digitalWrite(csPin, HIGH);
    SPI.endTransaction();
  }

  // MAX7219 no-decode mapping: bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G, bit7=DP
  uint8_t raw7seg(char c) {
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
      case 'C': case 'c': return 0x39; // A D E F
      case 'o':           return 0x5C; // C D E G
      case 'n':           return 0x54; // C E G (approx)
      case 'r':           return 0x50; // E G
      case 'E':           return 0x79; // A D E F G
      case 'P':           return 0x73; // A B E F G
      case 'H':           return 0x76; // B C E F G
      case 'L':           return 0x38; // D E F
      case '-':           return 0x40; // G

      case ' ':           return 0x00;
      case '.':           return 0x80; // DP only
      default:            return 0x00;
    }
  }

  bool isCodeBCompatible(char value) {
    return (value >= '0' && value <= '9') ||
           value == '-' || value == 'E' || value == 'H' ||
           value == 'L' || value == 'P' || value == ' ';
  }

  bool decodeEnabledForDigit(int digit) const {
    return (decodeMask & (1 << digit)) != 0;
  }

public:
  SimpleMAX7219(int cs) : csPin(cs) {}

  void setDecodeMode(uint8_t mask) {
    decodeMask = mask;
    writeRegister(0x09, decodeMask);
  }

  void begin() {
    pinMode(csPin, OUTPUT);
    digitalWrite(csPin, HIGH);
    delay(50);

    writeRegister(0x0C, 0x01); // normal operation
    delay(10);
    writeRegister(0x0B, 0x03); // scan limit: digits 0..3
    delay(10);

    // Default: decode ON for digits 0..3 (time/version)
    setDecodeMode(0x0F);
    delay(10);

    writeRegister(0x0A, 0x08); // intensity
    delay(10);

    clearDisplay(0);
    delay(20);
  }

  void clearDisplay(int device) {
    for (int i = 1; i <= 8; i++) {
      // If decode is enabled for that digit, blank=0x0F; else blank=0x00
      int digit = i - 1;
      writeRegister(i, decodeEnabledForDigit(digit) ? 0x0F : 0x00);
    }
  }

  void setDigit(int device, int digit, int value, bool dp) {
    if (digit < 0 || digit > 7) return;

    if (decodeEnabledForDigit(digit)) {
      uint8_t code = (uint8_t)(value & 0x0F);
      if (dp) code |= 0x80;
      writeRegister(digit + 1, code);
    } else {
      // raw digit
      char c = (char)('0' + (value % 10));
      uint8_t seg = raw7seg(c);
      if (dp) seg |= 0x80;
      writeRegister(digit + 1, seg);
    }
  }

  void setChar(int device, int digit, char value, bool dp) {
    if (digit < 0 || digit > 7) return;

    if (decodeEnabledForDigit(digit) && isCodeBCompatible(value)) {
      uint8_t code = 0x0F;
      if (value >= '0' && value <= '9') code = value - '0';
      else if (value == '-') code = 0x0A;
      else if (value == 'E') code = 0x0B;
      else if (value == 'H') code = 0x0C;
      else if (value == 'L') code = 0x0D;
      else if (value == 'P') code = 0x0E;
      else if (value == ' ') code = 0x0F;

      if (dp) code |= 0x80;
      writeRegister(digit + 1, code);
    } else {
      uint8_t seg = raw7seg(value);
      if (dp) seg |= 0x80;
      writeRegister(digit + 1, seg);
    }
  }
};
```

That fixes:

* decode/raw conflict
* raw bit order
* raw digits and dot

---

# The second fix: stop overwriting the version inside `setup()`

If you want the version guaranteed visible, you must enforce it before any other display writes.

The simplest proof (do this first):

Right after you write `2.00` in `setup()` add:

```cpp
lc.setDecodeMode(0x0F);  // digits use decode for version
delay(5000);
```

Then continue.

That will prove you can show version reliably.

Later, if you want it non-blocking, we can convert to a boot state machine — but let’s get it working first.

---

# Third fix: whenever you show text or IP, switch decode OFF

Because you want `Conn`, `AP`, `Err`, IP dots, etc. Use raw mode:

### Before writing “Conn”, “AP”, “Err”, IP scroll:

```cpp
lc.setDecodeMode(0x00);   // raw segments on all digits
```

### Before showing the time (digits):

```cpp
lc.setDecodeMode(0x0F);   // decode digits 0..3
```

You don’t need to spam this every call — only when mode changes — but even if you do, it’ll still work.

---

# Why this matches your exact symptom set

* Time works → decode mode for digits is correct and wiring is fine
* Colon dot flashes → DP bit works
* Version doesn’t show → overwritten immediately inside setup
* “Conn/AP/Err” don’t show → you were writing raw bytes into decode digits

This is not theoretical — it’s mechanically what the MAX7219 does.

---

# A fast “hard proof” test (60 seconds)

Add this right after `lc.begin()` in `setup()`:

```cpp
lc.setDecodeMode(0x00);   // raw
lc.clearDisplay(0);

// show 8888 raw (all segments, no decode)
lc.setChar(0, 0, '8', false);
lc.setChar(0, 1, '8', false);
lc.setChar(0, 2, '8', false);
lc.setChar(0, 3, '8', false);

delay(2000);
```

If you see **8888**, your raw mode is now correct.
Then text will work too.

---

If you apply the class replacement + the two decode mode switches + the setup delay, you’ll see:

1. `2.00` for 5 seconds
2. then `Conn` (or `AP`)
3. then IP scroll (with dots)
4. then time

If anything still doesn’t show after that, we’ve moved from “logic bug” into “hardware or pin conflict” territory — but right now your code has two very clear correctness problems that prevent those boot/status displays from ever being visible.

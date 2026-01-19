# You Got a Clock!

Hey! If you're reading this, I gave you one of my prototype NTP clocks. Welcome to the club of "Ian had to buy five of these."

## What You Have

It's a WiFi-connected clock that syncs time from the internet. Plug it into USB-C power, give it your WiFi credentials, and it'll show you the time. Forever. Accurately. That's it.

**The thermal probe port on the side? Ignore it.** The sensor chip on the board is broken on all of these. I designed it for temperature monitoring but the hardware doesn't work. The clock part works great though, so here we are.

## First Time Setup

1. **Plug it in** via USB-C (any phone charger works)
2. **Wait for "----"** on the display - that means it's ready for WiFi setup
3. **Go to the web installer:** [mcyork.github.io/ntp_clock](https://mcyork.github.io/ntp_clock)
4. **Click "Install"** and follow the prompts
5. **Enter your WiFi credentials** when asked
6. It'll connect, sync time, and show you its IP address twice
7. Done. It's a clock now.

If you ever move it to a different WiFi network, just go through the installer again.

## The Buttons

| Button | Short Press | Long Press (2 sec) |
|--------|-------------|-------------------|
| **MODE** | Toggle 12/24 hour | Show IP address |
| **UP** | Brightness up | — |
| **DOWN** | Brightness down | — |
| **UP + DOWN** | Force WiFi reconnect | — |

## If Your WiFi Goes Down

Don't panic. The clock will keep trying to reconnect for about 30 minutes (with increasing delays between attempts). If your router reboots overnight, it'll reconnect automatically when it comes back.

If it's been down too long and shows "AP" mode, just power cycle it - your WiFi credentials are saved.

## Factory Reset

If you want to wipe everything and start fresh:
1. Connect to the clock's WiFi hotspot (it'll be named `NTP_Clock_XXXXXX`)
2. Go to `192.168.4.1` in your browser
3. Click "Factory Reset"

Or just give it back to me and I'll fix it.

## Technical Bits (If You Care)

- **Hardware:** ESP32-S3-MINI-1, MAX7219 7-segment display
- **Firmware:** Open source, in this repo
- **Time source:** pool.ntp.org
- **Timezone:** Auto-detected from your IP on first connection (or set manually via web UI)

The display is actually 8 digits but only 4 are populated. The decimal point blinks like a colon to show seconds.

## Problems?

Text me. Or if you're feeling fancy, [open an issue](https://github.com/mcyork/ntp_clock/issues).

---

*Built with too much coffee and not enough sleep. Enjoy your clock.*

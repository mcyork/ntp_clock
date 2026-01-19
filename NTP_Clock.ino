/*
 * NTP Clock - Firmware v3.0
 * Hardware: ESP32-S3-MINI-1
 *
 * Features:
 * - WiFi connection to NTP server
 * - Displays time as HHMM on 7-segment display
 * - Decimal point on 100s digit flashes like a colon (seconds indicator)
 * - AP mode web interface for WiFi and preferences configuration
 * - Timezone configuration via Preferences with automatic DST handling
 * - Version display at boot
 * - IP address scrolling after connection
 * - Improv WiFi provisioning via ESP Web Tools
 *
 * v3.0 Changes:
 * - Complete state machine rewrite for reliability
 * - WiFi reconnection with exponential backoff (5s → 5min)
 * - Graceful recovery from temporary WiFi outages (~30 min before AP fallback)
 * - Fixed AP→STA transition bug (NTP sync was never retried)
 * - Enhanced buttons:
 *   - Long-press Mode (2s): Show IP address
 *   - Up+Down together: Force WiFi reconnect
 * - Serial logging with [STATE], [WIFI], [NTP], [BTN] prefixes
 */

 #define FIRMWARE_VERSION "3.01"

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SPI.h>
#include <Preferences.h>
#include <string.h>
#include <ImprovWiFiLibrary.h>
#include "SevenSegmentDisplay/MAX7219Display.h"
#include "SevenSegmentDisplay/MAX7219Display.cpp"
#include "web_pages.h"

// =============================================================================
// ESP32-S3 USB CDC WORKAROUND
// ESP32-S3 has a bug where Serial.available() doesn't reliably update.
// This buffered wrapper uses event callbacks to capture incoming data.
// =============================================================================
class BufferedHWCDC : public Stream {
private:
  uint8_t buffer[256];
  volatile uint16_t head = 0;
  volatile uint16_t tail = 0;
  volatile uint16_t count = 0;

public:
  void feedByte(uint8_t byte) {
    if (count < sizeof(buffer)) {
      buffer[tail] = byte;
      tail = (tail + 1) % sizeof(buffer);
      count++;
    }
  }

  virtual int available() override {
    return count;
  }

  virtual int read() override {
    if (count == 0) return -1;
    uint8_t byte = buffer[head];
    head = (head + 1) % sizeof(buffer);
    count--;
    return byte;
  }

  virtual int peek() override {
    if (count == 0) return -1;
    return buffer[head];
  }

  virtual void flush() override {
    // Nothing to flush for input buffer
  }

  virtual size_t write(uint8_t byte) override {
    return Serial.write(byte);
  }

  virtual size_t write(const uint8_t *buffer, size_t size) override {
    return Serial.write(buffer, size);
  }

  virtual int availableForWrite() override {
    return Serial.availableForWrite();
  }
};

BufferedHWCDC bufferedSerial;

// Event callback for ESP32-S3 USB CDC RX events
// This is called by Serial.onEvent() when data arrives
static void hwcdcEventCallback(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data) {
  if (event_base == ARDUINO_HW_CDC_EVENTS && event_id == ARDUINO_HW_CDC_RX_EVENT) {
    // Read all available bytes from Serial and feed to buffer
    int count = 0;
    while (Serial.available()) {
      bufferedSerial.feedByte(Serial.read());
      count++;
    }
    if (count > 0) {
      Serial.printf("[EVENT] Fed %d bytes to buffer\n", count);
      Serial.flush();
    }
  }
}
 
 // --- PIN DEFINITIONS ---
 const int PIN_BTN_MODE = 7; 
 const int PIN_BTN_UP   = 6; 
 const int PIN_BTN_DOWN = 5; 
 const int PIN_BUZZER   = 4; 
 
 // SPI Bus
 const int PIN_SPI_SCK  = 16;
 const int PIN_SPI_MOSI = 17;
 const int PIN_SPI_MISO = 18;
 
// Chip Selects
const int PIN_CS_DISP  = 11;
 
 // --- AP MODE CONFIGURATION ---
 String apSSID = "";  // Will be set dynamically using MAC address
 const char* AP_PASSWORD = ""; // Open AP
 
 // --- NTP CONFIGURATION ---
 const char* ntpServer = "pool.ntp.org";
 long  gmtOffset_sec = 0;  // Will be loaded from Preferences
 int   daylightOffset_sec = 3600;  // Default 1 hour for DST
 
// --- OBJECTS ---
MAX7219Display display(PIN_CS_DISP);
Preferences preferences;
WebServer server(80);
// Use buffered wrapper instead of Serial directly to work around ESP32-S3 USB CDC bug
ImprovWiFi improvSerial(&bufferedSerial);
 
// --- STATE MACHINE ---
enum ClockState {
  STATE_BOOT,              // Initial boot, showing version
  STATE_IMPROV_WAIT,       // Grace period for Improv provisioning
  STATE_WIFI_CONNECTING,   // Attempting WiFi connection
  STATE_NTP_SYNCING,       // WiFi connected, syncing time
  STATE_RUNNING,           // Normal operation - displaying time
  STATE_WIFI_LOST,         // WiFi dropped, attempting reconnect
  STATE_SHOWING_IP,        // Displaying IP address after connection
  STATE_AP_MODE            // Fallback configuration mode
};

ClockState currentState = STATE_BOOT;
ClockState previousState = STATE_BOOT;

// --- STATE VARIABLES ---
unsigned long stateEnteredAt = 0;     // When we entered current state
unsigned long lastTimeUpdate = 0;
int displayBrightness = 8;            // 0-15, default medium
bool use24Hour = true;                // 24-hour format (true) or 12-hour format (false)
bool improvConnected = false;         // Flag set when Improv WiFi successfully connects

// Reconnection state (for STATE_WIFI_LOST)
unsigned long lastReconnectAttempt = 0;
unsigned long reconnectInterval = 5000;           // Start at 5 seconds
const unsigned long MAX_RECONNECT_INTERVAL = 300000;  // Max 5 minutes between attempts
int reconnectAttempts = 0;
const int MAX_RECONNECT_BEFORE_AP = 36;           // ~30 min before AP fallback (with backoff)

// IP display state
bool ipScrollingStarted = false;      // Single flag, not scattered statics
unsigned long ipScrollDuration = 0;   // Calculated duration for exact number of scroll cycles

// Beep state variables (using LEDC instead of tone() to avoid timer conflicts)
unsigned long beepEndTime = 0;
bool beepActive = false;
 
 // Button state tracking
 unsigned long lastButtonPress = 0;
 bool lastModeState = false;
 bool lastUpState = false;
 bool lastDownState = false;
 
// Forward declarations
void handleButtons();
void handleRoot();
void handleConfig();
void handleSave();
void handleFactoryReset();
void startBeep(int frequency, int duration);
void updateBeep();
void beepBlocking(int frequency, int duration);
bool detectTimezoneFromIP();
void changeState(ClockState newState);
bool attemptNtpSync();
void startWifiReconnect();
const char* stateToString(ClockState state);

// =============================================================================
// STATE MACHINE HELPERS
// =============================================================================

void changeState(ClockState newState) {
  if (newState == currentState) return;

  Serial.printf("[STATE] %s -> %s\n", stateToString(currentState), stateToString(newState));
  Serial.flush();

  previousState = currentState;
  currentState = newState;
  stateEnteredAt = millis();

  // Reset state-specific variables on entry
  switch (newState) {
    case STATE_SHOWING_IP:
      ipScrollingStarted = false;
      break;
    case STATE_WIFI_LOST:
      reconnectAttempts = 0;
      reconnectInterval = 5000;
      lastReconnectAttempt = 0;
      break;
    case STATE_AP_MODE:
      ipScrollingStarted = false;
      break;
    default:
      break;
  }
}

const char* stateToString(ClockState state) {
  switch (state) {
    case STATE_BOOT: return "BOOT";
    case STATE_IMPROV_WAIT: return "IMPROV_WAIT";
    case STATE_WIFI_CONNECTING: return "WIFI_CONNECTING";
    case STATE_NTP_SYNCING: return "NTP_SYNCING";
    case STATE_RUNNING: return "RUNNING";
    case STATE_WIFI_LOST: return "WIFI_LOST";
    case STATE_SHOWING_IP: return "SHOWING_IP";
    case STATE_AP_MODE: return "AP_MODE";
    default: return "UNKNOWN";
  }
}

bool attemptNtpSync() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(500);  // Give NTP a moment

  struct tm timeinfo;
  for (int i = 0; i < 5; i++) {  // Try a few times
    if (getLocalTime(&timeinfo, 1000)) {
      Serial.println("[NTP] Time synced successfully");
      return true;
    }
    delay(200);
  }
  Serial.println("[NTP] Sync failed");
  return false;
}

void startWifiReconnect() {
  Serial.println("[WIFI] Starting reconnection...");
  WiFi.disconnect();
  delay(100);

  preferences.begin("wifi_config", true);  // Read-only
  String savedSSID = preferences.getString("ssid", "");
  String savedPassword = preferences.getString("password", "");
  preferences.end();

  if (savedSSID.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  }
}

// =============================================================================
// IMPROV WIFI CALLBACKS - These are REQUIRED for Improv to work!
// =============================================================================

// This function is called by the Improv library to actually connect to WiFi
// WITHOUT THIS, Improv receives credentials but doesn't know how to use them!
bool onImprovWiFiConnect(const char* ssid, const char* password) {
  Serial.printf("Improv: Attempting to connect to SSID: %s\n", ssid);
  Serial.flush();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  // Wait for connection (up to 10 seconds)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Improv: Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.flush();
    improvConnected = true;
    return true;
  } else {
    Serial.println("Improv: Connection failed");
    Serial.flush();
    return false;
  }
}

// This callback is called AFTER successful WiFi connection via Improv
// Use this to save credentials and do post-connection setup
void onImprovWiFiConnected(const char* ssid, const char* password) {
  Serial.printf("Improv: Saving credentials for SSID: %s\n", ssid);
  
  // Save WiFi credentials
  preferences.begin("wifi_config", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
  
  // Try to auto-detect timezone from IP geolocation if not already configured
  preferences.begin("ntp_clock", false);
  long savedTimezone = preferences.getLong("timezone", 0);
  preferences.end();
  
  // Only auto-detect if timezone hasn't been manually configured (default is 0)
  if (savedTimezone == 0) {
    detectTimezoneFromIP();
  }
}

// =============================================================================
// SETUP - Simplified with state machine
// =============================================================================

void setup() {
  // Init Serial IMMEDIATELY - ESP32-S3 USB CDC needs this early
  Serial.begin(115200);

  // CRITICAL: Wait for USB CDC to enumerate on ESP32-S3
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 2000) {
    delay(10);
  }
  delay(100);

  // CRITICAL: Register event callback for ESP32-S3 USB CDC workaround
  Serial.onEvent(hwcdcEventCallback);

  Serial.println("\n\n=== NTP Clock v" FIRMWARE_VERSION " Starting ===");
  Serial.println("State machine architecture");
  Serial.flush();

  // Initialize WiFi in STA mode early - Improv needs this
  WiFi.mode(WIFI_STA);
  delay(100);

  // Generate unique AP SSID using MAC address
  String macAddress = WiFi.macAddress();
  macAddress.replace(":", "");
  String macSuffix = macAddress.substring(macAddress.length() - 6);
  apSSID = "NTP_Clock_" + macSuffix;

  // ==========================================================================
  // IMPROV WIFI SETUP - Must happen BEFORE any blocking operations
  // ==========================================================================
  Serial.println("Setting up Improv WiFi...");

  improvSerial.setDeviceInfo(
    ImprovTypes::ChipFamily::CF_ESP32_S3,
    "NTP-Clock",
    FIRMWARE_VERSION,
    "NTP Clock"
  );

  improvSerial.setCustomConnectWiFi(onImprovWiFiConnect);
  improvSerial.onImprovConnected(onImprovWiFiConnected);

  Serial.println("Improv WiFi ready");
  Serial.flush();

  // ==========================================================================
  // HARDWARE INITIALIZATION
  // ==========================================================================

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_CS_DISP, OUTPUT);
  digitalWrite(PIN_CS_DISP, HIGH);

  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_BTN_UP,   INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  digitalWrite(PIN_BUZZER, LOW);

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
  delay(100);

  display.begin();
  delay(200);

  // Load preferences
  preferences.begin("ntp_clock", false);
  displayBrightness = preferences.getInt("brightness", 8);
  use24Hour = preferences.getBool("24hour", true);
  gmtOffset_sec = preferences.getLong("timezone", -28800);
  daylightOffset_sec = preferences.getInt("dst_offset", 0);
  preferences.end();

  display.setBrightness(displayBrightness);

  // ==========================================================================
  // SETUP WEB SERVER (always, for both AP and STA modes)
  // ==========================================================================
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);
  server.begin();

  // ==========================================================================
  // START STATE MACHINE - Show version first
  // ==========================================================================
  currentState = STATE_BOOT;
  stateEnteredAt = millis();
  display.displayText(FIRMWARE_VERSION, true);

  Serial.println("Setup complete - entering state machine");
  Serial.flush();
}

// =============================================================================
// LOOP - Clean state machine
// =============================================================================

void loop() {
  unsigned long now = millis();
  unsigned long stateElapsed = now - stateEnteredAt;

  // ==========================================================================
  // ALWAYS: Poll Serial for USB CDC workaround + handle Improv
  // ==========================================================================
  if (Serial.available() > 0) {
    while (Serial.available()) {
      bufferedSerial.feedByte(Serial.read());
    }
  }

  // CRITICAL: Always process Improv - allows provisioning in ANY state
  improvSerial.handleSerial();

  // Check if Improv just connected us (can happen in any state)
  if (improvConnected && WiFi.status() == WL_CONNECTED &&
      currentState != STATE_NTP_SYNCING &&
      currentState != STATE_SHOWING_IP &&
      currentState != STATE_RUNNING) {
    Serial.println("[IMPROV] Connected! Transitioning to NTP sync...");

    // Clean up AP mode if we were in it
    if (currentState == STATE_AP_MODE) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }

    beepBlocking(2000, 100);
    changeState(STATE_NTP_SYNCING);
  }

  // ==========================================================================
  // ALWAYS: Update beep and display
  // ==========================================================================
  updateBeep();
  display.update();
  server.handleClient();
  handleButtons();

  // ==========================================================================
  // STATE MACHINE
  // ==========================================================================
  switch (currentState) {

    // -------------------------------------------------------------------------
    // STATE_BOOT: Show version for 3 seconds, then decide next state
    // -------------------------------------------------------------------------
    case STATE_BOOT:
      if (stateElapsed >= 3000) {
        display.clear();
        changeState(STATE_IMPROV_WAIT);
      }
      break;

    // -------------------------------------------------------------------------
    // STATE_IMPROV_WAIT: Wait for Improv provisioning (10 seconds)
    // -------------------------------------------------------------------------
    case STATE_IMPROV_WAIT:
      // Show waiting indicator
      if (stateElapsed < 500) {
        display.displayText("----");
      }

      // If Improv connected us, we'll catch it above
      if (WiFi.status() == WL_CONNECTED) {
        changeState(STATE_NTP_SYNCING);
        break;
      }

      // After 10 seconds, try saved credentials
      if (stateElapsed >= 10000) {
        preferences.begin("wifi_config", true);
        String savedSSID = preferences.getString("ssid", "");
        preferences.end();

        if (savedSSID.length() > 0) {
          display.displayText("Conn");
          changeState(STATE_WIFI_CONNECTING);
          startWifiReconnect();
        } else {
          // No saved credentials, go to AP mode
          changeState(STATE_AP_MODE);
        }
      }
      break;

    // -------------------------------------------------------------------------
    // STATE_WIFI_CONNECTING: Trying to connect with saved credentials
    // -------------------------------------------------------------------------
    case STATE_WIFI_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        beepBlocking(2000, 100);

        // Auto-detect timezone if not configured
        preferences.begin("ntp_clock", true);
        long savedTimezone = preferences.getLong("timezone", 0);
        preferences.end();

        if (savedTimezone == 0) {
          detectTimezoneFromIP();
          preferences.begin("ntp_clock", true);
          gmtOffset_sec = preferences.getLong("timezone", -28800);
          daylightOffset_sec = preferences.getInt("dst_offset", 0);
          preferences.end();
        }

        changeState(STATE_NTP_SYNCING);
      }
      else if (stateElapsed >= 20000) {
        // 20 seconds to connect - longer than before for slow routers
        Serial.println("[WIFI] Connection timeout, entering AP mode");
        changeState(STATE_AP_MODE);
      }
      break;

    // -------------------------------------------------------------------------
    // STATE_NTP_SYNCING: WiFi connected, sync time
    // -------------------------------------------------------------------------
    case STATE_NTP_SYNCING:
      display.displayText("Sync");

      if (attemptNtpSync()) {
        beepBlocking(2500, 50);
        delay(100);
        beepBlocking(3000, 50);
        changeState(STATE_SHOWING_IP);
      }
      else if (stateElapsed >= 10000) {
        // NTP failed but WiFi is up - show IP anyway, retry later
        Serial.println("[NTP] Sync failed, will retry in RUNNING state");
        changeState(STATE_SHOWING_IP);
      }
      break;

    // -------------------------------------------------------------------------
    // STATE_SHOWING_IP: Display IP address exactly 2 complete scroll cycles
    // -------------------------------------------------------------------------
    case STATE_SHOWING_IP:
      if (!ipScrollingStarted) {
        IPAddress ip = WiFi.localIP();
        char ipStr[16];
        sprintf(ipStr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.startScrolling(ipStr, 350);
        ipScrollingStarted = true;

        // Calculate exact duration for 2 complete scroll cycles
        // Scroll goes from position 0 to (len-4), then resets = (len-3) steps per cycle
        // Each step takes 350ms
        int len = strlen(ipStr);
        int stepsPerCycle = (len > 4) ? (len - 3) : 1;
        ipScrollDuration = (unsigned long)stepsPerCycle * 350 * 2;  // 2 cycles

        Serial.printf("[IP] Showing %s for %lu ms (2 cycles)\n", ipStr, ipScrollDuration);
      }

      if (stateElapsed >= ipScrollDuration) {
        display.clear();
        changeState(STATE_RUNNING);
      }
      break;

    // -------------------------------------------------------------------------
    // STATE_RUNNING: Normal clock operation
    // -------------------------------------------------------------------------
    case STATE_RUNNING:
      // CHECK FOR WIFI LOSS - The critical missing piece!
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Connection lost! Starting reconnect...");
        display.displayText("rCon");  // Brief "reconnecting" indicator
        delay(500);
        changeState(STATE_WIFI_LOST);
        break;
      }

      // Update time display every second
      if (now - lastTimeUpdate >= 1000) {
        lastTimeUpdate = now;

        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
          int hours = timeinfo.tm_hour;
          int minutes = timeinfo.tm_min;
          int seconds = timeinfo.tm_sec;
          bool showColon = (seconds % 2 == 0);
          display.displayTime(hours, minutes, showColon, !use24Hour);
        } else {
          // NTP lost but WiFi still up - try to resync
          Serial.println("[NTP] Time sync lost, reconfiguring...");
          configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
          display.displayText("Sync");
        }
      }
      break;

    // -------------------------------------------------------------------------
    // STATE_WIFI_LOST: Reconnecting with exponential backoff
    // -------------------------------------------------------------------------
    case STATE_WIFI_LOST:
      // Check if reconnected
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Reconnected!");
        beepBlocking(2000, 100);

        // Re-sync NTP after reconnection
        if (attemptNtpSync()) {
          beepBlocking(2500, 50);
        }

        changeState(STATE_RUNNING);
        break;
      }

      // Attempt reconnection with backoff
      if (now - lastReconnectAttempt >= reconnectInterval) {
        lastReconnectAttempt = now;
        reconnectAttempts++;

        Serial.printf("[WIFI] Reconnect attempt %d (interval: %lums)\n",
                      reconnectAttempts, reconnectInterval);

        // Show attempt count briefly
        char attemptStr[5];
        sprintf(attemptStr, "r%3d", min(reconnectAttempts, 999));
        display.displayText(attemptStr);

        startWifiReconnect();

        // Exponential backoff: 5s, 10s, 20s, 40s, 80s, 160s, 300s (max)
        reconnectInterval = min(reconnectInterval * 2, MAX_RECONNECT_INTERVAL);
      }

      // After many attempts (~30 min with backoff), fall back to AP mode
      if (reconnectAttempts >= MAX_RECONNECT_BEFORE_AP) {
        Serial.println("[WIFI] Max reconnect attempts, entering AP mode");
        changeState(STATE_AP_MODE);
      }
      break;

    // -------------------------------------------------------------------------
    // STATE_AP_MODE: Configuration portal
    // -------------------------------------------------------------------------
    case STATE_AP_MODE:
      // One-time AP setup on entry
      if (previousState != STATE_AP_MODE) {
        Serial.printf("[AP] Starting AP: %s\n", apSSID.c_str());
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apSSID.c_str(), AP_PASSWORD);
        delay(500);
        beepBlocking(1500, 200);
        previousState = STATE_AP_MODE;  // Mark as initialized
      }

      // Check if WiFi connected (via Improv or web config)
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[AP] WiFi connected, transitioning out of AP mode");
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        beepBlocking(2000, 100);
        delay(100);
        beepBlocking(3000, 100);
        changeState(STATE_NTP_SYNCING);
        break;
      }

      // Scroll AP IP address
      if (!ipScrollingStarted) {
        String apIP = WiFi.softAPIP().toString();
        display.startScrolling(apIP.c_str(), 350);
        ipScrollingStarted = true;
      }
      break;
  }

  delay(10);
}

// =============================================================================
// BUTTON HANDLERS
// =============================================================================

// Long-press tracking
static unsigned long modePressStart = 0;
static bool modeLongPressHandled = false;

void handleButtons() {
  bool btnMode = (digitalRead(PIN_BTN_MODE) == LOW);
  bool btnUp = (digitalRead(PIN_BTN_UP) == LOW);
  bool btnDown = (digitalRead(PIN_BTN_DOWN) == LOW);
  unsigned long now = millis();

  // =========================================================================
  // UP + DOWN together: Force WiFi reconnect (useful if stuck)
  // =========================================================================
  if (btnUp && btnDown && (now - lastButtonPress > 500)) {
    Serial.println("[BTN] Up+Down: Force reconnect");
    startBeep(1500, 100);
    delay(100);
    startBeep(2000, 100);

    if (currentState == STATE_RUNNING || currentState == STATE_WIFI_LOST) {
      // Force a fresh reconnection attempt
      changeState(STATE_WIFI_LOST);
      reconnectAttempts = 0;
      reconnectInterval = 5000;
      lastReconnectAttempt = 0;
      startWifiReconnect();
    }
    lastButtonPress = now;
    return;  // Don't process individual buttons this cycle
  }

  // =========================================================================
  // MODE button: Short press = 12/24h toggle, Long press (2s) = Show IP
  // =========================================================================
  if (btnMode) {
    if (!lastModeState) {
      // Button just pressed
      modePressStart = now;
      modeLongPressHandled = false;
    }
    else if (!modeLongPressHandled && (now - modePressStart >= 2000)) {
      // Long press detected (2 seconds)
      modeLongPressHandled = true;
      startBeep(2500, 100);

      // Show IP address for exactly 2 scroll cycles
      if (currentState == STATE_RUNNING && WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        char ipStr[16];
        sprintf(ipStr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.startScrolling(ipStr, 350);

        // Calculate exact duration for 2 complete scroll cycles
        int len = strlen(ipStr);
        int stepsPerCycle = (len > 4) ? (len - 3) : 1;
        unsigned long scrollDuration = (unsigned long)stepsPerCycle * 350 * 2;

        Serial.printf("[BTN] Long press: Showing IP %s for %lu ms\n", ipStr, scrollDuration);

        unsigned long scrollStart = millis();
        while (millis() - scrollStart < scrollDuration) {
          display.update();
          delay(10);
        }
        display.clear();
        lastTimeUpdate = 0;  // Force immediate time update
      }
      else if (currentState == STATE_AP_MODE) {
        // In AP mode, show the SSID instead
        const char* ssid = apSSID.c_str();
        display.startScrolling(ssid, 350);

        int len = strlen(ssid);
        int stepsPerCycle = (len > 4) ? (len - 3) : 1;
        unsigned long scrollDuration = (unsigned long)stepsPerCycle * 350 * 2;

        Serial.printf("[BTN] Long press: Showing SSID %s for %lu ms\n", ssid, scrollDuration);

        unsigned long scrollStart = millis();
        while (millis() - scrollStart < scrollDuration) {
          display.update();
          server.handleClient();  // Keep serving in AP mode
          delay(10);
        }
        ipScrollingStarted = false;  // Reset to show IP again
      }
      lastButtonPress = now;
    }
  }
  else if (lastModeState && !modeLongPressHandled) {
    // Button released without long press - do short press action
    if (now - lastButtonPress > 200) {
      use24Hour = !use24Hour;
      preferences.begin("ntp_clock", false);
      preferences.putBool("24hour", use24Hour);
      preferences.end();
      startBeep(2000, 50);
      Serial.printf("[BTN] Mode: %s hour format\n", use24Hour ? "24" : "12");
      lastButtonPress = now;
      lastTimeUpdate = 0;  // Force immediate refresh
    }
  }
  lastModeState = btnMode;

  // =========================================================================
  // UP button: Increase brightness
  // =========================================================================
  if (btnUp && !btnDown && !lastUpState && (now - lastButtonPress > 200)) {
    if (displayBrightness < 15) {
      displayBrightness++;
      display.setBrightness(displayBrightness);
      preferences.begin("ntp_clock", false);
      preferences.putInt("brightness", displayBrightness);
      preferences.end();
      startBeep(1500, 30);
      Serial.printf("[BTN] Brightness: %d\n", displayBrightness);
    }
    lastButtonPress = now;
  }
  lastUpState = btnUp;

  // =========================================================================
  // DOWN button: Decrease brightness
  // =========================================================================
  if (btnDown && !btnUp && !lastDownState && (now - lastButtonPress > 200)) {
    if (displayBrightness > 0) {
      displayBrightness--;
      display.setBrightness(displayBrightness);
      preferences.begin("ntp_clock", false);
      preferences.putInt("brightness", displayBrightness);
      preferences.end();
      startBeep(1000, 30);
      Serial.printf("[BTN] Brightness: %d\n", displayBrightness);
    }
    lastButtonPress = now;
  }
  lastDownState = btnDown;
}

// =============================================================================
// WEB SERVER HANDLERS
// =============================================================================

void handleRoot() {
  server.send(200, "text/html", getConfigPageHTML(preferences));
}

void handleConfig() {
  server.send(200, "text/html", getConfigPageHTML(preferences));
}

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String timezoneStr = server.arg("timezone");
  String dstOffsetStr = server.arg("dst_offset");
  String brightnessStr = server.arg("brightness");
  String hourFormatStr = server.arg("hour_format");
  
  preferences.begin("wifi_config", false);
  preferences.putString("ssid", ssid);
  if (password.length() > 0) {
    preferences.putString("password", password);
  }
  preferences.end();
  
  long timezoneOffset = timezoneStr.toInt();
  int dstOffset = dstOffsetStr.toInt();
  preferences.begin("ntp_clock", false);
  preferences.putLong("timezone", timezoneOffset);
  preferences.putInt("dst_offset", dstOffset);
  
  if (brightnessStr.length() > 0) {
    int brightness = brightnessStr.toInt();
    if (brightness >= 0 && brightness <= 15) {
      preferences.putInt("brightness", brightness);
      displayBrightness = brightness;
      display.setBrightness(displayBrightness);
    }
  }
  
  if (hourFormatStr.length() > 0) {
    preferences.putBool("24hour", hourFormatStr == "24");
    use24Hour = (hourFormatStr == "24");
  }
  
  preferences.end();
  
  server.send(200, "text/html", getSaveSuccessPageHTML());
  delay(1000);
  ESP.restart();
}

void handleFactoryReset() {
  preferences.begin("wifi_config", false);
  preferences.clear();
  preferences.end();
  
  preferences.begin("ntp_clock", false);
  preferences.clear();
  preferences.end();
  
  server.send(200, "text/html", getFactoryResetPageHTML());
  delay(1000);
  ESP.restart();
}

// =============================================================================
// BEEP FUNCTIONS
// =============================================================================

void startBeep(int frequency, int duration) {
  ledcAttach(PIN_BUZZER, frequency, 8);
  ledcWrite(PIN_BUZZER, 128);
  beepEndTime = millis() + duration;
  beepActive = true;
}

void updateBeep() {
  if (beepActive && millis() >= beepEndTime) {
    ledcWrite(PIN_BUZZER, 0);
    ledcDetach(PIN_BUZZER);
    beepActive = false;
  }
}

void beepBlocking(int frequency, int duration) {
  ledcAttach(PIN_BUZZER, frequency, 8);
  ledcWrite(PIN_BUZZER, 128);
  delay(duration);
  ledcWrite(PIN_BUZZER, 0);
  ledcDetach(PIN_BUZZER);
}

// =============================================================================
// TIMEZONE DETECTION
// =============================================================================

bool detectTimezoneFromIP() {
  HTTPClient http;
  http.begin("http://ip-api.com/json/?fields=status,offset");
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error && doc["status"] == "success" && doc.containsKey("offset")) {
      long offset = doc["offset"].as<long>();
      
      preferences.begin("ntp_clock", false);
      preferences.putLong("timezone", offset);
      preferences.putInt("dst_offset", 0);
      preferences.end();
      
      gmtOffset_sec = offset;
      daylightOffset_sec = 0;
      
      http.end();
      return true;
    }
  }
  
  http.end();
  return false;
}

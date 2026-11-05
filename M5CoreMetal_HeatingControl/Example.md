/*
  ****************************************************
  * Technics & Applications
  * Antifreeze module for PCGO-TA
  * Platform: M5Stack Core Basic (Metal)
  * ### FW Version 3.0 ###
  * 
  * Code: Raf Olaerts
  *
  * Hardware (Supplier: TinyTronics):
  *   - M5Stack M5Core Metal – ESP32 IoT Development Kit (Ref: 005320)
  *   - M5Stack 4-Channel Relay 13.2 Module V1.1 (Ref: 005642)
  *   - DFRobot CHT8305 I2C Temperature and Humidity Sensor (Ref: 004608)
  ****************************************************
*/


#include <M5Stack.h>
#include <Wire.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <ArduinoJson.h>  // ArduinoJson v7.x - must be included early

// Undefine min/max macros to avoid conflicts with C++ standard library
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <WebServer.h>
#include <PubSubClient.h>
#include <algorithm>

// ArduinoJson v7 uses JsonDocument (not DynamicJsonDocument)
// All types are in the global namespace after #include <ArduinoJson.h>
#include "CHT8305.h"  // Voeg de CHT8305 bibliotheek toe
#include "Module_4RELAY.h"

// Free Fonts voor betere leesbaarheid - gebruik TFT_eSPI Free Fonts
// De fonts zijn al geïncludeerd via M5Stack.h, dus we kunnen ze direct gebruiken
// Definieer font constanten voor gemakkelijker gebruik
#define FF1 &FreeSans9pt7b      // Kleine tekst (was setTextSize(1))
#define FF12 &FreeSans12pt7b     // Normale tekst (was setTextSize(2))
#define FF18 &FreeSans18pt7b     // Grote tekst (was setTextSize(3))
#define FF12B &FreeSansBold12pt7b // Normale tekst bold (voor headers)
#define FF18B &FreeSansBold18pt7b // Grote tekst bold (voor waarden)
// Voor zeer kleine labels, gebruik TomThumb (6 pixels hoog - veel kleiner dan FreeSans9pt7b)
#define FFSMALL &TomThumb  // Zeer kleine tekst voor labels

// Constants
#define MAX_TRIGGERS 8
#define EEPROM_SIZE 1024
#define TRIGGER_SIZE 32  // Size of one trigger in EEPROM
#define TRIGGER_START_ADDR 0

// EEPROM Addresses for new configuration sections
#define WIFI_CONFIG_START_ADDR 256
#define WIFI_CONFIG_SIZE 132  // WiFiConfig is 129 bytes, use 132 for safety
#define MQTT_CONFIG_START_ADDR (WIFI_CONFIG_START_ADDR + WIFI_CONFIG_SIZE)  // 256 + 132 = 388
#define MQTT_CONFIG_SIZE 192
#define WEB_AUTH_CONFIG_START_ADDR (MQTT_CONFIG_START_ADDR + MQTT_CONFIG_SIZE)  // 388 + 192 = 580
#define WEB_AUTH_CONFIG_SIZE 64
#define SETTINGS_INIT_MARKER_ADDR (WEB_AUTH_CONFIG_START_ADDR + WEB_AUTH_CONFIG_SIZE)  // 580 + 64 = 644
#define MAIN_INIT_MARKER_ADDR 1023

// WiFi AP Configuration
#define AP_SSID "T&A_FrostProtection"
#define AP_IP IPAddress(192, 168, 4, 1)
#define AP_SUBNET IPAddress(255, 255, 255, 0)

// I2C Addresses
#define CHT8305_ADDR 0x40  // Temperature & Humidity sensor
#define MODULE_4RELAY_ADDR 0x26    // 4Relay module address

// CHT8305 Registers
#define CHT8305_TEMP_REG 0x00
#define CHT8305_HUM_REG 0x01
#define CHT8305_CONFIG_REG 0x02
#define CHT8305_ALERT_REG 0x03
#define CHT8305_VOLTAGE_REG 0x04
#define CHT8305_MANUF_REG 0xFE
#define CHT8305_ID_REG 0xFF

// CHT8305 Configuration bits
#define CHT8305_CONFIG_SOFT_RESET 0x8000
#define CHT8305_CONFIG_CLOCK_STRETCH 0x4000
#define CHT8305_CONFIG_HEATER 0x2000
#define CHT8305_CONFIG_MODE 0x1000
#define CHT8305_CONFIG_VCCS 0x0800
#define CHT8305_CONFIG_T_RES 0x0400
#define CHT8305_CONFIG_H_RES 0x0300
#define CHT8305_CONFIG_VCC_ENABLE 0x0004

// Display dimensions
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Colors
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_GRAY 0x7BEF
#define COLOR_DARKGRAY 0x4208
#define COLOR_RELAY_ON COLOR_RED      // Kleur voor actieve relay
#define COLOR_RELAY_OFF COLOR_GREEN   // Kleur voor inactieve relay

// Trigger structure
struct Trigger {
  uint8_t enabled;        // 0 = disabled, 1 = enabled
  uint8_t sensorType;     // 0 = temperature, 1 = humidity
  uint8_t relayNum;       // 1-4
  float activateValue;    // Value to activate relay
  float deactivateValue;  // Value to deactivate relay
};

// WiFi Configuration structure
struct WiFiConfig {
  char ssid[64];          // WiFi SSID (max 63 chars + null terminator)
  char password[64];       // WiFi password (max 63 chars + null terminator)
  uint8_t configured;      // 0xFF = configured, 0x00 = not configured
} __attribute__((packed));  // Pack struct to avoid alignment issues

// MQTT Configuration structure
struct MQTTConfig {
  char brokerIP[16];      // MQTT broker IP address (IPv4 string)
  uint16_t brokerPort;    // MQTT broker port (default: 1883)
  char username[32];      // MQTT username
  char password[32];      // MQTT password
  char clientID[32];      // MQTT client ID
  char deviceName[32];    // Device name for HA discovery
  uint8_t enabled;        // 0 = disabled, 1 = enabled
  uint8_t autoDiscovery;  // 0 = disabled, 1 = enabled
} __attribute__((packed));  // Pack struct to avoid alignment issues

// Web Auth Configuration structure
struct WebAuthConfig {
  char username[32];      // Web interface username
  char password[32];      // Web interface password
} __attribute__((packed));  // Pack struct to avoid alignment issues

// Global variables
CHT8305 CHT(CHT8305_ADDR);  // Voeg CHT8305 object toe
Trigger triggers[MAX_TRIGGERS];
float currentTemp = 0;
float currentHumidity = 0;
int selectedItem = 0;
int currentPage = 0;  // 0 = main, 1 = triggers list, 2 = trigger config, 3 = settings
bool isEditingValue = false;  // false = selecting item, true = editing value
int selectedTrigger = 0;  // Index of the trigger being viewed/edited
int selectedMenuItem = 0;  // Index of the menu item in trigger config
int triggerListScroll = 0;  // Tracks scroll position for triggers list
int selectedSettingsItem = 0;  // Index of the menu item in settings page
bool settingsConfirmingReset = false;  // Flag for confirmation prompt
int settingsResetAction = 0;  // 1 = WiFi Reset, 2 = Auth Reset
unsigned long longPressStartTime = 0;  // Timer for long press detection
bool longPressHandled = false;  // Flag to track if long press was handled
bool ignoreFirstSettingsButtonRelease = false;  // Flag to ignore first button release after entering Settings via long press

// Replace sprite declarations at the top
TFT_eSprite sprite(&M5.Lcd);  // Single sprite for full screen
TFT_eSprite triggerListSprite(&M5.Lcd);  // Extra sprite for trigger list
// triggerHeaderSprite removed to save memory - draw directly on LCD instead

// Variables to track changes
float lastTemp = -999;
float lastHumidity = -999;
bool relayStates[4] = {false, false, false, false};  // Internal tracking of relay states
bool lastDrawnRelayStates[4] = {false, false, false, false}; // Track last drawn states
bool manualRelayControl[4] = {false, false, false, false};  // Track which relays are manually controlled (not by triggers)
bool needsRedraw = true;

// Add MODULE_4RELAY object as global variable
MODULE_4RELAY RELAY;

// Add at the top with other global variables
bool sensorConnected = false;
unsigned long sensorWarningDisplayStart = 0;  // For non-blocking warning display
const float INVALID_VALUE = -999.0;  // Speciale waarde voor ongeldige metingen
const unsigned long SENSOR_WARNING_DISPLAY_MS = 3000;  // Show warning for 3 seconds

// WiFi, Web Server and MQTT global variables
WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Configuration structures
WiFiConfig wifiConfig;
MQTTConfig mqttConfig;
WebAuthConfig webAuthConfig;

// WiFi and MQTT state variables
bool wifiConnected = false;
bool mqttConnected = false;
bool wifiAPMode = false;
unsigned long lastWiFiReconnect = 0;
unsigned long lastMQTTPublish = 0;
unsigned long lastMQTTReconnect = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;  // 10 seconds
const unsigned long MQTT_RECONNECT_INTERVAL = 30000;  // 30 seconds
const unsigned long MQTT_PUBLISH_INTERVAL = 5000;     // 5 seconds
int wifiReconnectAttempts = 0;
const int MAX_WIFI_RECONNECT_ATTEMPTS = 30;

// Function Prototypes
void drawMainScreen(bool forceRedraw);
void drawTriggersListScreen(); // Deze had geen parameter
void drawTriggerConfigScreen(bool forceRedraw);
void drawSettingsScreen(bool forceRedraw);

// EEPROM Management functions
void loadWiFiConfig();
void saveWiFiConfig();
void loadMQTTConfig();
void saveMQTTConfig();
void loadWebAuthConfig();
void saveWebAuthConfig();
void initializeSettingsEEPROM();

// WiFi Manager functions
void initWiFi();
void startAPMode();
void connectToWiFi();
void handleWiFiReconnect();
void resetWiFiConfig();
String getWiFiStatus();

// MQTT functions
void initMQTT();
bool connectMQTT();
void publishDiscoveryConfig();
void publishSensorData();
void publishRelayState(int relay);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleMQTTReconnect();

// Web Server functions
void setupWebServer();
String getMACAddress();
bool authenticateRequest();

// Add debug function at the top
void debugTrigger(const char* prefix, int index) {
  Serial.printf("%s Trigger %d: enabled=%d, sensor=%d, relay=%d, activate=%.1f, deactivate=%.1f\n",
    prefix, index, triggers[index].enabled, triggers[index].sensorType,
    triggers[index].relayNum, triggers[index].activateValue, triggers[index].deactivateValue);
}

// Get relay state (relay: 1-4)
bool getRelay(uint8_t relay) {
  if(relay < 1 || relay > 4) return false;
  return relayStates[relay - 1];  // Return cached state
}

// Set relay state (relay: 1-4, state: true=ON, false=OFF)
// manual: true if set manually (via MQTT/web), false if set by triggers
void setRelay(uint8_t relay, bool state, bool manual = false) {
  if(relay < 1 || relay > 4) return;
  
  uint8_t channel = relay - 1;  // Convert 1-based relay number to 0-based channel
  
  // Only change state and trigger redraw if the state is actually different
  if (relayStates[relay - 1] != state) {
      RELAY.setRelay(channel, state);
      relayStates[relay - 1] = state;
      manualRelayControl[relay - 1] = manual;  // Mark as manually controlled if manual=true
      Serial.printf("Set relay %d to %s (%s)\n", relay, state ? "ON" : "OFF", manual ? "manual" : "automatic");
      needsRedraw = true; // Trigger redraw when relay state changes
      
      // Publish to MQTT if connected
      if (mqttClient.connected() && mqttConfig.enabled) {
        publishRelayState(relay);
      }
  } else if (manual) {
      // Even if state is the same, mark as manual if it's a manual command
      manualRelayControl[relay - 1] = true;
  }
}

// Add function to clear EEPROM
void clearEEPROM() {
  Serial.println("Clearing EEPROM...");
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("EEPROM cleared");
}

// EEPROM Management functions
void initializeSettingsEEPROM() {
  Serial.println("Initializing settings EEPROM with defaults...");
  
  // Initialize WiFi config
  strncpy(wifiConfig.ssid, "", sizeof(wifiConfig.ssid) - 1);
  wifiConfig.ssid[sizeof(wifiConfig.ssid) - 1] = '\0';
  strncpy(wifiConfig.password, "", sizeof(wifiConfig.password) - 1);
  wifiConfig.password[sizeof(wifiConfig.password) - 1] = '\0';
  wifiConfig.configured = 0x00;
  saveWiFiConfig();
  
  // Initialize MQTT config
  strncpy(mqttConfig.brokerIP, "192.168.1.100", sizeof(mqttConfig.brokerIP) - 1);
  mqttConfig.brokerIP[sizeof(mqttConfig.brokerIP) - 1] = '\0';
  mqttConfig.brokerPort = 1883;
  strncpy(mqttConfig.username, "", sizeof(mqttConfig.username) - 1);
  mqttConfig.username[sizeof(mqttConfig.username) - 1] = '\0';
  strncpy(mqttConfig.password, "", sizeof(mqttConfig.password) - 1);
  mqttConfig.password[sizeof(mqttConfig.password) - 1] = '\0';
  String clientID = "FrostProtection_" + getMACAddress();
  strncpy(mqttConfig.clientID, clientID.c_str(), sizeof(mqttConfig.clientID) - 1);
  mqttConfig.clientID[sizeof(mqttConfig.clientID) - 1] = '\0';
  strncpy(mqttConfig.deviceName, "Frost Protection", sizeof(mqttConfig.deviceName) - 1);
  mqttConfig.deviceName[sizeof(mqttConfig.deviceName) - 1] = '\0';
  mqttConfig.enabled = 0;
  mqttConfig.autoDiscovery = 1;
  saveMQTTConfig();
  
  // Initialize Web Auth config with defaults
  strncpy(webAuthConfig.username, "admin", sizeof(webAuthConfig.username) - 1);
  webAuthConfig.username[sizeof(webAuthConfig.username) - 1] = '\0';
  strncpy(webAuthConfig.password, "admin", sizeof(webAuthConfig.password) - 1);
  webAuthConfig.password[sizeof(webAuthConfig.password) - 1] = '\0';
  saveWebAuthConfig();
  
  // Set settings initialization marker
  EEPROM.write(SETTINGS_INIT_MARKER_ADDR, 0xAA);
  EEPROM.commit();
  
  Serial.println("Settings EEPROM initialized");
}

void loadWiFiConfig() {
  Serial.println("Loading WiFi config from EEPROM...");
  EEPROM.get(WIFI_CONFIG_START_ADDR, wifiConfig);
  
  // Debug: Print what we loaded
  Serial.printf("Loaded WiFi config - SSID: '%s', configured byte: 0x%02X\n", wifiConfig.ssid, wifiConfig.configured);
  
  // Validate loaded data - check for corruption
  bool hasSSID = (wifiConfig.ssid[0] != '\0' && strlen(wifiConfig.ssid) > 0);
  
  // Priority 1: If SSID exists but configured byte is wrong, fix only the configured byte
  if (hasSSID && wifiConfig.configured != 0xFF && wifiConfig.configured != 0x00) {
    // SSID is present but configured byte is corrupted - fix it without losing data
    Serial.printf("WiFi config has valid SSID '%s' but corrupted configured byte (0x%02X), fixing configured byte only...\n", 
                  wifiConfig.ssid, wifiConfig.configured);
    wifiConfig.configured = 0xFF;
    saveWiFiConfig();
    Serial.printf("WiFi config fixed - SSID preserved: '%s', configured set to 0xFF\n", wifiConfig.ssid);
  }
  // Priority 2: If SSID exists and configured is 0xFF, it's valid
  else if (hasSSID && wifiConfig.configured == 0xFF) {
    Serial.printf("Loaded WiFi SSID: %s (length: %d, configured: 0x%02X) - VALID\n", 
                  wifiConfig.ssid, strlen(wifiConfig.ssid), wifiConfig.configured);
  }
  // Priority 3: If SSID exists but configured is 0x00, it's not configured (but data is valid)
  else if (hasSSID && wifiConfig.configured == 0x00) {
    Serial.printf("Loaded WiFi SSID: %s (length: %d) - Not configured (configured: 0x%02X)\n", 
                  wifiConfig.ssid, strlen(wifiConfig.ssid), wifiConfig.configured);
  }
  // Priority 4: No SSID or completely invalid - reset everything
  else {
    Serial.printf("WiFi config invalid or missing (SSID empty: %s, configured: 0x%02X), initializing...\n", 
                  hasSSID ? "no" : "yes", wifiConfig.configured);
    strncpy(wifiConfig.ssid, "", sizeof(wifiConfig.ssid) - 1);
    wifiConfig.ssid[sizeof(wifiConfig.ssid) - 1] = '\0';
    strncpy(wifiConfig.password, "", sizeof(wifiConfig.password) - 1);
    wifiConfig.password[sizeof(wifiConfig.password) - 1] = '\0';
    wifiConfig.configured = 0x00;
    saveWiFiConfig();
  }
}

void saveWiFiConfig() {
  Serial.println("Saving WiFi config to EEPROM...");
  Serial.printf("Saving - SSID: '%s', configured: 0x%02X\n", wifiConfig.ssid, wifiConfig.configured);
  EEPROM.put(WIFI_CONFIG_START_ADDR, wifiConfig);
  if (EEPROM.commit()) {
    Serial.println("WiFi config saved successfully");
    // Verify what was written
    WiFiConfig verify;
    EEPROM.get(WIFI_CONFIG_START_ADDR, verify);
    Serial.printf("Verification - SSID: '%s', configured: 0x%02X\n", verify.ssid, verify.configured);
  } else {
    Serial.println("WiFi config save failed!");
  }
}

void loadMQTTConfig() {
  Serial.println("Loading MQTT config from EEPROM...");
  EEPROM.get(MQTT_CONFIG_START_ADDR, mqttConfig);
  
  // Debug: Print what we loaded
  Serial.printf("Loaded MQTT config - Broker IP: '%s', Port: %d, Enabled: %d\n", 
                mqttConfig.brokerIP, mqttConfig.brokerPort, mqttConfig.enabled);
  
  // Validate loaded data - check if brokerIP is valid (not empty and looks like an IP address)
  bool hasBrokerIP = (mqttConfig.brokerIP[0] != '\0' && strlen(mqttConfig.brokerIP) > 0);
  bool isValidIP = false;
  
  if (hasBrokerIP) {
    // Basic IP validation - check if it contains at least one dot and no invalid characters
    isValidIP = (strchr(mqttConfig.brokerIP, '.') != NULL);
    // Also check for non-printable characters (should not happen anymore with correct offsets)
    if (isValidIP && (mqttConfig.brokerIP[0] < 32 || mqttConfig.brokerIP[0] > 126)) {
      Serial.printf("WARNING: MQTT brokerIP starts with invalid character (0x%02X) - this should not happen!\n", 
                    (uint8_t)mqttConfig.brokerIP[0]);
      isValidIP = false;
    }
  }
  
  // Valid brokerIP - config is good
  if (hasBrokerIP && isValidIP) {
    Serial.printf("Loaded MQTT Broker: %s:%d (Enabled: %d, AutoDiscovery: %d) - VALID\n", 
                  mqttConfig.brokerIP, mqttConfig.brokerPort, mqttConfig.enabled, mqttConfig.autoDiscovery);
  }
  // No brokerIP or invalid format - initialize with defaults
  else {
    Serial.printf("MQTT config not found or invalid (brokerIP empty: %s, isValidIP: %s), initializing with defaults...\n",
                  hasBrokerIP ? "no" : "yes", isValidIP ? "yes" : "no");
    strncpy(mqttConfig.brokerIP, "192.168.1.100", sizeof(mqttConfig.brokerIP) - 1);
    mqttConfig.brokerIP[sizeof(mqttConfig.brokerIP) - 1] = '\0';
    mqttConfig.brokerPort = 1883;
    strncpy(mqttConfig.username, "", sizeof(mqttConfig.username) - 1);
    mqttConfig.username[sizeof(mqttConfig.username) - 1] = '\0';
    strncpy(mqttConfig.password, "", sizeof(mqttConfig.password) - 1);
    mqttConfig.password[sizeof(mqttConfig.password) - 1] = '\0';
    String clientID = "FrostProtection_" + getMACAddress();
    strncpy(mqttConfig.clientID, clientID.c_str(), sizeof(mqttConfig.clientID) - 1);
    mqttConfig.clientID[sizeof(mqttConfig.clientID) - 1] = '\0';
    strncpy(mqttConfig.deviceName, "Frost Protection", sizeof(mqttConfig.deviceName) - 1);
    mqttConfig.deviceName[sizeof(mqttConfig.deviceName) - 1] = '\0';
    mqttConfig.enabled = 0;
    mqttConfig.autoDiscovery = 1;
    saveMQTTConfig();
  }
}

void saveMQTTConfig() {
  Serial.println("Saving MQTT config to EEPROM...");
  Serial.printf("Saving - Broker IP: '%s', Port: %d, Enabled: %d\n", 
                mqttConfig.brokerIP, mqttConfig.brokerPort, mqttConfig.enabled);
  EEPROM.put(MQTT_CONFIG_START_ADDR, mqttConfig);
  if (EEPROM.commit()) {
    Serial.println("MQTT config saved successfully");
    // Verify what was written
    MQTTConfig verify;
    EEPROM.get(MQTT_CONFIG_START_ADDR, verify);
    Serial.printf("Verification - Broker IP: '%s', Port: %d, Enabled: %d\n", 
                  verify.brokerIP, verify.brokerPort, verify.enabled);
  } else {
    Serial.println("MQTT config save failed!");
  }
}

void loadWebAuthConfig() {
  Serial.println("Loading Web Auth config from EEPROM...");
  EEPROM.get(WEB_AUTH_CONFIG_START_ADDR, webAuthConfig);
  
  // Validate loaded data
  if (webAuthConfig.username[0] == '\0') {
    Serial.println("Web Auth config not found or invalid, initializing with defaults...");
    strncpy(webAuthConfig.username, "admin", sizeof(webAuthConfig.username) - 1);
    webAuthConfig.username[sizeof(webAuthConfig.username) - 1] = '\0';
    strncpy(webAuthConfig.password, "admin", sizeof(webAuthConfig.password) - 1);
    webAuthConfig.password[sizeof(webAuthConfig.password) - 1] = '\0';
    saveWebAuthConfig();
  } else {
    Serial.printf("Loaded Web Auth username: %s\n", webAuthConfig.username);
  }
}

void saveWebAuthConfig() {
  Serial.println("Saving Web Auth config to EEPROM...");
  EEPROM.put(WEB_AUTH_CONFIG_START_ADDR, webAuthConfig);
  if (EEPROM.commit()) {
    Serial.println("Web Auth config saved successfully");
  } else {
    Serial.println("Web Auth config save failed!");
  }
}

String getMACAddress() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

// WiFi Manager functions
void initWiFi() {
  Serial.println("Initializing WiFi...");
  
  if (wifiConfig.configured == 0xFF && strlen(wifiConfig.ssid) > 0) {
    // WiFi is configured, try to connect
    Serial.println("WiFi configured, connecting...");
    connectToWiFi();
  } else {
    // WiFi not configured, start AP mode
    Serial.println("WiFi not configured, starting AP mode...");
    startAPMode();
  }
}

void startAPMode() {
  Serial.println("Starting AP mode...");
  wifiAPMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.disconnect();
  delay(100);
  
  // Configure IP BEFORE starting AP (this is the correct order)
  // Note: softAPConfig must be called BEFORE softAP on some ESP32 versions
  WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
  
  // Start AP with DHCP enabled (channel 1, hidden=false, max connections=4)
  // Note: The 4th parameter is max_connection, not a flag
  if (!WiFi.softAP(AP_SSID, "", 1, false, 4)) {
    Serial.println("Failed to start AP mode!");
    wifiAPMode = false;
    return;
  }
  
  delay(1000);  // Give AP time to fully initialize and start DHCP server
  
  IPAddress apIP = WiFi.softAPIP();
  IPAddress apGateway = WiFi.softAPIP();
  IPAddress apSubnet = AP_SUBNET;
  
  Serial.printf("AP started. SSID: %s\n", AP_SSID);
  Serial.printf("AP IP: %s\n", apIP.toString().c_str());
  Serial.printf("AP Gateway: %s\n", apGateway.toString().c_str());
  Serial.printf("AP Subnet: %s\n", apSubnet.toString().c_str());
  Serial.printf("AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
  Serial.printf("Connected stations: %d\n", WiFi.softAPgetStationNum());
  
  wifiConnected = false; // Not connected to external network
}

void connectToWiFi() {
  Serial.printf("Connecting to WiFi: %s\n", wifiConfig.ssid);
  wifiAPMode = false;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  WiFi.begin(wifiConfig.ssid, wifiConfig.password);
  
  int attempts = 0;
  const int maxAttempts = 20;
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiReconnectAttempts = 0;
    Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    wifiConnected = false;
    Serial.println("WiFi connection failed!");
    // Do NOT fallback to AP mode - keep trying to reconnect
  }
}

void handleWiFiReconnect() {
  // Only try to reconnect if WiFi is configured and not in AP mode
  if (wifiConfig.configured == 0xFF && strlen(wifiConfig.ssid) > 0 && !wifiAPMode) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      unsigned long currentMillis = millis();
      
      // Try to reconnect every 10 seconds
      if (currentMillis - lastWiFiReconnect >= WIFI_RECONNECT_INTERVAL) {
        lastWiFiReconnect = currentMillis;
        wifiReconnectAttempts++;
        
        Serial.printf("WiFi disconnected. Reconnect attempt %d...\n", wifiReconnectAttempts);
        
        // Exponential backoff after max attempts
        unsigned long backoffDelay = WIFI_RECONNECT_INTERVAL;
        if (wifiReconnectAttempts > MAX_WIFI_RECONNECT_ATTEMPTS) {
          backoffDelay = std::min(WIFI_RECONNECT_INTERVAL * (1UL << std::min((wifiReconnectAttempts - MAX_WIFI_RECONNECT_ATTEMPTS) / 5, 6)), 300000UL); // Max 5 minutes
        }
        
        WiFi.disconnect();
        delay(100);
        WiFi.begin(wifiConfig.ssid, wifiConfig.password);
        
        // Check connection after short delay
        delay(2000);
        if (WiFi.status() == WL_CONNECTED) {
          wifiConnected = true;
          wifiReconnectAttempts = 0;
          Serial.printf("WiFi reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
          Serial.println("Reconnection failed. Will retry...");
          // Adjust next reconnect time based on backoff
          lastWiFiReconnect = currentMillis - WIFI_RECONNECT_INTERVAL + backoffDelay;
        }
      }
    } else if (!wifiConnected) {
      // Just reconnected
      wifiConnected = true;
      wifiReconnectAttempts = 0;
      Serial.printf("WiFi reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
    }
  }
}

void resetWiFiConfig() {
  Serial.println("Resetting WiFi configuration...");
  strncpy(wifiConfig.ssid, "", sizeof(wifiConfig.ssid) - 1);
  wifiConfig.ssid[sizeof(wifiConfig.ssid) - 1] = '\0';
  strncpy(wifiConfig.password, "", sizeof(wifiConfig.password) - 1);
  wifiConfig.password[sizeof(wifiConfig.password) - 1] = '\0';
  wifiConfig.configured = 0x00;
  saveWiFiConfig();
  
  WiFi.disconnect();
  delay(100);
  
  // Restart ESP32 to go back to AP mode
  Serial.println("Restarting ESP32...");
  ESP.restart();
}

String getWiFiStatus() {
  if (wifiAPMode) {
    return "AP Mode";
  } else if (wifiConnected && WiFi.status() == WL_CONNECTED) {
    return "Connected";
  } else if (wifiConfig.configured == 0xFF) {
    return "Connecting...";
  } else {
    return "Disconnected";
  }
}

// MQTT functions
void initMQTT() {
  Serial.println("Initializing MQTT...");
  mqttClient.setServer(mqttConfig.brokerIP, mqttConfig.brokerPort);
  mqttClient.setCallback(mqttCallback);
  
  if (mqttConfig.enabled) {
    connectMQTT();
  }
}

bool connectMQTT() {
  if (!mqttConfig.enabled) {
    return false;
  }
  
  Serial.printf("Connecting to MQTT broker at %s:%d...\n", mqttConfig.brokerIP, mqttConfig.brokerPort);
  
  bool connected = false;
  if (strlen(mqttConfig.username) > 0) {
    connected = mqttClient.connect(mqttConfig.clientID, mqttConfig.username, mqttConfig.password);
  } else {
    connected = mqttClient.connect(mqttConfig.clientID);
  }
  
  if (connected) {
    mqttConnected = true;
    Serial.println("MQTT connected!");
    
    // Publish Auto Discovery configs if enabled
    if (mqttConfig.autoDiscovery) {
      publishDiscoveryConfig();
    }
    
    // Subscribe to relay control topics
    for (int i = 1; i <= 4; i++) {
      String topic = "frostprotection/relay/" + String(i) + "/set";
      mqttClient.subscribe(topic.c_str());
      Serial.printf("Subscribed to: %s\n", topic.c_str());
    }
    
    // Publish current relay states
    for (int i = 1; i <= 4; i++) {
      publishRelayState(i);
    }
    
    // Publish initial sensor data
    publishSensorData();
    
    return true;
  } else {
    mqttConnected = false;
    Serial.printf("MQTT connection failed! State: %d\n", mqttClient.state());
    return false;
  }
}

void publishDiscoveryConfig() {
  if (!mqttClient.connected()) return;
  
  String macSuffix = getMACAddress();
  macSuffix.toLowerCase();
  macSuffix = macSuffix.substring(macSuffix.length() - 6); // Last 6 chars
  
  // Temperature sensor
  JsonDocument tempConfig;
  String tempName = String(mqttConfig.deviceName) + " Temperature";
  tempConfig["name"] = tempName;
  tempConfig["unique_id"] = String("frostprotection_temp_") + macSuffix;
  tempConfig["state_topic"] = "frostprotection/sensor/temperature";
  tempConfig["unit_of_measurement"] = "°C";
  tempConfig["device_class"] = "temperature";
  tempConfig["device"]["identifiers"][0] = String("frostprotection_") + macSuffix;
  tempConfig["device"]["name"] = mqttConfig.deviceName;
  tempConfig["device"]["model"] = "Antifreeze Module V3";
  tempConfig["device"]["manufacturer"] = "Technics & Applications";
  
  String tempTopic = "homeassistant/sensor/frostprotection_temp_" + macSuffix + "/config";
  String tempPayload;
  serializeJson(tempConfig, tempPayload);
  mqttClient.publish(tempTopic.c_str(), tempPayload.c_str(), true);
  
  // Humidity sensor
  JsonDocument humConfig;
  String humName = String(mqttConfig.deviceName) + " Humidity";
  humConfig["name"] = humName;
  humConfig["unique_id"] = String("frostprotection_humidity_") + macSuffix;
  humConfig["state_topic"] = "frostprotection/sensor/humidity";
  humConfig["unit_of_measurement"] = "%";
  humConfig["device_class"] = "humidity";
  humConfig["device"]["identifiers"][0] = String("frostprotection_") + macSuffix;
  humConfig["device"]["name"] = mqttConfig.deviceName;
  humConfig["device"]["model"] = "Antifreeze Module V3";
  humConfig["device"]["manufacturer"] = "Technics & Applications";
  
  String humTopic = "homeassistant/sensor/frostprotection_humidity_" + macSuffix + "/config";
  String humPayload;
  serializeJson(humConfig, humPayload);
  mqttClient.publish(humTopic.c_str(), humPayload.c_str(), true);
  
  // Relay switches
  for (int i = 1; i <= 4; i++) {
    JsonDocument relayConfig;
    String relayName = String(mqttConfig.deviceName) + " Relay " + String(i);
    relayConfig["name"] = relayName;
    relayConfig["unique_id"] = String("frostprotection_relay") + String(i) + "_" + macSuffix;
    relayConfig["state_topic"] = String("frostprotection/relay/") + String(i) + "/state";
    relayConfig["command_topic"] = String("frostprotection/relay/") + String(i) + "/set";
    relayConfig["payload_on"] = "ON";
    relayConfig["payload_off"] = "OFF";
    relayConfig["state_on"] = "ON";
    relayConfig["state_off"] = "OFF";
    relayConfig["device"]["identifiers"][0] = String("frostprotection_") + macSuffix;
    relayConfig["device"]["name"] = mqttConfig.deviceName;
    relayConfig["device"]["model"] = "Antifreeze Module V3";
    relayConfig["device"]["manufacturer"] = "Technics & Applications";
    
    String relayTopic = "homeassistant/switch/frostprotection_relay" + String(i) + "_" + macSuffix + "/config";
    String relayPayload;
    serializeJson(relayConfig, relayPayload);
    mqttClient.publish(relayTopic.c_str(), relayPayload.c_str(), true);
  }
  
  Serial.println("MQTT Auto Discovery configs published");
}

void publishSensorData() {
  if (!mqttClient.connected() || !mqttConfig.enabled) return;
  
  char tempStr[16];
  char humStr[16];
  
  if (currentTemp == INVALID_VALUE) {
    strcpy(tempStr, "unavailable");
  } else {
    snprintf(tempStr, sizeof(tempStr), "%.1f", currentTemp);
  }
  
  if (currentHumidity == INVALID_VALUE) {
    strcpy(humStr, "unavailable");
  } else {
    snprintf(humStr, sizeof(humStr), "%.1f", currentHumidity);
  }
  
  mqttClient.publish("frostprotection/sensor/temperature", tempStr, true);
  mqttClient.publish("frostprotection/sensor/humidity", humStr, true);
}

void publishRelayState(int relay) {
  if (!mqttClient.connected() || !mqttConfig.enabled || relay < 1 || relay > 4) return;
  
  String topic = "frostprotection/relay/" + String(relay) + "/state";
  String state = relayStates[relay - 1] ? "ON" : "OFF";
  mqttClient.publish(topic.c_str(), state.c_str(), true);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convert payload to string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.printf("[MQTT] Message received - Topic: '%s', Payload: '%s' (length: %d)\n", topic, message, length);
  
  // Check if it's a relay command
  String topicStr = String(topic);
  Serial.printf("[MQTT] Topic string: '%s', startsWith 'frostprotection/relay/': %s, endsWith '/set': %s\n", 
                topicStr.c_str(), 
                topicStr.startsWith("frostprotection/relay/") ? "yes" : "no",
                topicStr.endsWith("/set") ? "yes" : "no");
  
  if (topicStr.startsWith("frostprotection/relay/") && topicStr.endsWith("/set")) {
    // Extract relay number - simple and robust method
    // Topic format: "frostprotection/relay/X/set" where X is 1-4
    int relayNum = 0;
    
    // Find the character after "frostprotection/relay/" (should be '1', '2', '3', or '4')
    // "frostprotection/relay/" = 22 characters (not 23!)
    int prefixLen = 22; // Length of "frostprotection/relay/"
    if (topicStr.length() > prefixLen) {
      char relayChar = topicStr.charAt(prefixLen);
      Serial.printf("[MQTT] Found character at position %d: '%c' (ASCII: %d)\n", prefixLen, relayChar, (int)relayChar);
      
      if (relayChar >= '1' && relayChar <= '4') {
        relayNum = relayChar - '0';
        Serial.printf("[MQTT] Extracted relay number: %d (from char '%c')\n", relayNum, relayChar);
      } else {
        Serial.printf("[MQTT] ERROR: Invalid relay character '%c' (expected '1'-'4')\n", relayChar);
      }
    } else {
      Serial.printf("[MQTT] ERROR: Topic too short (length: %d, expected > %d)\n", topicStr.length(), prefixLen);
    }
    
    if (relayNum >= 1 && relayNum <= 4) {
      bool newState = (strcmp(message, "ON") == 0 || strcmp(message, "1") == 0 || strcmp(message, "on") == 0);
      Serial.printf("[MQTT] Setting relay %d to %s (message was: '%s')\n", relayNum, newState ? "ON" : "OFF", message);
      
      setRelay(relayNum, newState, true);  // true = manual control via MQTT
      
      // Publish updated state
      publishRelayState(relayNum);
      
      Serial.printf("[MQTT] Relay %d successfully set to %s via MQTT (manual control)\n", relayNum, newState ? "ON" : "OFF");
    } else {
      Serial.printf("[MQTT] ERROR: Could not extract valid relay number from topic '%s' (got %d, length: %d)\n", 
                    topicStr.c_str(), relayNum, topicStr.length());
    }
  } else {
    Serial.printf("[MQTT] Topic does not match relay command pattern (startsWith: %s, endsWith: %s)\n",
                  topicStr.startsWith("frostprotection/relay/") ? "yes" : "no",
                  topicStr.endsWith("/set") ? "yes" : "no");
  }
}

void handleMQTTReconnect() {
  if (!mqttConfig.enabled) return;
  
  if (!mqttClient.connected()) {
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastMQTTReconnect >= MQTT_RECONNECT_INTERVAL) {
      lastMQTTReconnect = currentMillis;
      Serial.println("Attempting MQTT reconnection...");
      if (connectMQTT()) {
        Serial.println("MQTT reconnected!");
      }
    }
  } else if (!mqttConnected) {
    mqttConnected = true;
  }
}

// Web Server Authentication
bool authenticateRequest() {
  if (!server.authenticate(webAuthConfig.username, webAuthConfig.password)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// Web Server Setup
void setupWebServer() {
  // Root route - Dashboard page with authentication
  server.on("/", HTTP_GET, [](){
    Serial.println("[WEBSERVER] ===== ROOT ROUTE CALLED =====\n");
    Serial.printf("[WEBSERVER] Client IP: %s\n", server.client().remoteIP().toString().c_str());
    Serial.printf("[WEBSERVER] URI: %s\n", server.uri().c_str());
    
    if (!authenticateRequest()) {
      Serial.println("[WEBSERVER] Authentication failed");
      return;
    }
    Serial.println("[WEBSERVER] Authentication successful, sending dashboard");
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Frost Protection Dashboard</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}h1{color:#4CAF50}.container{max-width:800px;margin:0 auto}.card{background:#2a2a2a;border-radius:8px;padding:20px;margin:10px 0}.status{display:flex;justify-content:space-between;margin:10px 0}.value{font-size:32px;font-weight:bold;color:#4CAF50}.relay{background:#333;border-radius:5px;padding:15px;margin:10px 0;cursor:pointer;transition:background 0.3s}.relay.on{background:#d32f2f}.relay.off{background:#388e3c}.btn{background:#2196F3;color:#fff;border:none;padding:10px 20px;border-radius:5px;cursor:pointer;margin:5px}.btn:hover{background:#1976D2}nav{text-align:center;margin:20px 0}nav a{color:#4CAF50;text-decoration:none;margin:0 15px;font-weight:bold}nav a:hover{text-decoration:underline}</style>";
    html += "</head><body><div class='container'><h1>Frost Protection Dashboard</h1>";
    
    html += "<nav><a href='/'>Dashboard</a> | <a href='/wifi'>WiFi</a> | <a href='/mqtt'>MQTT</a> | <a href='/triggers'>Triggers</a> | <a href='/settings'>Settings</a></nav>";
    
    html += "<div class='card'><h2>Sensor Data</h2>";
    html += "<div class='status'><span>Temperature:</span><span class='value' id='temp'>--.-</span>°C</div>";
    html += "<div class='status'><span>Humidity:</span><span class='value' id='hum'>--.-</span>%</div></div>";
    
    html += "<div class='card'><h2>Relay Control</h2>";
    html += "<p style='color:#888;font-size:14px;margin-bottom:15px'>Click on a relay to toggle it on/off</p>";
    for(int i = 1; i <= 4; i++) {
      String state = relayStates[i-1] ? "on" : "off";
      html += "<div class='relay " + state + "' onclick='toggleRelay(" + String(i) + ")'>";
      html += "<strong>Relay " + String(i) + ": <span id='relay" + String(i) + "'>" + (relayStates[i-1] ? "ON" : "OFF") + "</span></strong></div>";
    }
    html += "</div>";
    
    html += "<div class='card'><h2>Connection Status</h2>";
    html += "<div class='status'><span>WiFi:</span><span id='wifiStatus'>" + getWiFiStatus() + "</span></div>";
    html += "<div class='status'><span>IP Address:</span><span id='ipAddr'>" + (wifiAPMode ? WiFi.softAPIP().toString() : (wifiConnected ? WiFi.localIP().toString() : "N/A")) + "</span></div>";
    html += "<div class='status'><span>MQTT:</span><span id='mqttStatus'>" + String(mqttConnected ? "Connected" : "Disconnected") + "</span></div></div>";
    
    html += "<script>function updateData(){fetch('/api/data').then(r=>r.json()).then(d=>{";
    html += "document.getElementById('temp').textContent=d.temp!==null?d.temp.toFixed(1):'--.-';";
    html += "document.getElementById('hum').textContent=d.hum!==null?d.hum.toFixed(1):'--.-';";
    html += "let relays=document.querySelectorAll('.relay');";
    html += "for(let i=1;i<=4;i++){let el=document.getElementById('relay'+i);let cls=relays[i-1];";
    html += "if(d.relays[i-1]){el.textContent='ON';if(cls){cls.classList.remove('off');cls.classList.add('on');}}else{el.textContent='OFF';if(cls){cls.classList.remove('on');cls.classList.add('off');}}}";
    html += "document.getElementById('wifiStatus').textContent=d.wifiStatus||'';";
    html += "document.getElementById('ipAddr').textContent=d.ipAddr||'';";
    html += "document.getElementById('mqttStatus').textContent=d.mqttStatus||'';}).catch(e=>console.error(e))}";
    html += "function toggleRelay(n){fetch('/api/relay',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({relay:n,state:document.getElementById('relay'+n).textContent==='OFF'})}).then(()=>updateData())}";
    html += "updateData();setInterval(updateData,2000);</script></body></html>";
    
    server.send(200, "text/html", html);
  });
  
  // Test route without authentication (for debugging)
  server.on("/test", HTTP_GET, [](){
    Serial.printf("[WEBSERVER] ===== TEST ROUTE CALLED =====\n");
    Serial.printf("[WEBSERVER] Client IP: %s\n", server.client().remoteIP().toString().c_str());
    Serial.printf("[WEBSERVER] Request URI: %s\n", server.uri().c_str());
    server.send(200, "text/plain", "Web server is working! AP IP: " + WiFi.softAPIP().toString());
  });
  
  // Another test route to check if server is responding at all
  server.on("/ping", HTTP_GET, [](){
    Serial.println("[WEBSERVER] ===== PING ROUTE CALLED =====\n");
    server.send(200, "text/plain", "PONG");
  });
  
  // Root route without auth for testing (kept for debugging)
  server.on("/hello", HTTP_GET, [](){
    Serial.println("[WEBSERVER] ===== HELLO ROUTE CALLED =====\n");
    server.send(200, "text/html", "<h1>Hello from ESP32!</h1><p>Web server is working!</p>");
  });
  
  // Dashboard route (alias to root for convenience)
  server.on("/dashboard", HTTP_GET, [](){
    // Redirect to root
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  
  // WiFi Configuration page
  server.on("/wifi", HTTP_GET, [](){
    if (!authenticateRequest()) return;
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>WiFi Configuration</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}.container{max-width:600px;margin:0 auto}.card{background:#2a2a2a;border-radius:8px;padding:20px;margin:10px 0}.form-group{margin:15px 0}label{display:block;margin-bottom:5px}input,select{width:100%;padding:10px;border:1px solid #555;border-radius:5px;background:#333;color:#fff;box-sizing:border-box}.btn{background:#2196F3;color:#fff;border:none;padding:10px 20px;border-radius:5px;cursor:pointer;margin:5px}.btn:hover{background:#1976D2}.btn-danger{background:#d32f2f}.btn-danger:hover{background:#b71c1c}nav{text-align:center;margin:20px 0}nav a{color:#4CAF50;text-decoration:none;margin:0 15px}</style>";
    html += "</head><body><div class='container'><h1>WiFi Configuration</h1>";
    html += "<nav><a href='/'>Dashboard</a> | <a href='/wifi'>WiFi</a> | <a href='/mqtt'>MQTT</a> | <a href='/triggers'>Triggers</a> | <a href='/settings'>Settings</a></nav>";
    
    html += "<div class='card'><h2>Network Scan</h2>";
    html += "<button class='btn' onclick='scanNetworks()'>Scan for Networks</button>";
    html += "<div id='networks'></div></div>";
    
    html += "<div class='card'><form id='wifiForm'><h2>WiFi Configuration</h2>";
    html += "<div class='form-group'><label>SSID:</label><select id='ssid' name='ssid' required></select></div>";
    html += "<div class='form-group'><label>Password:</label><input type='password' id='password' name='password'></div>";
    html += "<button type='submit' class='btn'>Save Configuration</button></form></div>";
    
    html += "<div class='card'><h2>Current Status</h2>";
    html += "<p>Status: " + getWiFiStatus() + "</p>";
    html += "<p>SSID: " + String(wifiConfig.ssid) + "</p>";
    html += "<p>IP: " + (wifiAPMode ? WiFi.softAPIP().toString() : (wifiConnected ? WiFi.localIP().toString() : "N/A")) + "</p></div>";
    
    html += "<script>function scanNetworks(){fetch('/api/networks').then(r=>r.json()).then(d=>{";
    html += "let sel=document.getElementById('ssid');sel.innerHTML='';";
    html += "d.networks.forEach(n=>{let opt=document.createElement('option');opt.value=n;opt.textContent=n;sel.appendChild(opt);});";
    html += "if(d.currentSSID){sel.value=d.currentSSID;}";
    html += "document.getElementById('networks').innerHTML='<p>Found '+d.networks.length+' networks</p>';}).catch(e=>console.error(e))}";
    html += "document.getElementById('wifiForm').addEventListener('submit',e=>{e.preventDefault();";
    html += "fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(document.getElementById('ssid').value)+'&password='+encodeURIComponent(document.getElementById('password').value)}).then(r=>{if(r.ok){alert('WiFi configuration saved! Module will restart.');setTimeout(()=>location.reload(),2000);}else{alert('Error saving configuration');}}).catch(e=>alert('Error: '+e));});";
    html += "scanNetworks();</script></body></html>";
    
    server.send(200, "text/html", html);
  });
  
  server.on("/wifi", HTTP_POST, [](){
    if (!authenticateRequest()) return;
    
    if (server.hasArg("ssid") && server.hasArg("password")) {
      String ssid = server.arg("ssid");
      String password = server.arg("password");
      
      strncpy(wifiConfig.ssid, ssid.c_str(), sizeof(wifiConfig.ssid) - 1);
      wifiConfig.ssid[sizeof(wifiConfig.ssid) - 1] = '\0';
      strncpy(wifiConfig.password, password.c_str(), sizeof(wifiConfig.password) - 1);
      wifiConfig.password[sizeof(wifiConfig.password) - 1] = '\0';
      wifiConfig.configured = 0xFF;
      
      saveWiFiConfig();
      
      server.send(200, "text/plain", "WiFi configuration saved. Module will restart...");
      delay(1000);
      ESP.restart();
    } else {
      server.send(400, "text/plain", "Missing SSID or password");
    }
  });
  
  // MQTT Configuration page
  server.on("/mqtt", HTTP_GET, [](){
    if (!authenticateRequest()) return;
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>MQTT Configuration</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}.container{max-width:600px;margin:0 auto}.card{background:#2a2a2a;border-radius:8px;padding:20px;margin:10px 0}.form-group{margin:15px 0}label{display:block;margin-bottom:5px}input,select{width:100%;padding:10px;border:1px solid #555;border-radius:5px;background:#333;color:#fff;box-sizing:border-box}.btn{background:#2196F3;color:#fff;border:none;padding:10px 20px;border-radius:5px;cursor:pointer;margin:5px}.btn:hover{background:#1976D2}nav{text-align:center;margin:20px 0}nav a{color:#4CAF50;text-decoration:none;margin:0 15px}input[type='checkbox']{width:auto}</style>";
    html += "</head><body><div class='container'><h1>MQTT Configuration</h1>";
    html += "<nav><a href='/'>Dashboard</a> | <a href='/wifi'>WiFi</a> | <a href='/mqtt'>MQTT</a> | <a href='/triggers'>Triggers</a> | <a href='/settings'>Settings</a></nav>";
    
    html += "<div class='card'><form id='mqttForm'><h2>MQTT Settings</h2>";
    html += "<div class='form-group'><label>Broker IP:</label><input type='text' id='brokerIP' value='" + String(mqttConfig.brokerIP) + "' required></div>";
    html += "<div class='form-group'><label>Broker Port:</label><input type='number' id='brokerPort' value='" + String(mqttConfig.brokerPort) + "' required></div>";
    html += "<div class='form-group'><label>Username:</label><input type='text' id='username' value='" + String(mqttConfig.username) + "'></div>";
    html += "<div class='form-group'><label>Password:</label><input type='password' id='password' value='" + String(mqttConfig.password) + "'></div>";
    html += "<div class='form-group'><label>Client ID:</label><input type='text' id='clientID' value='" + String(mqttConfig.clientID) + "' required></div>";
    html += "<div class='form-group'><label>Device Name:</label><input type='text' id='deviceName' value='" + String(mqttConfig.deviceName) + "' required></div>";
    html += "<div class='form-group'><label><input type='checkbox' id='enabled' " + String(mqttConfig.enabled ? "checked" : "") + "> Enable MQTT</label></div>";
    html += "<div class='form-group'><label><input type='checkbox' id='autoDiscovery' " + String(mqttConfig.autoDiscovery ? "checked" : "") + "> Enable Home Assistant Auto Discovery</label></div>";
    html += "<button type='submit' class='btn'>Save Configuration</button></form></div>";
    
    html += "<div class='card'><h2>Status</h2>";
    html += "<p>MQTT: " + String(mqttConnected ? "Connected" : "Disconnected") + "</p>";
    html += "<p>Broker: " + String(mqttConfig.brokerIP) + ":" + String(mqttConfig.brokerPort) + "</p></div>";
    
    html += "<script>document.getElementById('mqttForm').addEventListener('submit',e=>{e.preventDefault();";
    html += "const d={brokerIP:document.getElementById('brokerIP').value,brokerPort:parseInt(document.getElementById('brokerPort').value),username:document.getElementById('username').value,password:document.getElementById('password').value,clientID:document.getElementById('clientID').value,deviceName:document.getElementById('deviceName').value,enabled:document.getElementById('enabled').checked,autoDiscovery:document.getElementById('autoDiscovery').checked};";
    html += "fetch('/mqtt',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).then(r=>r.json()).then(d=>{if(d.success){alert('MQTT configuration saved!');location.reload();}else{alert('Error: '+d.error);}}).catch(e=>alert('Error: '+e));});</script></body></html>";
    
    server.send(200, "text/html", html);
  });
  
  server.on("/mqtt", HTTP_POST, [](){
    if (!authenticateRequest()) return;
    
    String body = server.arg("plain");
    if (body.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
      return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error && doc.containsKey("brokerIP")) {
      strncpy(mqttConfig.brokerIP, doc["brokerIP"] | "192.168.1.100", sizeof(mqttConfig.brokerIP) - 1);
      mqttConfig.brokerIP[sizeof(mqttConfig.brokerIP) - 1] = '\0';
      mqttConfig.brokerPort = doc["brokerPort"] | 1883;
      strncpy(mqttConfig.username, doc["username"] | "", sizeof(mqttConfig.username) - 1);
      mqttConfig.username[sizeof(mqttConfig.username) - 1] = '\0';
      strncpy(mqttConfig.password, doc["password"] | "", sizeof(mqttConfig.password) - 1);
      mqttConfig.password[sizeof(mqttConfig.password) - 1] = '\0';
      strncpy(mqttConfig.clientID, doc["clientID"] | mqttConfig.clientID, sizeof(mqttConfig.clientID) - 1);
      mqttConfig.clientID[sizeof(mqttConfig.clientID) - 1] = '\0';
      strncpy(mqttConfig.deviceName, doc["deviceName"] | "Frost Protection", sizeof(mqttConfig.deviceName) - 1);
      mqttConfig.deviceName[sizeof(mqttConfig.deviceName) - 1] = '\0';
      mqttConfig.enabled = doc["enabled"] | false;
      mqttConfig.autoDiscovery = doc["autoDiscovery"] | true;
      
      saveMQTTConfig();
      
      // Reinitialize MQTT if enabled
      if (mqttConfig.enabled && wifiConnected) {
        initMQTT();
      } else {
        mqttClient.disconnect();
        mqttConnected = false;
      }
      
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
    }
  });
  
  // Triggers Configuration page
  server.on("/triggers", HTTP_GET, [](){
    if (!authenticateRequest()) return;
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Triggers Configuration</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}.container{max-width:900px;margin:0 auto}.card{background:#2a2a2a;border-radius:8px;padding:20px;margin:10px 0}table{width:100%;border-collapse:collapse}th,td{border:1px solid #555;padding:10px;text-align:left}th{background:#333}input,select{width:100%;padding:5px;border:1px solid #555;border-radius:3px;background:#333;color:#fff;box-sizing:border-box}.btn{background:#2196F3;color:#fff;border:none;padding:10px 20px;border-radius:5px;cursor:pointer;margin:5px}.btn:hover{background:#1976D2}nav{text-align:center;margin:20px 0}nav a{color:#4CAF50;text-decoration:none;margin:0 15px}input[type='checkbox']{width:auto}</style>";
    html += "</head><body><div class='container'><h1>Triggers Configuration</h1>";
    html += "<nav><a href='/'>Dashboard</a> | <a href='/wifi'>WiFi</a> | <a href='/mqtt'>MQTT</a> | <a href='/triggers'>Triggers</a> | <a href='/settings'>Settings</a></nav>";
    
    html += "<div class='card'><form id='triggersForm'><table><thead><tr><th>Enabled</th><th>Sensor</th><th>Relay</th><th>Activate At</th><th>Deactivate At</th></tr></thead><tbody>";
    
    for(int i = 0; i < MAX_TRIGGERS; i++) {
      html += "<tr><td><input type='checkbox' id='enabled" + String(i) + "' " + String(triggers[i].enabled ? "checked" : "") + "></td>";
      html += "<td><select id='sensorType" + String(i) + "'><option value='0' " + String(triggers[i].sensorType == 0 ? "selected" : "") + ">Temperature</option>";
      html += "<option value='1' " + String(triggers[i].sensorType == 1 ? "selected" : "") + ">Humidity</option></select></td>";
      html += "<td><select id='relayNum" + String(i) + "'>";
      for(int j = 1; j <= 4; j++) {
        html += "<option value='" + String(j) + "' " + String(triggers[i].relayNum == j ? "selected" : "") + ">Relay " + String(j) + "</option>";
      }
      html += "</select></td>";
      html += "<td><input type='number' step='0.5' id='activateValue" + String(i) + "' value='" + String(triggers[i].activateValue, 1) + "'></td>";
      html += "<td><input type='number' step='0.5' id='deactivateValue" + String(i) + "' value='" + String(triggers[i].deactivateValue, 1) + "'></td></tr>";
    }
    
    html += "</tbody></table><button type='submit' class='btn'>Save All Triggers</button></form></div>";
    
    html += "<script>document.getElementById('triggersForm').addEventListener('submit',e=>{e.preventDefault();const t=[];";
    html += "for(let i=0;i<8;i++){t.push({enabled:document.getElementById('enabled'+i).checked,sensorType:parseInt(document.getElementById('sensorType'+i).value),relayNum:parseInt(document.getElementById('relayNum'+i).value),activateValue:parseFloat(document.getElementById('activateValue'+i).value),deactivateValue:parseFloat(document.getElementById('deactivateValue'+i).value)});}";
    html += "fetch('/triggers',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({triggers:t})}).then(r=>r.json()).then(d=>{if(d.success){alert('Triggers saved!');location.reload();}else{alert('Error: '+d.error);}}).catch(e=>alert('Error: '+e));});</script></body></html>";
    
    server.send(200, "text/html", html);
  });
  
  server.on("/triggers", HTTP_POST, [](){
    if (!authenticateRequest()) return;
    
    String body = server.arg("plain");
    if (body.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
      return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error && doc.containsKey("triggers")) {
      JsonArray triggersArray = doc["triggers"];
      if (triggersArray.size() == MAX_TRIGGERS) {
        for(int i = 0; i < MAX_TRIGGERS; i++) {
          triggers[i].enabled = triggersArray[i]["enabled"] ? 1 : 0;
          triggers[i].sensorType = triggersArray[i]["sensorType"] | 0;
          triggers[i].relayNum = triggersArray[i]["relayNum"] | 1;
          triggers[i].activateValue = triggersArray[i]["activateValue"] | 3.0f;
          triggers[i].deactivateValue = triggersArray[i]["deactivateValue"] | 5.0f;
        }
        saveTriggersToEEPROM();
        server.send(200, "application/json", "{\"success\":true}");
      } else {
        server.send(400, "application/json", "{\"error\":\"Invalid number of triggers\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
    }
  });
  
  // Settings page
  server.on("/settings", HTTP_GET, [](){
    if (!authenticateRequest()) return;
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Settings</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}.container{max-width:600px;margin:0 auto}.card{background:#2a2a2a;border-radius:8px;padding:20px;margin:10px 0}.form-group{margin:15px 0}label{display:block;margin-bottom:5px}input{width:100%;padding:10px;border:1px solid #555;border-radius:5px;background:#333;color:#fff;box-sizing:border-box}.btn{background:#2196F3;color:#fff;border:none;padding:10px 20px;border-radius:5px;cursor:pointer;margin:5px}.btn:hover{background:#1976D2}.btn-danger{background:#d32f2f}.btn-danger:hover{background:#b71c1c}nav{text-align:center;margin:20px 0}nav a{color:#4CAF50;text-decoration:none;margin:0 15px}</style>";
    html += "</head><body><div class='container'><h1>Settings</h1>";
    html += "<nav><a href='/'>Dashboard</a> | <a href='/wifi'>WiFi</a> | <a href='/mqtt'>MQTT</a> | <a href='/triggers'>Triggers</a> | <a href='/settings'>Settings</a></nav>";
    
    html += "<div class='card'><form id='authForm'><h2>Web Authentication</h2>";
    html += "<div class='form-group'><label>Username:</label><input type='text' id='username' value='" + String(webAuthConfig.username) + "' required></div>";
    html += "<div class='form-group'><label>Password:</label><input type='password' id='password' value='" + String(webAuthConfig.password) + "' required></div>";
    html += "<button type='submit' class='btn'>Save Credentials</button></form></div>";
    
    html += "<div class='card'><h2>System Actions</h2>";
    html += "<button class='btn btn-danger' onclick='resetAuth()'>Reset Auth to Default</button></div>";
    
    html += "<script>document.getElementById('authForm').addEventListener('submit',e=>{e.preventDefault();";
    html += "fetch('/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:document.getElementById('username').value,password:document.getElementById('password').value})}).then(r=>r.json()).then(d=>{if(d.success){alert('Credentials saved!');location.reload();}else{alert('Error: '+d.error);}}).catch(e=>alert('Error: '+e));});";
    html += "function resetAuth(){if(confirm('Reset authentication to admin/admin?')){fetch('/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({resetAuth:true})}).then(r=>r.json()).then(d=>{if(d.success){alert('Auth reset! Page will reload.');setTimeout(()=>location.reload(),1000);}else{alert('Error: '+d.error);}}).catch(e=>alert('Error: '+e));}}</script></body></html>";
    
    server.send(200, "text/html", html);
  });
  
  server.on("/settings", HTTP_POST, [](){
    if (!authenticateRequest()) return;
    
    String body = server.arg("plain");
    if (body.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
      return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error) {
      if (doc.containsKey("resetAuth") && doc["resetAuth"]) {
        strncpy(webAuthConfig.username, "admin", sizeof(webAuthConfig.username) - 1);
        webAuthConfig.username[sizeof(webAuthConfig.username) - 1] = '\0';
        strncpy(webAuthConfig.password, "admin", sizeof(webAuthConfig.password) - 1);
        webAuthConfig.password[sizeof(webAuthConfig.password) - 1] = '\0';
        saveWebAuthConfig();
        server.send(200, "application/json", "{\"success\":true}");
      } else if (doc.containsKey("username") && doc.containsKey("password")) {
        strncpy(webAuthConfig.username, doc["username"] | "admin", sizeof(webAuthConfig.username) - 1);
        webAuthConfig.username[sizeof(webAuthConfig.username) - 1] = '\0';
        strncpy(webAuthConfig.password, doc["password"] | "admin", sizeof(webAuthConfig.password) - 1);
        webAuthConfig.password[sizeof(webAuthConfig.password) - 1] = '\0';
        saveWebAuthConfig();
        server.send(200, "application/json", "{\"success\":true}");
      } else {
        server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    }
  });
  
  // API: Get sensor data
  server.on("/api/data", HTTP_GET, [](){
    if (!authenticateRequest()) return;
    
    JsonDocument doc;
    doc["temp"] = currentTemp == INVALID_VALUE ? JsonVariant() : currentTemp;
    doc["hum"] = currentHumidity == INVALID_VALUE ? JsonVariant() : currentHumidity;
    JsonArray relays = doc.createNestedArray("relays");
    for(int i = 0; i < 4; i++) {
      relays.add(relayStates[i]);
    }
    doc["wifiStatus"] = getWiFiStatus();
    doc["ipAddr"] = wifiAPMode ? WiFi.softAPIP().toString() : (wifiConnected ? WiFi.localIP().toString() : "N/A");
    doc["mqttStatus"] = mqttConnected ? "Connected" : "Disconnected";
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // API: Control relay
  server.on("/api/relay", HTTP_POST, [](){
    if (!authenticateRequest()) return;
    
    String body = server.arg("plain");
    if (body.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
      return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error && doc.containsKey("relay") && doc.containsKey("state")) {
      int relay = doc["relay"];
      bool state = doc["state"];
      if (relay >= 1 && relay <= 4) {
        setRelay(relay, state, true);  // true = manual control via web interface
        server.send(200, "application/json", "{\"success\":true}");
      } else {
        server.send(400, "application/json", "{\"error\":\"Invalid relay number\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
    }
  });
  
  // API: Scan networks
  server.on("/api/networks", HTTP_GET, [](){
    if (!authenticateRequest()) return;
    
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray networks = doc.createNestedArray("networks");
    for(int i = 0; i < n; i++) {
      networks.add(WiFi.SSID(i));
    }
    doc["currentSSID"] = String(wifiConfig.ssid);
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // API: Get triggers
  server.on("/api/triggers", HTTP_GET, [](){
    if (!authenticateRequest()) return;
    
    JsonDocument doc;
    JsonArray triggersArray = doc.createNestedArray("triggers");
    for(int i = 0; i < MAX_TRIGGERS; i++) {
      JsonObject trigger = triggersArray.createNestedObject();
      trigger["enabled"] = triggers[i].enabled == 1;
      trigger["sensorType"] = triggers[i].sensorType;
      trigger["relayNum"] = triggers[i].relayNum;
      trigger["activateValue"] = triggers[i].activateValue;
      trigger["deactivateValue"] = triggers[i].deactivateValue;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // Handle 404 - Not Found
  server.onNotFound([](){
    Serial.printf("[WEBSERVER] ===== 404 - Path not found =====\n");
    Serial.printf("[WEBSERVER] URI: %s\n", server.uri().c_str());
    Serial.printf("[WEBSERVER] Client IP: %s\n", server.client().remoteIP().toString().c_str());
    server.send(404, "text/plain", "404: Not Found - " + server.uri());
  });
}

// Modify setup function
void setup() {
  Serial.begin(115200);
  M5.begin();
  M5.Power.begin();
  M5.Lcd.setRotation(2);
  M5.Lcd.fillScreen(COLOR_BLACK);
  
  // Create sprite with 4-bit color depth
  if (!sprite.createSprite(SCREEN_WIDTH, 120, 4)) {
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("Sprite creation failed!");
    while(1) delay(100);
  }
  
  // Create trigger list sprite (smaller size)
  if (!triggerListSprite.createSprite(SCREEN_WIDTH, 200, 4)) {  // Vergroot naar 200 pixels
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("Trigger list sprite creation failed!");
    while(1) delay(100);
  }

  // triggerHeaderSprite removed to save memory - we'll draw header directly on LCD
  
  sprite.setTextDatum(TL_DATUM);
  sprite.setSwapBytes(true);
  triggerListSprite.setTextDatum(TL_DATUM);
  triggerListSprite.setSwapBytes(true);
  
  // Initialize 4RELAY module
  while (!RELAY.begin(&Wire, MODULE_4RELAY_ADDR, 21, 22, 100000L)) {
    Serial.println("4RELAY INIT ERROR");
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("4RELAY INIT ERROR");
    delay(1000);
  }
  Serial.println("4RELAY initialized successfully");
  
  // Initialize CHT8305 with retry
  unsigned long startTime = millis();
  bool sensorFound = false;
  
  while (millis() - startTime < 5000) {
    if (CHT.begin()) {
      sensorFound = true;
      Serial.println("CHT8305 sensor found!");
      break;
    }
    delay(100);
  }
  
  if (!sensorFound) {
    M5.Lcd.fillScreen(COLOR_RED);
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
    M5.Lcd.setTextDatum(MC_DATUM);  // Middle Center datum voor perfecte centrering
    
    const char* line1 = "ERROR";
    const char* line2 = "Temperature";
    const char* line3 = "Sensor";
    const char* line4 = "not found!";
    
    const int lineHeight = 30;
    const int totalHeight = 4 * lineHeight;
    int startY = (SCREEN_HEIGHT - totalHeight) / 2;
    
    // Gebruik drawString met MC_DATUM voor perfecte centrering van elke regel
    M5.Lcd.drawString(line1, SCREEN_WIDTH / 2, startY);
    M5.Lcd.drawString(line2, SCREEN_WIDTH / 2, startY + lineHeight);
    M5.Lcd.drawString(line3, SCREEN_WIDTH / 2, startY + (2 * lineHeight));
    M5.Lcd.drawString(line4, SCREEN_WIDTH / 2, startY + (3 * lineHeight));
    
    M5.Lcd.setTextDatum(TL_DATUM);  // Reset naar Top Left datum
    delay(3000);
    M5.Lcd.fillScreen(COLOR_BLACK);
  }
  
  // Initialize EEPROM and load triggers
  EEPROM.begin(EEPROM_SIZE);
  
  byte initialized = EEPROM.read(MAIN_INIT_MARKER_ADDR);
  if (initialized != 0xAA) {
    Serial.println("Initializing triggers with default values:");
    for(int i = 0; i < MAX_TRIGGERS; i++) {
      triggers[i].enabled = 0;
      triggers[i].sensorType = 0;
      triggers[i].relayNum = 1;
      triggers[i].activateValue = 3.0f;
      triggers[i].deactivateValue = 5.0f;
      debugTrigger("Initialized", i);
    }
    saveTriggersToEEPROM();
    EEPROM.write(MAIN_INIT_MARKER_ADDR, 0xAA);
    EEPROM.commit();
  } else {
    Serial.println("Loading triggers from EEPROM:");
    loadTriggersFromEEPROM();
  }
  
  // Initialize Settings EEPROM if needed
  byte settingsInitialized = EEPROM.read(SETTINGS_INIT_MARKER_ADDR);
  if (settingsInitialized != 0xAA) {
    Serial.println("Settings EEPROM not initialized, initializing now...");
    initializeSettingsEEPROM();
  } else {
    Serial.println("Loading settings from EEPROM...");
    loadWiFiConfig();
    loadMQTTConfig();
    loadWebAuthConfig();
  }
  
  // Reset alle relay states en lastDrawnRelayStates
  for(int i = 0; i < 4; i++) {
    relayStates[i] = false;
    lastDrawnRelayStates[i] = true; // Zet op tegenovergestelde van relayStates
  }
  
  // Initialize WiFi
  initWiFi();
  
  // Give WiFi time to fully initialize (especially important for AP mode)
  if (wifiAPMode) {
    delay(500);
    Serial.printf("AP Mode active. IP: %s\n", WiFi.softAPIP().toString().c_str());
  }
  
  // Initialize Web Server (always start, works in both AP and Station mode)
  Serial.println("[WEBSERVER] Setting up web server routes...");
  setupWebServer();
  Serial.println("[WEBSERVER] Starting web server on port 80...");
  
  // For AP mode, we need to ensure the server starts after AP is fully ready
  if (wifiAPMode) {
    delay(2000);  // Extra delay for AP mode - give DHCP and AsyncTCP time to initialize
    Serial.println("[WEBSERVER] Waiting for AP and AsyncTCP to be fully ready...");
    
    // Force AsyncTCP to initialize on AP interface
    // This is sometimes needed for AsyncWebServer in AP mode
    Serial.println("[WEBSERVER] Ensuring AsyncTCP is ready...");
  }
  
  server.begin();
  Serial.println("[WEBSERVER] Web server started on port 80");
  
  // Additional verification for AP mode
  if (wifiAPMode) {
    delay(500);
    Serial.println("[WEBSERVER] Web server should now be listening on AP interface");
  }
  
  // Verify AP IP
  if (wifiAPMode) {
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[WEBSERVER] AP IP verified: %s\n", apIP.toString().c_str());
    if (apIP != AP_IP) {
      Serial.printf("[WEBSERVER] WARNING: AP IP mismatch! Expected %s, got %s\n", 
                     AP_IP.toString().c_str(), apIP.toString().c_str());
    }
    Serial.printf("[WEBSERVER] AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
    Serial.printf("[WEBSERVER] AP SSID: %s\n", WiFi.softAPSSID().c_str());
  }
  
  if (wifiAPMode) {
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[WEBSERVER] AP Mode - IP: %s\n", apIP.toString().c_str());
    Serial.printf("[WEBSERVER] Gateway: %s\n", apIP.toString().c_str());
    Serial.printf("[WEBSERVER] Subnet: %s\n", AP_SUBNET.toString().c_str());
    Serial.printf("[WEBSERVER] Connected stations: %d\n", WiFi.softAPgetStationNum());
    Serial.println("[WEBSERVER] Test route: http://192.168.4.1/test");
    Serial.println("[WEBSERVER] Main page: http://192.168.4.1/ (login: admin/admin)");
  } else if (wifiConnected) {
    Serial.printf("[WEBSERVER] Station Mode - IP: %s\n", WiFi.localIP().toString().c_str());
  }
  
  // Periodic check for connected stations (for debugging)
  static unsigned long lastStationCheck = 0;
  if (wifiAPMode && millis() - lastStationCheck > 5000) {
    lastStationCheck = millis();
    int stations = WiFi.softAPgetStationNum();
    if (stations > 0) {
      Serial.printf("[WEBSERVER] %d station(s) connected to AP\n", stations);
    }
  }
  
  // Initialize MQTT (if WiFi is connected and MQTT is enabled)
  if (wifiConnected && mqttConfig.enabled) {
    initMQTT();
  }
  
  // Force eerste tekening van het scherm
  needsRedraw = true;
  
  // Teken het hoofdscherm met force redraw
  drawMainScreen(true);
}

void loop() {
  M5.update(); 
  
  static unsigned long lastUpdate = 0;
  static unsigned long lastStationCheck = 0;
  unsigned long currentMillis = millis();
  
  // Periodic check for connected stations (for debugging)
  static int lastStationCount = 0;
  static bool serverRestartedAfterConnection = false;
  if (wifiAPMode && currentMillis - lastStationCheck > 5000) {
    lastStationCheck = currentMillis;
    int stations = WiFi.softAPgetStationNum();
    if (stations != lastStationCount) {
      lastStationCount = stations;
      Serial.printf("[WEBSERVER] Station count changed: %d station(s) connected to AP\n", stations);
      // WebServer doesn't need restart after connection, just log it
      if (stations > 0) {
        Serial.println("[WEBSERVER] Station connected - web server should be ready");
      }
    }
  }
  
  // Handle web server requests (must be called regularly)
  server.handleClient();
  
  if (currentMillis - lastUpdate >= 1000) {
    readSensorValues(); 
    checkTriggers();    
    
    if (currentTemp != lastTemp || currentHumidity != lastHumidity) {
      lastTemp = currentTemp;
      lastHumidity = currentHumidity;
      needsRedraw = true; 
    }
    lastUpdate = currentMillis;
  }
  
  // Handle WiFi reconnect (non-blocking)
  handleWiFiReconnect();
  
  // Handle MQTT (only if WiFi is connected and MQTT is enabled)
  if (mqttConfig.enabled && (wifiConnected || WiFi.status() == WL_CONNECTED)) {
    // Initialize MQTT if enabled and not yet initialized
    if (!mqttClient.connected()) {
      if (mqttClient.state() == MQTT_CONNECTION_TIMEOUT || mqttClient.state() == -1) {
        initMQTT(); // Re-initialize if needed
      }
      handleMQTTReconnect();
    }
    
    // Process MQTT messages (non-blocking) - MUST be called regularly to receive messages
    if (mqttClient.connected()) {
      mqttClient.loop();
      
      // Publish sensor data periodically
      if (currentMillis - lastMQTTPublish >= MQTT_PUBLISH_INTERVAL) {
        publishSensorData();
        lastMQTTPublish = currentMillis;
      }
    }
  }

  // --- Button Handling & Page Logic ---
  int oldPage = currentPage;
  int oldSelectedItem = selectedItem;
  int oldSelectedMenuItem = selectedMenuItem;
  bool oldIsEditingValue = isEditingValue;
  bool pageJustChanged = false; // Flag to track if we just changed page

  if (currentPage == 0) { // Main screen
    // Simple long press detection
    if (M5.BtnB.isPressed()) {
      if (longPressStartTime == 0) {
        longPressStartTime = millis();
      }
      // Check if button has been pressed for 2 seconds
      if (millis() - longPressStartTime >= 2000 && !longPressHandled) {
        // Long press detected - go to Settings immediately
        currentPage = 3;
        selectedSettingsItem = 1;  // Start at first item (not Back)
        settingsConfirmingReset = false;
        longPressHandled = true;  // Mark that we handled it
        ignoreFirstSettingsButtonRelease = true;  // Ignore the button release that will happen
        needsRedraw = true;
      }
    } else {
      // Button released
      if (longPressHandled) {
        // We just went to Settings via long press - ignore this release
        longPressHandled = false;
        longPressStartTime = 0;
      } else if (longPressStartTime > 0) {
        // Normal press (less than 2 seconds) - go to triggers list
        if (millis() - longPressStartTime < 2000) {
          currentPage = 1;
          selectedItem = 0;
        }
        longPressStartTime = 0;
      }
    }
  } else if (currentPage == 1) { // Triggers list
    if (M5.BtnC.wasReleased()) { if (selectedItem > 0) selectedItem--; }
    if (M5.BtnA.wasReleased()) { if (selectedItem < MAX_TRIGGERS) selectedItem++; }
    if (M5.BtnB.wasReleased()) {
      if (selectedItem == 0) {  // Back to Main button
        currentPage = 0;
        selectedItem = 0;
      } else {
        selectedTrigger = selectedItem;
        selectedMenuItem = 0;
        currentPage = 2;
        isEditingValue = false;
      }
    }
  } else if (currentPage == 2) { // Trigger config
     if (!isEditingValue) {
       if (M5.BtnC.wasReleased()) { if (selectedMenuItem > 0) selectedMenuItem--; }
       if (M5.BtnA.wasReleased()) { if (selectedMenuItem < 5) selectedMenuItem++; }
       if (M5.BtnB.wasReleased()) {
         if (selectedMenuItem == 5) {
           currentPage = 1;
           selectedItem = selectedTrigger;
         } else {
           isEditingValue = true;
         }
       }
     } else { // Editing value
       bool valueChanged = false;
       float step = (selectedMenuItem == 3 || selectedMenuItem == 4) ? 0.1f : 1.0f;  // 0.1° stapgrootte voor ON at en OFF at
       float change = 0;
       
       // Directe respons op button release
       if (M5.BtnC.wasReleased()) change = step;
       if (M5.BtnA.wasReleased()) change = -step;
       
       // Snellere continue waardeverandering
       if (M5.BtnC.isPressed() && M5.BtnC.pressedFor(200, 50)) change = step;
       if (M5.BtnA.isPressed() && M5.BtnA.pressedFor(200, 50)) change = -step;

        if (change != 0) {
             valueChanged = true;
             switch(selectedMenuItem) {
               case 0: triggers[selectedTrigger - 1].enabled = !triggers[selectedTrigger - 1].enabled; break;
               case 1: triggers[selectedTrigger - 1].sensorType = !triggers[selectedTrigger - 1].sensorType; break;
               case 2: 
                     if (change > 0) triggers[selectedTrigger - 1].relayNum = (triggers[selectedTrigger - 1].relayNum % 4) + 1;
                     else triggers[selectedTrigger - 1].relayNum = (triggers[selectedTrigger - 1].relayNum == 1) ? 4 : triggers[selectedTrigger - 1].relayNum - 1;
                     break; 
               case 3: triggers[selectedTrigger - 1].activateValue += change; break;
               case 4: triggers[selectedTrigger - 1].deactivateValue += change; break;
             }
             
             // Force directe redraw na waardeverandering
             needsRedraw = true;
        }
       
       if (valueChanged) { debugTrigger("After change", selectedTrigger - 1); }
       
       if (M5.BtnB.wasReleased()) {
         isEditingValue = false;
         saveTriggersToEEPROM();
         needsRedraw = true; // Force redraw na opslaan
       }
     }
  } else if (currentPage == 3) { // Settings page
    // Ignore first button release if we just entered via long press
    if (ignoreFirstSettingsButtonRelease) {
      if (M5.BtnB.wasReleased()) {
        // This is the release from the long press - ignore it
        ignoreFirstSettingsButtonRelease = false;
      }
      // Skip all button handling while ignoring
    } else if (!settingsConfirmingReset) {
      // Normal Settings page button handling
      if (M5.BtnC.wasReleased()) { if (selectedSettingsItem > 0) selectedSettingsItem--; }
      if (M5.BtnA.wasReleased()) { if (selectedSettingsItem < 2) selectedSettingsItem++; } // 0=Back, 1=WiFi Reset, 2=Auth Reset
      if (M5.BtnB.wasReleased()) {
        if (selectedSettingsItem == 0) {  // Back to Main
          currentPage = 0;
          selectedSettingsItem = 0;
          longPressHandled = false;  // Reset flag when leaving Settings
          longPressStartTime = 0;  // Reset timer
          ignoreFirstSettingsButtonRelease = false;  // Reset ignore flag
        } else if (selectedSettingsItem == 1) {  // WiFi Reset
          settingsConfirmingReset = true;
          settingsResetAction = 1;
          selectedSettingsItem = 0; // Cancel button (veiliger default)
          needsRedraw = true;
        } else if (selectedSettingsItem == 2) {  // Auth Reset
          settingsConfirmingReset = true;
          settingsResetAction = 2;
          selectedSettingsItem = 0; // Cancel button (veiliger default)
          needsRedraw = true;
        }
      }
    } else {
      // Confirmation mode
      if (M5.BtnC.wasReleased()) { selectedSettingsItem = 1; } // Yes (up)
      if (M5.BtnA.wasReleased()) { selectedSettingsItem = 0; } // Cancel (down)
      if (M5.BtnB.wasReleased()) {
        if (selectedSettingsItem == 1) {  // Confirm
          if (settingsResetAction == 1) {  // WiFi Reset
            resetWiFiConfig(); // This will restart the ESP32
          } else if (settingsResetAction == 2) {  // Auth Reset
            strncpy(webAuthConfig.username, "admin", sizeof(webAuthConfig.username) - 1);
            webAuthConfig.username[sizeof(webAuthConfig.username) - 1] = '\0';
            strncpy(webAuthConfig.password, "admin", sizeof(webAuthConfig.password) - 1);
            webAuthConfig.password[sizeof(webAuthConfig.password) - 1] = '\0';
            saveWebAuthConfig();
            settingsConfirmingReset = false;
            settingsResetAction = 0;
            selectedSettingsItem = 0;
            needsRedraw = true;
          }
        } else if (selectedSettingsItem == 0) {  // Cancel
          settingsConfirmingReset = false;
          settingsResetAction = 0;
          selectedSettingsItem = 0;
          needsRedraw = true;
        }
      }
    }
  }
  
  // Reset long press state when not on main screen
  if (currentPage != 0) {
    if (!M5.BtnB.isPressed()) {
      longPressStartTime = 0;
      if (currentPage != 3) {
        longPressHandled = false;
        ignoreFirstSettingsButtonRelease = false;
      }
    }
  }


  // --- Determine if redraw is needed ---
  if (currentPage != oldPage) {
    needsRedraw = true;
    pageJustChanged = true; // Set the flag
    M5.Lcd.fillScreen(COLOR_BLACK); // Force full clear ONLY on page change
  } else {
      // Check for changes within the current page that require redraw
      if (currentPage == 1 && selectedItem != oldSelectedItem) needsRedraw = true;
      // For page 2, also redraw if value changed (checked via memcmp in draw function now)
      if (currentPage == 2 && (selectedMenuItem != oldSelectedMenuItem || isEditingValue != oldIsEditingValue)) needsRedraw = true;
      // For page 3 (Settings), redraw if selection changed or confirmation state changed
      if (currentPage == 3) {
        static int lastSelectedSettingsItem = -1;
        static bool lastSettingsConfirmingReset = false;
        if (selectedSettingsItem != lastSelectedSettingsItem || settingsConfirmingReset != lastSettingsConfirmingReset) {
          needsRedraw = true;
          lastSelectedSettingsItem = selectedSettingsItem;
          lastSettingsConfirmingReset = settingsConfirmingReset;
        }
      }
  }

  // --- Perform redraw if needed ---
  if (needsRedraw) {
    switch(currentPage) {
      case 0:
        drawMainScreen(pageJustChanged); // Pass the flag
        break;
      case 1:
        drawTriggersListScreen(); 
        break;
      case 2:
        drawTriggerConfigScreen(pageJustChanged); // Pass the flag
        break;
      case 3:
        drawSettingsScreen(pageJustChanged); // Pass the flag
        break;
    }
    needsRedraw = false; 
  }
  
  delay(10); 
}

// Modify readSensorValues to directly set needsRedraw if values change
static unsigned long sensorErrorStartTime = 0;
static const unsigned long SENSOR_ERROR_DEBOUNCE_MS = 5000;  // 5 seconds of errors before disabling relays
static bool sensorErrorWarningShown = false;

void readSensorValues() {
  // Serial.println("\nReading sensor values...");  // Disabled to reduce Serial output
  float oldTemp = currentTemp;
  float oldHumidity = currentHumidity;
  bool oldSensorConnected = sensorConnected;
  unsigned long currentMillis = millis();

  if (CHT.read() == 0) { 
    // Sensor reading successful
    sensorConnected = true;
    sensorErrorStartTime = 0;  // Reset error timer
    sensorErrorWarningShown = false;  // Reset warning flag
    sensorWarningDisplayStart = 0;  // Reset warning display timer
    currentTemp = CHT.getTemperature();
    currentHumidity = CHT.getHumidity();
    // Serial.printf("Temperature: %.1fC, Humidity: %.1f%%\n", currentTemp, currentHumidity);  // Disabled to reduce Serial output
    if (currentTemp != oldTemp || currentHumidity != oldHumidity || sensorConnected != oldSensorConnected) {
      needsRedraw = true;
    }
  } else {
    // Sensor reading failed
    if (sensorErrorStartTime == 0) {
      // First error - start timer
      sensorErrorStartTime = currentMillis;
    }
    
    // Only disable relays if error persists for more than debounce time
    if (currentMillis - sensorErrorStartTime >= SENSOR_ERROR_DEBOUNCE_MS) {
      if (oldSensorConnected) {
        // Sensor was connected but now disconnected for > 5 seconds
        sensorConnected = false;
        currentTemp = INVALID_VALUE;
        currentHumidity = INVALID_VALUE;
        
        // Only disable relays once, not repeatedly
        if (sensorErrorWarningShown == false) {
          for(int i = 1; i <= 4; i++) { 
            setRelay(i, false, false);  // false = automatic safety shutdown
            manualRelayControl[i - 1] = false;  // Reset manual control on safety shutdown
          }
          sensorErrorWarningShown = true;
          sensorWarningDisplayStart = currentMillis;  // Start warning display timer
          
          // Show warning (non-blocking - will be redrawn in next loop)
          needsRedraw = true;
        }
      } else {
        // Sensor was already disconnected
        sensorConnected = false;
        currentTemp = INVALID_VALUE;
        currentHumidity = INVALID_VALUE;
        needsRedraw = true;
      }
    } else {
      // Error is recent (< 5 seconds) - don't disable relays yet, might be temporary
      // Keep old values but mark as potentially disconnected
      // This prevents temporary I2C glitches from disabling relays
    }
    
    // Only log error if it's persistent
    if (currentMillis - sensorErrorStartTime >= SENSOR_ERROR_DEBOUNCE_MS) {
      static unsigned long lastErrorLog = 0;
      if (currentMillis - lastErrorLog >= 10000) {  // Log every 10 seconds max
        Serial.println("Error reading sensor data (persistent)");
        lastErrorLog = currentMillis;
      }
    } else {
      // Log occasional errors (but not too often)
      static unsigned long lastErrorLog = 0;
      if (currentMillis - lastErrorLog >= 5000) {  // Log every 5 seconds max
        Serial.println("Error reading sensor data (temporary?)");
        lastErrorLog = currentMillis;
      }
    }
  }
  
  // Handle warning display (non-blocking)
  if (sensorWarningDisplayStart > 0 && currentMillis - sensorWarningDisplayStart >= SENSOR_WARNING_DISPLAY_MS) {
    // Warning display time expired - clear it
    sensorWarningDisplayStart = 0;
    needsRedraw = true;
  }
}

// Save triggers to EEPROM
void saveTriggersToEEPROM() {
  Serial.println("Saving triggers:");
  for(int i = 0; i < MAX_TRIGGERS; i++) {
    // Validate data before saving
    if (triggers[i].enabled > 1) triggers[i].enabled = 0;
    if (triggers[i].sensorType > 1) triggers[i].sensorType = 0;
    if (triggers[i].relayNum < 1 || triggers[i].relayNum > 4) triggers[i].relayNum = 1;
    
    // Check for NaN or invalid float values
    if (isnan(triggers[i].activateValue) || triggers[i].activateValue < -40 || triggers[i].activateValue > 100) {
      triggers[i].activateValue = 4.0f;
    }
    if (isnan(triggers[i].deactivateValue) || triggers[i].deactivateValue < -40 || triggers[i].deactivateValue > 100) {
      triggers[i].deactivateValue = 6.0f;
    }
    
    int addr = TRIGGER_START_ADDR + (i * TRIGGER_SIZE);
    EEPROM.put(addr, triggers[i]);
    debugTrigger("Saving", i);
  }
  
  if (EEPROM.commit()) {
    Serial.println("EEPROM save successful");
  } else {
    Serial.println("EEPROM save failed!");
  }
}

// Load triggers from EEPROM
void loadTriggersFromEEPROM() {
  Serial.println("Loading triggers from EEPROM:");
  for(int i = 0; i < MAX_TRIGGERS; i++) {
    int addr = TRIGGER_START_ADDR + (i * TRIGGER_SIZE);
    EEPROM.get(addr, triggers[i]);
    
    // Validate loaded data
    if (triggers[i].enabled > 1) triggers[i].enabled = 0;
    if (triggers[i].sensorType > 1) triggers[i].sensorType = 0;
    if (triggers[i].relayNum < 1 || triggers[i].relayNum > 4) triggers[i].relayNum = 1;
    
    // Check for NaN or invalid float values
    if (isnan(triggers[i].activateValue) || triggers[i].activateValue < -40 || triggers[i].activateValue > 100) {
      triggers[i].activateValue = 4.0f;
    }
    if (isnan(triggers[i].deactivateValue) || triggers[i].deactivateValue < -40 || triggers[i].deactivateValue > 100) {
      triggers[i].deactivateValue = 6.0f;
    }
    
    Serial.printf("Loaded Trigger %d: enabled=%d (raw byte: 0x%02X), sensor=%d, relay=%d, activate=%.1f, deactivate=%.1f\n",
      i, triggers[i].enabled, (uint8_t)triggers[i].enabled, triggers[i].sensorType,
      triggers[i].relayNum, triggers[i].activateValue, triggers[i].deactivateValue);
  }
}

// Check and apply all triggers
void checkTriggers() {
  // Als sensor niet verbonden is, zet alle relays uit (safety override)
  if (!sensorConnected) {
    for(int i = 0; i < 4; i++) {
      if(relayStates[i]) {
        setRelay(i + 1, false, false);  // false = automatic safety shutdown
        manualRelayControl[i] = false;  // Reset manual control on safety shutdown
      }
    }
    return;
  }
  
  // Check alle triggers en bepaal welke relays aan moeten
  bool shouldBeOn[4] = {false, false, false, false};
  
  // Debug info
  // Serial.println("\nChecking triggers:");  // Disabled to reduce Serial output
  // Serial.printf("Current Temp: %.1f, Humidity: %.1f\n", currentTemp, currentHumidity);  // Disabled to reduce Serial output
  
  for(int i = 0; i < MAX_TRIGGERS; i++) {
    // Debug de enabled status
    // Serial.printf("Checking Trigger %d: enabled=%d (raw byte: 0x%02X)\n", i, triggers[i].enabled, (uint8_t)triggers[i].enabled);  // Disabled to reduce Serial output
      
    if(triggers[i].enabled != 1) {  // Expliciete check op waarde 1
      // Serial.printf("Trigger %d: Disabled (enabled value = %d)\n", i, triggers[i].enabled);  // Disabled to reduce Serial output
      continue;
    }
    
    float sensorValue = triggers[i].sensorType == 0 ? currentTemp : currentHumidity;
    uint8_t relayIndex = triggers[i].relayNum - 1;
    float onAt = triggers[i].activateValue;
    float offAt = triggers[i].deactivateValue;
    bool isHeatingMode = onAt < offAt;
    
    // Serial.printf("Trigger %d: Type=%s, Relay=%d, ON at=%.1f, OFF at=%.1f, Current=%.1f, Mode=%s\n", i, triggers[i].sensorType == 0 ? "Temp" : "Hum", triggers[i].relayNum, onAt, offAt, sensorValue, isHeatingMode ? "Heating" : "Cooling");  // Disabled to reduce Serial output
    
    if (isHeatingMode) {
      // Verwarmen/bevochtigen: AAN onder de ON waarde, UIT boven de OFF waarde
      if (sensorValue <= onAt) {
        shouldBeOn[relayIndex] = true;
        // If trigger wants relay ON, reset manual control (trigger takes over)
        if (manualRelayControl[relayIndex]) {
          manualRelayControl[relayIndex] = false;
          Serial.printf("Trigger activated for relay %d - switching to automatic control\n", triggers[i].relayNum);
        }
        // Serial.printf("  -> Should activate relay %d (value %.1f <= %.1f)\n", triggers[i].relayNum, sensorValue, onAt);  // Disabled
      }
      else if (sensorValue >= offAt) {
        if (!shouldBeOn[relayIndex]) {
          // Serial.printf("  -> Should deactivate relay %d (value %.1f >= %.1f)\n", triggers[i].relayNum, sensorValue, offAt);  // Disabled
        }
      }
      else if (relayStates[relayIndex]) {
        shouldBeOn[relayIndex] = true;
        Serial.printf("  -> Keeping relay %d on (value %.1f between %.1f and %.1f)\n", 
          triggers[i].relayNum, sensorValue, onAt, offAt);
      }
    } else {
      // Koelen/ontvochtigen: AAN boven de ON waarde, UIT onder de OFF waarde
      if (sensorValue >= onAt) {
        shouldBeOn[relayIndex] = true;
        // If trigger wants relay ON, reset manual control (trigger takes over)
        if (manualRelayControl[relayIndex]) {
          manualRelayControl[relayIndex] = false;
          Serial.printf("Trigger activated for relay %d - switching to automatic control\n", triggers[i].relayNum);
        }
        Serial.printf("  -> Should activate relay %d (value %.1f >= %.1f)\n", 
          triggers[i].relayNum, sensorValue, onAt);
      }
      else if (sensorValue <= offAt) {
        if (!shouldBeOn[relayIndex]) {
          Serial.printf("  -> Should deactivate relay %d (value %.1f <= %.1f)\n", 
            triggers[i].relayNum, sensorValue, offAt);
        }
      }
      else if (relayStates[relayIndex]) {
        shouldBeOn[relayIndex] = true;
        Serial.printf("  -> Keeping relay %d on (value %.1f between %.1f and %.1f)\n", 
          triggers[i].relayNum, sensorValue, offAt, onAt);
      }
    }
  }
  
  // Pas de relay states toe (alleen voor relays die niet handmatig worden bediend)
  for(int i = 0; i < 4; i++) {
    // Skip relays that are manually controlled
    if (manualRelayControl[i]) {
      continue;  // Don't override manual control
    }
    
    if(relayStates[i] != shouldBeOn[i]) {
      setRelay(i + 1, shouldBeOn[i], false);  // false = automatic control by triggers
      Serial.printf("Relay %d state changed to %s (automatic)\n", i + 1, shouldBeOn[i] ? "ON" : "OFF");
    }
  }
}

// Add boolean parameter 'forceRedraw'
void drawMainScreen(bool forceRedraw) { 
  // Check if we need to show sensor warning (non-blocking)
  unsigned long currentMillis = millis();
  bool showWarning = (sensorWarningDisplayStart > 0 && 
                      currentMillis - sensorWarningDisplayStart < SENSOR_WARNING_DISPLAY_MS);
  
  // Draw top part (Temp/Hum) on sprite
  sprite.fillSprite(COLOR_BLACK); 
  
  // Show warning overlay if needed
  if (showWarning) {
    sprite.fillRect(5, 5, SCREEN_WIDTH-10, 110, COLOR_RED);
    sprite.setTextColor(COLOR_WHITE);
    sprite.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString("WARNING", SCREEN_WIDTH / 2, 30);
    sprite.drawString("SENSOR", SCREEN_WIDTH / 2, 55);
    sprite.drawString("DISCONNECTED", SCREEN_WIDTH / 2, 80);
    sprite.setTextDatum(TL_DATUM);
  } else {
    // Normal display
    // Temperature Box (on sprite)
    sprite.drawRect(5, 5, SCREEN_WIDTH-10, 50, COLOR_WHITE);
    sprite.setTextColor(COLOR_WHITE);
    sprite.setFreeFont(FFSMALL);  // TomThumb - zeer kleine font voor labels
    sprite.setCursor(10, 12);  // 2 pixels naar beneden (was 10)
    sprite.print("TEMPERATURE");
    sprite.setFreeFont(FF18B);  // FreeSansBold18pt7b voor grote tekst bold (waarden)
    sprite.setTextDatum(MC_DATUM); 
    if (currentTemp == INVALID_VALUE) { sprite.drawString("--.-C", SCREEN_WIDTH / 2, 30); }  // 5 pixels hoger (was 35)
    else { char tempStr[10]; sprintf(tempStr, "%.1fC", currentTemp); sprite.drawString(tempStr, SCREEN_WIDTH / 2, 30); }  // 5 pixels hoger (was 35)
    
    // Humidity Box (on sprite)
    sprite.drawRect(5, 60, SCREEN_WIDTH-10, 50, COLOR_WHITE);
    sprite.setFreeFont(FFSMALL);  // TomThumb - zeer kleine font voor labels
    sprite.setCursor(10, 67);  // 2 pixels naar beneden (was 65)
    sprite.print("HUMIDITY");
    sprite.setFreeFont(FF18B);  // FreeSansBold18pt7b voor grote tekst bold (waarden)
    if (currentHumidity == INVALID_VALUE) { sprite.drawString("--.-%%", SCREEN_WIDTH / 2, 84); }  // 4 pixels naar beneden (was 80)
    else { char humStr[10]; sprintf(humStr, "%.1f%%", currentHumidity); sprite.drawString(humStr, SCREEN_WIDTH / 2, 84); }  // 4 pixels naar beneden (was 80)
    sprite.setTextDatum(TL_DATUM);
  }

  // Push the completed sprite for the top part
  sprite.pushSprite(0, 0);

  // --- Draw relay states directly on LCD (Below 120px) ---
  // Als forceRedraw true is, reset de lastDrawnRelayStates naar de tegenovergestelde waarden
  if (forceRedraw) {
    for(int i = 0; i < 4; i++) {
      lastDrawnRelayStates[i] = !relayStates[i];
    }
  }

  const int relayBoxYStart = 125; 
  const int relayBoxHeight = 35;
  const int relayBoxSpacing = 5;

  // Teken alle relay-boxen
  for(int i = 0; i < 4; i++) {
    int y = relayBoxYStart + (i * (relayBoxHeight + relayBoxSpacing));
    if (y + relayBoxHeight > SCREEN_HEIGHT) break; 

    bool currentState = getRelay(i + 1); 

    // Teken opnieuw als forceRedraw true is OF als de state is veranderd
    if (forceRedraw || currentState != lastDrawnRelayStates[i]) { 
      uint16_t bgColor = currentState ? COLOR_RELAY_ON : COLOR_RELAY_OFF;
      uint16_t fgColor = currentState ? COLOR_WHITE : COLOR_BLACK;
      const char* statusText = currentState ? "ON" : "OFF";

      // Wis eerst het gebied
      M5.Lcd.fillRect(5, y, SCREEN_WIDTH-10, relayBoxHeight, COLOR_BLACK);
      
      // Teken de box
      M5.Lcd.fillRect(5, y, SCREEN_WIDTH-10, relayBoxHeight, bgColor);
      M5.Lcd.drawRect(5, y, SCREEN_WIDTH-10, relayBoxHeight, COLOR_WHITE);
      
      M5.Lcd.setTextColor(fgColor, bgColor); 
      M5.Lcd.setTextDatum(MC_DATUM); 
      M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
      char relayStr[20];
      sprintf(relayStr, "RELAY %d %s", i + 1, statusText);
      M5.Lcd.drawString(relayStr, SCREEN_WIDTH / 2, y + relayBoxHeight / 2); 

      lastDrawnRelayStates[i] = currentState; 
    }
  }
  M5.Lcd.setTextDatum(TL_DATUM); 
}


void drawTriggersListScreen() {
  // Draw header and back button directly on LCD (no sprite to save memory)
  // Clear header area
  M5.Lcd.fillRect(0, 0, SCREEN_WIDTH, 80, COLOR_BLACK);
  
  // Draw header
  M5.Lcd.drawRect(5, 5, SCREEN_WIDTH-10, 30, COLOR_WHITE);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setTextDatum(MC_DATUM); 
  M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
  M5.Lcd.drawString("TRIGGERS", SCREEN_WIDTH / 2, 20); 
  
  // Draw back button
  uint16_t backButtonColor = (selectedItem == 0) ? COLOR_BLUE : COLOR_DARKGRAY;
  M5.Lcd.fillRect(5, 40, SCREEN_WIDTH-10, 35, backButtonColor);
  M5.Lcd.drawRect(5, 40, SCREEN_WIDTH-10, 35, COLOR_WHITE);
  M5.Lcd.setTextColor(COLOR_WHITE, backButtonColor);
  M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.drawString("Back to Main", SCREEN_WIDTH / 2, 57);
  
  M5.Lcd.setTextDatum(TL_DATUM);

  // Calculate scroll parameters
  const int TRIGGER_HEIGHT = 35;
  const int TRIGGER_SPACING = 5;
  const int LIST_START_Y = 0; // Start at top of sprite
  const int TOTAL_LIST_ITEMS = MAX_TRIGGERS; // Nu alleen de triggers
  const int VISIBLE_ITEMS = 5; // Aantal items dat zichtbaar is in de sprite
  
  // Calculate scroll position
  static int currentScrollPosition = 0;
  
  if (selectedItem > 0) {  // Alleen scrollen als we een trigger selecteren
    int selectedItemIndex = selectedItem - 1;  // -1 omdat back button index 0 is
    int selectedItemTopY = selectedItemIndex * (TRIGGER_HEIGHT + TRIGGER_SPACING);
    int selectedItemBottomY = selectedItemTopY + TRIGGER_HEIGHT;
    
    // Bepaal of we moeten scrollen
    if (selectedItemBottomY > currentScrollPosition + 200) {
      currentScrollPosition = selectedItemBottomY - 200 + TRIGGER_SPACING;
    } else if (selectedItemTopY < currentScrollPosition) {
      currentScrollPosition = selectedItemTopY;
    }
    
    // Beperk de scroll positie
    int maxScroll = (TOTAL_LIST_ITEMS - VISIBLE_ITEMS) * (TRIGGER_HEIGHT + TRIGGER_SPACING);
    currentScrollPosition = std::max(0, std::min(currentScrollPosition, maxScroll));
  } else {
    currentScrollPosition = 0;  // Reset scroll positie als we de back button selecteren
  }

  // Clear the trigger list sprite
  triggerListSprite.fillSprite(COLOR_BLACK);

  // Bepaal welke triggers we moeten tekenen
  int startIndex = currentScrollPosition / (TRIGGER_HEIGHT + TRIGGER_SPACING);
  int endIndex = std::min(startIndex + VISIBLE_ITEMS + 1, MAX_TRIGGERS);

  // Draw visible triggers in the list sprite
  int y = LIST_START_Y;
  for(int i = startIndex; i < endIndex; i++) {
    uint16_t boxColor = (i + 1 == selectedItem) ? COLOR_BLUE : COLOR_DARKGRAY;
    uint16_t borderColor = COLOR_WHITE;
    uint16_t textColor = COLOR_WHITE;

    // Draw item box
    triggerListSprite.fillRect(5, y, SCREEN_WIDTH - 10, TRIGGER_HEIGHT, boxColor);
    triggerListSprite.drawRect(5, y, SCREEN_WIDTH - 10, TRIGGER_HEIGHT, borderColor);
    triggerListSprite.setTextColor(textColor, boxColor);

    triggerListSprite.setTextDatum(TL_DATUM); 
    triggerListSprite.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
    
    char line1[30];
    snprintf(line1, sizeof(line1), "%d) %s %s R%d", i + 1, triggers[i].enabled ? "ON " : "OFF", triggers[i].sensorType == 0 ? "Temp" : "Hum ", triggers[i].relayNum);
    triggerListSprite.drawString(line1, 10, y + 5); 

    triggerListSprite.setFreeFont(FFSMALL);  // TomThumb - zeer kleine font voor details
    char line2[30];
    snprintf(line2, sizeof(line2), "ON:%.1f / OFF:%.1f %s", triggers[i].activateValue, triggers[i].deactivateValue, triggers[i].sensorType == 0 ? "C" : "%");
    triggerListSprite.drawString(line2, 10, y + 26);  // Nog 2 pixels lager (was 24) 
    
    y += TRIGGER_HEIGHT + TRIGGER_SPACING;
  }

  // Push the list sprite to the screen with scroll offset
  triggerListSprite.pushSprite(0, 80 - (currentScrollPosition % (TRIGGER_HEIGHT + TRIGGER_SPACING)));

  // Draw scroll indicator at the bottom of the screen
  const int indicatorY = SCREEN_HEIGHT - 30; // 30 pixels from bottom
  const int indicatorSize = 20;
  const int indicatorX = (SCREEN_WIDTH - indicatorSize) / 2; // Center horizontally
  
  // Clear indicator area
  M5.Lcd.fillRect(indicatorX, indicatorY, indicatorSize, indicatorSize, COLOR_BLACK);
  
  // Draw appropriate arrow based on scroll position
  if (currentScrollPosition == 0) {
    // At top, show down arrow
    M5.Lcd.fillTriangle(
      indicatorX + indicatorSize/2, indicatorY + indicatorSize,  // Bottom point
      indicatorX, indicatorY,                                   // Top left
      indicatorX + indicatorSize, indicatorY,                   // Top right
      COLOR_WHITE                                              // Color
    );
  } else if (currentScrollPosition >= (MAX_TRIGGERS - VISIBLE_ITEMS) * (TRIGGER_HEIGHT + TRIGGER_SPACING)) {
    // At bottom, show up arrow
    M5.Lcd.fillTriangle(
      indicatorX + indicatorSize/2, indicatorY,                 // Top point
      indicatorX, indicatorY + indicatorSize,                   // Bottom left
      indicatorX + indicatorSize, indicatorY + indicatorSize,   // Bottom right
      COLOR_WHITE                                              // Color
    );
  } else {
    // In middle, show both arrows
    M5.Lcd.fillTriangle(
      indicatorX + indicatorSize/2, indicatorY + indicatorSize/2,  // Center point
      indicatorX, indicatorY,                                      // Top left
      indicatorX + indicatorSize, indicatorY,                      // Top right
      COLOR_WHITE                                                 // Color
    );
    M5.Lcd.fillTriangle(
      indicatorX + indicatorSize/2, indicatorY + indicatorSize/2,  // Center point
      indicatorX, indicatorY + indicatorSize,                      // Bottom left
      indicatorX + indicatorSize, indicatorY + indicatorSize,      // Bottom right
      COLOR_WHITE                                                 // Color
    );
  }
}

// Add boolean parameter 'forceRedraw'
void drawTriggerConfigScreen(bool forceRedraw) { 
  // Draw header and first 2 items on sprite
  sprite.fillSprite(COLOR_BLACK); 
  sprite.setTextDatum(TL_DATUM); // Reset just in case

  // Header
  sprite.drawRect(5, 5, SCREEN_WIDTH-10, 30, COLOR_WHITE);
  sprite.setTextColor(COLOR_WHITE);
  sprite.setTextDatum(MC_DATUM); 
  sprite.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
  
  char headerText[20];
  if (isEditingValue) {
    snprintf(headerText, sizeof(headerText), "EDIT VALUE");
  } else {
    snprintf(headerText, sizeof(headerText), "TRIGGER %d", selectedTrigger);
  }
  sprite.drawString(headerText, SCREEN_WIDTH / 2, 20);
  
  // Items on sprite
  const int itemHeight = 35;
  const int spacing = 5;
  int y_sprite = 40; 
  
  // Enabled status (Item 0) - on sprite
   uint16_t boxColor0 = (selectedMenuItem == 0) ? (isEditingValue ? COLOR_RED : COLOR_BLUE) : COLOR_DARKGRAY;
   sprite.fillRect(5, y_sprite, SCREEN_WIDTH-10, itemHeight, boxColor0);
   sprite.drawRect(5, y_sprite, SCREEN_WIDTH-10, itemHeight, COLOR_WHITE);
   sprite.setTextColor(COLOR_WHITE, boxColor0); // Set background color
   sprite.setTextDatum(MC_DATUM);
   sprite.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
   char enabledText[20];
   snprintf(enabledText, sizeof(enabledText), "Enabled: %s", triggers[selectedTrigger - 1].enabled ? "Yes" : "No");  // -1 voor correcte index
   sprite.drawString(enabledText, SCREEN_WIDTH / 2, y_sprite + itemHeight / 2);
   y_sprite += itemHeight + spacing;

  // Sensor type (Item 1) - on sprite
   uint16_t boxColor1 = (selectedMenuItem == 1) ? (isEditingValue ? COLOR_RED : COLOR_BLUE) : COLOR_DARKGRAY;
   sprite.fillRect(5, y_sprite, SCREEN_WIDTH-10, itemHeight, boxColor1);
   sprite.drawRect(5, y_sprite, SCREEN_WIDTH-10, itemHeight, COLOR_WHITE);
   sprite.setTextColor(COLOR_WHITE, boxColor1); // Set background color
   sprite.setTextDatum(MC_DATUM);
   sprite.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
   char sensorText[30];
   snprintf(sensorText, sizeof(sensorText), "Sensor: %s", triggers[selectedTrigger - 1].sensorType == 0 ? "Temperature" : "Humidity");  // -1 voor correcte index
   sprite.drawString(sensorText, SCREEN_WIDTH / 2, y_sprite + itemHeight / 2);
   
  sprite.setTextDatum(TL_DATUM); // Reset sprite datum
  
  // Push the sprite
  sprite.pushSprite(0, 0);

  // --- Items drawn directly on LCD (Below 120px) ---
  // Track last drawn state for selective redraw
  static int lastDrawnMenuItem = -1;
  static bool lastDrawnIsEditing = !isEditingValue;
  static Trigger lastDrawnTrigger; // Store whole trigger state for comparison
  
  // Determine if bottom needs full redraw
  // Force redraw if flag is set OR if relevant state changed significantly
   bool forceBottomRedraw = forceRedraw || (isEditingValue != lastDrawnIsEditing);
   if (memcmp(&triggers[selectedTrigger - 1], &lastDrawnTrigger, sizeof(Trigger)) != 0) {  // -1 voor correcte index
       forceBottomRedraw = true; // Also force if trigger data changed
       memcpy(&lastDrawnTrigger, &triggers[selectedTrigger - 1], sizeof(Trigger));  // -1 voor correcte index
   }
   
   // If forced, reset last drawn menu item to ensure highlights update
   if (forceBottomRedraw) {
       lastDrawnMenuItem = -1; 
       // No need to clear LCD here, drawing commands overwrite or use background color
   }


  int y_lcd = 125; 

  for (int itemIndex = 2; itemIndex <= 5; ++itemIndex) {
       if (y_lcd + itemHeight > SCREEN_HEIGHT) break; 

      bool isSelected = (selectedMenuItem == itemIndex);
      uint16_t boxColor = isSelected ? (isEditingValue ? COLOR_RED : COLOR_BLUE) : COLOR_DARKGRAY;
      
      char itemText[30];
      switch(itemIndex) {
          case 2: snprintf(itemText, sizeof(itemText), "Relay: %d", triggers[selectedTrigger - 1].relayNum); break;  // -1 voor correcte index
          case 3: snprintf(itemText, sizeof(itemText), "ON at: %.1f%s", triggers[selectedTrigger - 1].activateValue, triggers[selectedTrigger - 1].sensorType == 0 ? "C" : "%"); break;  // -1 voor correcte index
          case 4: snprintf(itemText, sizeof(itemText), "OFF at: %.1f%s", triggers[selectedTrigger - 1].deactivateValue, triggers[selectedTrigger - 1].sensorType == 0 ? "C" : "%"); break;  // -1 voor correcte index
          case 5: snprintf(itemText, sizeof(itemText), "Back to Triggers"); boxColor = isSelected ? COLOR_BLUE : COLOR_DARKGRAY; break; 
      }
      
      // Redraw if forced OR if selection changed for this item
      if (forceBottomRedraw || (isSelected != (lastDrawnMenuItem == itemIndex)) ) { 
           M5.Lcd.fillRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight, boxColor);
           M5.Lcd.drawRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight, COLOR_WHITE);
           M5.Lcd.setTextColor(COLOR_WHITE, boxColor); 
           M5.Lcd.setTextDatum(MC_DATUM);
           M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
           M5.Lcd.drawString(itemText, SCREEN_WIDTH / 2, y_lcd + itemHeight / 2);
      }
      y_lcd += itemHeight + spacing;
  }

  // Update tracking variables *after* drawing
  lastDrawnMenuItem = selectedMenuItem; 
  lastDrawnIsEditing = isEditingValue; // Update edit state tracking
  // lastDrawnTrigger already updated if memcmp detected changes
  
  M5.Lcd.setTextDatum(TL_DATUM); 
}

void drawSettingsScreen(bool forceRedraw) {
  // Draw header on sprite
  sprite.fillSprite(COLOR_BLACK);
  sprite.setTextDatum(TL_DATUM);
  
  // Header
  sprite.drawRect(5, 5, SCREEN_WIDTH-10, 30, COLOR_WHITE);
  sprite.setTextColor(COLOR_WHITE);
  sprite.setTextDatum(MC_DATUM);
  sprite.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
  sprite.drawString("SETTINGS", SCREEN_WIDTH / 2, 20);
  sprite.setTextDatum(TL_DATUM);
  
  // Push header sprite
  sprite.pushSprite(0, 0);
  
  // Draw items directly on LCD (Below 40px)
  const int itemHeight = 35;
  const int spacing = 5;
  int y_lcd = 45;
  
  if (settingsConfirmingReset) {
    // Confirmation screen
    // Clear area above buttons
    M5.Lcd.fillRect(5, y_lcd, SCREEN_WIDTH-10, SCREEN_HEIGHT - y_lcd, COLOR_BLACK);
    M5.Lcd.setTextColor(COLOR_RED);
    M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
    M5.Lcd.setTextDatum(MC_DATUM);  // Middle Center voor centrering
    // Tekst 70 pixels lager en gecentreerd
    int textY = y_lcd + 70;
    if (settingsResetAction == 1) {
      M5.Lcd.drawString("Reset WiFi?", SCREEN_WIDTH / 2, textY);
    } else if (settingsResetAction == 2) {
      M5.Lcd.drawString("Reset Auth?", SCREEN_WIDTH / 2, textY);
    }
    M5.Lcd.setTextDatum(TL_DATUM);  // Terug naar Top Left
    
    // Calculate position for buttons at bottom (2 items: Yes and Cancel)
    int totalItemsHeight = 2 * itemHeight + spacing; // 2 items + 1 spacing
    int buttonStartY = SCREEN_HEIGHT - totalItemsHeight;
    
    // Draw Yes button
    y_lcd = buttonStartY;
    uint16_t confirmColor = (selectedSettingsItem == 1) ? COLOR_BLUE : COLOR_DARKGRAY;
    M5.Lcd.fillRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight, confirmColor);
    M5.Lcd.drawRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight, COLOR_WHITE);
    M5.Lcd.setTextColor(COLOR_WHITE, confirmColor);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
    M5.Lcd.drawString("Yes - Reset", SCREEN_WIDTH / 2, y_lcd + itemHeight / 2);
    
    // Draw Cancel button
    y_lcd += itemHeight + spacing;
    uint16_t cancelColor = (selectedSettingsItem == 0) ? COLOR_BLUE : COLOR_DARKGRAY;
    M5.Lcd.fillRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight, cancelColor);
    M5.Lcd.drawRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight, COLOR_WHITE);
    M5.Lcd.setTextColor(COLOR_WHITE, cancelColor);
    M5.Lcd.drawString("Cancel", SCREEN_WIDTH / 2, y_lcd + itemHeight / 2);
  } else {
    // Normal settings screen
    // Network Information - 10 pixels lager
    y_lcd += 10;  // Network info 10 pixels lager
    M5.Lcd.fillRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight * 2 + spacing, COLOR_BLACK);
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setFreeFont(FF1);  // FreeSans9pt7b - tussen TomThumb en FF12 in (9pt)
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setCursor(10, y_lcd);
    M5.Lcd.print("Network:");
    y_lcd += 16;  // 1 pixel extra regelafstand (was 15)
    
    // Labels links, waarden rechts uitgelijnd
    String wifiStatus = getWiFiStatus();
    String ipAddr = wifiAPMode ? WiFi.softAPIP().toString() : (wifiConnected ? WiFi.localIP().toString() : "N/A");
    String ssid = String(wifiConfig.ssid);
    if (ssid.length() == 0) ssid = "No Conf.";
    
    // SSID: label links, waarde rechts - beide op dezelfde y-positie
    M5.Lcd.setTextDatum(TL_DATUM);  // Top Left voor labels
    M5.Lcd.drawString("SSID:", 10, y_lcd);
    M5.Lcd.setTextDatum(TR_DATUM);  // Top Right voor rechts uitlijning
    M5.Lcd.drawString(ssid.substring(0, 18).c_str(), SCREEN_WIDTH - 10, y_lcd);
    y_lcd += 16;  // 1 pixel extra regelafstand (was 15)
    
    // IP: label links, waarde rechts - beide op dezelfde y-positie
    M5.Lcd.setTextDatum(TL_DATUM);  // Top Left voor labels
    M5.Lcd.drawString("IP:", 10, y_lcd);
    M5.Lcd.setTextDatum(TR_DATUM);  // Top Right voor rechts uitlijning
    M5.Lcd.drawString(ipAddr.substring(0, 18).c_str(), SCREEN_WIDTH - 10, y_lcd);
    y_lcd += 16;  // 1 pixel extra regelafstand (was 15)
    
    // Status: label links, waarde rechts - beide op dezelfde y-positie
    M5.Lcd.setTextDatum(TL_DATUM);  // Top Left voor labels
    M5.Lcd.drawString("Status:", 10, y_lcd);
    M5.Lcd.setTextDatum(TR_DATUM);  // Top Right voor rechts uitlijning
    M5.Lcd.drawString(wifiStatus.substring(0, 15).c_str(), SCREEN_WIDTH - 10, y_lcd);
    M5.Lcd.setTextDatum(TL_DATUM);  // Terug naar Top Left
    // RSSI info weggelaten
    y_lcd += 20;
    
    // MQTT Information - 20 pixels lager (was 15, nu 5 pixels extra)
    y_lcd += 20;  // MQTT info 20 pixels lager (was 15)
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setFreeFont(FF1);  // FreeSans9pt7b - tussen TomThumb en FF12 in (9pt)
    M5.Lcd.setCursor(10, y_lcd);
    M5.Lcd.print("MQTT:");
    y_lcd += 16;  // 1 pixel extra regelafstand (was 15)
    
    // Labels links, waarden rechts uitgelijnd
    String mqttStatus = mqttConnected ? "Connected" : "Disconnected";
    if (!mqttConfig.enabled) mqttStatus = "Disabled";
    
    // Status: label links, waarde rechts - beide op dezelfde y-positie
    M5.Lcd.setTextDatum(TL_DATUM);  // Top Left voor labels
    M5.Lcd.drawString("Status:", 10, y_lcd);
    M5.Lcd.setTextDatum(TR_DATUM);  // Top Right voor rechts uitlijning
    M5.Lcd.drawString(mqttStatus.substring(0, 15).c_str(), SCREEN_WIDTH - 10, y_lcd);
    M5.Lcd.setTextDatum(TL_DATUM);  // Terug naar Top Left
    if (mqttConfig.enabled && strlen(mqttConfig.brokerIP) > 0) {
      y_lcd += 16;  // 1 pixel extra regelafstand (was 15)
      // Broker: label links, waarde rechts - beide op dezelfde y-positie
      char brokerStr[50];
      snprintf(brokerStr, sizeof(brokerStr), "%s:%d", mqttConfig.brokerIP, mqttConfig.brokerPort);
      M5.Lcd.setTextDatum(TL_DATUM);  // Top Left voor labels
      M5.Lcd.drawString("Broker:", 10, y_lcd);
      M5.Lcd.setTextDatum(TR_DATUM);  // Top Right voor rechts uitlijning
      M5.Lcd.drawString(brokerStr, SCREEN_WIDTH - 10, y_lcd);
      M5.Lcd.setTextDatum(TL_DATUM);  // Terug naar Top Left
    }
    y_lcd += 25;
    
    // Clear area below info and above buttons
    int buttonAreaStart = SCREEN_HEIGHT - (3 * itemHeight + 2 * spacing); // 3 items + 2 spacings
    if (y_lcd < buttonAreaStart) {
      M5.Lcd.fillRect(5, y_lcd, SCREEN_WIDTH-10, buttonAreaStart - y_lcd, COLOR_BLACK);
    }
    
    // Action items - positioned at bottom
    int totalItemsHeight = 3 * itemHeight + 2 * spacing; // 3 items + 2 spacings
    int buttonStartY = SCREEN_HEIGHT - totalItemsHeight;
    
    for (int itemIndex = 0; itemIndex <= 2; itemIndex++) {
      y_lcd = buttonStartY + itemIndex * (itemHeight + spacing);
      
      bool isSelected = (selectedSettingsItem == itemIndex);
      uint16_t boxColor = isSelected ? COLOR_BLUE : COLOR_DARKGRAY;
      
      char itemText[30];
      switch(itemIndex) {
        case 0: snprintf(itemText, sizeof(itemText), "Back to Main"); break;
        case 1: snprintf(itemText, sizeof(itemText), "Reset WiFi Config"); break;
        case 2: snprintf(itemText, sizeof(itemText), "Reset Auth Config"); break;
      }
      
      M5.Lcd.fillRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight, boxColor);
      M5.Lcd.drawRect(5, y_lcd, SCREEN_WIDTH-10, itemHeight, COLOR_WHITE);
      M5.Lcd.setTextColor(COLOR_WHITE, boxColor);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.setFreeFont(FF12);  // FreeSans12pt7b voor normale tekst
      M5.Lcd.drawString(itemText, SCREEN_WIDTH / 2, y_lcd + itemHeight / 2);
    }
  }
  
  M5.Lcd.setTextDatum(TL_DATUM);
}
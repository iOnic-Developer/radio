// ========================================================================
// ==       M5Core2 Internet Radio - CJ's Fish & Chips Radio           ==
// ========================================================================
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <vector>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioLibs/A2DPStream.h"

// ========================================================================
//                          USER CONFIGURATION
// ========================================================================
#define WIFI_SSID "David's WiFi"
#define WIFI_PASSWORD "Pulseshun22"
#define BT_SPEAKER_NAME "WZ-BT5.0"
// ========================================================================

// --- System State Enum ---
enum SystemState {
    STATE_INIT,
    STATE_CONNECTING_WIFI,
    STATE_CONNECTING_BT,
    STATE_READY,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_ERROR
};

// --- Radio Station Structure ---
struct RadioStation {
  const char* name;
  const char* url;
};

// --- Radio Stations List ---
std::vector<RadioStation> stations = {
  {"Capital FM", "http://media-ice.musicradio.com/CapitalMP3"},
  {"Heart UK", "http://media-ice.musicradio.com/HeartLondonMP3"},
  {"BBC Radio 1", "http://bbcmedia.ic.llnwd.net/stream/bbcmedia_radio1_mf_p"},
  {"BBC Radio 2", "http://bbcmedia.ic.llnwd.net/stream/bbcmedia_radio2_mf_p"},
  {"BBC Radio 3", "http://bbcmedia.ic.llnwd.net/stream/bbcmedia_radio3_mf_p"},
  {"BBC Radio 4", "http://bbcmedia.ic.llnwd.net/stream/bbcmedia_radio4fm_mf_p"},
  {"Classic FM", "http://media-ice.musicradio.com/ClassicFMMP3"},
  {"Smooth Radio", "http://media-ice.musicradio.com/SmoothLondonMP3"},
  {"LBC News", "http://media-ice.musicradio.com/LBC973MP3"},
  {"Kiss FM UK", "http://media-ice.musicradio.com/KissMP3"},
  {"Absolute Radio", "http://media-ice.musicradio.com/AbsoluteRadioMP3"},
  {"Radio X UK", "http://media-ice.musicradio.com/RadioXMP3"},
  {"BBC World Service", "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service"},
  {"BassDrive", "http://ice.bassdrive.net:80/stream"}
};

int currentStationIndex = 0;
String statusMessage = "";
SystemState currentState = STATE_INIT;
bool wifiConnected = false;
bool btConnected = false;
bool urlError = false; // Track if we had a URL error

// --- Audio Engine Objects ---
A2DPStream a2dp_stream;
URLStream url_stream;
EncodedAudioStream *decoder = nullptr;
StreamCopy copier;

// --- GUI Constants ---
const int STATUS_BAR_HEIGHT = 30;
const int BUTTON_BAR_HEIGHT = 40;

// Modern Color Scheme
#define BACKGROUND M5.Lcd.color565(25, 25, 35)    // Dark blue-gray
#define ACCENT M5.Lcd.color565(0, 150, 200)       // Bright blue
#define TEXT_COLOR M5.Lcd.color565(240, 240, 240) // Off-white
#define WIFI_COLOR M5.Lcd.color565(80, 180, 80)   // Green for connected
#define WIFI_OFF_COLOR M5.Lcd.color565(180, 80, 80) // Red for disconnected
#define BT_COLOR M5.Lcd.color565(80, 130, 220)    // Blue for BT
#define ERROR_COLOR M5.Lcd.color565(220, 80, 80)  // Red for errors

// --- Function Prototypes ---
void stopAudio();
bool startAudio();
void drawGUI();
void updateStatusBar();
void drawNowPlaying();
void drawButtonLabels();
void changeState(SystemState newState);
void drawBootScreen(const String& message);
void checkConnections();
void fullCleanup(); // Added for proper cleanup after errors

// ========================================================================
//                         AUDIO FUNCTIONS
// ========================================================================
void fullCleanup() {
    // Completely clean up all audio resources
    copier.end();
    url_stream.end();
    if (decoder) {
        decoder->end();
        delete decoder;
        decoder = nullptr;
    }
    // Add a small delay to ensure resources are freed
    delay(100);
}

void stopAudio() {
    if (currentState == STATE_PLAYING) {
        fullCleanup();
        Serial.println("Audio stopped.");
        changeState(STATE_PAUSED);
    }
}

bool startAudio() {
    // Always do a full cleanup before starting new audio
    fullCleanup();
    
    if (!wifiConnected || !btConnected) {
        statusMessage = "Check Connections";
        drawNowPlaying();
        return false;
    }

    Serial.printf("Starting stream from: %s\n", stations[currentStationIndex].url);
    statusMessage = "Connecting...";
    urlError = false;
    drawNowPlaying();

    // Reset URL stream by ending and beginning again
    url_stream.setTimeout(8000); // 8 second timeout
    
    if (!url_stream.begin(stations[currentStationIndex].url, "audio/mpeg")) {
        Serial.println("Error: URL stream failed to begin!");
        statusMessage = "Stream Failed!";
        urlError = true;
        fullCleanup(); // Clean up on failure
        drawNowPlaying();
        return false;
    }

    // Create fresh decoder instance
    decoder = new EncodedAudioStream(&a2dp_stream, new MP3DecoderHelix());
    if (!decoder->begin()) {
        Serial.println("Error: Decoder failed to begin!");
        statusMessage = "Decoder Error!";
        urlError = true;
        fullCleanup();
        drawNowPlaying();
        return false;
    }

    // Start the copier
    copier.begin(*decoder, url_stream);
    statusMessage = "Playing";
    Serial.println("Playback started!");
    changeState(STATE_PLAYING);
    return true;
}

// ========================================================================
//                            GUI FUNCTIONS
// ========================================================================
void drawBootScreen(const String& message) {
    M5.Lcd.fillScreen(BACKGROUND);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(ACCENT, BACKGROUND);
    M5.Lcd.setFont(&fonts::Font4);
    M5.Lcd.drawString("CJ's Fish & Chips Radio", M5.Lcd.width() / 2, M5.Lcd.height() / 2 - 30);
    
    M5.Lcd.setTextColor(TEXT_COLOR, BACKGROUND);
    M5.Lcd.setFont(&fonts::Font2);
    M5.Lcd.drawString(message, M5.Lcd.width() / 2, M5.Lcd.height() / 2 + 10);
}

void updateStatusBar() {
    int w = M5.Lcd.width();
    int h = STATUS_BAR_HEIGHT;
    
    M5.Lcd.fillRect(0, 0, w, h, BACKGROUND);
    
    // Draw WiFi indicator
    M5.Lcd.fillCircle(20, h/2, 8, wifiConnected ? WIFI_COLOR : WIFI_OFF_COLOR);
    M5.Lcd.setTextColor(TEXT_COLOR, BACKGROUND);
    M5.Lcd.setFont(&fonts::Font0);
    M5.Lcd.drawString(wifiConnected ? "WiFi" : "No WiFi", 35, h/2 - 5);
    
    // Draw BT indicator
    M5.Lcd.fillCircle(w - 50, h/2, 8, btConnected ? BT_COLOR : WIFI_OFF_COLOR);
    M5.Lcd.drawString(btConnected ? "BT" : "No BT", w - 35, h/2 - 5);
    
    // Draw battery indicator
    int batteryLevel = M5.Power.getBatteryLevel();
    M5.Lcd.fillRect(w - 90, h/2 - 8, 25, 16, BACKGROUND);
    M5.Lcd.drawRect(w - 90, h/2 - 8, 25, 16, TEXT_COLOR);
    M5.Lcd.fillRect(w - 90, h/2 - 8, batteryLevel * 25 / 100, 16, 
                   batteryLevel > 30 ? WIFI_COLOR : WIFI_OFF_COLOR);
    M5.Lcd.drawString(String(batteryLevel) + "%", w - 60, h/2 - 5);
}

void drawNowPlaying() {
    // Calculate the content area between status bar and button labels
    int contentTop = STATUS_BAR_HEIGHT;
    int contentHeight = M5.Lcd.height() - STATUS_BAR_HEIGHT - BUTTON_BAR_HEIGHT;
    
    M5.Lcd.fillRect(0, contentTop, M5.Lcd.width(), contentHeight, BACKGROUND);
    
    // Draw title
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(ACCENT, BACKGROUND);
    M5.Lcd.setFont(&fonts::Font2);
    M5.Lcd.drawString("CJ's Fish & Chips Radio", M5.Lcd.width() / 2, contentTop + 15);
    
    // Station name
    M5.Lcd.setTextColor(urlError ? ERROR_COLOR : ACCENT, BACKGROUND);
    M5.Lcd.setFont(&fonts::Font4);
    
    String stationName = stations[currentStationIndex].name;
    M5.Lcd.drawString(stationName, M5.Lcd.width() / 2, contentTop + contentHeight/2 - 20);
    
    // Status message
    M5.Lcd.setTextColor(urlError ? ERROR_COLOR : TEXT_COLOR, BACKGROUND);
    M5.Lcd.setFont(&fonts::Font2);
    M5.Lcd.drawString(statusMessage, M5.Lcd.width() / 2, contentTop + contentHeight/2 + 10);
    
    // Station index
    M5.Lcd.setTextColor(TEXT_COLOR, BACKGROUND);
    String stationInfo = String(currentStationIndex + 1) + "/" + String(stations.size());
    M5.Lcd.drawString(stationInfo, M5.Lcd.width() / 2, contentTop + contentHeight/2 + 40);
}

void drawButtonLabels() {
    int btnY = M5.Lcd.height() - BUTTON_BAR_HEIGHT;
    int btnWidth = M5.Lcd.width() / 3;
    
    M5.Lcd.fillRect(0, btnY, M5.Lcd.width(), BUTTON_BAR_HEIGHT, BACKGROUND);
    
    // Draw button labels
    M5.Lcd.setTextColor(TEXT_COLOR, BACKGROUND);
    M5.Lcd.setFont(&fonts::Font2);
    M5.Lcd.setTextDatum(MC_DATUM);
    
    // Left button (Previous)
    M5.Lcd.drawString("Previous", btnWidth/2, btnY + BUTTON_BAR_HEIGHT/2);
    
    // Center button (Play/Pause)
    M5.Lcd.drawString(currentState == STATE_PLAYING ? "Pause" : "Play", 
                     M5.Lcd.width()/2, btnY + BUTTON_BAR_HEIGHT/2);
    
    // Right button (Next)
    M5.Lcd.drawString("Next", M5.Lcd.width() - btnWidth/2, btnY + BUTTON_BAR_HEIGHT/2);
    
    // Draw separator lines
    M5.Lcd.drawLine(btnWidth, btnY, btnWidth, btnY + BUTTON_BAR_HEIGHT, TEXT_COLOR);
    M5.Lcd.drawLine(btnWidth * 2, btnY, btnWidth * 2, btnY + BUTTON_BAR_HEIGHT, TEXT_COLOR);
}

void drawGUI() {
    M5.Lcd.fillScreen(BACKGROUND);
    updateStatusBar();
    drawNowPlaying();
    drawButtonLabels();
}

// ========================================================================
//                         STATE MANAGEMENT
// ========================================================================
void changeState(SystemState newState) {
    currentState = newState;
    drawGUI();
}

void checkConnections() {
    bool newWifiStatus = (WiFi.status() == WL_CONNECTED);
    bool newBtStatus = a2dp_stream.isConnected();
    
    if (newWifiStatus != wifiConnected || newBtStatus != btConnected) {
        wifiConnected = newWifiStatus;
        btConnected = newBtStatus;
        
        if (!wifiConnected || !btConnected) {
            if (currentState == STATE_PLAYING) {
                stopAudio();
                statusMessage = "Connection Lost";
            }
        } else if (currentState == STATE_PAUSED) {
            statusMessage = "Ready";
        }
        
        updateStatusBar();
    }
}

// ========================================================================
//                         MAIN FUNCTIONS
// ========================================================================
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(BACKGROUND);
    
    Serial.begin(115200);
    AudioLogger::instance().begin(Serial, AudioLogger::Warning);
    
    drawBootScreen("Connecting to WiFi...");
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startTime = millis();
    
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        drawBootScreen("WiFi Failed!");
        delay(2000);
        ESP.restart();
    }
    
    drawBootScreen("Connecting to BT...");
    
    auto a2dp_cfg = a2dp_stream.defaultConfig(TX_MODE);
    a2dp_cfg.name = BT_SPEAKER_NAME;
    a2dp_cfg.auto_reconnect = true;
    a2dp_stream.begin(a2dp_cfg);
    
    // Wait a bit for BT connection
    delay(3000);
    
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    btConnected = a2dp_stream.isConnected();
    
    statusMessage = "Ready";
    changeState(STATE_READY);
}

void loop() {
    M5.update();
    
    // Check connections periodically
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 2000) {
        checkConnections();
        lastCheck = millis();
    }
    
    // Handle button presses
    if (M5.BtnA.wasPressed()) { // Left button - Previous station
        currentStationIndex--;
        if (currentStationIndex < 0) currentStationIndex = stations.size() - 1;
        
        if (currentState == STATE_PLAYING) {
            startAudio();
        } else {
            statusMessage = "Ready";
            urlError = false; // Reset error state when changing stations
            drawNowPlaying();
        }
    }
    
    if (M5.BtnB.wasPressed()) { // Center button - Play/Pause
        if (currentState == STATE_PLAYING) {
            stopAudio();
            statusMessage = "Paused";
        } else {
            startAudio();
        }
    }
    
    if (M5.BtnC.wasPressed()) { // Right button - Next station
        currentStationIndex++;
        if (currentStationIndex >= stations.size()) currentStationIndex = 0;
        
        if (currentState == STATE_PLAYING) {
            startAudio();
        } else {
            statusMessage = "Ready";
            urlError = false; // Reset error state when changing stations
            drawNowPlaying();
        }
    }
    
    // Handle audio streaming
    if (currentState == STATE_PLAYING) {
        if (!copier.copy()) {
            Serial.println("Stream error, stopping playback");
            stopAudio();
            statusMessage = "Stream Error";
            urlError = true;
        }
    }
    
    delay(10);
}
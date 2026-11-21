 
// Kirill Borisenko
// "Charlie" audio device with display.
// MADE IN RUSSIA

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "time.h"

#define AMP_SD   33
#define I2S_BCLK 27
#define I2S_LRC  26
#define I2S_DIN  25

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define MY_SDA 19
#define MY_SCL 18

const char* ntpServer = "pool.ntp.org";

const long gmtOffset_sec = 10800; 
const int daylightOffset_sec = 0;

// Wi-Fi
const char* wifi_ssid = "florestdev";
const char* wifi_password = "florestdev-victoria123";

// Audio
I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

// Display
Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET
);

// Screen content
String currentTitle = "florestdev";
String currentBottom = "Initializing...";

// Time update
unsigned long last_clock_update = 0;

// Audio diagnostics
volatile uint32_t audio_packets = 0;
volatile uint32_t audio_bytes = 0;

uint32_t last_packets = 0;
uint32_t last_bytes = 0;

unsigned long last_report = 0;

// Forward declarations
void setText(String title, String bottomText);
void updateDisplay();
void getDateTime(char* dateBuffer, size_t dateSize,
                 char* timeBuffer, size_t timeSize);


// ==========================================
// DATE AND TIME
// ==========================================

void getDateTime(
    char* dateBuffer,
    size_t dateSize,
    char* timeBuffer,
    size_t timeSize
) {
    struct tm timeinfo;

    if (getLocalTime(&timeinfo, 10)) {

        strftime(
            dateBuffer,
            dateSize,
            "%d.%m.%Y",
            &timeinfo
        );

        strftime(
            timeBuffer,
            timeSize,
            "%H:%M:%S",
            &timeinfo
        );

    } else {

        snprintf(dateBuffer, dateSize, "--.--.----");
        snprintf(timeBuffer, timeSize, "--:--:--");

    }
}


// ==========================================
// OLED DISPLAY
// ==========================================

void updateDisplay() {

    char dateBuffer[16];
    char timeBuffer[16];

    getDateTime(
        dateBuffer,
        sizeof(dateBuffer),
        timeBuffer,
        sizeof(timeBuffer)
    );

    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    // Title
    display.setCursor(0, 0);
    display.println(currentTitle);

    // Bottom text
    display.setCursor(0, 16);
    display.println(currentBottom);

    // Separator
    display.drawLine(0, 34, 127, 34, SSD1306_WHITE);

    // Date
    display.setCursor(0, 42);
    display.print("DATE: ");
    display.println(dateBuffer);

    // Time
    display.setCursor(0, 54);
    display.print("TIME: ");
    display.println(timeBuffer);

    display.display();
}


// Change screen content.
// Date and time are automatically included.

void setText(String title, String bottomText) {

    currentTitle = title;
    currentBottom = bottomText;

    updateDisplay();
}


// ==========================================
// AUDIO CALLBACK
// ==========================================

void audio_callback(
    const uint8_t *data,
    uint32_t length
) {
    audio_packets++;
    audio_bytes += length;
}


// ==========================================
// BLUETOOTH METADATA CALLBACK
// ==========================================

void avrc_metadata_callback(
    uint8_t id,
    const uint8_t *text
) {

    switch (id) {

        case ESP_AVRC_MD_ATTR_TITLE:

            setText(
                "Now playing:",
                (const char*)text
            );

            break;

        case ESP_AVRC_MD_ATTR_ARTIST:

            Serial.print("Исполнитель: ");
            Serial.println((const char*)text);

            break;

        case ESP_AVRC_MD_ATTR_ALBUM:

            Serial.print("Альбом: ");
            Serial.println((const char*)text);

            break;
    }
}


// ==========================================
// BLUETOOTH CONNECTION CALLBACK
// ==========================================

void connection_state_changed(
    esp_a2d_connection_state_t state,
    void *ptr
) {

    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {

        Serial.println(
            "Устройство подключено к колонке!"
        );

        setText(
            "florestdev",
            "connected"
        );

    }

    else if (
        state == ESP_A2D_CONNECTION_STATE_DISCONNECTED
    ) {

        Serial.println(
            "Устройство отключено."
        );

        setText(
            "florestdev",
            "disconnected"
        );

    }
}


// ==========================================
// SETUP
// ==========================================

void setup() {

    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("     FlorestAudio Diagnostics");
    Serial.println("================================");

    // OLED
    Wire.begin(MY_SDA, MY_SCL);

    display.begin(
        SSD1306_SWITCHCAPVCC,
        0x3C
    );

    setText(
        "florestdev",
        "Init..."
    );


    // ======================================
    // WIFI / INTERNET
    // ======================================

    WiFi.mode(WIFI_STA);

    WiFi.config(
        INADDR_NONE,
        INADDR_NONE,
        INADDR_NONE,
        INADDR_NONE
    );

    WiFi.setHostname("florestdev-audio");

    WiFi.begin(
        wifi_ssid,
        wifi_password
    );

    Serial.println("[WIFI] Connecting...");

    while (WiFi.status() != WL_CONNECTED) {

        delay(500);
        Serial.print(".");

    }

    Serial.println();
    Serial.println("[WIFI] Connected!");

    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());

    setText(
        "florestdev",
        "Internet ready! :)"
    );


    // ======================================
    // NTP TIME SYNCHRONIZATION
    // ======================================

    configTime(
        gmtOffset_sec,
        daylightOffset_sec,
        ntpServer
    );

    Serial.println("[NTP] Time synchronization started");


    // ======================================
    // AMPLIFIER MAX98357A
    // ======================================

    Serial.println(
        "[1] Initializing MAX98357A..."
    );

    pinMode(AMP_SD, OUTPUT);

    digitalWrite(
        AMP_SD,
        HIGH
    );

    delay(100);

    Serial.print("[OK] SD GPIO ");
    Serial.print(AMP_SD);
    Serial.println(" = HIGH");


    // ======================================
    // I2S
    // ======================================

    Serial.println(
        "[2] Initializing I2S..."
    );

    auto cfg = i2s.defaultConfig(TX_MODE);

    cfg.pin_bck = I2S_BCLK;
    cfg.pin_ws = I2S_LRC;
    cfg.pin_data = I2S_DIN;

    cfg.sample_rate = 44100;
    cfg.bits_per_sample = 16;
    cfg.channels = 2;
    cfg.i2s_format = I2S_STD_FORMAT;

    if (!i2s.begin(cfg)) {

        Serial.println(
            "[ERROR] I2S initialization FAILED!"
        );

        return;
    }

    Serial.println("[OK] I2S initialized");

    Serial.print("[INFO] BCLK: GPIO ");
    Serial.println(I2S_BCLK);

    Serial.print("[INFO] LRC: GPIO ");
    Serial.println(I2S_LRC);

    Serial.print("[INFO] DIN: GPIO ");
    Serial.println(I2S_DIN);


    // ======================================
    // BLUETOOTH A2DP
    // ======================================

    Serial.println(
        "[3] Initializing Bluetooth A2DP..."
    );

    a2dp_sink.set_volume(100);

    // PCM audio monitoring
    a2dp_sink.set_stream_reader(
        audio_callback,
        true
    );

    // AVRCP metadata
    a2dp_sink.set_avrc_metadata_callback(
        avrc_metadata_callback
    );

    // Connection monitoring
    a2dp_sink.set_on_connection_state_changed(
        connection_state_changed
    );

    // Start Bluetooth
    a2dp_sink.start(
        "Charlie (florestdev)"
    );

    Serial.println("[OK] Bluetooth started");
    Serial.println("[INFO] Waiting for phone...");
    Serial.println("================================");

    setText(
        "florestdev",
        "Ready!"
    );

}


// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

    // ======================================
    // CLOCK UPDATE (EVERY SECOND)
    // ======================================

    if (millis() - last_clock_update >= 1000) {

        last_clock_update = millis();

        // Refresh date and time without
        // changing the current screen content.

        updateDisplay();

    }


    // ======================================
    // AUDIO DIAGNOSTICS (EVERY SECOND)
    // ======================================

    if (millis() - last_report >= 1000) {

        last_report = millis();

        uint32_t packets = audio_packets;
        uint32_t bytes = audio_bytes;

        Serial.println();
        Serial.println("------ AUDIO DIAGNOSTICS ------");

        Serial.print("Total packets: ");
        Serial.println(packets);

        Serial.print("Total bytes: ");
        Serial.println(bytes);

        if (packets > last_packets) {

            Serial.println(
                "[OK] Bluetooth audio data received!"
            );

        } else {

            Serial.println(
                "[WARN] No new audio packets!"
            );

        }

        if (bytes > last_bytes) {

            Serial.println(
                "[OK] PCM stream active"
            );

        }

        last_packets = packets;
        last_bytes = bytes;

        Serial.println("-------------------------------");

    }

    delay(10);

}

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

// ==========================================
// CONFIGURATION
// ==========================================

#define AMP_SD   33
#define I2S_BCLK 27
#define I2S_LRC  26
#define I2S_DIN  25

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

#define MY_SDA 19
#define MY_SCL 18

#define OLED_ADDRESS 0x3C

#define WIFI_TIMEOUT_MS 15000
#define REPORT_INTERVAL 1000

// ==========================================
// NETWORK
// ==========================================

static const char WIFI_SSID[] = "florestdev";
static const char WIFI_PASSWORD[] = "florestdev-victoria123";

static const char NTP_SERVER[] = "pool.ntp.org";

static constexpr long GMT_OFFSET = 10800;
static constexpr int DST_OFFSET = 0;

// ==========================================
// AUDIO
// ==========================================

I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

// ==========================================
// DISPLAY
// ==========================================

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET
);

// Fixed-size screen buffers
static char currentTitle[32] = "florestdev";
static char currentBottom[96] = "Initializing...";

// Shared screen state
static portMUX_TYPE screenMux = portMUX_INITIALIZER_UNLOCKED;

static bool screenDirty = true;

// ==========================================
// AUDIO DIAGNOSTICS
// ==========================================

static uint32_t audioPackets = 0;
static uint32_t audioBytes = 0;

static uint32_t lastPackets = 0;
static uint32_t lastBytes = 0;

static uint32_t lastReport = 0;

// ==========================================
// SYSTEM STATE
// ==========================================

static bool wifiStarted = false;
static bool wifiFinished = false;
static bool ntpStarted = false;

static uint32_t wifiStartTime = 0;
static uint32_t lastScreenUpdate = 0;

static time_t lastDisplayedSecond = 0;

// ==========================================
// SCREEN STATE
// ==========================================

void setText(const char* title, const char* bottom)
{
    portENTER_CRITICAL(&screenMux);

    snprintf(
        currentTitle,
        sizeof(currentTitle),
        "%s",
        title ? title : ""
    );

    snprintf(
        currentBottom,
        sizeof(currentBottom),
        "%s",
        bottom ? bottom : ""
    );

    screenDirty = true;

    portEXIT_CRITICAL(&screenMux);
}

// ==========================================
// DATE AND TIME
// ==========================================

static void getDateTime(
    char* dateBuffer,
    size_t dateSize,
    char* timeBuffer,
    size_t timeSize
)
{
    const time_t now = time(nullptr);

    struct tm timeInfo;

    if (now > 1700000000 && localtime_r(&now, &timeInfo))
    {
        strftime(
            dateBuffer,
            dateSize,
            "%d.%m.%Y",
            &timeInfo
        );

        strftime(
            timeBuffer,
            timeSize,
            "%H:%M:%S",
            &timeInfo
        );
    }
    else
    {
        snprintf(dateBuffer, dateSize, "--.--.----");
        snprintf(timeBuffer, timeSize, "--:--:--");
    }
}

// ==========================================
// OLED UPDATE
// ==========================================

static void updateDisplay()
{
    char title[sizeof(currentTitle)];
    char bottom[sizeof(currentBottom)];

    // Copy shared state
    portENTER_CRITICAL(&screenMux);

    memcpy(title, currentTitle, sizeof(title));
    memcpy(bottom, currentBottom, sizeof(bottom));

    screenDirty = false;

    portEXIT_CRITICAL(&screenMux);

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

    display.setCursor(0, 0);
    display.println(title);

    display.setCursor(0, 16);
    display.println(bottom);

    display.drawLine(0, 34, 127, 34, SSD1306_WHITE);

    display.setCursor(0, 42);
    display.print("DATE: ");
    display.println(dateBuffer);

    display.setCursor(0, 54);
    display.print("TIME: ");
    display.println(timeBuffer);

    display.display();
}

// ==========================================
// AUDIO CALLBACK
// ==========================================

void audio_callback(
    const uint8_t* data,
    uint32_t length
)
{
    (void)data;

    __atomic_fetch_add(
        &audioPackets,
        1,
        __ATOMIC_RELAXED
    );

    __atomic_fetch_add(
        &audioBytes,
        length,
        __ATOMIC_RELAXED
    );
}

// ==========================================
// BLUETOOTH METADATA
// ==========================================

void avrc_metadata_callback(
    uint8_t id,
    const uint8_t* text
)
{
    if (id != ESP_AVRC_MD_ATTR_TITLE)
        return;

    if (text == nullptr)
        return;

    setText(
        "Now playing:",
        reinterpret_cast<const char*>(text)
    );
}

// ==========================================
// BLUETOOTH CONNECTION
// ==========================================

void connection_state_changed(
    esp_a2d_connection_state_t state,
    void* ptr
)
{
    (void)ptr;

    switch (state)
    {
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            setText("florestdev", "connected");
            break;

        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            setText("florestdev", "disconnected");
            break;

        default:
            break;
    }
}

// ==========================================
// WIFI MANAGEMENT
// ==========================================

static void updateWiFi()
{
    if (wifiFinished)
        return;

    if (!wifiStarted)
    {
        wifiStarted = true;
        wifiStartTime = millis();

        WiFi.mode(WIFI_STA);
        WiFi.setHostname("florestdev-audio");

        WiFi.begin(
            WIFI_SSID,
            WIFI_PASSWORD
        );

        Serial.println("[WIFI] Connecting...");
        return;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        if (!ntpStarted)
        {
            ntpStarted = true;

            configTime(
                GMT_OFFSET,
                DST_OFFSET,
                NTP_SERVER
            );

            Serial.print("[WIFI] IP: ");
            Serial.println(WiFi.localIP());

            Serial.println("[NTP] Synchronization started");
        }

        const time_t now = time(nullptr);

        if (now > 1700000000)
        {
            Serial.println("[NTP] Time synchronized");

            setText(
                "florestdev",
                "Internet ready!"
            );

            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);

            wifiFinished = true;
            return;
        }
    }

    // Wi-Fi timeout
    if (millis() - wifiStartTime >= WIFI_TIMEOUT_MS)
    {
        Serial.println("[WIFI] Timeout. Disabling Wi-Fi.");

        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);

        wifiFinished = true;
    }
}

// ==========================================
// AUDIO REPORT
// ==========================================

static void updateAudioDiagnostics()
{
    const uint32_t now = millis();

    if (now - lastReport < REPORT_INTERVAL)
        return;

    lastReport = now;

    const uint32_t packets = __atomic_load_n(
        &audioPackets,
        __ATOMIC_RELAXED
    );

    const uint32_t bytes = __atomic_load_n(
        &audioBytes,
        __ATOMIC_RELAXED
    );

    // Unsigned subtraction handles counter overflow
    const uint32_t newPackets = packets - lastPackets;
    const uint32_t newBytes = bytes - lastBytes;

    lastPackets = packets;
    lastBytes = bytes;

    Serial.printf(
        "[AUDIO] Packets: %lu | Bytes: %lu | %s\n",
        (unsigned long)packets,
        (unsigned long)bytes,
        newPackets ? "ACTIVE" : "NO DATA"
    );
}

// ==========================================
// DISPLAY SCHEDULER
// ==========================================

static void updateScreenTask()
{
    const uint32_t now = millis();

    if (now - lastScreenUpdate < 100)
        return;

    bool dirty;

    portENTER_CRITICAL(&screenMux);
    dirty = screenDirty;
    portEXIT_CRITICAL(&screenMux);

    const time_t currentSecond = time(nullptr);

    const bool clockChanged =
        currentSecond > 0 &&
        currentSecond != lastDisplayedSecond;

    if (!dirty && !clockChanged)
        return;

    lastScreenUpdate = now;
    lastDisplayedSecond = currentSecond;

    updateDisplay();
}

// ==========================================
// SETUP
// ==========================================

void setup()
{
    Serial.begin(115200);

    Serial.println();
    Serial.println("================================");
    Serial.println("     FlorestAudio");
    Serial.println("================================");

    // OLED
    Wire.begin(MY_SDA, MY_SCL);

    if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDRESS
    ))
    {
        Serial.println("[ERROR] OLED initialization failed");
    }
    else
    {
        display.clearDisplay();
        display.display();
    }

    setText("florestdev", "Initializing...");

    // Start Wi-Fi asynchronously
    updateWiFi();

    // Amplifier
    pinMode(AMP_SD, OUTPUT);
    digitalWrite(AMP_SD, HIGH);

    delay(100);

    // I2S
    auto cfg = i2s.defaultConfig(TX_MODE);

    cfg.pin_bck = I2S_BCLK;
    cfg.pin_ws = I2S_LRC;
    cfg.pin_data = I2S_DIN;

    cfg.sample_rate = 44100;
    cfg.bits_per_sample = 16;
    cfg.channels = 2;
    cfg.i2s_format = I2S_STD_FORMAT;

    if (!i2s.begin(cfg))
    {
        Serial.println("[ERROR] I2S initialization failed");
        return;
    }

    Serial.println("[OK] I2S initialized");

    // Bluetooth
    a2dp_sink.set_volume(100);

    a2dp_sink.set_stream_reader(
        audio_callback,
        true
    );

    a2dp_sink.set_avrc_metadata_callback(
        avrc_metadata_callback
    );

    a2dp_sink.set_on_connection_state_changed(
        connection_state_changed
    );

    a2dp_sink.start(
        "Charlie (florestdev)"
    );

    Serial.println("[OK] Bluetooth started");
    Serial.println("[INFO] Waiting for phone");

    setText("florestdev", "Ready!");
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop()
{
    updateWiFi();

    updateScreenTask();

    updateAudioDiagnostics();

    delay(10);
}

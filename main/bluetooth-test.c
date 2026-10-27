#include "BluetoothA2DPSource.h"

#define LED_PIN 48 // GPIO 48 for ESP32-S3 WROOM onboard LED

// Global library instance
BluetoothA2DPSource a2dp_source;

// Function prototypes (Standard C)
void connection_state_changed(esp_a2dp_connection_state_t state, void *ptr);
int32_t get_sound_data(uint8_t *data, int32_t byteCount);

// Connection state callback
void connection_state_changed(esp_a2dp_connection_state_t state, void *ptr) {
    if (state == ESP_A2DP_CONNECTION_STATE_CONNECTED) {
        Serial.println("Success! Headphones connected.");
        digitalWrite(LED_PIN, HIGH); // Solid LED = Connected
    } else if (state == ESP_A2DP_CONNECTION_STATE_DISCONNECTED) {
        Serial.println("Headphones disconnected. Searching...");
        digitalWrite(LED_PIN, LOW);  // LED off = Searching
    }
}

// Audio buffer filling callback (Generates 440 Hz sine wave PCM data)
int32_t get_sound_data(uint8_t *data, int32_t byteCount) {
    static float phase = 0.0;
    int16_t *samples = (int16_t *)data;
    int num_samples = byteCount / 2;
    
    for (int i = 0; i < num_samples; i += 2) {
        int16_t sample = (int16_t)(10000.0 * sin(phase));
        samples[i] = sample;     // Left channel
        samples[i+1] = sample;   // Right channel
        
        phase += 2.0 * 3.1415926535 * 440.0 / 44100.0;
        if (phase >= 2.0 * 3.1415926535) {
            phase -= 2.0 * 3.1415926535;
        }
    }
    return byteCount;
}

void setup() {
    Serial.begin(115200);
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.println("Starting Bluetooth A2DP Source (C Style)...");
    
    // Register callbacks
    a2dp_source.set_on_connection_state_changed(connection_state_changed);
    a2dp_source.set_data_callback(get_sound_data);
    
    // Start streaming under device name "ESP32-Player"
    a2dp_source.start("ESP32-Player"); 
}

void loop() {
    // Keep loop clean; Bluetooth runs on background FreeRTOS tasks
    delay(1000);
}
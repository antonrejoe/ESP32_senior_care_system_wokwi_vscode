#include <WiFi.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <math.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Audio.h"

// GPS module connected to RX2 (16) and TX2 (17)
static const int RXPin = 16, TXPin = 17;
static const uint32_t GPSBaud = 9600;

// Create a TinyGPS++ object
TinyGPSPlus gps;

// Create a hardware serial object
HardwareSerial ss(2);

// Replace with your network credentials
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Server addresses
const String gpsServerName = "http://192.168.147.29:5000/update-gps";
const String reminderServerName = "http://192.168.147.29:5000/reminders";

// Interval in milliseconds between sending GPS data
const unsigned long gpsInterval = 10000; // 10 seconds
unsigned long previousGpsMillis = 0;

// Interval in milliseconds between fetching reminders
const unsigned long reminderInterval = 3000; // 3 seconds
unsigned long previousReminderMillis = 0;

// Initialize the audio object
Audio audio;

// Buzzer pin
const int buzzerPin = 13; // Replace with the correct GPIO pin for the buzzer

// Switch pin
const int switchPin = 12; // Replace with the correct GPIO pin for the switch


#define LED_BUILTIN 2


// Mode switch
enum Mode { BUZZER, AUDIO };
Mode currentMode = AUDIO; // Default mode

struct HttpResponse {
    int status;
    String body;
    String location;
};

HttpResponse HttpPost(const String& url, const String& payload, bool followRedirects) {
    HttpResponse response;
    WiFiClient client;
    HTTPClient http;
    
    http.setFollowRedirects(followRedirects ? HTTPC_FORCE_FOLLOW_REDIRECTS : HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Length", String(payload.length()));
    http.addHeader("Content-Type", "application/json");

    if (http.begin(client, url)) {
        response.status = http.POST(payload);
        response.body = http.getString();
        response.location = http.getLocation();
        http.end();
    } else {
        Serial.println("Failed to connect to server");
        response.status = -1;
    }
    
    return response;
}

HttpResponse HttpGet(const String& url) {
    HttpResponse response;
    WiFiClient client;
    HTTPClient http;

    if (http.begin(client, url)) {
        response.status = http.GET();
        response.body = http.getString();
        http.end();
    } else {
        Serial.println("Failed to connect to server");
        response.status = -1;
    }
    
    return response;
}

String parseReminder(String payload) {
    StaticJsonDocument<200> doc;

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return "";
    }

    // Check if the parsed document has the correct structure
    if (!doc.containsKey("name")) {
        Serial.println("Reminder JSON does not contain 'name'.");
        return "";  // Return an empty string if the expected key is not present
    }

    // Extract the "name" field from the JSON object
    String name = doc["name"].as<String>();

    return name;
}

void blinkLED(int times, int delayTime) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_BUILTIN, HIGH); // Turn the LED on
        delay(delayTime);
        digitalWrite(LED_BUILTIN, LOW);  // Turn the LED off
        delay(delayTime);
    }
}

unsigned long getCurrentTime() {
    time_t now;
    time(&now);
    return now;
}


void playReminder(String reminder) {
    if (reminder.isEmpty()) {
        Serial.println("No valid reminder found.");
        return;
    }

    Serial.println("Reminder: " + reminder);

    // Blink the LED based on the reminder
    if (reminder ) {
    //if (true){
        blinkLED(3, 500);  // Blink the LED 3 times with a 500 ms delay
    } else if (reminder == "Drink water") {
        blinkLED(2, 500);  // Blink the LED 2 times with a 500 ms delay
    } else {
        Serial.println("Unknown reminder: " + reminder);
    }
}

void stopReminder() {
    if (currentMode == AUDIO) {
        audio.stopSong(); // Method to stop the audio playback
    } else if (currentMode == BUZZER) {
        noTone(buzzerPin); // Stop the buzzer sound
    }
}

void setup() {
    Serial.begin(115200);

    // Initialize GPS module
    ss.begin(GPSBaud, SERIAL_8N1, RXPin, TXPin);

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    // Initialize the audio library and speaker
    audio.setPinout(26, 25, 22);  // Set the correct I2S pins for your setup
    SPIFFS.begin(true);  // Start SPIFFS filesystem for audio files

    // Initialize buzzer pin
    pinMode(buzzerPin, OUTPUT);

    // Initialize switch pin
    pinMode(switchPin, INPUT_PULLUP); // Assuming active low switch

    // Initialize the built-in LED pin
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    // Update GPS data
    while (ss.available() > 0) {
        gps.encode(ss.read());
    }

    // Check if it's time to send GPS data
    unsigned long currentMillis = millis();
    if (currentMillis - previousGpsMillis >= gpsInterval) {
        previousGpsMillis = currentMillis;

        if (true) { // Replace with gps.location.isValid() if using real GPS
            float latitude = 36.778259;
            float longitude = -119.417931;

            Serial.print("Latitude= ");
            Serial.print(latitude, 6);
            Serial.print(" Longitude= ");
            Serial.println(longitude, 6);

            // Create JSON string with GPS data
            String jsonPayload = String("{\"lat\":") + String(latitude, 6) + 
                                String(", \"lng\":") + String(longitude, 6) + "}";

            // Send GPS data to external server
            if (WiFi.status() == WL_CONNECTED) {
                HttpResponse httpResponse = HttpPost(gpsServerName, jsonPayload, true);
                
                if (httpResponse.status > 0) {
                    Serial.println(httpResponse.status);
                    Serial.println(httpResponse.body);
                } else {
                    Serial.print("Error on sending POST: ");
                    Serial.println(httpResponse.status);
                }
            }
        } else {
            Serial.println("Waiting for valid GPS signal...");
        }
    }

    // Check the switch state to determine whether to process reminders
    bool switchState = digitalRead(switchPin) == HIGH; // Assuming LOW means the switch is pressed

    // Check if it's time to fetch reminders
    if (currentMillis - previousReminderMillis >= reminderInterval) {
        previousReminderMillis = currentMillis;

        if (WiFi.status() == WL_CONNECTED) {
            if (switchState) {
                HttpResponse httpResponse = HttpGet(reminderServerName);
                
                if (httpResponse.status > 0) {
                    Serial.println(httpResponse.status);
                    Serial.println(httpResponse.body);
                    
                    String reminder = parseReminder(httpResponse.body);
                    if (reminder.length() > 0) {
                        stopReminder(); // Stop any previous reminder
                        playReminder(reminder);
                    } else {
                        Serial.println("No reminders found.");
                    }
                } else {
                    Serial.print("Error on fetching reminders: ");
                    Serial.println(httpResponse.status);
                }
            } else {
                Serial.println("Reminder processing is turned off.");
            }
        } else {
            Serial.println("WiFi not connected.");
        }
    }
}

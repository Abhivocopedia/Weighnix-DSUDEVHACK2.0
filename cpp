#include <Arduino.h>
#include <HX711.h>
#include <WiFi.h>
#include <WebServer.h>

// Pins
#define BUZZER_PIN 25
#define LOADCELL_DOUT_PIN 16
#define LOADCELL_SCK_PIN 4

// WiFi Credentials
const char* ssid = "SS-ID";
const char* password = "PASSWORD";

// Calibration Factors
float calibration_factor = -96650.0;
long offset = 28800;
float full_kg = 14.2;
float full_grams = full_kg * 1000; // 14.2 kg = 14,200 grams
float tare_kg = 15.5;              // Weight of empty cylinder
float tare_grams = tare_kg * 1000; // 15.5 kg = 15,500 grams

bool isLPGMode = true;

// Web Server on port 80
WebServer server(80);

// Global Variables
HX711 scale;
float currentGasGrams = 0;
float currentGasPercentage = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize scale
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.set_offset(offset);

  // Connect to WiFi
  connectToWiFi();

  // Setup Web Server Routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/calibrate", handleCalibrate);
  server.on("/mode", handleMode);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  readSensorData();
  checkAlerts();
  delay(5000); // Check every 5 seconds
}

// --- WiFi Connection ---
void connectToWiFi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// --- Sensor Reading ---
void readSensorData() {
  if (scale.is_ready()) {
    float total_weight_kg = scale.get_units(5); // Total weight in kg
    float total_weight_grams = total_weight_kg * 1000; // Convert to grams
    
    // Calculate gas weight: Total weight - Tare weight
    currentGasGrams = total_weight_grams - tare_grams;
    
    // Calculate percentage: (Current Gas / Full Gas) × 100
    currentGasPercentage = (currentGasGrams / full_grams) * 100.0;
    
    // Ensure percentage doesn't go below 0 or above 100
    currentGasPercentage = constrain(currentGasPercentage, 0, 100);
    
    Serial.print("Total Weight: ");
    Serial.print(total_weight_grams, 0);
    Serial.print(" g | Gas Weight: ");
    Serial.print(currentGasGrams, 0);
    Serial.print(" g | Percentage: ");
    Serial.print(currentGasPercentage, 1);
    Serial.println("%");
  }
}

// --- Alert System ---
void checkAlerts() {
  // Low gas alert
  if (currentGasPercentage < 15.0 && currentGasPercentage > 10.0) {
    triggerAlert("LOW_GAS: Gas at " + String(currentGasPercentage, 1) + "%");
  }
  else if (currentGasPercentage <= 10.0) {
    triggerAlert("CRITICAL: Gas at " + String(currentGasPercentage, 1) + "%!");
  }

  // Sudden drop detection (more than 500 grams drop)
  static float last_grams = full_grams;
  float drop = last_grams - currentGasGrams;
  if (drop > 500.0) { // 500 grams sudden drop
    triggerAlert("SUDDEN_DROP: Gas dropped by " + String(drop, 0) + " grams");
  }
  last_grams = currentGasGrams;
}

void triggerAlert(String message) {
  Serial.println("ALERT: " + message);
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    delay(300);
  }
}

// --- Web Server Handlers ---
void handleRoot() {
  String html = "<html><head><meta http-equiv='refresh' content='5'></head>";
  html += "<body style='font-family: Arial; text-align: center;'>";
  html += "<h1>LPG Gas Monitor</h1>";
  html += "<h2 style='color: blue;'>" + String(currentGasPercentage, 1) + "%</h2>";
  html += "<h3>" + String(currentGasGrams, 0) + " grams remaining</h3>";
  html += "<p>Full Capacity: " + String(full_grams, 0) + " grams</p>";
  html += "<p>Mode: " + String(isLPGMode ? "LPG" : "OTHER") + "</p>";
  html += "<p><a href='/data'>JSON Data</a></p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{";
  json += "\"percentage\":" + String(currentGasPercentage, 1) + ",";
  json += "\"grams\":" + String(currentGasGrams, 0) + ",";
  json += "\"mode\":\"" + String(isLPGMode ? "LPG" : "OTHER") + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleCalibrate() {
  if (server.method() == HTTP_POST) {
    scale.tare();
    server.send(200, "text/plain", "Calibration Started");
  }
}

void handleMode() {
  if (server.method() == HTTP_POST) {
    String mode = server.arg("mode");
    isLPGMode = (mode == "LPG");
    server.send(200, "text/plain", "Mode changed to " + mode);
  }
}

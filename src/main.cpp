#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <WiFi.h>         // For WiFi
#include <HTTPClient.h>   // For HTTP requests
#include <WiFiClientSecure.h> // For HTTPS workaround
#include <DHTesp.h>

// --- WiFi Credentials (Specifically for Wokwi Simulator) ---
const char* ssid = "Wokwi-GUEST";
const char* password = ""; // Wokwi-GUEST has no password

// --- Backend URL ---
// Your Supabase Function URL
const char* serverUrl = "https://elxrhewruujmwthlhhni.supabase.co/functions/v1/log-sensor-data";

// --- LCD Setup ---
LiquidCrystal_I2C lcd1(0x27, 16, 2); // 16x2 LCD
LiquidCrystal_I2C lcd2(0x3F, 20, 4); // 20x4 LCD

// --- Pin Definitions ---
const int TEMP_PIN_Z1 = 34;
const int LIGHT_PIN_Z1 = 35;
const int DHT_PIN_Z2 = 25;    // Pin for DHT22 Data (Zone 2)
const int GREEN_LED_PIN = 19;
const int YELLOW_LED_PIN = 18;
const int RED_LED_PIN = 5;
const int BUZZER_PIN = 17;
const int FAN_LED_PIN = 16;

// --- Sensor & ADC Constants ---
const float ADC_MAX_VALUE = 4095.0; // ESP32 is 12-bit ADC (0-4095)
const float ADC_REF_VOLTAGE = 3.3;

const float BETA = 3950; // NTC Beta Coefficient (Matches Wokwi default attribute)
const float R_KNOWN = 10000; // Known resistance for NTC voltage divider (10K as per Wokwi docs)
const float T0_KELVIN = 298.15; // Reference Temperature in Kelvin (25 C)

const float LDR_GAMMA = 0.7;
const float LDR_RL10 = 50; // Resistance at 10 lux
const float LDR_SERIES_RESISTOR = 10000; // Known resistance for LDR voltage divider

// --- Alert Thresholds ---
const float TEMP_HIGH_THRESHOLD = 30.0; // Temperature threshold for alerts
const float LIGHT_LOW_THRESHOLD = 100; // Light threshold for zone 1 alert
const float HUMIDITY_HIGH_THRESHOLD = 70.0; // Humidity Threshold for Zone 2 Alert

// --- Global Alert Flags ---
bool zone1_alert = false;
bool zone2_alert = false;
bool high_temp_alert = false;

// --- DHT Sensor Setup ---
DHTesp dht; // Create DHTesp object
float humidityZ2 = -999.0; // Variable to hold humidity reading
float temperatureCZ2 = -999.0; // Variable to hold DHT temperature reading

// --- Sensor Reading Functions ---

/**
 * @brief Reads temperature from an NTC thermistor using the direct formula.
 * @param pin The analog pin connected to the NTC circuit.
 * @return Temperature in degrees Celsius, or -999.0 on error.
 */
float readNtcTemperature(int pin) {
  int analogValue = analogRead(pin);
  // Basic check for out-of-range ADC values
  if (analogValue <= 0 || analogValue >= ADC_MAX_VALUE) {
      Serial.printf("NTC Pin %d: Invalid ADC reading: %d\n", pin, analogValue);
      return -999.0; // Error code for invalid reading
  }

  // --- MODIFIED CALCULATION ---
  // Using the direct formula derived from Wokwi documentation, adapted for 12-bit ADC
  // Formula: 1 / ( log(1 / (ADC_Max / Vin - 1)) / BETA + 1/T0 ) - 273.15
  // Where (ADC_Max / Vin - 1) is equivalent to (R_Known / R_NTC)
  // So, 1 / (ADC_Max / Vin - 1) is equivalent to (R_NTC / R_Known)

  float term1 = ADC_MAX_VALUE / (float)analogValue - 1.0;
  // Avoid division by zero or log(0) if analogValue is exactly ADC_MAX_VALUE (handled by check above, but good practice)
  if (term1 <= 0) {
       Serial.printf("NTC Pin %d: Calculation term1 invalid (<=0): %.2f\n", pin, term1);
       return -999.0;
  }

  float term2 = log(1.0 / term1); // This is log(R_NTC / R_Known)
  float term3 = term2 / BETA;      // log(R_NTC / R_Known) / BETA
  float term4 = term3 + (1.0 / T0_KELVIN); // + 1/T0
  // Avoid division by zero if term4 is zero
  if (abs(term4) < 1e-9) { // Check if term4 is very close to zero
      Serial.printf("NTC Pin %d: Calculation term4 near zero: %.6f\n", pin, term4);
      return -999.0;
  }

  float kelvin = 1.0 / term4;       // Calculate temperature in Kelvin
  float celsius = kelvin - 273.15; // Convert to Celsius
  // --- END OF MODIFIED CALCULATION ---


  // Add a check for non-finite results (NaN, infinity) which can occur with extreme values
  if (!isfinite(celsius)) {
      Serial.printf("NTC Pin %d: Calculated temperature is not finite: %.2f\n", pin, celsius);
      return -999.0;
  }
  return celsius;
}

/**
 * @brief Reads illuminance (lux) from an LDR connected to an analog pin.
 * @param pin The analog pin connected to the LDR circuit.
 * @return Estimated lux value, 0.0 for "too bright", -1.0 for "too dark" or error.
 */
float readLdrLux(int pin) {
  int analogValue = analogRead(pin);
  // Convert analog value to voltage
  float voltage = (float)analogValue / ADC_MAX_VALUE * ADC_REF_VOLTAGE;

  // Handle edge cases for voltage (near 0V or near VRef)
  if (voltage <= 0.01) {
       return -1.0; // Error code or indicator for "too dark" / disconnected
  }
  if (voltage >= (ADC_REF_VOLTAGE - 0.01)) {
      return 0.0; // Error code or indicator for "too bright" / saturated
  }

  // Calculate LDR resistance using voltage divider formula
  float resistance = LDR_SERIES_RESISTOR * voltage / (ADC_REF_VOLTAGE - voltage);
   if (resistance <=0) return 0.0; // Should not happen if voltage checks passed

  // Calculate Lux using the provided constants structure
  float lux = pow(LDR_RL10 * 1e3 * pow(10, LDR_GAMMA) / resistance, (1.0 / LDR_GAMMA));

  if (!isfinite(lux)) { // Check for NaN or Inf
      return -1.0; // Error code
  }

  return lux;
}

// --- WiFi and Network Functions ---

/**
 * @brief Connects the ESP32 to the configured WiFi network.
 * Includes Wokwi-specific fix by specifying channel 6.
 */
void setupWiFi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  Serial.print("ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());
  lcd2.clear();
  lcd2.setCursor(0,0);
  lcd2.print("Connecting WiFi...");

  // *** WOKWI SIMULATOR FIX ***
  // Use the 3-argument version of WiFi.begin() to specify the channel (6)
  // This skips the network scan which doesn't work reliably for Wokwi-GUEST.
  WiFi.begin(ssid, password, 6);
  // **************************

  int retries = 0;
  // Loop to check connection status (should connect quickly with the fix)
  while (WiFi.status() != WL_CONNECTED && retries < 30) { // Timeout after ~7.5 seconds
    wl_status_t status = WiFi.status();
    Serial.print(".");
    Serial.print(status); // Print status code for debugging
    delay(250); // Check status every 250ms
    retries++;
  }

  // Check final connection status
  if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.println("WiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      // Display connection success on LCD2
      lcd2.setCursor(0,0);
      lcd2.print("WiFi Connected      ");
      lcd2.setCursor(0,1);
      lcd2.print("IP: ");
      lcd2.print(WiFi.localIP());
      delay(2500); // Show IP for a bit
      // Clear connection messages for normal operation display
      lcd2.setCursor(0,0);
      lcd2.print("                    ");
      lcd2.setCursor(0,1);
      lcd2.print("                    ");

  } else {
      Serial.println("");
      Serial.println("WiFi connection failed!");
      // Display failure message on LCD2
      lcd2.setCursor(0,0);
      lcd2.print("WiFi Failed!        ");
      // Consider adding a persistent error state or retry logic here
  }
}

/**
 * @brief Sends the provided JSON data string to the configured backend URL via HTTP POST.
 * Includes workaround for Wokwi SSL issues by disabling certificate validation.
 * @param jsonData The JSON string containing sensor data to send.
 */
void sendDataToBackend(String jsonData) {
    // Only attempt to send if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {

        // --- WOKWI SSL/HTTPS WORKAROUND ---
        // Create a WiFiClientSecure object
        WiFiClientSecure client;
        // !! WARNING !! Skipping certificate validation. Insecure for production!
        // This is often necessary for HTTPS connections within the Wokwi simulator.
        client.setInsecure();
        // ---------------------------------

        HTTPClient http; // Create HTTP client object

        Serial.print("Sending data to backend: ");
        Serial.println(serverUrl);
        // Serial.println(jsonData); // Uncomment to print JSON before sending

        // Set timeout for the HTTP request (optional, default is 5 seconds)
        http.setTimeout(10000); // 10 seconds timeout

        // Initialize the HTTP request, passing the insecure client object
        // This tells HTTPClient to use our custom (insecure) SSL settings
        http.begin(client, serverUrl); // <<< MODIFIED to use the insecure client

        // Set headers for the request
        http.addHeader("Content-Type", "application/json");
        // Optional: Add API key header if your Supabase function requires it
        // http.addHeader("apikey", "YOUR_SUPABASE_ANON_KEY");
        // http.addHeader("Authorization", "Bearer YOUR_SUPABASE_ANON_KEY");

        // Send the HTTP POST request with the JSON payload
        int httpResponseCode = http.POST(jsonData);

        // Check the response code
        if (httpResponseCode > 0) { // Positive code usually means success or redirect
            String response = http.getString(); // Get the response payload
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            Serial.print("Response: ");
            Serial.println(response);
            // Check specifically for success codes (e.g., 200 OK, 201 Created)
             if (httpResponseCode != 200 && httpResponseCode != 201) {
                 // Log unexpected success code if needed
                 Serial.println("Note: HTTP response code was not 200 or 201.");
             }
        } else { // Negative code means an error occurred during sending
            Serial.print("Error sending POST: ");
            Serial.println(httpResponseCode);
            // Provide more detailed error information
            Serial.printf("HTTP POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
        }

        // End the HTTP connection (free up resources)
        http.end();
    } else {
        Serial.println("WiFi not connected. Cannot send data.");
        // Optional: Could attempt to reconnect WiFi here, but be careful of blocking
        // setupWiFi();
    }
}

// --- Main Setup ---
void setup() {
  // Initialize Serial communication
  Serial.begin(115200);
  Serial.println("Multi-Zone Environmental Monitor Initializing...");

  // Initialize I2C communication
  Wire.begin();

  // Initialize LCD displays
  lcd1.init();
  lcd1.backlight();
  lcd1.print("Initializing Z1");

  lcd2.init();
  lcd2.backlight();
  lcd2.setCursor(0,0);
  lcd2.print("Initializing Sys...");

  // Initialize digital output pins
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_LED_PIN, OUTPUT);

  // Set initial LED and buzzer states
  digitalWrite(GREEN_LED_PIN, HIGH); // Start with Green ON (assuming OK)
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(FAN_LED_PIN, LOW);

  // Connect to WiFi (will display status on LCD2)
  setupWiFi();

  // Initialize DHT sensor
  dht.setup(DHT_PIN_Z2, DHTesp::DHT22); // Initialize DHT sensor
  Serial.println("DHT22 Initialized");

  // Short delay after setup procedures
  delay(1000);
  lcd1.clear(); // Clear LCD1 ready for loop data
  // LCD2 screen is managed by WiFi status and loop()
}

// --- Main Loop ---
void loop() {
  // Record start time to calculate loop duration for delay
  unsigned long loopStartTime = millis();

  // 1. Read all sensors
  float temperatureCZ1 = readNtcTemperature(TEMP_PIN_Z1);
  float luxZ1 = readLdrLux(LIGHT_PIN_Z1);
  // Read DHT22 sensor for Zone 2
  TempAndHumidity newValues = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE) {
    temperatureCZ2 = newValues.temperature;
    humidityZ2 = newValues.humidity;
  } else {
    Serial.print("Error reading DHT22: ");
    Serial.println(dht.getStatusString());
    if (temperatureCZ2 == -999.0) temperatureCZ2 = -998.0;
    if (humidityZ2 == -999.0) humidityZ2 = -998.0;
  }

  // 2. Reset alert flags for the current reading cycle
  zone1_alert = false;
  zone2_alert = false;
  high_temp_alert = false;

  // 3. Evaluate alert conditions based on sensor readings and thresholds
  // Zone 1 Alerts
  bool z1_temp_alert = (temperatureCZ1 != -999.0 && temperatureCZ1 > TEMP_HIGH_THRESHOLD);
  bool z1_lux_alert = (luxZ1 != -1.0 && luxZ1 < LIGHT_LOW_THRESHOLD && luxZ1 != 0.0); // Alert on low light, ignore errors/bright
  if (z1_temp_alert || z1_lux_alert) {
    zone1_alert = true;
  }

  // Zone 2 Alerts (Temperature and Humidity)
  bool z2_temp_alert = (temperatureCZ2 != -999.0 && temperatureCZ2 != -998.0 && temperatureCZ2 > TEMP_HIGH_THRESHOLD);
  bool z2_humidity_alert = (humidityZ2 != -999.0 && humidityZ2 != -998.0 && humidityZ2 > HUMIDITY_HIGH_THRESHOLD);
  if (z2_temp_alert || z2_humidity_alert) {
    zone2_alert = true;
  }

  // High Temperature Alert (for Fan) based on either zone's temperature
  if (z1_temp_alert || z2_temp_alert) {
    high_temp_alert = true;
  }

  // 4. Control indicator LEDs based on alert status
  bool yellowState = zone1_alert;
  bool redState = zone2_alert;
  bool greenState = !(zone1_alert || zone2_alert); // Green ON only if NO alerts

  digitalWrite(YELLOW_LED_PIN, yellowState);
  digitalWrite(RED_LED_PIN, redState);
  digitalWrite(GREEN_LED_PIN, greenState);

  // 5. Control Buzzer - brief beep if any alert is active
  int buzzerDelay = 0; // Track time spent buzzing for delay calculation
  if (zone1_alert || zone2_alert) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100); // Beep duration
    digitalWrite(BUZZER_PIN, LOW);
    buzzerDelay = 100; // Record the delay caused by the buzzer
  } else {
    digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off if no alerts
  }

  // 6. Control Fan LED based on high temperature alert
  digitalWrite(FAN_LED_PIN, high_temp_alert);

  // 7. Update LCD Displays with current data and status
  // LCD 1 (16x2) - Detailed readings
  lcd1.clear();
  lcd1.setCursor(0, 0);
  lcd1.print("Z1:");
  if (temperatureCZ1 == -999.0) lcd1.print("ERR"); else lcd1.print(temperatureCZ1, 1);
  lcd1.print("C ");
  // Display Lux status: DARK for error/dark, >BRT for saturated, value otherwise
  if (luxZ1 == -1.0) lcd1.print("DARK"); else if (luxZ1 == 0.0) lcd1.print(">BRT"); else lcd1.print((int)luxZ1);
  lcd1.print("lx");
  if (zone1_alert) lcd1.print("!"); // Indicate Zone 1 alert

  lcd1.setCursor(0, 1);
  lcd1.print("Z2:");
  if (temperatureCZ2 <= -998.0) lcd1.print("ERR"); else lcd1.print(temperatureCZ2, 1);
  lcd1.print("C ");
  if (humidityZ2 <= -998.0) lcd1.print("H:ERR"); else { lcd1.print("H:"); lcd1.print((int)humidityZ2); lcd1.print("%"); }
  if (zone2_alert) lcd1.print("!");

  // LCD 2 (20x4) - System Status Summary
  lcd2.clear();
  lcd2.setCursor(0, 0); // Line 1: Overall Status
  if (zone1_alert || zone2_alert) {
      lcd2.print("SYSTEM ALERT ACTIVE!");
  } else {
      lcd2.print("System Status: OK");
  }

  lcd2.setCursor(0, 1); // Line 2: Zone 1 Summary
  lcd2.print("Z1: ");
  if (temperatureCZ1 == -999.0) lcd2.print("T:ERR "); else { lcd2.print("T:"); lcd2.print(temperatureCZ1, 1); lcd2.print("C "); }
  if (luxZ1 == -1.0) lcd2.print("L:DARK"); else if (luxZ1 == 0.0) lcd2.print("L:>BRT"); else { lcd2.print("L:"); lcd2.print((int)luxZ1); lcd2.print("lx"); }
  if (zone1_alert) lcd2.print(" !");

  lcd2.setCursor(0, 2); // Line 3: Zone 2 Summary
  lcd2.print("Z2: ");
  if (temperatureCZ2 <= -998.0) lcd2.print("T:ERR "); else { lcd2.print("T:"); lcd2.print(temperatureCZ2, 1); lcd2.print("C "); }
  if (humidityZ2 <= -998.0) lcd2.print("H:ERR"); else { lcd2.print("H:"); lcd2.print((int)humidityZ2); lcd2.print("%"); }
  if (zone2_alert) lcd2.print(" !");

  lcd2.setCursor(0, 3); // Line 4: Fan Status and WiFi Indicator
  lcd2.print("Fan Status: ");
  lcd2.print(high_temp_alert ? "ON" : "OFF");
  // Add a WiFi status indicator if not connected
  if (WiFi.status() != WL_CONNECTED) {
      lcd2.setCursor(18, 3); // Position at end of line
      lcd2.print("WF!"); // Indicate WiFi problem
  }


   // 8. Generate JSON Data String for backend
   String jsonData = "{";
   jsonData += "\"zone1\":{";
   jsonData += "\"tempC\":";
   if (temperatureCZ1 == -999.0) jsonData += "null"; else jsonData += String(temperatureCZ1, 1);
   jsonData += ",\"lux\":";
   // Use null for errors, specific strings for states, number otherwise
   if (luxZ1 == -1.0) jsonData += "\"DARK\""; else if (luxZ1 == 0.0) jsonData += "\"BRIGHT\""; else jsonData += String((int)luxZ1);
   jsonData += ",\"alert\":";
   jsonData += zone1_alert ? "true" : "false";
   jsonData += "},";
   jsonData += "\"zone2\":{\"dhtTempC\":";
   if (temperatureCZ2 <= -998.0) jsonData += "null"; else jsonData += String(temperatureCZ2, 1);
   jsonData += ",\"humidity\":";
   if (humidityZ2 <= -998.0) jsonData += "null"; else jsonData += String(humidityZ2, 1);
   jsonData += ",\"alert\":";
   jsonData += zone2_alert ? "true" : "false";
   jsonData += "},";
   jsonData += "\"fan_on\":";
   jsonData += high_temp_alert ? "true" : "false";
   jsonData += "}";
   // --- END Generate JSON Data ---

   // 9. Print JSON locally to Serial Monitor for debugging
   Serial.println(jsonData);

   // 10. Send the collected data to the Supabase backend function
   sendDataToBackend(jsonData);

   // 11. Calculate delay to maintain ~30 second loop interval
   unsigned long loopEndTime = millis();
   long loopDuration = loopEndTime - loopStartTime; // Time spent in this loop iteration
   // Target interval minus loop duration minus any delay already caused by the buzzer
   long delayTime = 30000 - loopDuration - buzzerDelay;

   // Ensure delay is not negative if loop took longer than target
   if (delayTime < 0) {
       delayTime = 100; // Set a minimum delay to prevent blocking CPU completely
       Serial.println("Warning: Loop duration exceeded target interval!");
   }

   // Wait for the calculated duration
   delay(delayTime);
}


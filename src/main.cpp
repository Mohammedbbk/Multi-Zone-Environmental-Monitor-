#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHTesp.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* serverUrl = "https://elxrhewruujmwthlhhni.supabase.co/functions/v1/log-sensor-data";

LiquidCrystal_I2C lcd1(0x27, 16, 2);
LiquidCrystal_I2C lcd2(0x3F, 20, 4);

const int TEMP_PIN_Z1 = 34;
const int LIGHT_PIN_Z1 = 35;
const int DHT_PIN_Z2 = 25;
const int GREEN_LED_PIN = 19;
const int YELLOW_LED_PIN = 18;
const int RED_LED_PIN = 5;
const int BUZZER_PIN = 17;
const int FAN_LED_PIN = 16;

const float ADC_MAX_VALUE = 4095.0;
const float ADC_REF_VOLTAGE = 3.3;

const float BETA = 3950;
const float R_KNOWN = 10000;
const float T0_KELVIN = 298.15;

const float LDR_GAMMA = 0.7;
const float LDR_RL10 = 50;
const float LDR_SERIES_RESISTOR = 10000;

const float TEMP_HIGH_THRESHOLD = 30.0;
const float LIGHT_LOW_THRESHOLD = 100;
const float HUMIDITY_HIGH_THRESHOLD = 70.0;

bool zone1_alert = false;
bool zone2_alert = false;
bool high_temp_alert = false;

DHTesp dht;
float humidityZ2 = -999.0;
float temperatureCZ2 = -999.0;

float readNtcTemperature(int pin) {
  int analogValue = analogRead(pin);
  if (analogValue <= 0 || analogValue >= ADC_MAX_VALUE) {
      Serial.printf("NTC Pin %d: Invalid ADC reading: %d\n", pin, analogValue);
      return -999.0;
  }

  float term1 = ADC_MAX_VALUE / (float)analogValue - 1.0;
  if (term1 <= 0) {
       Serial.printf("NTC Pin %d: Calculation term1 invalid (<=0): %.2f\n", pin, term1);
       return -999.0;
  }

  float term2 = log(1.0 / term1);
  float term3 = term2 / BETA;
  float term4 = term3 + (1.0 / T0_KELVIN);
  if (abs(term4) < 1e-9) {
      Serial.printf("NTC Pin %d: Calculation term4 near zero: %.6f\n", pin, term4);
      return -999.0;
  }

  float kelvin = 1.0 / term4;
  float celsius = kelvin - 273.15;

  if (!isfinite(celsius)) {
      Serial.printf("NTC Pin %d: Calculated temperature is not finite: %.2f\n", pin, celsius);
      return -999.0;
  }
  return celsius;
}

float readLdrLux(int pin) {
  int analogValue = analogRead(pin);
  float voltage = (float)analogValue / ADC_MAX_VALUE * ADC_REF_VOLTAGE;

  if (voltage <= 0.01) {
       return -1.0;
  }
  if (voltage >= (ADC_REF_VOLTAGE - 0.01)) {
      return 0.0;
  }

  float resistance = LDR_SERIES_RESISTOR * voltage / (ADC_REF_VOLTAGE - voltage);
  if (resistance <=0) return 0.0;

  float lux = pow(LDR_RL10 * 1e3 * pow(10, LDR_GAMMA) / resistance, (1.0 / LDR_GAMMA));

  if (!isfinite(lux)) {
      return -1.0;
  }

  return lux;
}

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

  WiFi.begin(ssid, password, 6);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    wl_status_t status = WiFi.status();
    Serial.print(".");
    Serial.print(status);
    delay(250);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.println("WiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      lcd2.setCursor(0,0);
      lcd2.print("WiFi Connected      ");
      lcd2.setCursor(0,1);
      lcd2.print("IP: ");
      lcd2.print(WiFi.localIP());
      delay(2500);
      lcd2.setCursor(0,0);
      lcd2.print("                    ");
      lcd2.setCursor(0,1);
      lcd2.print("                    ");

  } else {
      Serial.println("");
      Serial.println("WiFi connection failed!");
      lcd2.setCursor(0,0);
      lcd2.print("WiFi Failed!        ");
  }
}

void sendDataToBackend(String jsonData) {
    if (WiFi.status() == WL_CONNECTED) {

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;

        Serial.print("Sending data to backend: ");
        Serial.println(serverUrl);

        http.setTimeout(10000);

        http.begin(client, serverUrl);

        http.addHeader("Content-Type", "application/json");
     
        int httpResponseCode = http.POST(jsonData);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            Serial.print("Response: ");
            Serial.println(response);
             if (httpResponseCode != 200 && httpResponseCode != 201) {
                 Serial.println("Note: HTTP response code was not 200 or 201.");
             }
        } else {
            Serial.print("Error sending POST: ");
            Serial.println(httpResponseCode);
            Serial.printf("HTTP POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
        }

        http.end();
    } else {
        Serial.println("WiFi not connected. Cannot send data.");

    }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Multi-Zone Environmental Monitor Initializing...");

  Wire.begin();

  lcd1.init();
  lcd1.backlight();
  lcd1.print("Initializing Z1");

  lcd2.init();
  lcd2.backlight();
  lcd2.setCursor(0,0);
  lcd2.print("Initializing Sys...");

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_LED_PIN, OUTPUT);

  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(FAN_LED_PIN, LOW);

  setupWiFi();

  dht.setup(DHT_PIN_Z2, DHTesp::DHT22);
  Serial.println("DHT22 Initialized");

  delay(1000);
  lcd1.clear(); 
}

void loop() {
  unsigned long loopStartTime = millis();

  float temperatureCZ1 = readNtcTemperature(TEMP_PIN_Z1);
  float luxZ1 = readLdrLux(LIGHT_PIN_Z1);
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

  zone1_alert = false;
  zone2_alert = false;
  high_temp_alert = false;

  bool z1_temp_alert = (temperatureCZ1 != -999.0 && temperatureCZ1 > TEMP_HIGH_THRESHOLD);
  bool z1_lux_alert = (luxZ1 != -1.0 && luxZ1 < LIGHT_LOW_THRESHOLD && luxZ1 != 0.0);
  if (z1_temp_alert || z1_lux_alert) {
    zone1_alert = true;
  }

  bool z2_temp_alert = (temperatureCZ2 != -999.0 && temperatureCZ2 != -998.0 && temperatureCZ2 > TEMP_HIGH_THRESHOLD);
  bool z2_humidity_alert = (humidityZ2 != -999.0 && humidityZ2 != -998.0 && humidityZ2 > HUMIDITY_HIGH_THRESHOLD);
  if (z2_temp_alert || z2_humidity_alert) {
    zone2_alert = true;
  }

  if (z1_temp_alert || z2_temp_alert) {
    high_temp_alert = true;
  }

  bool yellowState = zone1_alert;
  bool redState = zone2_alert;
  bool greenState = !(zone1_alert || zone2_alert);

  digitalWrite(YELLOW_LED_PIN, yellowState);
  digitalWrite(RED_LED_PIN, redState);
  digitalWrite(GREEN_LED_PIN, greenState);

  int buzzerDelay = 0;
  if (zone1_alert || zone2_alert) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    buzzerDelay = 100;
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  digitalWrite(FAN_LED_PIN, high_temp_alert);

  lcd1.clear();
  lcd1.setCursor(0, 0);
  lcd1.print("Z1:");
  if (temperatureCZ1 == -999.0) lcd1.print("ERR"); else lcd1.print(temperatureCZ1, 1);
  lcd1.print("C ");
  if (luxZ1 == -1.0) lcd1.print("DARK"); else if (luxZ1 == 0.0) lcd1.print(">BRT"); else lcd1.print((int)luxZ1);
  lcd1.print("lx");
  if (zone1_alert) lcd1.print("!");

  lcd1.setCursor(0, 1);
  lcd1.print("Z2:");
  if (temperatureCZ2 <= -998.0) lcd1.print("ERR"); else lcd1.print(temperatureCZ2, 1);
  lcd1.print("C ");
  if (humidityZ2 <= -998.0) lcd1.print("H:ERR"); else { lcd1.print("H:"); lcd1.print((int)humidityZ2); lcd1.print("%"); }
  if (zone2_alert) lcd1.print("!");

  lcd2.clear();
  lcd2.setCursor(0, 0);
  if (zone1_alert || zone2_alert) {
      lcd2.print("SYSTEM ALERT ACTIVE!");
  } else {
      lcd2.print("System Status: OK");
  }

  lcd2.setCursor(0, 1);
  lcd2.print("Z1: ");
  if (temperatureCZ1 == -999.0) lcd2.print("T:ERR "); else { lcd2.print("T:"); lcd2.print(temperatureCZ1, 1); lcd2.print("C "); }
  if (luxZ1 == -1.0) lcd2.print("L:DARK"); else if (luxZ1 == 0.0) lcd2.print("L:>BRT"); else { lcd2.print("L:"); lcd2.print((int)luxZ1); lcd2.print("lx"); }
  if (zone1_alert) lcd2.print(" !");

  lcd2.setCursor(0, 2);
  lcd2.print("Z2: ");
  if (temperatureCZ2 <= -998.0) lcd2.print("T:ERR "); else { lcd2.print("T:"); lcd2.print(temperatureCZ2, 1); lcd2.print("C "); }
  if (humidityZ2 <= -998.0) lcd2.print("H:ERR"); else { lcd2.print("H:"); lcd2.print((int)humidityZ2); lcd2.print("%"); }
  if (zone2_alert) lcd2.print(" !");

  lcd2.setCursor(0, 3);
  lcd2.print("Fan Status: ");
  lcd2.print(high_temp_alert ? "ON" : "OFF");
  if (WiFi.status() != WL_CONNECTED) {
      lcd2.setCursor(18, 3);
      lcd2.print("WF!");
  }

   String jsonData = "{";
   jsonData += "\"zone1\":{";
   jsonData += "\"tempC\":";
   if (temperatureCZ1 == -999.0) jsonData += "null"; else jsonData += String(temperatureCZ1, 1);
   jsonData += ",\"lux\":";
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

   Serial.println(jsonData);

   sendDataToBackend(jsonData);

   unsigned long loopEndTime = millis();
   long loopDuration = loopEndTime - loopStartTime;
   long delayTime = 30000 - loopDuration - buzzerDelay;

   if (delayTime < 0) {
       delayTime = 100;
       Serial.println("Warning: Loop duration exceeded target interval!");
   }

   delay(delayTime);
}

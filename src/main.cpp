#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ssl_client.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <ThingSpeak.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <SPI.h>
#include <BME280I2C.h>

void ota_setup();
void updateIOT(void);
void LineSend(String message, const char *token);

const char *ssid = "106F3F0E6E10_G";
const char *password = "y87ux8ty75st4";
const char *ntpServerName1 = "ntp.nict.jp";
const char *hostname = "Temp1F-M5StampPico";
const char *lineToken = "ZX71us6POf2azIB7OGO1BQpgI7f3ahBXKsSYFMz4teO";

#define BUTTON_PIN 39 // M5StampPico
#define SESPWR 26
#define PIXEL_PIN 27

bool update_now = false;

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP (5 * 60)    /* Time ESP32 will go to sleep (in seconds) */
#define BF_WAKE_SEC 10
#define UPTIME 3
#define ON_SEC 13
#define RGB_BRIGHTNESS 1

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int LineMseg = 0;

BME280I2C::Settings settings(
    BME280::OSR_X4,
    BME280::OSR_X4,
    BME280::OSR_X4,
    BME280::Mode_Forced,
    BME280::StandbyTime_1000ms,
    BME280::Filter_Off,
    BME280::SpiEnable_False,
    BME280I2C::I2CAddr_0x76 // I2C address. I2C specific.
);
BME280I2C bme(settings);
float temp(NAN), hum(NAN), pres(NAN);

float vcc = 0.0;
#define AD_AVG 10

void print_wakeup_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.println("Wakeup caused by external signal using RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.println("Wakeup caused by external signal using RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("Wakeup caused by timer");
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    Serial.println("Wakeup caused by touchpad");
    break;
  case ESP_SLEEP_WAKEUP_ULP:
    Serial.println("Wakeup caused by ULP program");
    break;
  default:
    Serial.printf("Wakeup was not caused by deep sleep: %d\r\n", wakeup_reason);
    break;
  }
}

void get_sensor(void)
{
  digitalWrite(SESPWR, 0);
  delay(500);
  Wire.begin();

  if (!bme.begin())
  {
    Serial.println("Could not find BME280 sensor!");
  }
  else
  {
    //  bme.begin();
    delay(100);
    BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
    BME280::PresUnit presUnit(BME280::PresUnit_hPa);

    bme.read(pres, temp, hum, tempUnit, presUnit);
  }

  analogReadResolution(12);
  for (int cnt = 0; cnt < AD_AVG; cnt++)
  {
    vcc += analogRead(36) / 4095.0 * 3.45 * 2.0;
    delay(5);
  }
  vcc /= (float)AD_AVG;

  digitalWrite(SESPWR, 1);
  pinMode(SESPWR, INPUT);
}

void setup()
{
  pinMode(SESPWR, OUTPUT);
  digitalWrite(SESPWR, 1);

  digitalWrite(PIXEL_PIN, LOW);                   // Turn the RGB LED off
  neopixelWrite(PIXEL_PIN, RGB_BRIGHTNESS, 0, 0); // Red

  Serial.begin(115200);
  delay(1000); // Take some time to open up the Serial Monitor

  // Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  // Print the wakeup reason for ESP32
  print_wakeup_reason();

  WiFi.hostname(hostname);
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  int retries = 0;
  while ((WiFi.status() != WL_CONNECTED) && (retries < 10))
  {
    Serial.print(".");
    retries++;
    neopixelWrite(PIXEL_PIN, 0, 0, RGB_BRIGHTNESS); // Blue
    delay(1000);
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("WiFi NOT connected...\nRestart...");
    neopixelWrite(PIXEL_PIN, 0, 0, 0); // Off / black
    ESP.restart();
  }
  neopixelWrite(PIXEL_PIN, 0, RGB_BRIGHTNESS, 0); // Green
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  configTzTime("JST-9", ntpServerName1);
  ota_setup();
}

void loop()
{
  static unsigned long previousMillis = 0;
  static unsigned long OnMillis = 0;
  unsigned long currentMillis = millis();

  ArduinoOTA.handle();
  if (update_now == true)
    return;

  if (previousMillis != currentMillis && (currentMillis % 1000) == 0)
  {
    Serial.print("*");

    if (OnMillis == 0)
    {
      time_t t = time(nullptr);
      struct tm *ltm = localtime(&t);
      if (ltm->tm_sec == ON_SEC)
      {
        Serial.println("");
        Serial.println(ltm, "Time:%Y-%m-%d %H:%M:%Si");
        updateIOT();
        OnMillis = currentMillis;
      }
    }
    previousMillis = currentMillis;
  }

  if (OnMillis != 0 && currentMillis > (UPTIME * 1000 + OnMillis))
  {
    OnMillis = 0;
    WiFi.disconnect(true);
    esp_wifi_stop();

    Serial.println("");
    Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +
                   " Seconds");

    Serial.println("Going to sleep now");

    esp_sleep_enable_timer_wakeup((TIME_TO_SLEEP - UPTIME - BF_WAKE_SEC) * uS_TO_S_FACTOR);
    neopixelWrite(PIXEL_PIN, 0, 0, 0); // Off / black
    esp_deep_sleep_start();
    Serial.println("This will never be printed");
  }
}

void updateIOT(void)
{
  unsigned long myChannelNumber = 145005;
  const char *myWriteAPIKey = "K4M13XJR2J3N2CNK";

  get_sensor();

  WiFiClient client;

  ThingSpeak.begin(client);
  ThingSpeak.setField(1, WiFi.RSSI());
  ThingSpeak.setField(2, vcc);
  ThingSpeak.setField(3, temp);
  ThingSpeak.setField(4, hum);
  ThingSpeak.setField(5, pres);
  // ThingSpeak.setField(8, String(ESP.getFreeHeap()));
  // ThingSpeak.setField(8, air_hum);

  time_t t = time(nullptr);
  struct tm *ltm = localtime(&t);
  String s;
  char str[40];
  strftime(str, sizeof(str) - 1, "Time:%Y-%m-%d %H:%M:%Si", ltm);
  s = String(str) + " BootCnt:" + String(bootCount);
  ThingSpeak.setStatus(s);

  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (x == 200)
  {
    Serial.println("Channel update successful.");
  }
  else
  {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
  client.stop();

  if (vcc < 3.1 && LineMseg == 0)
  {
    LineMseg = 1;
    String mesg(hostname);
    mesg += ": BATT Low " + String(vcc) + "V 電池交換だよ！";
    LineSend(mesg, lineToken);
  }
}

void ota_setup()
{
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA
      .onStart([]()
               {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()

    Serial.println("Start updating " + type);
    update_now = true; })
      .onEnd([]()
             { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total)
                  { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
      .onError([](ota_error_t error)
               {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    delay(3000);
    ESP.restart(); });

  ArduinoOTA.begin();
}

void LineSend(String message, const char *token)
{
  const char *host = "notify-api.line.me";

  WiFiClientSecure client;
  Serial.println("Try");
  client.setInsecure();
  // LineのAPIサーバに接続
  if (!client.connect(host, 443))
  {
    Serial.println("Connection failed");
    return;
  }
  Serial.println("Connected");
  // リクエストを送信
  String query = String("message=") + message;
  String request = String("") +
                   "POST /api/notify HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Authorization: Bearer " + token + "\r\n" +
                   "Content-Length: " + String(query.length()) + "\r\n" +
                   "Content-Type: application/x-www-form-urlencoded\r\n\r\n" +
                   query + "\r\n";
  client.print(request);

  // 受信終了まで待つ
  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    Serial.println(line);
    if (line == "\r")
    {
      break;
    }
  }

  String line = client.readStringUntil('\n');
  Serial.println(line);
}

#include <Arduino.h>
#define ARDUINOJSON_USE_LONG_LONG 1 // https://arduinojson.org/v6/api/config/use_long_long/, to use 64-bit long in getEpocTime().
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

#include <Preferences.h>
#include <SPIFFS.h>
#include <FS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <AsyncMqttClient.h>
extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}
#include "esp_log.h"
#include "Button2.h"
#include "HardwareSerial.h"
#include "SPI.h"
#include "TFT_eSPI.h"
#include "onewire.h"
#include "ds18x20.h"
#include "ds2408.h"
#include "ds2423.h"

#ifndef TFT_DISPOFF
#define TFT_DISPOFF 0x28
#endif

#ifndef TFT_SLPIN
#define TFT_SLPIN 0x10
#endif

#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS 5
#define TFT_DC 16
#define TFT_RST 23
#define TFT_BL 4  // Display backlight control pin

#define RXD2 33
#define TXD2 32
#define FIRST_BUTTON 0
#define SECOND_BUTTON 35

#define PARAM_FILE "/param.json"
#define AUX_MQTTSETTING "/mqtt_settings"
#define AUX_MQTTSAVE "/mqtt_save"

#define SAMPLE_DELAY 20000          // milliseconds between reading sensors.
#define FORCE_MQTT_PUSH 60000       // if still nothing has changed after this many milliseconds, we force a push to show that we are alive.
#define TEMPERATURE_HYSTERESIS 0.5  // degrees celsius.
#define WDT_TIMEOUT_SEC 60          // main loop watchdog, if stalled longer than XX seconds we will reboot.

static const char* TAG = "TinyOWC";

static const int64_t START_TIME = esp_timer_get_time();

enum STATES {
  NO_DEVICES,
  START_SCANNING,
  SCANNING_DONE,
  OPERATIONAL
};

const char Base_Html[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML>
  <html lang="en">
    <head>
      <title>Tiny-OWC</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <link rel="icon" href="data:,">
      <style>
        * {
            font-family: courier, arial, helvetica;
          }
      </style>
    </head>
    <body>
      <h1>Tiny-OWC</h1>
      <p>Board id: %UNIQUE_ID%</p>
      <p>%UPTIME%</p>
      <h3>1-Wire devices:</h3>
      %ONE_WIRE_DEVICES%
      <p>
        <a href="/setup">Setup</a>
      </p>

    </body>
  </html>
)rawliteral";

const String PROGRESS_INDICATOR[] = { "|", "/", "-", "\\" };
uint8_t progressIndicator = 0;

STATES state = NO_DEVICES;
Preferences preferences;

char buff[512];
int64_t conversionStarted = 0;

Button2 firstButton = Button2(FIRST_BUTTON);
Button2 secondButton = Button2(SECOND_BUTTON);
bool isScanning = false;
long lastReadingTime = 0;
long numberOfSamplesSinceReboot = 0;

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI(135, 240);  // Width, Height, screen dimension

DS2480B ds(Serial2);

WebServer webserver;
AsyncMqttClient mqttClient;
AutoConnect portal(webserver);
bool portalConnected = false;
String mqttserver;
String mqttserver_port;
String mqtt_base_topic;
String mqtt_topic;
String mqtt_base_cmdtopic;
String mqtt_cmdtopic;
TimerHandle_t mqttReconnectTimer;

String uniqueId = String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
String appName = "Tiny-OWC_" + uniqueId;

extern void pushStateToMQTT(onewireNode& node);
extern void pushAllStateToMQTT();

void clearScreen() {
  tft.fillScreen(TFT_BLACK);
}

void printState() {
    String text;
    switch (state) {
      case NO_DEVICES:
        text = "No devices, please scan";
        break;
      case START_SCANNING:
        text = "Scanning...";
        break;
      case SCANNING_DONE:
        text = "Found devices, Save or Exit";
        break;
      case OPERATIONAL:
        snprintf(buff, sizeof(buff), "Operational, WiFi:%s, MQTT:%s", WiFi.isConnected() ? "OK" : "-", mqttClient.connected() ? "OK" : "-");
        text = String(buff);
        break;
      default:
        text = "unknown";
        break;
    }

    tft.setTextSize(1);
    tft.setCursor(0, tft.height() - tft.fontHeight());
    tft.printf("state: %s\n", text.c_str());
}

/**
 * Gets seconds since Unix epoctime (1970-01-01)
 */
int64_t getEpocTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}

/**
 * Get current date/time as a string
 * @param format e.g. "%d %b %Y, %H:%M:%S%z"
 * @param timeout for how many milliseconds we try to obtain time
 */
String getTime(String format, uint32_t timeout = 5000) {
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo, timeout)) {
        return F("Failed to obtain time");
    }

    char outstr[80];
    strftime(outstr, sizeof(outstr), format.c_str(), &timeinfo); // ISO 8601 time

    return String(outstr);
}

// Load AutoConnectAux (desired web page) JSON from SPIFFS.
bool loadAux(const String auxName) {
  bool rc = false;
  String fn = auxName + ".json";
  File fs = SPIFFS.open(fn.c_str(), "r");
  if (fs) {
    rc = portal.load(fs);
    fs.close();
  } else {
    ESP_LOGE(TAG, "SPIFFS open failed: %s", fn);
  }
  
  return rc;
}

void scanOneWireNetwork() {
  if (!isScanning) {
    isScanning = true;
    byte addr[8];

    ESP_LOGI(TAG, "Start scanning 1-wire network...");

    clearScreen();
    printState();
    tft.drawString("Scanning 1-wire...", tft.width() / 2, tft.height() / 2);
    tft.setCursor(0, 0);

    while (ds.search(addr)) {
      Serial.print("Found device with ROM =");
      for (uint8_t i = 0; i < 8; i++) {
        Serial.write(' ');
        Serial.print(addr[i], HEX);
      }
      Serial.println();
      tft.print(".");

      if (DS2480B::crc8(addr, 7) != addr[7]) {
        ESP_LOGW(TAG, "CRC is not valid on detected 1-wire device, ignoring.");
      } else {
        onewireNode node;
        populateNode(node, addr);

        if (familyIdToNameTranslation(node.familyId).length() > 0) {
          scannedOneWireNodes.push_back(node);
        } else {
          ESP_LOGI(TAG, "Unsupported 1-wire device(family=%x) found, ignoring.", node.familyId);
        }
      }

      esp_task_wdt_reset();
      delay(5);
    }
  
    ds.reset_search();
    ds.reset();

    ESP_LOGI(TAG, "Scanning done.");

    clearScreen();
    tft.setCursor(0, 0);

    if (scannedOneWireNodes.size() > 0) {
      for (auto i : scannedOneWireNodes) {
        snprintf(buff, sizeof(buff), "Found %s (%s)", i.idStr.c_str(), familyIdToNameTranslation(i.familyId).c_str());
        tft.println(buff);
      }
    } else {
      tft.println("No devices found. :-(");
    }

    delay(3000);

    state = SCANNING_DONE;
    isScanning = false;
  }
}

void loadSettings() {
  auto serializedNodes = preferences.getString("nodes", "[]");
  DynamicJsonDocument doc(4096);

  auto error = deserializeJson(doc, serializedNodes);

  if (error) {
    ESP_LOGE(TAG, "Failed loading settings, deserializeJson() failed with code: %s.", error.c_str());
    return;
  }

  JsonArray nodesArray = doc.as<JsonArray>();
  for (JsonObject jsonNode : nodesArray) {
    onewireNode node;

    JsonArray idArray = jsonNode["id"];
    for (uint8_t i = 0, size = idArray.size(); i < size; i++) {
      node.id[i] = idArray[i];
    }

    JsonArray actuatorIdArray = jsonNode["actuatorId"];
    for (uint8_t i = 0, size = actuatorIdArray.size(); i < size; i++) {
      node.actuatorId[i] = actuatorIdArray[i];
    }
    
    node.name = jsonNode["name"].as<String>();
    node.actuatorPin = jsonNode["actuatorPin"] | -1;
    node.lowLimit = jsonNode["lowLimit"] | UNSET_TEMPERATURE;
    node.highLimit = jsonNode["highLimit"] | UNSET_TEMPERATURE;

    populateNode(node, node.id);
    oneWireNodes.push_back(node);
  }
}

void saveSettings(std::vector<onewireNode> &nodes) {
  DynamicJsonDocument doc(4096);
  JsonArray nodesArray = doc.to<JsonArray>();
  
  for (auto n : nodes) {
    auto jsonNode = nodesArray.createNestedObject();

    auto idArray = jsonNode.createNestedArray("id");
    for (auto i : n.id) {
      idArray.add(i);
    }

    auto actuatorIdArray = jsonNode.createNestedArray("actuatorId");
    for (auto i : n.actuatorId) {
      actuatorIdArray.add(i);
    }
    if (n.name != NULL && n.name.length() > 0) {
      jsonNode["name"] = n.name;
    } else {
      jsonNode["name"] = "";
    }
    jsonNode["actuatorPin"] = n.actuatorPin;
    jsonNode["lowLimit"] = n.lowLimit;
    jsonNode["highLimit"] = n.highLimit;
  }

  String jsonString;
  serializeJson(doc, jsonString);

  if (jsonString.length() > 0) {
    ESP_LOGI(TAG, "Saving node list: %s", jsonString.c_str());  // please note that IDs are saved in their decimal value (NOT hexadecimal), and thus looks a bit strange. 
    preferences.putString("nodes", jsonString);
  }
}

void firstButtonClick(Button2& btn) { 
  ESP_LOGD(TAG, "firstButtonClick()");

  if (state == SCANNING_DONE) {
    // If we scanned but pressed Exit, throw away scanned devices.
    scannedOneWireNodes.clear();
  }

  clearScreen();
}

void secondButtonClick(Button2& btn) {
  ESP_LOGD(TAG, "secondButtonClick()");

  if (state == SCANNING_DONE) {
    saveSettings(scannedOneWireNodes);
    oneWireNodes = scannedOneWireNodes;
    scannedOneWireNodes.clear();
    clearScreen();

    // Set DS2408 to a known state (testmode=off,strobe out,all relayes off).
    for (auto &node : oneWireNodes) {
      if (node.familyId == DS2408) {
        ds2408_reset(ds, node);
      }
    }
  } else if (state == NO_DEVICES || state == OPERATIONAL) {
    state = START_SCANNING;
  }
}

void secondButtonTrippleClick(Button2& btn) {
  ESP_LOGD(TAG, "secondButtonTrippleClick(), erasing all known nodes!");

  oneWireNodes.clear();
  scannedOneWireNodes.clear();
  preferences.putString("nodes", "[]");
  ESP.restart();
}

void printOneWireNodes() {
  clearScreen();
  tft.setCursor(0, 0);
  tft.setTextSize(1);

  tft.println("1-Wire devices:");
  tft.println();
  Serial.println("printOneWireNodes():");

  if (oneWireNodes.size() > 0) {
    for (auto i : oneWireNodes) {
      if (isTemperatureSensor(i.familyId)) {
        if (i.failedReadingsInRow < 5) {
          if (i.lowLimit > UNSET_TEMPERATURE && i.highLimit > UNSET_TEMPERATURE && i.actuatorPin > -1) {
            snprintf(buff, sizeof(buff), "%s (%s): %.1f\nLimits: %.1f - %.1f. Status: %s", i.idStr.c_str(), familyIdToNameTranslation(i.familyId).c_str(), i.temperature, i.lowLimit, i.highLimit, i.status ? "open" : "closed");
          } else {
            snprintf(buff, sizeof(buff), "%s (%s): %.1f", i.idStr.c_str(), familyIdToNameTranslation(i.familyId).c_str(), i.temperature);
          }
        } else {
          snprintf(buff, sizeof(buff), "%s (%s): Not connected.", i.idStr.c_str(), familyIdToNameTranslation(i.familyId).c_str());
        }
      } else if (i.familyId == DS2408) {
        snprintf(buff, sizeof(buff), "%s (%s)\nPins: %d %d %d %d %d %d %d %d",
         i.idStr.c_str(),
         familyIdToNameTranslation(i.familyId).c_str(),
         i.actuatorPinState[0],
         i.actuatorPinState[1],
         i.actuatorPinState[2],
         i.actuatorPinState[3],
         i.actuatorPinState[4],
         i.actuatorPinState[5],
         i.actuatorPinState[6],
         i.actuatorPinState[7]);
      } else if (i.familyId == DS2406 || i.familyId == DS2413) {
        snprintf(buff, sizeof(buff), "%s (%s)\nPins: %d %d",
         i.idStr.c_str(),
         familyIdToNameTranslation(i.familyId).c_str(),
         i.actuatorPinState[0],
         i.actuatorPinState[1]);
      } else if (i.familyId == DS2405) {
        snprintf(buff, sizeof(buff), "%s (%s)\nPins: %d",
         i.idStr.c_str(),
         familyIdToNameTranslation(i.familyId).c_str(),
         i.actuatorPinState[0]);
      } else if (i.familyId == DS2423) {
        snprintf(buff, sizeof(buff), "%s (%s)\nCounters: %d %d",
         i.idStr.c_str(),
         familyIdToNameTranslation(i.familyId).c_str(),
         i.counters[0],
         i.counters[1]);
      } else {
        snprintf(buff, sizeof(buff), "%s (%s)", i.idStr.c_str(), familyIdToNameTranslation(i.familyId).c_str());
      }
      tft.println(buff);
      Serial.println(buff);
    }

    tft.drawString("Samples since power-on: " + String(numberOfSamplesSinceReboot), 0, tft.height() - tft.fontHeight() * 2);
    Serial.println("Samples since power-on: " + String(numberOfSamplesSinceReboot));
  } else {
    tft.println("none, please scan.");
  }
}

String loadParams(AutoConnectAux &aux, PageArgument &args) {
  (void)(args);
  File param = SPIFFS.open(PARAM_FILE, "r");
  if (param) {
    if (!aux.loadElement(param)) {
      ESP_LOGE(TAG, "%s file failed to parse.", PARAM_FILE);
    }
    param.close();
  } else {
    ESP_LOGE(TAG, "%s file failed to open.", PARAM_FILE);
  }

  return String("");
}

String saveParams(AutoConnectAux &aux, PageArgument &args)
{
  mqttserver = args.arg("mqttserver");
  mqttserver.trim();

  mqttserver_port = args.arg("mqttserver_port");
  mqttserver_port.trim();

  mqtt_base_topic = args.arg("mqtt_base_topic");
  mqtt_base_topic.trim();

  mqtt_base_cmdtopic = args.arg("mqtt_base_cmdtopic");
  mqtt_base_cmdtopic.trim();

  // The entered value is owned by AutoConnectAux of /mqtt_settings.
  // To retrieve the elements of /mqtt_settings, it is necessary to get the AutoConnectAux object of /mqtt_settings.
  File param = SPIFFS.open(PARAM_FILE, "w");
  portal.aux(AUX_MQTTSETTING)->saveElement(param, {"mqttserver", "mqttserver_port", "mqtt_base_topic", "mqtt_base_cmdtopic"});
  param.close();

  // Echo back saved parameters to AutoConnectAux page.
  AutoConnectText &echo = aux["parameters"].as<AutoConnectText>();
  echo.value = "Server: " + mqttserver + "<br>";
  echo.value += "Port: " + mqttserver_port + "<br>";
  echo.value += "Publish topic: " + mqtt_base_topic + "<br>";
  echo.value += "Command topic: " + mqtt_base_cmdtopic + "<br>";

  return String("");
}

bool startedCapturePortal(IPAddress ip) {
  tft.println("Portal started, IP:" + ip.toString() + "/setup");
  Serial.println("Capture portal started, IP:" + ip.toString() + "/setup");
  return true;
}

void watchdogSetup() {
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);

  esp_err_t err = esp_task_wdt_add(NULL);
  switch (err) {
    case ESP_OK:
      Serial.printf("Watchdog activated OK (timeout is %d seconds)\n", WDT_TIMEOUT_SEC);
      break;
    case ESP_ERR_INVALID_ARG:
      Serial.println("Watchdog activation error: invalid argument");
      break;
    case ESP_ERR_NO_MEM:
      Serial.println("Watchdog activation error: insufficent memory");
      break;
    case ESP_ERR_INVALID_STATE:
      Serial.println("Watchdog activation error: not initialized yet");
      break;
    default:
      Serial.printf("Watchdog activation error: %d\n", err);
      break;
  }
}

void onMqttConnect(bool sessionPresent) {
  ESP_LOGI(TAG, "Connected to MQTT. Session present: %s", String(sessionPresent));
  if (mqtt_cmdtopic.length() > 0) {
    // TODO: should we subscribe if session is already pressent? Maybe not...
    mqttClient.subscribe(mqtt_cmdtopic.c_str(), 2);
    // make sure broker has our current state.
    pushAllStateToMQTT();
  }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  ESP_LOGI(TAG, "Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  ESP_LOGI(TAG, "MQTT subscribing to topic '%s'.", mqtt_cmdtopic.c_str());
}

void onMqttUnsubscribe(uint16_t packetId) {
  ESP_LOGI(TAG, "MQTT unsubscribing topic.");
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  ESP_LOGI(TAG, "MQTT message received, payload: %s", payload);

  StaticJsonDocument<250> doc;

  auto error = deserializeJson(doc, payload);

  if (error) {
    ESP_LOGW(TAG, "deserializeJson() failed with code: %s.", error.c_str());
    return;
  }

  auto command = doc["command"];
  if (command.isNull()) return;

  if (command == "setSensor") {
    /* Example
    {
      "command": "setSensor",
      "id": "28.EEA89B19160262",
      "name": "bedroom",
      "actuatorId": "29.29E1030000009C",
      "actuatorPin": 0,
      "lowLimit": 22,
      "highLimit": 24
    }
    */
    auto id = doc["id"].as<String>();
    auto name = doc["name"].as<String>();
    auto actuatorId = doc["actuatorId"];
    auto actuatorPin = doc["actuatorPin"] | -1;
    auto lowLimit = doc["lowLimit"] | UNSET_TEMPERATURE;
    auto highLimit = doc["highLimit"] | UNSET_TEMPERATURE;

    auto it = std::find_if (oneWireNodes.begin(), oneWireNodes.end(), [&id](const onewireNode& n) {
      return n.idStr == id;
    });
    if (it != oneWireNodes.end()) {
      auto node = it.base();
      stringToId(actuatorId, node->actuatorId);
      node->actuatorPin = actuatorPin;
      node->lowLimit = lowLimit;
      node->highLimit = highLimit;

      if (name != NULL && name.length() > 0) {
        name.trim();
        node->name = name.substring(0, 20);
      } else {
        node->name = "";
      }

      saveSettings(oneWireNodes);
      ESP_LOGI(TAG, "Settings for sensor '%s' updated.", id.c_str());
      pushStateToMQTT(*node);
    }    
  } else {
    ESP_LOGI(TAG, "Unsupported MQTT command received: '%s'.", command);
  }
}

void connectToMqtt() {
  ESP_LOGI(TAG, "Connecting to MQTT...");

  if (WiFi.isConnected()) {
    mqttClient.connect();
  }
}

void WiFiEvent(WiFiEvent_t event) {
    switch(event) {
      case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "WiFi connected and got IP.");

        configTime(1 * 3600, 1 * 3600, "pool.ntp.org"); // second parameter is daylight offset (3600 = summertime)
        ESP_LOGI(TAG, "Time: %s", getTime("%d %b %Y, %H:%M:%S%z").c_str());

        connectToMqtt();
        break;
      case SYSTEM_EVENT_STA_DISCONNECTED:
        xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
        ESP_LOGI(TAG, "Lost WiFi connection, trying to reconnect...");
        WiFi.begin();
        break;
      default:
        break;
    }
}

void handle_indexHtml() {
  auto html = String(Base_Html);

  html.replace("%UNIQUE_ID%" , uniqueId);

  auto seconds = (esp_timer_get_time() - START_TIME) / 1000000; // esp_timer_get_time in microseconds
  uint16_t days, hours, minutes;

  minutes = floor(seconds / 60);
  seconds = seconds % 60;
  hours = floor(minutes / 60);
  minutes = minutes % 60;
  days = floor(hours / 24);
  hours = hours % 24;
  
  snprintf(buff, sizeof(buff), "Uptime: <strong>%d</strong> days, <strong>%d</strong> hours, <strong>%d</strong> min, <strong>%lli</strong> sec", days, hours, minutes, seconds);
  html.replace("%UPTIME%" , String(buff));

  String oneWireList = "<ul>";
  for (auto i : oneWireNodes) {
    if (isTemperatureSensor(i.familyId)) {
        if (i.failedReadingsInRow < 5) {
          snprintf(buff, sizeof(buff), "<li>%s (%s), name: \"%s\", temp: %.1f, low-limit: %.1f, high-limit: %.1f, actuator-pin: %d, status: %s, errors: %d, success: %d</li>", 
          i.idStr.c_str(),
          familyIdToNameTranslation(i.familyId).c_str(),
          i.name.c_str(),
          i.temperature,
          i.lowLimit,
          i.highLimit,
          i.actuatorPin,
          i.status ? "open" : "closed",
          i.errors,
          i.success);
        } else {
          snprintf(buff, sizeof(buff), "<li>%s (%s), name: \"%s\": Not connected.</li>", i.idStr.c_str(), familyIdToNameTranslation(i.familyId).c_str(), i.name.c_str());
        }
    } else if (i.familyId == DS2408) {
      snprintf(buff, sizeof(buff), "<li>%s (%s), name: \"%s\", pins: %d %d %d %d %d %d %d %d, errors: %d, success: %d</li>",
        i.idStr.c_str(),
        familyIdToNameTranslation(i.familyId).c_str(),
        i.name.c_str(),
        i.actuatorPinState[0],
        i.actuatorPinState[1],
        i.actuatorPinState[2],
        i.actuatorPinState[3],
        i.actuatorPinState[4],
        i.actuatorPinState[5],
        i.actuatorPinState[6],
        i.actuatorPinState[7],
        i.errors,
        i.success);
    } else if (i.familyId == DS2406 || i.familyId == DS2413) {
      snprintf(buff, sizeof(buff), "<li>%s (%s), name: \"%s\", pins: %d %d, errors: %d, success: %d</li>",
        i.idStr.c_str(),
        familyIdToNameTranslation(i.familyId).c_str(),
        i.name.c_str(),
        i.actuatorPinState[0],
        i.actuatorPinState[1],
        i.errors,
        i.success);
    } else if (i.familyId == DS2405) {
      snprintf(buff, sizeof(buff), "<li>%s (%s), name: \"%s\", pins: %d, errors: %d, success: %d</li>",
        i.idStr.c_str(),
        familyIdToNameTranslation(i.familyId).c_str(),
        i.name.c_str(),
        i.actuatorPinState[0],
        i.errors,
        i.success);
    } else if (i.familyId == DS2423) {
      snprintf(buff, sizeof(buff), "<li>%s (%s), name: \"%s\", counters: %d %d, errors: %d, success: %d</li>",
        i.idStr.c_str(),
        familyIdToNameTranslation(i.familyId).c_str(),
        i.name.c_str(),
        i.counters[0],
        i.counters[1],
        i.errors,
        i.success);
    } else {
      snprintf(buff, sizeof(buff), "<li>%s (%s)</li>", i.idStr.c_str(), familyIdToNameTranslation(i.familyId).c_str());
    }

    oneWireList += String(buff);
  }
  oneWireList += "</ul>";
  html.replace("%ONE_WIRE_DEVICES%" , oneWireList);

  webserver.send(200, "text/html", html);
}

void handle_ping() {
  webserver.send(200, "text/plain", "pong");
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2, false, 500);  // 500ms timeout to DS2480
  Serial.println("Setup serial ports done.");

  tft.init();
  tft.setRotation(3);
  clearScreen();
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 0);
  tft.setTextDatum(MC_DATUM);

  /*if (TFT_BL > 0) {  // TFT_BL has been set in the TFT_eSPI library in the
  User
  // Setup file TTGO_T_Display.h
    pinMode(TFT_BL, OUTPUT);                 // Set backlight pin to output
  mode digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);  // Turn backlight on.
    // TFT_BACKLIGHT_ON has
    // been set in the TFT_eSPI library in the
    // User Setup file TTGO_T_Display.h
  }*/
  tft.println("LCD initialized.");
  Serial.println("LCD initialized.");

  tft.println("Device name: " + appName);
  Serial.println("Device name: " + appName);

  preferences.begin("tiny-owc", false);
  loadSettings();
  tft.println("Settings loaded.");
  Serial.println("Settings loaded.");

  tft.println("DS2480B initializing...");
  Serial.println("DS2480B initializing...");

  ds.begin();

  tft.println("DS2480B initialized.");
  Serial.println("DS2480B initialized.");
  
  if (!SPIFFS.begin(true)) {
    tft.println("An Error has occurred while mounting SPIFFS.");
    Serial.println("An Error has occurred while mounting SPIFFS.");
    delay(5000);
    ESP.restart();
  }

  Serial.println("SPIFFS initialized.");

  // load web pages from SPIFFS, reboot if we fail.
  if (!loadAux(AUX_MQTTSETTING) || !loadAux(AUX_MQTTSAVE)) {
    tft.println("Error loading webpages from SPIFFS.");
    Serial.println("An Error has occurred while loading webpages from SPIFFS.");
    delay(5000);
    ESP.restart();
  }

  tft.println("Pages loaded from SPIFFS.");
  Serial.println("Web pages loaded from SPIFFS.");

  AutoConnectAux *settings = portal.aux(AUX_MQTTSETTING);
  PageArgument args;
  AutoConnectAux &mqtt_setting = *settings;
  loadParams(mqtt_setting, args);
  AutoConnectInput &mqttserverElm = mqtt_setting["mqttserver"].as<AutoConnectInput>();
  AutoConnectInput &mqttserver_portElm = mqtt_setting["mqttserver_port"].as<AutoConnectInput>();
  AutoConnectInput &mqtt_topicElm = mqtt_setting["mqtt_base_topic"].as<AutoConnectInput>();
  AutoConnectInput &mqtt_cmdtopicElm = mqtt_setting["mqtt_base_cmdtopic"].as<AutoConnectInput>();

  if (mqttserverElm.value.length()) {
    mqttserver = mqttserverElm.value;    
    ESP_LOGI(TAG, "mqttserver set to '%s'", mqttserver.c_str());
  }
  if (mqttserver_portElm.value.length()) {
    mqttserver_port = mqttserver_portElm.value;
    ESP_LOGI(TAG, "mqttserver_port set to '%d'", mqttserver_port.toInt());
  }
  if (mqtt_topicElm.value.length()) {
    mqtt_base_topic = mqtt_topicElm.value;
    mqtt_topic = mqtt_base_topic + "/" + uniqueId;
    ESP_LOGI(TAG, "mqtt_topic set to '%s'", mqtt_topic.c_str());
  }
  if (mqtt_cmdtopicElm.value.length()) {
    mqtt_base_cmdtopic = mqtt_cmdtopicElm.value;
    mqtt_cmdtopic = mqtt_base_cmdtopic + "/" + uniqueId;
    ESP_LOGI(TAG, "mqtt_cmdtopic set to '%s'", mqtt_cmdtopic.c_str());
  }

  portal.on(AUX_MQTTSETTING, loadParams);
  portal.on(AUX_MQTTSAVE, saveParams);

  if (mqttserver.length() > 0 && mqttserver_port.length() > 0) {
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onSubscribe(onMqttSubscribe);
    mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setClientId(appName.c_str());
    mqttClient.setMaxTopicLength(256);
    mqttClient.setWill(mqtt_topic.c_str(), 2, true, "DISCONNECTED");
    mqttClient.setServer(mqttserver.c_str(), mqttserver_port.toInt());
    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
    WiFi.onEvent(WiFiEvent);
    tft.println("MQTT support loaded.");
  }

  AutoConnectConfig config;
  config.apid = appName;
  config.title = appName;
  config.bootUri = AC_ONBOOTURI_HOME; // add menuitem for OTA update
  config.homeUri = "/";
  config.portalTimeout = 45000; // continue in offline mode after 45 seconds, if WiFi connection not available
  config.retainPortal = true;   // continue the portal function even if the captive portal timed out
  config.psk = "12345678";      // default password
  config.ota = AC_OTA_BUILTIN; 
  portal.config(config);

  webserver.on("/", handle_indexHtml);
  webserver.on("/ping", handle_ping);
  portal.onDetect(startedCapturePortal);
  
  WiFi.setSleep(false); // https://github.com/espressif/arduino-esp32/issues/1484

  portalConnected = portal.begin();

  if (portalConnected) {
    tft.println("WiFi connected: " + WiFi.localIP().toString());
    Serial.println("WiFi connected: " + WiFi.localIP().toString());

    if (MDNS.begin(appName.c_str())) {
      MDNS.addService("http", "tcp", 80);
    }
  } else {
    tft.println("Portal started, IP:" + WiFi.localIP().toString() + "/setup");
    Serial.println("Capture portal started, IP:" + WiFi.localIP().toString() + "/setup");
  }

  delay(3000);

  clearScreen();
  tft.setTextSize(3);
  tft.drawString("Tiny-OWC", tft.width() / 2, tft.height() / 2);
  tft.setTextSize(1);
  tft.setCursor(0, tft.height() - tft.fontHeight());
  snprintf(buff, sizeof(buff), "Build: %s %s", __DATE__, __TIME__);
  tft.println(buff);
  Serial.println(buff);
  
  delay(2000);

  firstButton.setClickHandler(firstButtonClick);
  firstButton.setLongClickHandler(firstButtonClick);
  secondButton.setClickHandler(secondButtonClick);
  secondButton.setLongClickHandler(secondButtonClick);
  secondButton.setTripleClickHandler(secondButtonTrippleClick);

  printOneWireNodes();

  // Set DS2408 to a known state (testmode=off,strobe out,all relayes off).
  for (auto &node : oneWireNodes) {
    if (node.familyId == DS2408) {
      ds2408_reset(ds, node);
    }
  }

  Serial.println("Setup() done.");
}

void pushStateToMQTT(onewireNode& node) {
  // USE this if modifying JSON-message, https://arduinojson.org/v6/assistant/
  StaticJsonDocument<350> jsonNode;
  auto time = getEpocTime();

  ESP_LOGD(TAG, "Pushing state to MQTT broker, node: %s.", node.idStr.c_str());

  jsonNode["id"] = node.idStr;
  jsonNode["name"] = node.name;
  jsonNode["time"] = time;
  jsonNode["errors"] = node.errors;
  jsonNode["success"] = node.success;

  if (isTemperatureSensor(node.familyId)) {
    jsonNode["temp"] = ((int)(node.temperature * 100)) / 100.0; // round to two decimals
    jsonNode["lowLimit"] = node.lowLimit;
    jsonNode["highLimit"] = node.highLimit;
    jsonNode["status"] = node.status;
    jsonNode["actuatorId"] = idToString(node.actuatorId);
    jsonNode["actuatorPin"] = node.actuatorPin;
  } else if (node.familyId == DS2408) {
    auto pinStateArray = jsonNode.createNestedArray("pinState");
    for (auto i : node.actuatorPinState) {
      pinStateArray.add(i);
    }
  } else if (node.familyId == DS2406 || node.familyId == DS2413) {
    auto pinStateArray = jsonNode.createNestedArray("pinState");
    pinStateArray.add(pinStateArray[0]);
    pinStateArray.add(pinStateArray[1]);
  } else if (node.familyId == DS2405) {
    auto pinStateArray = jsonNode.createNestedArray("pinState");
    pinStateArray.add(pinStateArray[0]);
  } else if (node.familyId == DS2423) {
    auto countersArray = jsonNode.createNestedArray("counters");
    for (auto i : node.counters) {
      countersArray.add(i);
    }
  }

  String jsonString;
  serializeJson(jsonNode, jsonString);
  auto nodeTopic = mqtt_topic + "/" + node.idStr;
  if (mqttClient.publish(nodeTopic.c_str(), 1, true, jsonString.c_str()) > 0) {
    // if success...
    node.millisWhenLastPush = millis();
  }
}

void pushAllStateToMQTT() {
  for (auto &node : oneWireNodes) {
    pushStateToMQTT(node);
  }
}

// Main work of Tiny-OWC done here.
void actOnSensors() {
  auto currentTime = millis();

  if (oneWireNodes.size() > 0 && lastReadingTime + SAMPLE_DELAY < currentTime) {

    if (!conversionStarted) {
      startSimultaneousConversion(ds);
      conversionStarted = esp_timer_get_time();
    } else {
      // check if 1 second has elapsed since conversion started, if true then we can read temperature from all temperature sensors.
      if (conversionStarted + 1000000 < esp_timer_get_time()) {
        conversionStarted = 0;

        for (auto &node : oneWireNodes) {
          if (isTemperatureSensor(node.familyId)) {
            auto reading = readConversion(ds, node);
            auto currentMillis = millis();

            if (reading != UNSET_TEMPERATURE) {
              node.failedReadingsInRow = 0;
              auto temperature = rawToCelsius(reading);

              // filter some noise by only including changes larger than 0.5 degrees.
              ESP_LOGD(TAG, "Temp reading: raw %d, temp %.2f, last %.2f", reading, temperature, node.lastTemperature);

              // Only record changes if temperature are greater than hysteresis, we don't want too frequent changes.
              // 85 we don't need to measure this high temperatures, 85 is also the power-on temperature of the sensor.
              if (abs(temperature - node.temperature) > TEMPERATURE_HYSTERESIS && temperature < 85) {
                node.lastTemperature = node.temperature == UNSET_TEMPERATURE ? temperature : node.temperature;
                node.temperature = temperature;
                // If sensor has lowlimit, highlimit and a DS2408 pin to control, then control pin output according to temperature.
                if (node.actuatorPin > -1 && node.lowLimit > UNSET_TEMPERATURE && node.highLimit > UNSET_TEMPERATURE) {
                  if (node.temperature < node.lowLimit || node.temperature > node.highLimit) {  
                    auto actuatorNode = getOneWireNode(node.actuatorId);
                    if (actuatorNode != nullptr) {
                      node.status = node.temperature < node.lowLimit;

                      uint8_t actuatorState = 0;
                      for (auto i = 0; i < 8; i++) {
                        bitWrite(actuatorState, i, actuatorNode->actuatorPinState[i]);
                      }
                      auto oldActuatorState = actuatorState;

                      bitWrite(actuatorState, node.actuatorPin, !node.status);  // invert bit since 1 means relay off.
                      actuatorState = setState(ds, *actuatorNode, actuatorState);
                      // if we managed to set state.
                      if (actuatorState > -1) {
                        actuatorNode->actuatorPinState[node.actuatorPin] = !node.status;  // Low means On (reversed logic)

                        ESP_LOGI(TAG, "Adjusted actuator state, old value: %s, new value: %s.", String(oldActuatorState, BIN), String(actuatorState, BIN));
                        pushStateToMQTT(*actuatorNode);
                      }
                    }
                  }
                }
                
                pushStateToMQTT(node);           
              }
            } else {
              node.failedReadingsInRow++;
            }
            // make a force push even if nothing has changed, if changes are too infrequent.
            if (node.millisWhenLastPush + FORCE_MQTT_PUSH > currentMillis) {
              pushStateToMQTT(node);
            }
          } else if (node.familyId == DS2408) {

            pushStateToMQTT(node);
          } else if (node.familyId == DS2406 || node.familyId == DS2413) {
            // TODO
          } else if (node.familyId == DS2405) {
            // TODO
          } else if (node.familyId == DS2423) {
            auto a = getCounter(ds, node, 0);
            auto b = getCounter(ds, node, 1);

            if (a >= 0) {
              node.counters[0] = a;
            }

            if (b >= 0) {
              node.counters[1] = b;
            }
            
            if (a >= 0 || b >= 0) {
              pushStateToMQTT(node);
            }
          }
        }

        numberOfSamplesSinceReboot++;

        printOneWireNodes();
        
        lastReadingTime = currentTime;
      }
    }
  }
}

/*
* Just display a spinning indicator on LCD to show that the application is running its main loop.
*/
void printLoopProgress() {
  if ((millis() % 400) == 0) {  
    tft.setTextSize(2);
    tft.setTextColor(TFT_BLACK);
    tft.drawString(PROGRESS_INDICATOR[progressIndicator], tft.width() - tft.fontHeight(), tft.height() - tft.fontHeight()); // clear the old one.
    
    progressIndicator = (progressIndicator + 1) % (sizeof(PROGRESS_INDICATOR) / sizeof(PROGRESS_INDICATOR[0]));

    tft.setTextColor(TFT_GREEN);
    tft.drawString(PROGRESS_INDICATOR[progressIndicator], tft.width() - tft.fontHeight(), tft.height() - tft.fontHeight());
    tft.setTextColor(TFT_WHITE);
  }
}

void loop() {
  firstButton.loop();
  secondButton.loop();

  if (portalConnected) {
    portal.handleClient();
  }

  if (state == START_SCANNING) {
    scanOneWireNetwork();
  } else if (scannedOneWireNodes.size() > 0) {
    state = SCANNING_DONE;
  } else if (oneWireNodes.size() == 0) {
    state = NO_DEVICES;
  } else {
    state = OPERATIONAL;
    actOnSensors();
    printLoopProgress();
  }

  printState();

  esp_task_wdt_reset(); // reset watchdog to show that we are still alive.
}
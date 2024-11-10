/*
 * FSWebServer - Example WebServer with SPIFFS backend for esp8266
 */
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Ticker.h>
#include <StreamString.h>
#include "RTClib.h"
#include <ESP32Time.h>
#include <time.h>
#include "driver/uart.h"
#include "src/oi_can.h"
#include "src/config.h"

#define DBG_OUTPUT_PORT Serial
#define INVERTER_PORT UART_NUM_1
#define INVERTER_RX 16
#define INVERTER_TX 17
#define UART_TIMEOUT (100 / portTICK_PERIOD_MS)
#define UART_MESSBUF_SIZE 100
#ifndef LED_BUILTIN
#define LED_BUILTIN  8
#endif

const char* host = "inverter";
bool fastUart = false;
bool fastUartAvailable = true;
char uartMessBuff[UART_MESSBUF_SIZE];

WebServer server(80);
HTTPUpdateServer updater;
Ticker sta_tick;

RTC_PCF8523 ext_rtc;
ESP32Time int_rtc;
bool haveRTC = false;
Config config;

String getContentType(String filename) {
    if (server.hasArg("download")) return "application/octet-stream";
    else if (filename.endsWith(".htm")) return "text/html";
    else if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".xml")) return "text/xml";
    else if (filename.endsWith(".pdf")) return "application/x-pdf";
    else if (filename.endsWith(".zip")) return "application/x-zip";
    else if (filename.endsWith(".gz")) return "application/x-gzip";
    return "text/plain";
}

bool handleFileRead(String path) {
    if (path.endsWith("/")) path += "index.html";
    String contentType = getContentType(path);
    String pathWithGz = path + ".gz";
    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
        if (SPIFFS.exists(pathWithGz)) path += ".gz";
        File file = SPIFFS.open(path, "r");
        server.sendHeader("Cache-Control", "max-age=86400");
        server.streamFile(file, contentType);
        file.close();
        return true;
    }
    return false;
}

void handleRTCNow() {
    String output = "{ \"now\":\"";
    if (haveRTC) {
        DateTime t = ext_rtc.now();
        output += t.timestamp();
    } else {
        output += "NO RTC";
    }
    output += "\"}";
    server.send(200, "text/json", output);
}

void handleRTCSet() {
    if (server.hasArg("timestamp")) {
        String timestamp = server.arg("timestamp");
        server.send(200, "text/json", "{\"result\":\"" + timestamp + "\"}");
        DateTime now = DateTime(timestamp.toInt());
        ext_rtc.adjust(now);
        int_rtc.setTime(now.unixtime());
        handleRTCNow();
    } else {
        server.send(500, "text/json", "{\"result\":\"timestamp missing\"}");
    }
}

void setup(void) {

    DBG_OUTPUT_PORT.begin(115200);

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };
    //uart_param_config(INVERTER_PORT, &uart_config);
    //uart_set_pin(INVERTER_PORT, INVERTER_TX, INVERTER_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    //uart_driver_install(INVERTER_PORT, UART_MESSBUF_SIZE * 3, 0, 0, NULL, 0);
    delay(100);

    pinMode(LED_BUILTIN, OUTPUT);

    if (ext_rtc.begin()) {
        haveRTC = true;
        DBG_OUTPUT_PORT.println("External RTC found");
        if (!ext_rtc.initialized() || ext_rtc.lostPower()) {
            DBG_OUTPUT_PORT.println("RTC is NOT initialized, setting to build time");
            ext_rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
        ext_rtc.start();
        DateTime now = ext_rtc.now();
        int_rtc.setTime(now.unixtime());
    } else {
        DBG_OUTPUT_PORT.println("No RTC found");
    }

     Serial.println("Mounting SPIFFS...");
    if (SPIFFS.begin()) {
        Serial.println("SPIFFS mounted successfully");
    } else {
        Serial.println("Failed to mount SPIFFS");
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.begin();
    sta_tick.attach(10, []() {
        if (!(uint32_t)WiFi.localIP()) {
            WiFi.mode(WIFI_AP);
        }
    });

    MDNS.begin(host);

    config.load();

    if (config.getCanEnablePin() > 0) {
        pinMode(config.getCanEnablePin(), OUTPUT);
        digitalWrite(config.getCanEnablePin(), LOW);
    }

    OICan::Init(1, OICan::Baud500k, config.getCanTXPin(), config.getCanRXPin());

    updater.setup(&server);

    server.on("/rtc/now", HTTP_GET, handleRTCNow);
    server.on("/rtc/set", HTTP_POST, handleRTCSet);

    server.onNotFound([]() {
        if (!handleFileRead(server.uri())) {
            server.send(404, "text/plain", "FileNotFound");
        }
    });

    server.begin();
    MDNS.addService("http", "tcp", 80);
}

void loop(void) {
    server.handleClient();
    ArduinoOTA.handle();
    OICan::Loop();
}

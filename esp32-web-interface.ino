/*
  FSWebServer - Example WebServer with SPIFFS backend for esp8266
  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the ESP8266WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  upload the contents of the data folder with MkSPIFFS Tool ("ESP8266 Sketch Data Upload" in Tools menu in Arduino IDE)
  or you can upload the contents of a folder if you CD in that folder and run the following command:
  for file in `ls -A1`; do curl -F "file=@$PWD/$file" esp8266fs.local/edit; done

  access the sample web page at http://esp8266fs.local
  edit the page by going to http://esp8266fs.local/edit
*/
/*
 * This file is part of the esp32 web interface
 *
 * Copyright (C) 2023 Johannes Huebner <dev@johanneshuebner.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
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

#include <SPIFFS.h>
#include <time.h>
#include "src/oi_can.h"
#include "src/clara_uart.h"
#include "src/config.h"

#define DBG_OUTPUT_PORT Serial
#ifndef LED_BUILTIN
#define LED_BUILTIN  8
#endif


const char* host = "inverter";
bool fastUart = false;
//DynamicJsonDocument jsonDoc(30000);

WebServer server(80);
HTTPUpdateServer updater;
//holds the current upload
File fsUploadFile;
Ticker sta_tick;

Config config;

// Буфер під JSON терміналу: 128 рядків по 80 символів плюс лапки й коми.
static char termBuf[12288];



//format bytes
String formatBytes(uint64_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".bin")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".svg")) return "image/svg+xml";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  //DBG_OUTPUT_PORT.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    // SPIFFS не віддає ні ETag, ні Last-Modified, тож "max-age=86400" на всьому
    // означало, що після ./upload.sh браузер ще добу крутить стару веб-морду —
    // свіжий index.html поруч зі старим ui.js виглядає саме як "сторінка не працює".
    // Незмінні сторонні блоби лишаємо в кеші, свої файли завжди перепитуємо.
    bool cacheable = path.endsWith(".gz") || path.endsWith(".png") || path.endsWith(".gif");
    server.sendHeader("Cache-Control", cacheable ? "max-age=86400" : "no-cache");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    //DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    //DBG_OUTPUT_PORT.print("handleFileUpload Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  //DBG_OUTPUT_PORT.println("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}


static void handleCommand() {
  if(!server.hasArg("cmd")) {server.send(500, "text/plain", "BAD ARGS"); return;}

  String cmd = server.arg("cmd");

  digitalWrite(LED_BUILTIN, HIGH);

  if (cmd == "json") {
    if (!OICan::SendJson(server.client()))
      server.send(500, "text/plain", "CAN communication error");
  }
  else if (cmd.startsWith("set")) {
    String str(cmd);
    int nameStart = str.indexOf(' ');
    int valueStart = str.lastIndexOf(' ');
    String name = str.substring(nameStart, valueStart);
    double value = str.substring(valueStart, str.indexOf('\r')).toDouble();
    name.trim();

    switch (OICan::SetValue(name, value)) {
      case OICan::Ok:
        server.send(200, "text/plain", "Set Ok");
        break;
      case OICan::UnknownIndex:
        server.send(200, "text/plain", "Unknown Parameter");
        break;
      case OICan::ValueOutOfRange:
        server.send(200, "text/plain", "Value out of range");
        break;
      case OICan::CommError:
        server.send(200, "text/plain", "CAN communication error");
        break;
    }
  }
  else if (cmd.startsWith("stream")) {
    String str(cmd);
    int samplesStart = str.indexOf(' ');
    int namesStart = str.lastIndexOf(' ');
    int samples = str.substring(samplesStart, namesStart).toInt();

    String names = str.substring(namesStart, str.indexOf('\r'));
    String result = OICan::StreamValues(names, samples);

    server.send(200, "text/plain", result);
  }
  else if (cmd == "save") {
    if (OICan::SaveToFlash()) {
      server.send(200, "text/plain", "Parameters and CAN map saved");
    }
    else {
      server.send(200, "text/plain", "No reply to save command");
    }
  }

  digitalWrite(LED_BUILTIN, LOW);
}

static void handleCanMap() {
  digitalWrite(LED_BUILTIN, HIGH);
  OICan::SetResult res = OICan::Ok;

  if (server.hasArg("add")) {
    res = OICan::AddCanMapping(server.arg("add"));
  }
  else if (server.hasArg("remove")) {
    res = OICan::RemoveCanMapping(server.arg("remove"));
  }
  else if (server.hasArg("edit")) {
    res = OICan::RemoveCanMapping(server.arg("edit"));
    if (res == OICan::Ok)
      res = OICan::AddCanMapping(server.arg("edit"));
  }

  if (res == OICan::Ok)
    OICan::SendCanMapping(server.client());
  else if (res == OICan::CommError)
    server.send(500, "text/plain", "CAN communication error");
  else if (res == OICan::UnknownIndex)
    server.send(500, "text/plain", "Invalid request");
  digitalWrite(LED_BUILTIN, LOW);
}

static void handleUpdate()
{
  static int pages = 0;
  if(!server.hasArg("step") || !server.hasArg("file")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  int step = server.arg("step").toInt();
  String message;
  digitalWrite(LED_BUILTIN, HIGH);

  if (step < 0)
    pages = OICan::StartUpdate(server.arg("file"));
  else {
    while (OICan::GetCurrentUpdatePage() < step) {
      OICan::Loop();
    }
  }

  server.send(200, "text/json", "{ \"message\": \"" + message + "\", \"pages\": " + pages + " }");
  digitalWrite(LED_BUILTIN, LOW);
}

static void handleNodeId()
{
  if(server.hasArg("id") && server.hasArg("canspeed")) {
    int id = server.arg("id").toInt();
    int speed = server.arg("canspeed").toInt();
    OICan::BaudRate baud = speed == 0 ? OICan::Baud125k : (speed == 1 ? OICan::Baud250k : OICan::Baud500k);
    OICan::Init(id, baud, config.getCanTXPin(), config.getCanRXPin());
  }
  else if(server.hasArg("id")) {
    int id = server.arg("id").toInt();
    OICan::Init(id, OICan::Baud500k, config.getCanTXPin(), config.getCanRXPin());
  }

  server.send(200, "text/plain", String(OICan::GetNodeId()) + "," + String(OICan::GetBaudRate()));
}

static void handleSettings()
{
  bool updated = true;
  // Раніше друга умова дублювала canRXPin, тож форма без canTXPin проходила далі
  // й atoi("") клав туди 0.
  if(server.hasArg("canRXPin") && server.hasArg("canTXPin") && server.hasArg("canEnablePin"))
  {
    int rx = atoi(server.arg("canRXPin").c_str());
    int tx = atoi(server.arg("canTXPin").c_str());
    int en = atoi(server.arg("canEnablePin").c_str());

    if (!Config::isValidPin(rx) || !Config::isValidPin(tx) ||
        (en != 0 && !Config::isValidPin(en)))
    {
      server.send(400, "text/plain", "Invalid GPIO for ESP32-C3 (allowed: 0-10, 20, 21)");
      return;
    }

    config.setCanRXPin(rx);
    config.setCanTXPin(tx);
    config.setCanEnablePin(en);

    if (server.hasArg("claraBaud"))
    {
      int cbaud = atoi(server.arg("claraBaud").c_str());
      int crx = atoi(server.arg("claraRXPin").c_str());
      // Порожнє поле TX = слухаємо Клару, але нічого не шлемо.
      int ctx = server.arg("claraTXPin").length() ? atoi(server.arg("claraTXPin").c_str()) : -1;

      if (cbaud != 0 && (!Config::isValidPin(crx) || (ctx >= 0 && !Config::isValidPin(ctx))))
      {
        server.send(400, "text/plain", "Invalid GPIO for ESP32-C3 (allowed: 0-10, 20, 21)");
        return;
      }
      config.setClaraRXPin(crx);
      config.setClaraTXPin(ctx);
      config.setClaraBaud(cbaud);
      ClaraUart::Init(crx, ctx, cbaud);
    }

    config.saveSettings();
    OICan::Init(OICan::GetNodeId(), OICan::GetBaudRate(), config.getCanTXPin(), config.getCanRXPin());


    if (config.getCanEnablePin() > 0) {
      pinMode(config.getCanEnablePin(), OUTPUT);
      digitalWrite(config.getCanEnablePin(), LOW);
    }
  }
  else
  {
    File file = SPIFFS.open("/settings.html", "r");
    String html = file.readString();
    file.close();
    html.replace("%canRXPin%", String(config.getCanRXPin()).c_str());
    html.replace("%canTXPin%", String(config.getCanTXPin()).c_str());
    html.replace("%canEnablePin%", String(config.getCanEnablePin()).c_str());
    html.replace("%claraRXPin%", String(config.getClaraRXPin()).c_str());
    html.replace("%claraTXPin%", config.getClaraTXPin() < 0 ? "" : String(config.getClaraTXPin()).c_str());
    html.replace("%claraBaud%", String(config.getClaraBaud()).c_str());

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html", html);
    updated = false;
  }

  if (updated)
  {
    File file = SPIFFS.open("/settings-updated.html", "r");
    server.streamFile(file, getContentType("settings-updated.html"));
    file.close();
  }
}


/** @brief віддає рядки з Клари, новіші за since. Стан тримає клієнт, не сервер. */
static void handleTermGet()
{
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), NULL, 10) : 0;
  size_t len = ClaraUart::Json(since, termBuf, sizeof(termBuf));
  // Через setContentLength/sendContent, щоб не копіювати 12 КБ у String.
  server.setContentLength(len);
  server.send(200, "application/json", "");
  server.sendContent(termBuf, len);
}

/** @brief шле рядок у термінал Клари; cmd=clear чистить буфер локально. */
static void handleTermPost()
{
  if (server.hasArg("clear")) { ClaraUart::Clear(); server.send(200, "text/plain", "OK"); return; }
  if (!server.hasArg("cmd")) { server.send(400, "text/plain", "BAD ARGS"); return; }

  if (config.getClaraTXPin() < 0 || !ClaraUart::IsRunning())
  {
    server.send(409, "text/plain", "TX line not configured");
    return;
  }

  ClaraUart::Send(server.arg("cmd").c_str());
  server.send(200, "text/plain", "OK");
}


static void handleWifi()
{
  bool updated = true;
  if(server.hasArg("apSSID") && server.hasArg("apPW"))
  {
    String apSSID = server.arg("apSSID");
    String apPW = server.arg("apPW");

    // WPA2 вимагає щонайменше 8 символів. Коротший пароль softAP() відкидає, а
    // порожній піднімає точку доступу відкритою — на машині це не те, що хочеться
    // отримати випадково, тому не вгадуємо намір, а відмовляємо.
    if (apSSID.length() == 0)
    {
      server.send(400, "text/plain", "AP SSID must not be empty");
      return;
    }
    if (apPW.length() < 8)
    {
      server.send(400, "text/plain", "AP password must be at least 8 characters");
      return;
    }

    WiFi.softAP(apSSID.c_str(), apPW.c_str());
  }
  else if(server.hasArg("staSSID") && server.hasArg("staPW"))
  {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(server.arg("staSSID").c_str(), server.arg("staPW").c_str());
  }
  else
  {
    File file = SPIFFS.open("/wifi.html", "r");
    String html = file.readString();
    file.close();
    html.replace("%staSSID%", WiFi.SSID());
    html.replace("%apSSID%", WiFi.softAPSSID());
    html.replace("%staIP%", WiFi.localIP().toString());
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html", html);
    updated = false;
  }

  if (updated)
  {
    File file = SPIFFS.open("/wifi-updated.html", "r");
    server.streamFile(file, getContentType("wifi-updated.html"));
    file.close();
  }
}


static void handleBaud()
{
  if (fastUart)
    server.send(200, "text/html", "fastUart on");
  else
    server.send(200, "text/html", "fastUart off");
}

void staCheck(){
  sta_tick.detach();
  if(!(uint32_t)WiFi.localIP()){
    WiFi.mode(WIFI_AP); //disable station mode
  }
}

void setup(void){
  DBG_OUTPUT_PORT.begin(115200);

  delay(100);

  pinMode(LED_BUILTIN, OUTPUT);

 

  //Start SPI Flash file system
  SPIFFS.begin();

  //WIFI INIT
  #ifdef WIFI_IS_OFF_AT_BOOT
    enableWiFiAtBootTime();
  #endif
  WiFi.mode(WIFI_AP_STA);
  //WiFi.setPhyMode(WIFI_PHY_MODE_11B);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);//25); //dbm
  WiFi.begin();
  sta_tick.attach(10, staCheck);

  MDNS.begin(host);

  config.load();

  if (config.getCanEnablePin() > 0) {
    pinMode(config.getCanEnablePin(), OUTPUT);
    digitalWrite(config.getCanEnablePin(), LOW);
  }

  OICan::Init(1, OICan::Baud500k, config.getCanTXPin(), config.getCanRXPin());

  ClaraUart::Init(config.getClaraRXPin(), config.getClaraTXPin(), config.getClaraBaud());

  updater.setup(&server);

  //SERVER INIT
  ArduinoOTA.setHostname(host);
  ArduinoOTA.begin();
  // Менеджер файлів прибрано з UI; /edit лишається, бо ним заливає upload.sh
  // і ним же ui.js пише/стирає subscription.js.
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

  server.on("/wifi", handleWifi);
  server.on("/cmd", handleCommand);
  server.on("/canmap", handleCanMap);
  server.on("/fwupdate", handleUpdate);
  server.on("/baud", handleBaud);
  server.on("/version", [](){ server.send(200, "text/plain", "1.1.R"); });
  server.on("/nodeid", handleNodeId);
  server.on("/settings", handleSettings);
  server.on("/api/term", HTTP_GET, handleTermGet);
  server.on("/api/term", HTTP_POST, handleTermPost);

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
    {
      server.sendHeader("Refresh", "6; url=/update");
      server.send(404, "text/plain", "FileNotFound");
    }
  });

  server.begin();

  MDNS.addService("http", "tcp", 80);
}

void loop(void){
  // note: ArduinoOTA.handle() calls MDNS.update();
  server.handleClient();
  ArduinoOTA.handle();

  OICan::Loop();
  ClaraUart::Loop();
}

#include "web_server.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "app_mode.h"
#include "can_bus.h"
#include "debug.h"
#include "net_manager.h"
#include "transport.h"
#include "config.h"

// =======================================================
// ================= FILE API (MERGED)
// =======================================================

static File uploadFile;

// ===== STREAM BUFFER (SHARED TCP + WS) =====
static uint8_t streamBuf[4096]; // bigger = better batching
static size_t streamLen = 0;
static uint32_t streamLastFlush = 0;

static void listRecursive(File dir, const String &base, AsyncResponseStream *res, bool &first)
{
    File file = dir.openNextFile();

    while (file)
    {
        String name = file.name();
        String path = base + "/" + name;

        if (file.isDirectory())
        {
            listRecursive(file, path, res, first);
        }
        else
        {
            if (!first)
                res->print(",");
            first = false;

            res->print("{\"type\":\"file\",\"name\":\"");
            res->print(name);
            res->print("\",\"path\":\"");
            res->print(path);
            res->print("\",\"size\":");
            res->print(file.size());
            res->print("}");
        }

        file = dir.openNextFile();
    }
}

// ===== LIST =====
static void handleList(AsyncWebServerRequest *req)
{
    AsyncResponseStream *res = req->beginResponseStream("application/json");
    res->print("[");

    bool first = true;
    File root = LittleFS.open("/");
    listRecursive(root, "", res, first);

    res->print("]");
    req->send(res);
}

// ===== LOAD =====
static void handleLoad(AsyncWebServerRequest *req)
{
    if (!req->hasParam("path"))
        return req->send(400, "text/plain", "Missing path");

    String path = req->getParam("path")->value();

    File f = LittleFS.open(path, "r");
    if (f && f.isDirectory())
    {
        f.close();
        return req->send(400, "text/plain", "Is directory");
    }
    if (f)
        f.close();

    if (!LittleFS.exists(path))
    {
        if (LittleFS.exists(path + ".gz"))
            path += ".gz";
        else
            return req->send(404, "text/plain", "Not found");
    }

    String contentType = "text/plain";
    if (path.endsWith(".html") || path.endsWith(".html.gz"))
        contentType = "text/html";
    else if (path.endsWith(".js") || path.endsWith(".js.gz"))
        contentType = "application/javascript";
    else if (path.endsWith(".css") || path.endsWith(".css.gz"))
        contentType = "text/css";
    else if (path.endsWith(".json") || path.endsWith(".json.gz"))
        contentType = "application/json";

    AsyncWebServerResponse *response = req->beginResponse(LittleFS, path, contentType);
    response->addHeader("Content-Disposition", "attachment; filename=\"" + String(path.substring(path.lastIndexOf("/") + 1)) + "\"");

    if (path.endsWith(".gz"))
        response->addHeader("Content-Encoding", "gzip");

    req->send(response);
}

// ===== SAVE =====
static void handleSave(AsyncWebServerRequest *req,
                       uint8_t *data,
                       size_t len,
                       size_t index,
                       size_t total)
{
    if (index == 0)
    {
        if (!req->hasHeader("X-Path"))
            return req->send(400, "text/plain", "Missing path");

        String path = req->getHeader("X-Path")->value();

        // 🔥 prevent overwrite ONLY for New (empty file creation)
        if (len == 1 && LittleFS.exists(path))
        {
            req->send(409, "text/plain", "File exists");
            return;
        }

        uploadFile = LittleFS.open(path, "w");

        if (!uploadFile)
            return req->send(500, "text/plain", "Open failed");
    }

    if (uploadFile)
        uploadFile.write(data, len);

    if (index + len == total)
    {
        if (uploadFile)
            uploadFile.close();
        req->send(200, "text/plain", "OK");
    }
}

// ===== DELETE =====
static void handleDelete(AsyncWebServerRequest *req)
{
    if (!req->hasParam("path", true))
        return req->send(400, "text/plain", "Missing path");

    String path = req->getParam("path", true)->value();

    if (!LittleFS.exists(path))
        return req->send(404, "text/plain", "Not found");

    LittleFS.remove(path);
    req->send(200, "text/plain", "OK");
}

// set CAN log mask, return current mask and enabled categories as JSON
static void handleCanLog(AsyncWebServerRequest *req)
{
    bool changed = false;
    String mode = "none";

    // ===== PRESET =====
    if (req->hasParam("preset"))
    {
        String preset = req->getParam("preset")->value();
        preset.toLowerCase();

        if (preset == "prod" || preset == "debug" ||
            preset == "verbose" || preset == "silent")
        {
            setCanLogPreset(preset.c_str());
            mode = "preset";
            changed = true;
        }
        else
        {
            req->send(400, "application/json",
                      "{\"error\":\"invalid preset\"}");
            return;
        }
    }

    // ===== CUSTOM SET =====
    else if (req->hasParam("set"))
    {
        String arg = req->getParam("set")->value();
        arg.toLowerCase();

        if (arg == "all")
            setCANLogMask(0xFFFFFFFF);
        else if (arg == "none")
            setCANLogMask(0);
        else
            setCANLogMask(parseCategories(arg));

        mode = "custom";
        changed = true;
    }

    // ===== RESPONSE =====
    String resp = "{";
    resp += "\"mode\":\"" + mode + "\",";
    resp += "\"mask\":" + String(settings.canLogMask) + ",";
    resp += "\"categories\":\"" + categoriesToString(settings.canLogMask) + "\"";
    resp += "}";

    req->send(200, "application/json", resp);
}

// ===== RENAME =====
static void handleRename(AsyncWebServerRequest *req)
{
    if (!req->hasParam("from", true) || !req->hasParam("to", true))
        return req->send(400, "text/plain", "Missing params");

    String from = req->getParam("from", true)->value();
    String to = req->getParam("to", true)->value();

    if (!LittleFS.exists(from))
        return req->send(404, "text/plain", "Not found");

    File src = LittleFS.open(from, "r");
    File dst = LittleFS.open(to, "w");

    uint8_t buf[512];
    while (src.available())
    {
        size_t n = src.read(buf, sizeof(buf));
        dst.write(buf, n);
    }

    src.close();
    dst.close();

    LittleFS.remove(from);
    req->send(200, "text/plain", "OK");
}

// =======================================================
// ================= WEB UI (MERGED)
// =======================================================

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// ===== RING BUFFER =====
#define WEB_BUF_SIZE 256

static CANRxItem webBuf[WEB_BUF_SIZE];
static volatile uint16_t webHead = 0;
static volatile uint16_t webTail = 0;

// ===== PUSH =====
void webPushFrame(const CANRxItem &item)
{
    uint16_t next = (webHead + 1) % WEB_BUF_SIZE;
    if (next == webTail)
        return;

    webBuf[webHead] = item;
    webHead = next;
}

// ===== STATUS JSON =====
static String getStatusJson()
{
    String s = "{";
    s += "\"mode\":" + String(appState.mode) + ",";
    s += "\"baud\":" + String(CANDriver::getCurrentBaud()) + ",";
    s += "\"running\":" + String(appState.running ? "true" : "false") + ",";
    s += "\"listen\":" + String(CANDriver::isListenOnly() ? "true" : "false");
    s += ",\"canlog\":" + String(settings.canLogMask);
    s += "}";
    return s;
}

// ===== WS EVENT =====
static void onWsEvent(AsyncWebSocket *server,
                      AsyncWebSocketClient *client,
                      AwsEventType type,
                      void *arg,
                      uint8_t *data,
                      size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        client->text("{\"msg\":\"connected\"}");
    }
}

// =======================================================
// ================= INIT
// =======================================================

void webInit()
{
    if (!LittleFS.begin(true))
    {
        DEBUG_PRINTLN("LittleFS mount failed");
        return;
    }

    // ===== STATIC =====
    server.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html");

    // ===== ACE EDITOR =====
    server.on("/edit", HTTP_GET, [](AsyncWebServerRequest *req)
              { req->send(LittleFS, "/editor/editor.html", "text/html"); });

    // ===== FILE API =====
    server.on("/list", HTTP_GET, handleList);
    server.on("/file", HTTP_GET, handleLoad);
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL, handleSave);
    server.on("/delete", HTTP_POST, handleDelete);
    server.on("/rename", HTTP_POST, handleRename);
    server.on("/canlog", HTTP_GET, handleCanLog);
    server.on("/canlog", HTTP_POST, [](AsyncWebServerRequest *req){},
        NULL,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t)
    {
        String body = String((char*)data).substring(0, len);

        if (body.indexOf("preset=") >= 0)
        {
            String preset = body.substring(body.indexOf("=") + 1);
            preset.trim();
            setCanLogPreset(preset.c_str());
        }
    });

    // ===== STATUS =====
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req)
              { req->send(200, "application/json", getStatusJson()); });

    // ===== MODE =====
    server.on("/mode", HTTP_POST, [](AsyncWebServerRequest *req)
              {
        if (req->hasParam("m", true))
            appState.mode = (Mode)req->getParam("m", true)->value().toInt();

        req->send(200, "text/plain", "OK"); });

    // ===== BAUD =====
    server.on("/baud", HTTP_POST, [](AsyncWebServerRequest *req)
              {
        if (req->hasParam("v", true))
        {
            uint32_t b = req->getParam("v", true)->value().toInt();
            CANDriver::reinit(b, CANDriver::isListenOnly());
        }

        req->send(200, "text/plain", "OK"); });

    // ===== WS =====
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.begin();

    streamInit();
}

void streamInit()
{
    streamLen = 0;
    streamLastFlush = millis();
}

void streamPush(const uint8_t *data, size_t len)
{
    if (ws.count() == 0)
    {
        // no websocket client → skip WS entirely
        return;
    }

    // if overflow → flush first
    if (streamLen + len > sizeof(streamBuf))
    {
        if (streamLen > 0)
        {
            transportWrite(streamBuf, streamLen);
            ws.binaryAll(streamBuf, streamLen);
            streamLen = 0;
        }
    }

    memcpy(streamBuf + streamLen, data, len);
    streamLen += len;
}

void streamFlush()
{
    if (streamLen > 0 && (millis() - streamLastFlush > 5))
    {
        // TCP
        transportWrite(streamBuf, streamLen);

        // WS (safe)
        for (auto &client : ws.getClients())
        {
            if (client.canSend())
            {
                client.binary(streamBuf, streamLen);
            }
        }

        streamLen = 0;
        streamLastFlush = millis();
    }
}
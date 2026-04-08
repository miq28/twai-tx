#include "web_ui.h"
#include <ESPAsyncWebServer.h>
#include "app_mode.h"
#include "can_bus.h"
#include <LittleFS.h>
#include "debug.h"
#include "file_api.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// ===== RATE LIMIT =====
static uint32_t lastPushMs = 0;
#define WS_INTERVAL_MS 10  // was 20
#define WS_BATCH_FRAMES 16 // NEW

#define WEB_BUF_SIZE 256

static CANRxItem webBuf[WEB_BUF_SIZE];
static volatile uint16_t webHead = 0;
static volatile uint16_t webTail = 0;

static uint32_t wsFramesSent = 0;
static uint32_t wsBytesSent = 0;
static uint32_t wsDrops = 0;

void handleFileList(AsyncWebServerRequest *request)
{
    if (!request->hasArg("dir"))
    {
        request->send(400, "text/plain", "BAD ARGS");
        return;
    }

    String path = request->arg("dir");
    DEBUG("handleFileList: %s\r\n", path.c_str());

    File root = LittleFS.open(path);
    if (!root || !root.isDirectory())
    {
        request->send(404, "text/plain", "NOT DIR");
        return;
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");

    response->print("[");

    bool first = true;
    File file = root.openNextFile();

    while (file)
    {
        if (!first)
            response->print(",");
        first = false;

        const char *name = file.name();
        size_t size = file.size();
        bool isDir = file.isDirectory();

        // ===== DEBUG =====
        DEBUG("FILE: %s | %s | %u bytes\r\n",
              name,
              isDir ? "DIR" : "FILE",
              (unsigned int)size);

        // ===== JSON =====
        response->print("{\"type\":\"");
        response->print(isDir ? "dir" : "file");
        response->print("\",\"name\":\"");
        response->print(name[0] == '/' ? name + 1 : name);
        response->print("\"");

        if (!isDir)
        {
            response->print(",\"size\":");
            response->print(size);
        }

        response->print("}");

        file = root.openNextFile();
    }

    response->print("]");

    request->send(response);

    DEBUG("handleFileList done\r\n");
}

void webPushFrame(const CANRxItem &item)
{
    uint16_t next = (webHead + 1) % WEB_BUF_SIZE;
    if (next == webTail)
        return;

    webBuf[webHead] = item;
    webHead = next;
}

static bool webPopFrame(CANRxItem &out)
{
    if (webTail == webHead)
        return false;

    out = webBuf[webTail];
    webTail = (webTail + 1) % WEB_BUF_SIZE;
    return true;
}

// ===== JSON =====
static String getStatusJson()
{
    String s = "{";
    s += "\"mode\":" + String(appState.mode) + ",";
    s += "\"baud\":" + String(CANDriver::getCurrentBaud()) + ",";
    s += "\"running\":" + String(appState.running ? "true" : "false") + ",";
    s += "\"listen\":" + String(CANDriver::isListenOnly() ? "true" : "false") + ",";
    s += "\"rx\":" + String(rxBufferCount());
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

// ===== INIT =====
void webInit()
{
    // LittleFS.begin();
    if (!LittleFS.begin(true))
    { // true = format if mount fails
        DEBUG_PRINTLN("❌ LittleFS Mount Failed");
        return;
    }
    DEBUG_PRINTLN("✅ LittleFS Mounted Successfully");

    // server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request)
    //           {
    //     DEBUG("%s\n", __PRETTY_FUNCTION__);
    //     handleFileList(request); });

    server.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html");

    fileApiInit(server); // <-- ADD THIS

    // ===== WS =====
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // ===== STATUS =====
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req)
              { req->send(200, "application/json", getStatusJson()); });

    // ===== MODE =====
    server.on("/mode", HTTP_POST, [](AsyncWebServerRequest *req)
              {
        if (req->hasParam("m", true))
        {
            appState.mode = (Mode)req->getParam("m", true)->value().toInt();
        }
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

    server.on("/ws_stats", HTTP_GET, [](AsyncWebServerRequest *req)
              {
        String s = "{";
        s += "\"frames\":" + String(wsFramesSent) + ",";
        s += "\"bytes\":"  + String(wsBytesSent)  + ",";
        s += "\"drops\":"  + String(wsDrops);
        s += "}";
        req->send(200, "application/json", s); });

    server.begin();
}

// ===== LOOP =====
void webLoop()
{
    ws.cleanupClients();

    uint32_t now = millis();
    if (now - lastPushMs < WS_INTERVAL_MS)
        return;

    lastPushMs = now;

    // ===== SAMPLE BUFFER =====
    CANRxItem item;

    uint8_t batch[13 * WS_BATCH_FRAMES];
    int offset = 0;
    int count = 0;

    // BUILD
    while (rxBufferPop(item) && count < WS_BATCH_FRAMES)
    {
        const auto &m = item.msg;

        uint8_t *pkt = batch + offset;

        uint32_t id = m.identifier;

        pkt[0] = id & 0xFF;
        pkt[1] = (id >> 8) & 0xFF;
        pkt[2] = (id >> 16) & 0xFF;
        pkt[3] = (id >> 24) & 0xFF;

        pkt[4] = m.data_length_code;

        for (int i = 0; i < 8; i++)
            pkt[5 + i] = (i < m.data_length_code) ? m.data[i] : 0;

        offset += 13;
        count++;
    }

    if (offset == 0)
        return;

    // SEND (RESTORED SIMPLE MODE)

    if (ws.count() == 0)
    {
        wsDrops++;
        return;
    }

    // broadcast like old working version
    ws.binaryAll((const char *)batch, offset);

    // stats
    wsFramesSent += count;
    wsBytesSent += offset;
}
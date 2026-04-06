#include "net_manager.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include "debug.h"
#include "command.h"
#include "transport.h"

// ===== CONFIG =====
#define WIFI_SSID "galaxi"
#define WIFI_PASS "n1n4iqb4l"
#define OTA_HOSTNAME "esp32"
#define OTA_PORT 3232

// ===== TCP =====
static AsyncServer server(23);
static AsyncClient* client = nullptr;

// external (from transport)
extern InputContext serialCtx;

// ===== OTA STATE =====
enum OTAState {
    OTA_OFF = 0,
    OTA_READY
};

static OTAState otaState = OTA_OFF;

// ===== OTA HANDLERS (once) =====
static void initOTAHandlers()
{
    static bool installed = false;
    if (installed) return;
    installed = true;

    ArduinoOTA
        .onStart([]()
        {
            const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
            DEBUG("OTA Start: %s\n", type);
        })
        .onEnd([]()
        {
            DEBUG("OTA End\n");
        })
        .onProgress([](unsigned int progress, unsigned int total)
        {
            DEBUG("OTA %u%%\n", (progress / (total / 100)));
        })
        .onError([](ota_error_t error)
        {
            DEBUG("OTA Error[%u]\n", error);
        });
}

// ===== OTA CONTROL =====
static void startOTA()
{
    if (otaState == OTA_READY) return;

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.begin();

    otaState = OTA_READY;
    DEBUG("OTA READY\n");
}

static void stopOTA()
{
    if (otaState == OTA_OFF) return;
    otaState = OTA_OFF;
    DEBUG("OTA STOPPED\n");
}

// ===== WIFI EVENTS =====
static void setupWiFiEvents()
{
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info)
    {
        DEBUG("WiFi disconnected: %d\n", info.wifi_sta_disconnected.reason);
        stopOTA();
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t)
    {
        DEBUG("WiFi GOT IP: %s\n", WiFi.localIP().toString().c_str());

        initOTAHandlers();
        startOTA();

    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

// ===== TCP SERVER =====
static void setupServer()
{
    server.onClient([](void*, AsyncClient* c)
    {
        if (client)
            client->close();

        client = c;
        client->setNoDelay(true);

        client->onDisconnect([](void*, AsyncClient* c)
        {
            if (client == c) client = nullptr;
        }, nullptr);

        client->onError([](void*, AsyncClient* c, int8_t)
        {
            if (client == c) client = nullptr;
        }, nullptr);

        client->onData([](void*, AsyncClient*, void* data, size_t len)
        {
            uint8_t* d = (uint8_t*)data;
            for (size_t i = 0; i < len; i++)
                transportDispatchByte(d[i]);
        }, nullptr);

    }, nullptr);

    server.begin();
}

// ===== PUBLIC =====
void netInit()
{
    setupWiFiEvents();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    setupServer();

    DEBUG("NET INIT DONE\n");
}

void netLoop()
{
    if (otaState == OTA_READY)
    {
        ArduinoOTA.handle();
    }
}

size_t netWrite(const uint8_t* data, size_t len)
{
    if (client && client->connected() && client->canSend())
    {
        return client->write((const char*)data, len);
    }
    return 0;
}

bool netClientConnected()
{
    return (client && client->connected());
}
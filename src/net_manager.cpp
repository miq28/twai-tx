#include "net_manager.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include "debug.h"
#include "command.h"
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include "transport.h"
#include "config.h"
#include <Preferences.h>
#include <NetBIOS.h>

// ===== CONFIG =====
#if defined(WEACT_STUDIO_CAN485_V1)
#define BASE_DEVICE_NAME "WEACT_CAN485"
#elif defined(WAVESHARE_ESP32_S3_RS485_CAN)
#define BASE_DEVICE_NAME "WAVESHARE_CAN485"
#else
#define BASE_DEVICE_NAME "ESP32"
#endif
#define WIFI_SSID "galaxi"
#define WIFI_PASS "n1n4iqb4l"
#define OTA_HOSTNAME "esp32"
#define OTA_PORT 3232
#define MDNS_NAME "esp32"
#define TELNET_PORT 23
#define OBD_PORT 35000

// ===== TCP =====
static AsyncServer server(23);
static AsyncClient *client = nullptr;

// ===== UDP =====
static WiFiUDP udp;
static IPAddress broadcastAddr(255, 255, 255, 255);
static uint32_t lastBroadcast = 0;
#define DISCOVERY_PORT 17222

// request magic (what you already broadcast)
static const uint8_t GVRET_MAGIC[4] = {0x1C, 0xEF, 0xAC, 0xED};

// GVRET device type
#define DEVICE_TYPE_WIFI 0x01

// container for device name
char deviceName[32];

// ===== OTA STATE =====
enum OTAState
{
    OTA_OFF = 0,
    OTA_READY
};

static OTAState otaState = OTA_OFF;

void buildDeviceName(char *out, size_t outSize, const char *baseName)
{
    uint64_t chipid = ESP.getEfuseMac();            // 48-bit MAC
    uint16_t shortId = (uint16_t)(chipid & 0xFFFF); // last 2 bytes

    // Format: BASE_XXXX
    snprintf(out, outSize, "%s_%04X", baseName, shortId);
}

// ===== OTA HANDLERS (once) =====
static void initOTAHandlers()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    ArduinoOTA
        .onStart([]()
                 {
            const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
            DEBUG("OTA Start: %s\n", type); })
        .onEnd([]()
               { DEBUG("OTA End\n"); })
        .onProgress([](unsigned int progress, unsigned int total)
                    { DEBUG("OTA %u%%\n", (progress / (total / 100))); })
        .onError([](ota_error_t error)
                 { DEBUG("OTA Error[%u]\n", error); });
}

// ===== OTA CONTROL =====
static void startOTA()
{
    if (otaState == OTA_READY)
        return;

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.begin();

    otaState = OTA_READY;
    DEBUG("OTA READY\n");
}

static void stopOTA()
{
    if (otaState == OTA_OFF)
        return;
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
        MDNS.end();
        udp.stop(); }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t)
                 {
                     DEBUG("WiFi GOT IP: %s\n", WiFi.localIP().toString().c_str());

                     // ===== OTA =====
                     initOTAHandlers();
                     startOTA();
                     NBNS.begin(deviceName);

                     // ===== MDNS =====
                     if (!MDNS.begin(MDNS_NAME))
                     {
                         DEBUG("Error setting up MDNS responder!\n");
                     }
                     else
                     {
                         MDNS.addService("gvretServer", "tcp", TELNET_PORT);
                         MDNS.addService("ELM327", "tcp", OBD_PORT);
                         DEBUG("MDNS started\n");
                     }

                     // ===== UDP LISTENER =====
                     udp.begin(DISCOVERY_PORT);
                     DEBUG("UDP listening on %d\n", DISCOVERY_PORT); },
                 ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

// ===== TCP SERVER =====
static void setupServer()
{
    server.onClient([](void *, AsyncClient *c)
                    {
                        if (client)
                            client->close();

                        client = c;
                        client->setNoDelay(true);

                        client->onDisconnect([](void *, AsyncClient *c)
                                             {
            if (client == c) client = nullptr; }, nullptr);

                        client->onError([](void *, AsyncClient *c, int8_t)
                                        {
            if (client == c) client = nullptr; }, nullptr);

                        client->onData([](void *, AsyncClient *, void *data, size_t len)
                                       {
            // uint8_t* d = (uint8_t*)data;
            // for (size_t i = 0; i < len; i++)
            //     transportDispatchByte(d[i]);
            transportDispatchBuffer((const uint8_t*)data, len); }, nullptr); },
                    nullptr);

    server.begin();
}

// ===== PUBLIC =====
void netInit()
{
    buildDeviceName(deviceName, sizeof(deviceName), BASE_DEVICE_NAME);

    Preferences prefs;
    prefs.begin(PREF_NAME, false);
    // prefs.clear();
    if (!prefs.isKey("wifiMode"))
        prefs.putUChar("wifiMode", 2);
    if (!prefs.isKey("AP_SSID"))
        prefs.putString("AP_SSID", deviceName);
    if (!prefs.isKey("AP_PASS"))
        prefs.putString("AP_PASS", "12345678");
    if (!prefs.isKey("STA_SSID"))
        prefs.putString("STA_SSID", "galaxi");
    if (!prefs.isKey("STA_PASS"))
        prefs.putString("STA_PASS", "n1n4iqb4l");

    settings.wifiMode = prefs.getUChar("wifiMode");
    prefs.getString("AP_SSID", settings.AP_SSID, sizeof(settings.AP_SSID));
    prefs.getString("AP_PASS", settings.AP_PASS, sizeof(settings.AP_PASS));
    prefs.getString("STA_SSID", settings.STA_SSID, sizeof(settings.STA_SSID));
    prefs.getString("STA_PASS", settings.STA_PASS, sizeof(settings.STA_PASS));

    prefs.end();

    // settings.wifiMode = 2;
    DEBUG("WIFI MODE: %d\n", settings.wifiMode);

    setupWiFiEvents();

    if (settings.wifiMode == 1)
    {
        DEBUG_PRINTLN("Wifi mode: STA");
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(settings.STA_SSID, settings.STA_PASS);
    }
    else if (settings.wifiMode == 2)
    {
        DEBUG_PRINTLN("Wifi mode: AP");
        WiFi.mode(WIFI_AP);
        WiFi.setSleep(false);
        WiFi.softAP((const char *)settings.AP_SSID, (const char *)settings.AP_PASS);
    }
    else
        // Turn Wi-Fi off
        WiFi.mode(WIFI_OFF);

    setupServer();

    DEBUG("NET INIT DONE\n");
}

void netLoop()
{
    // ===== OTA =====
    if (otaState == OTA_READY)
    {
        ArduinoOTA.handle();
    }

    // ===== UDP BROADCAST (announce) =====
    if (WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP)
    {
        uint32_t now = micros();

        if ((now - lastBroadcast) > 1000000ul)
        {
            lastBroadcast = now;

            udp.beginPacket(broadcastAddr, DISCOVERY_PORT);
            udp.write(GVRET_MAGIC, 4);
            udp.endPacket();
        }
    }
}

size_t netWrite(const uint8_t *data, size_t len)
{
    if (client && client->connected())
    {
        return client->write((const char *)data, len);
    }
    return 0;
}

bool netClientConnected()
{
    return (client && client->connected());
}
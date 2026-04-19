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
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

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
#define OTA_PORT 3232
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

static void setHostnameEarly(const char *name)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif)
    {
        esp_netif_set_hostname(netif, name);
    }
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

    ArduinoOTA.setHostname(deviceName);
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
                     DEBUG("*** WiFi GOT IP: %s ***\n", WiFi.localIP().toString().c_str());
                     DEBUG("*** HOST NAME: %s ***\n", WiFi.getHostname());

                     // ===== OTA =====
                     initOTAHandlers();
                     startOTA();

                     // ===== NetBIOS =====
                     if (!NBNS.begin(deviceName))
                     {
                         DEBUG("Error setting up NetBIOS!\n");
                     } else
                        DEBUG("NetBIOS started\n");

                     // ===== MDNS =====
                     if (!MDNS.begin(deviceName))
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

    setupWiFiEvents();

    if (settings.wifiMode == 1)
    {
        DEBUG_PRINTLN("Wifi mode: STA");

        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true, true); // erase + stop
        delay(200);

        // ===== INIT LOW LEVEL (REQUIRED FOR HOSTNAME) =====
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif)
        {
            esp_netif_set_hostname(netif, deviceName);
        }
        WiFi.setHostname(deviceName);
        WiFi.setSleep(false);
        WiFi.begin(settings.STA_SSID, settings.STA_PASS);
    }
    else if (settings.wifiMode == 2)
    {
        DEBUG_PRINTLN("Wifi mode: AP");

        WiFi.mode(WIFI_AP);
        WiFi.disconnect(true); // reset state
        delay(100);

        // set hostname on AP netif
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (netif)
        {
            esp_netif_set_hostname(netif, deviceName);
        }
        WiFi.setHostname(deviceName);
        WiFi.setSleep(false);
        WiFi.softAP(settings.AP_SSID, settings.AP_PASS);

        DEBUG("AP SSID: %s, PASS: %s\n", settings.AP_SSID, settings.AP_PASS);
    }
    else
    {
        DEBUG_PRINTLN("Wifi mode: OFF");
        WiFi.mode(WIFI_OFF);
    }

    setupServer();

    DEBUG("NET INIT DONE\n");
}

#pragma pack(push, 1)
struct GVRET_UDP
{
    uint8_t magic[4];
    uint8_t version;
    uint8_t devType;
    uint16_t port;
    uint8_t flags;
};
#pragma pack(pop)

void sendDiscovery()
{
    GVRET_UDP pkt;

    pkt.magic[0] = 0x1C;
    pkt.magic[1] = 0xEF;
    pkt.magic[2] = 0xAC;
    pkt.magic[3] = 0xED;

    pkt.version = 1;
    pkt.devType = 0x01;     // WIFI
    pkt.port = TELNET_PORT; // IMPORTANT: little endian (ESP is already LE)
    pkt.flags = 0;

    udp.beginPacket(broadcastAddr, DISCOVERY_PORT);
    udp.write((uint8_t *)&pkt, sizeof(pkt));
    udp.endPacket();

    udp.beginPacket(broadcastAddr, DISCOVERY_PORT + 1);
    udp.printf("name=%s", deviceName);
    udp.endPacket();
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

            // udp.beginPacket(broadcastAddr, DISCOVERY_PORT);
            // // udp.write(GVRET_MAGIC, 4);
            // udp.write((uint8_t*)deviceName, strlen(deviceName));
            // udp.endPacket();

            // String ip = WiFi.localIP().toString();
            // udp.beginPacket(broadcastAddr, DISCOVERY_PORT);
            // udp.write(GVRET_MAGIC, 4);
            // udp.print("|ip=");
            // udp.print(ip);
            // udp.print("|name=");
            // udp.print(deviceName);
            // udp.endPacket();

            // ===== SavvyCAN (DO NOT TOUCH FORMAT) =====
            udp.beginPacket(broadcastAddr, 17222);
            udp.write(GVRET_MAGIC, 4);
            udp.endPacket();

            // ===== Your discovery =====
            udp.beginPacket(broadcastAddr, 17223);
            udp.printf("name=%s;ip=%u.%u.%u.%u;port=%d",
                       deviceName,
                       WiFi.localIP()[0], WiFi.localIP()[1],
                       WiFi.localIP()[2], WiFi.localIP()[3],
                       TELNET_PORT);
            udp.endPacket();

            // sendDiscovery();
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
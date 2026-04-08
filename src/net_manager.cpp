#include "net_manager.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include "debug.h"
#include "command.h"
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include "transport.h"

// ===== CONFIG =====
#define WIFI_SSID "galaxi"
#define WIFI_PASS "n1n4iqb4l"
#define OTA_HOSTNAME "esp32"
#define OTA_PORT 3232
#define MDNS_NAME "esp32"
#define TELNET_PORT 23
#define OBD_PORT 35000

// ===== TCP =====
static AsyncServer serverCLI(TELNET_PORT);
static AsyncServer serverELM(OBD_PORT);

static AsyncClient *clientCLI = nullptr;
static AsyncClient *clientELM = nullptr;

// ===== UDP =====
static WiFiUDP udp;
static IPAddress broadcastAddr(255, 255, 255, 255);
static uint32_t lastBroadcast = 0;
#define DISCOVERY_PORT 17222
static uint32_t lastDiscoveryReply = 0;
static IPAddress lastDiscoveryIP;

// request magic (what you already broadcast)
static const uint8_t GVRET_MAGIC[4] = {0x1C, 0xEF, 0xAC, 0xED};

// GVRET device type
#define DEVICE_TYPE_WIFI 0x01

static uint8_t udpRxBuf[32];

// ===== OTA STATE =====
enum OTAState
{
    OTA_OFF = 0,
    OTA_READY
};

static OTAState otaState = OTA_OFF;

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

static size_t buildGVRETResponse(uint8_t *buf)
{
    uint8_t *p = buf;

    // magic
    memcpy(p, GVRET_MAGIC, 4);
    p += 4;

    // protocol version
    *p++ = 0x01;

    // device type (WiFi)
    *p++ = DEVICE_TYPE_WIFI;

    // IP
    IPAddress ip = WiFi.localIP();
    *p++ = ip[0];
    *p++ = ip[1];
    *p++ = ip[2];
    *p++ = ip[3];

    // port (big endian)
    uint16_t port = TELNET_PORT;
    *p++ = (port >> 8) & 0xFF;
    *p++ = (port & 0xFF);

    // name (null-terminated)
    const char *name = MDNS_NAME;
    size_t nameLen = strlen(name);
    memcpy(p, name, nameLen);
    p += nameLen;
    *p++ = 0x00;

    return (p - buf);
}

// ===== TCP SERVER =====
static void setupServer(AsyncServer &srv, AsyncClient *&clientRef)
{
    srv.onClient([](void *arg, AsyncClient *c)
                 {
                     AsyncClient *&client = *(AsyncClient **)arg;

                     if (client)
                         client->close();

                     client = c;
                     client->setNoDelay(true);

                     client->onDisconnect([](void *arg, AsyncClient *c)
                                          {
            AsyncClient*& client = *(AsyncClient**)arg;
            if (client == c) client = nullptr; }, arg);

                     client->onError([](void *arg, AsyncClient *c, int8_t)
                                     {
            AsyncClient*& client = *(AsyncClient**)arg;
            if (client == c) client = nullptr; }, arg);

                     client->onData([](void *, AsyncClient *, void *data, size_t len)
                                    { transportDispatchBuffer((const uint8_t *)data, len); }, nullptr); },
                 &clientRef);

    srv.begin();
}

// ===== PUBLIC =====
void netInit()
{
    setupWiFiEvents();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    setupServer(serverCLI, clientCLI);
    setupServer(serverELM, clientELM);

    DEBUG("Servers started: CLI=%d, ELM=%d\n", TELNET_PORT, OBD_PORT);
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

    // ===== UDP RX (discovery request handling) =====
    static uint32_t lastUdpPoll = 0;
    uint32_t nowMs = millis();

    if (WiFi.status() != WL_CONNECTED)
    {
        goto NETLOOP_END;
    }

    if ((nowMs - lastUdpPoll) >= 10) // 100 Hz max
    {
        lastUdpPoll = nowMs;

        // ===== SKIP UDP WHEN BUSY =====
        if (netClientConnected())
        {
            goto NETLOOP_END;
        }

        int packetSize = udp.parsePacket();

        // ===== GUARD =====
        if (packetSize <= 0 || packetSize > sizeof(udpRxBuf))
        {
            goto NETLOOP_END;
        }

        int len = udp.read(udpRxBuf, sizeof(udpRxBuf));

        if (len >= 4 && memcmp(udpRxBuf, GVRET_MAGIC, 4) == 0)
        {
            // ===== IGNORE IF ALREADY CONNECTED =====
            // ===== but allow same client reconnect =====
            // allow same client, block others
            if (netClientConnected())
            {
                IPAddress rip = udp.remoteIP();

                if ((clientCLI && clientCLI->remoteIP() != rip) &&
                    (clientELM && clientELM->remoteIP() != rip))
                {
                    goto NETLOOP_END;
                }
            }

            IPAddress rip = udp.remoteIP();
            uint32_t now = millis();

            // ===== RATE LIMIT =====
            if (rip == lastDiscoveryIP && (now - lastDiscoveryReply) < 1000)
            {
                goto NETLOOP_END;
            }

            lastDiscoveryIP = rip;
            lastDiscoveryReply = now;

            DEBUG("GVRET discovery from %s\n", rip.toString().c_str());

            uint8_t resp[64];
            size_t respLen = buildGVRETResponse(resp);

            udp.beginPacket(rip, udp.remotePort());
            udp.write(resp, respLen);
            udp.endPacket();

            DEBUG("GVRET response sent\n");
        }
    }

NETLOOP_END:
    delay(0); // yield to lwIP / WiFi
}

size_t netWrite(const uint8_t *data, size_t len)
{
    if (clientELM && clientELM->connected() && clientELM->canSend())
    {
        return clientELM->write((const char *)data, len);
    }

    if (clientCLI && clientCLI->connected() && clientCLI->canSend())
    {
        return clientCLI->write((const char *)data, len);
    }

    return 0;
}

bool netClientConnected()
{
    return (clientELM && clientELM->connected()) ||
           (clientCLI && clientCLI->connected());
}
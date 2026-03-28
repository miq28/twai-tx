#include "gvret_protocol.h"
#include "can_driver.h"
#include "Arduino.h"
#include <Preferences.h>
#include "can_common.h"

//=========================

class CAN_FRAME;
#define WIFI_BUFF_SIZE 2048
#define NUM_BUSES 1
#define CFG_BUILD_NUM 911
#define PREF_NAME "WeactCAN485"
uint8_t buff[20];
byte transmitBuffer[WIFI_BUFF_SIZE];
int transmitBufferLength;
Preferences nvPrefs;
CAN_FRAME build_out_frame;
uint32_t build_int;
int out_bus;
char deviceName[20];
char otaHost[40];
char otaFilename[100];
enum STATE
{
    IDLE,
    GET_COMMAND,
    BUILD_CAN_FRAME,
    TIME_SYNC,
    GET_DIG_INPUTS,
    GET_ANALOG_INPUTS,
    SET_DIG_OUTPUTS,
    SETUP_CANBUS,
    GET_CANBUS_PARAMS,
    GET_DEVICE_INFO,
    SET_SINGLEWIRE_MODE,
    SET_SYSTYPE,
    ECHO_CAN_FRAME,
    SETUP_EXT_BUSES
};
STATE state;
enum GVRET_PROTOCOL
{
    PROTO_BUILD_CAN_FRAME = 0,
    PROTO_TIME_SYNC = 1,
    PROTO_DIG_INPUTS = 2,
    PROTO_ANA_INPUTS = 3,
    PROTO_SET_DIG_OUT = 4,
    PROTO_SETUP_CANBUS = 5,
    PROTO_GET_CANBUS_PARAMS = 6,
    PROTO_GET_DEV_INFO = 7,
    PROTO_SET_SW_MODE = 8,
    PROTO_KEEPALIVE = 9,
    PROTO_SET_SYSTYPE = 10,
    PROTO_ECHO_CAN_FRAME = 11,
    PROTO_GET_NUMBUSES = 12,
    PROTO_GET_EXT_BUSES = 13,
    PROTO_SET_EXT_BUSES = 14,
    PROTO_BUILD_FD_FRAME = 20,
    PROTO_SETUP_FD = 21,
    PROTO_GET_FD = 22,
};
// Get the value of XOR'ing all the bytes together. This creates a reasonable checksum that can be used
// to make sure nothing too stupid has happened on the comm.
uint8_t checksumCalc(uint8_t *buffer, int length)
{
    uint8_t valu = 0;
    for (int c = 0; c < length; c++)
    {
        valu ^= buffer[c];
    }
    return valu;
}
struct SystemSettings
{
    boolean txToggle; // LED toggle values
    boolean lawicelMode;
    boolean lawicellExtendedMode;
    boolean lawicelAutoPoll;
    boolean lawicelTimestamping;
    int lawicelPollCounter;
    boolean lawicelBusReception[NUM_BUSES]; // does user want to see messages from this bus?
    int8_t numBuses;                        // number of buses this hardware currently supports.
};
SystemSettings SysSettings;
struct CANFDSettings
{
    uint32_t nomSpeed;
    uint32_t fdSpeed;
    boolean enabled;
    boolean listenOnly;
    boolean fdMode;
};
struct EEPROMSettings
{
    CANFDSettings canSettings[NUM_BUSES];

    boolean useBinarySerialComm; // use a binary protocol on the serial link or human readable format?

    uint8_t logLevel;   // Level of logging to output on serial line
    uint8_t systemType; // 0 = A0RET, 1 = EVTV ESP32 Board, 2 = Macchine 5-CAN board

    boolean enableBT; // are we enabling bluetooth too?
    char btName[32];
    int sendingBus;

    boolean enableLawicel;

    // if we're using WiFi then output to serial is disabled (it's far too slow to keep up)
    uint8_t wifiMode; // 0 = don't use wifi, 1 = connect to an AP, 2 = Create an AP
    char SSID[32];    // null terminated string for the SSID
    char WPA2Key[64]; // Null terminated string for the key. Can be a passphase or the actual key
} __attribute__((__packed__));
EEPROMSettings settings;
void buildDeviceName(char *out, size_t outSize, const char *baseName)
{
    uint64_t chipid = ESP.getEfuseMac();              // 48-bit MAC
    uint32_t shortId = (uint32_t)(chipid & 0xFFFFFF); // last 3 bytes

    // Format: BASE_XXXXXX
    snprintf(out, outSize, "%s_%06X", baseName, shortId);
}
void loadSettings()
{
    // LOGI("Loading settings....");

    // Logger::console("%i\n", espChipRevision);

    // canBuses = nullptr;

    nvPrefs.begin(PREF_NAME, false);

    // nvPrefs.clear(); // Delete all keys in this namespace
    // Or use: nvPrefs.remove("myKey"); // Delete a specific key
    // nvPrefs.end(); // Close the namespace
    // DEBUG("Preferences cleared");

    settings.useBinarySerialComm = nvPrefs.getBool("binarycomm", false);
    settings.logLevel = nvPrefs.getUChar("loglevel", 1); // info
    settings.wifiMode = nvPrefs.getUChar("wifiMode", 1); // Wifi defaults to connect to my home AP
    settings.enableBT = nvPrefs.getBool("enable-bt", false);
    settings.enableLawicel = nvPrefs.getBool("enableLawicel", true);
    settings.sendingBus = nvPrefs.getInt("sendingBus", 0);

    // LOGI("Running on Weact Studio CAN-485");
    SysSettings.numBuses = 1;
    // CAN0.setCANPins(GPIO_NUM_26, GPIO_NUM_27);
    // canBuses = &CAN0;
    SysSettings.txToggle = true;
    SysSettings.lawicelAutoPoll = false;
    SysSettings.lawicelMode = false;
    SysSettings.lawicellExtendedMode = false;
    SysSettings.lawicelTimestamping = false;
    buildDeviceName(deviceName, sizeof(deviceName), "WeactCAN485"); // become WeactCAN485_XXXXXX
    strcpy(otaHost, "media3.evtv.me");
    strcpy(otaFilename, "/esp32ret.bin");

    if (nvPrefs.getString("SSID", settings.SSID, 32) == 0)
    {
        if (settings.wifiMode == 1)
            strcpy(settings.SSID, "galaxi");
        else
            strcpy(settings.SSID, deviceName);
        // strcat(settings.SSID, "SSID");
    }

    if (nvPrefs.getString("wpa2Key", settings.WPA2Key, 64) == 0)
        if (settings.wifiMode == 1)
            strcpy(settings.WPA2Key, "n1n4iqb4l");
        else
            strcpy(settings.WPA2Key, "12345678");

    if (nvPrefs.getString("btname", settings.btName, 32) == 0)
        strcpy(settings.btName, deviceName);
    // strcat(settings.btName, deviceName);

    char buff[80];
    for (int i = 0; i < SysSettings.numBuses; i++)
    {
        sprintf(buff, "can%ispeed", i);
        settings.canSettings[i].nomSpeed = nvPrefs.getUInt(buff, 500000);
        sprintf(buff, "can%i_en", i);
        settings.canSettings[i].enabled = nvPrefs.getBool(buff, (i < 2) ? true : false);
        sprintf(buff, "can%i-listenonly", i);
        settings.canSettings[i].listenOnly = nvPrefs.getBool(buff, false);
        sprintf(buff, "can%i-fdspeed", i);
        settings.canSettings[i].fdSpeed = nvPrefs.getUInt(buff, 5000000);
        sprintf(buff, "can%i-fdmode", i);
        settings.canSettings[i].fdMode = nvPrefs.getBool(buff, false);
    }

    nvPrefs.end();

    // Logger::setLoglevel((Logger::LogLevel)settings.logLevel);

    for (int rx = 0; rx < NUM_BUSES; rx++)
        SysSettings.lawicelBusReception[rx] = true; // default to showing messages on RX
}
void sendFrameToBuffer(CAN_FRAME &frame)
{
    // Protect sendFrameToBuffer()
    // worst-case frame size ~32 bytes (safe margin)
    if (transmitBufferLength > (WIFI_BUFF_SIZE - 64))
    {
        return; // existing safety
    }

    // 🔴 ADD THIS RIGHT AFTER
    // if (transmitBufferLength > 256)
    // {
    //     gvretForceFlush = true; // request early flush
    // }

    int whichBus = 0;
    uint8_t temp;
    size_t writtenBytes;
    if (settings.useBinarySerialComm)
    {
        if (frame.extended)
            frame.id |= 1 << 31;
        transmitBuffer[transmitBufferLength++] = 0xF1;
        transmitBuffer[transmitBufferLength++] = 0; // 0 = canbus frame sending
        uint32_t now = micros();
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 24);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 24);
        transmitBuffer[transmitBufferLength++] = frame.length + (uint8_t)(whichBus << 4);
        for (int c = 0; c < frame.length; c++)
        {
            transmitBuffer[transmitBufferLength++] = frame.data.uint8[c];
        }
        // temp = checksumCalc(buff, 11 + frame.length);
        temp = 0;
        transmitBuffer[transmitBufferLength++] = temp;
        // Serial.write(buff, 12 + frame.length);
    }
    else
    {
        writtenBytes = sprintf((char *)&transmitBuffer[transmitBufferLength], "%d - %x", micros(), frame.id);
        transmitBufferLength += writtenBytes;
        if (frame.extended)
            sprintf((char *)&transmitBuffer[transmitBufferLength], " X ");
        else
            sprintf((char *)&transmitBuffer[transmitBufferLength], " S ");
        transmitBufferLength += 3;
        writtenBytes = sprintf((char *)&transmitBuffer[transmitBufferLength], "%i %i", whichBus, frame.length);
        transmitBufferLength += writtenBytes;
        for (int c = 0; c < frame.length; c++)
        {
            writtenBytes = sprintf((char *)&transmitBuffer[transmitBufferLength], " %x", frame.data.uint8[c]);
            transmitBufferLength += writtenBytes;
        }
        sprintf((char *)&transmitBuffer[transmitBufferLength], "\r\n");
        transmitBufferLength += 2;
    }
}
void displayFrame(CAN_FRAME &frame)
{
    sendFrameToBuffer(frame);

    // if (settings.enableLawicel && SysSettings.lawicelMode)
    // {
    //     lawicel.sendFrameToBuffer(frame);
    // }
    // else
    // {
    //     // 🔥 ALWAYS push to unified GVRET buffer
    //     gvret.sendFrameToBuffer(frame);
    // }
}
GVRETProtocol::GVRETProtocol()
{
    step = 0;
    state = IDLE;
}
//=========================

void GVRETProtocol::reset()
{
    state = IDLE;
    step = 0;
    id = 0;
    dlc = 0;
    extended = false;

    loadSettings();
}

void GVRETProtocol::handleByte(uint8_t in_byte)
{
    // switch (state)
    // {
    // case IDLE:
    //     if (b == 0xE7) // handshake
    //     {
    //         handshakeDone = true;
    //     }
    //     else if (b == 0xF1)
    //     {
    //         state = GET_COMMAND;
    //     }
    //     break;

    // case GET_COMMAND:
    //     handleCommand(b);
    //     break;

    // case BUILD_FRAME:
    //     buildFrame(b);
    //     break;
    // }

    uint32_t busSpeed = 0;
    uint32_t now = micros();

    uint8_t temp8;
    uint16_t temp16;

    switch (state)
    {
    case IDLE:
        if (in_byte == 0xF1)
        {
            state = GET_COMMAND;
        }
        else if (in_byte == 0xE7)
        {
            settings.useBinarySerialComm = true;
            SysSettings.lawicelMode = false;
        }
        else
        {
            // console.rcvCharacter((uint8_t)in_byte);
        }
        break;
    case GET_COMMAND:
        switch (in_byte)
        {
        case PROTO_BUILD_CAN_FRAME:
            state = BUILD_CAN_FRAME;
            buff[0] = 0xF1;
            step = 0;
            break;
        case PROTO_TIME_SYNC:
            state = TIME_SYNC;
            step = 0;
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 1; // time sync
            transmitBuffer[transmitBufferLength++] = (uint8_t)(now & 0xFF);
            transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 8);
            transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 16);
            transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 24);
            break;
        case PROTO_DIG_INPUTS:
            // immediately return the data for digital inputs
            temp8 = 0; // getDigital(0) + (getDigital(1) << 1) + (getDigital(2) << 2) + (getDigital(3) << 3) + (getDigital(4) << 4) + (getDigital(5) << 5);
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 2; // digital inputs
            transmitBuffer[transmitBufferLength++] = temp8;
            temp8 = checksumCalc(buff, 2);
            transmitBuffer[transmitBufferLength++] = temp8;
            state = IDLE;
            break;
        case PROTO_ANA_INPUTS:
            // immediately return data on analog inputs
            temp16 = 0; // getAnalog(0);  // Analogue input 1
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 3;
            transmitBuffer[transmitBufferLength++] = temp16 & 0xFF;
            transmitBuffer[transmitBufferLength++] = uint8_t(temp16 >> 8);
            temp16 = 0; // getAnalog(1);  // Analogue input 2
            transmitBuffer[transmitBufferLength++] = temp16 & 0xFF;
            transmitBuffer[transmitBufferLength++] = uint8_t(temp16 >> 8);
            temp16 = 0; // getAnalog(2);  // Analogue input 3
            transmitBuffer[transmitBufferLength++] = temp16 & 0xFF;
            transmitBuffer[transmitBufferLength++] = uint8_t(temp16 >> 8);
            temp16 = 0; // getAnalog(3);  // Analogue input 4
            transmitBuffer[transmitBufferLength++] = temp16 & 0xFF;
            transmitBuffer[transmitBufferLength++] = uint8_t(temp16 >> 8);
            temp16 = 0; // getAnalog(4);  // Analogue input 5
            transmitBuffer[transmitBufferLength++] = temp16 & 0xFF;
            transmitBuffer[transmitBufferLength++] = uint8_t(temp16 >> 8);
            temp16 = 0; // getAnalog(5);  // Analogue input 6
            transmitBuffer[transmitBufferLength++] = temp16 & 0xFF;
            transmitBuffer[transmitBufferLength++] = uint8_t(temp16 >> 8);
            temp16 = 0; // getAnalog(6);  // Vehicle Volts
            transmitBuffer[transmitBufferLength++] = temp16 & 0xFF;
            transmitBuffer[transmitBufferLength++] = uint8_t(temp16 >> 8);
            temp8 = checksumCalc(buff, 9);
            transmitBuffer[transmitBufferLength++] = temp8;
            state = IDLE;
            break;
        case PROTO_SET_DIG_OUT:
            state = SET_DIG_OUTPUTS;
            buff[0] = 0xF1;
            break;
        case PROTO_SETUP_CANBUS:
            state = SETUP_CANBUS;
            step = 0;
            buff[0] = 0xF1;
            break;
        case PROTO_GET_CANBUS_PARAMS:
            // immediately return data on canbus params
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 6;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].enabled + ((unsigned char)settings.canSettings[0].listenOnly << 4);
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].nomSpeed;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].nomSpeed >> 8;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].nomSpeed >> 16;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].nomSpeed >> 24;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[1].enabled + ((unsigned char)settings.canSettings[1].listenOnly << 4);
            transmitBuffer[transmitBufferLength++] = settings.canSettings[1].nomSpeed;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[1].nomSpeed >> 8;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[1].nomSpeed >> 16;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[1].nomSpeed >> 24;
            state = IDLE;
            break;
        case PROTO_GET_DEV_INFO:
            // immediately return device information
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 7;
            transmitBuffer[transmitBufferLength++] = CFG_BUILD_NUM & 0xFF;
            transmitBuffer[transmitBufferLength++] = (CFG_BUILD_NUM >> 8);
            transmitBuffer[transmitBufferLength++] = 0x20;
            transmitBuffer[transmitBufferLength++] = 0;
            transmitBuffer[transmitBufferLength++] = 0;
            transmitBuffer[transmitBufferLength++] = 0; // was single wire mode. Should be rethought for this board.
            state = IDLE;
            break;
        case PROTO_SET_SW_MODE:
            buff[0] = 0xF1;
            state = SET_SINGLEWIRE_MODE;
            step = 0;
            break;
        case PROTO_KEEPALIVE:
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 0x09;
            transmitBuffer[transmitBufferLength++] = 0xDE;
            transmitBuffer[transmitBufferLength++] = 0xAD;

            // gvretForceFlush = true; // 🔴 signal urgent send

            Serial.write(transmitBuffer, transmitBufferLength);
            transmitBufferLength = 0;

            state = IDLE;
            break;
        case PROTO_SET_SYSTYPE:
            buff[0] = 0xF1;
            state = SET_SYSTYPE;
            step = 0;
            break;
        case PROTO_ECHO_CAN_FRAME:
            state = ECHO_CAN_FRAME;
            buff[0] = 0xF1;
            step = 0;
            break;
        case PROTO_GET_NUMBUSES:
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 12;
            transmitBuffer[transmitBufferLength++] = SysSettings.numBuses;
            state = IDLE;
            break;
        case PROTO_GET_EXT_BUSES:
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 13;
            for (int u = 2; u < 17; u++)
                transmitBuffer[transmitBufferLength++] = 0;
            step = 0;
            state = IDLE;
            break;
        case PROTO_SET_EXT_BUSES:
            state = SETUP_EXT_BUSES;
            step = 0;
            buff[0] = 0xF1;
            break;
        }
        break;
    case BUILD_CAN_FRAME:
        buff[1 + step] = in_byte;
        switch (step)
        {
        case 0:
            build_out_frame.id = in_byte;
            break;
        case 1:
            build_out_frame.id |= in_byte << 8;
            break;
        case 2:
            build_out_frame.id |= in_byte << 16;
            break;
        case 3:
            build_out_frame.id |= in_byte << 24;
            if (build_out_frame.id & 1 << 31)
            {
                build_out_frame.id &= 0x7FFFFFFF;
                build_out_frame.extended = true;
            }
            else
                build_out_frame.extended = false;
            break;
        case 4:
            out_bus = in_byte & 3;
            break;
        case 5:
            build_out_frame.length = in_byte & 0xF;
            if (build_out_frame.length > 8)
            {
                build_out_frame.length = 8;
            }
            break;
        default:
            if (step < build_out_frame.length + 6)
            {
                build_out_frame.data.uint8[step - 6] = in_byte;
            }
            else
            {
                state = IDLE;
                // this would be the checksum byte. Compute and compare.
                // temp8 = checksumCalc(buff, step);
                build_out_frame.rtr = 0;
                if (out_bus < NUM_BUSES)
                    // canManager.sendFrame(canBuses, build_out_frame);
                    CANDriver::sendFrame(build_out_frame);
            }
            break;
        }
        step++;
        break;
    case TIME_SYNC:
        state = IDLE;
        break;
    case GET_DIG_INPUTS:
        // nothing to do
        break;
    case GET_ANALOG_INPUTS:
        // nothing to do
        break;
    case SET_DIG_OUTPUTS: // todo: validate the XOR byte
        buff[1] = in_byte;
        // for(int c = 0; c < 8; c++){
        //     if(in_byte & (1 << c)) setOutput(c, true);
        //     else setOutput(c, false);
        // }
        state = IDLE;
        break;
    case SETUP_CANBUS: // todo: validate checksum
        switch (step)
        {
        case 0:
            build_int = in_byte;
            break;
        case 1:
            build_int |= in_byte << 8;
            break;
        case 2:
            build_int |= in_byte << 16;
            break;
        case 3:
            build_int |= in_byte << 24;
            busSpeed = build_int & 0xFFFFF;
            if (busSpeed > 1000000)
                busSpeed = 1000000;

            if (build_int > 0)
            {
                if (build_int & 0x80000000ul) // signals that enabled and listen only status are also being passed
                {
                    if (build_int & 0x40000000ul)
                    {
                        settings.canSettings[0].enabled = true;
                    }
                    else
                    {
                        settings.canSettings[0].enabled = false;
                    }
                    if (build_int & 0x20000000ul)
                    {
                        settings.canSettings[0].listenOnly = true;
                    }
                    else
                    {
                        settings.canSettings[0].listenOnly = false;
                    }
                }
                else
                {
                    // if not using extended status mode then just default to enabling - this was old behavior
                    settings.canSettings[0].enabled = true;
                }
                // CAN0.set_baudrate(build_int);
                settings.canSettings[0].nomSpeed = busSpeed;
            }
            else
            { // disable first canbus
                settings.canSettings[0].enabled = false;
            }

            if (settings.canSettings[0].enabled)
            {
                CANDriver::reinit(
                    settings.canSettings[0].nomSpeed,
                    settings.canSettings[0].listenOnly);
            }
            else
                // canBuses->disable();
                // TODO: stop CAN driver
                break;
        case 4:
            build_int = in_byte;
            break;
        case 5:
            build_int |= in_byte << 8;
            break;
        case 6:
            build_int |= in_byte << 16;
            break;
        case 7:
            build_int |= in_byte << 24;
            busSpeed = build_int & 0xFFFFF;
            if (busSpeed > 1000000)
                busSpeed = 1000000;

            settings.canSettings[1].enabled = false;

            state = IDLE;
            // now, write out the new canbus settings to EEPROM
            // EEPROM.writeBytes(0, &settings, sizeof(settings));
            // EEPROM.commit();
            // setPromiscuousMode();
            break;
        }
        step++;
        break;
    case GET_CANBUS_PARAMS:
        // nothing to do
        break;
    case GET_DEVICE_INFO:
        // nothing to do
        break;
    case SET_SINGLEWIRE_MODE:
        if (in_byte == 0x10)
        {
        }
        else
        {
        }
        // EEPROM.writeBytes(0, &settings, sizeof(settings));
        // EEPROM.commit();
        state = IDLE;
        break;
    case SET_SYSTYPE:
        settings.systemType = in_byte;
        // EEPROM.writeBytes(0, &settings, sizeof(settings));
        // EEPROM.commit();
        // loadSettings();
        state = IDLE;
        break;
    case ECHO_CAN_FRAME:
        buff[1 + step] = in_byte;
        switch (step)
        {
        case 0:
            build_out_frame.id = in_byte;
            break;
        case 1:
            build_out_frame.id |= in_byte << 8;
            break;
        case 2:
            build_out_frame.id |= in_byte << 16;
            break;
        case 3:
            build_out_frame.id |= in_byte << 24;
            if (build_out_frame.id & 1 << 31)
            {
                build_out_frame.id &= 0x7FFFFFFF;
                build_out_frame.extended = true;
            }
            else
                build_out_frame.extended = false;
            break;
        case 4:
            out_bus = in_byte & 1;
            break;
        case 5:
            build_out_frame.length = in_byte & 0xF;
            if (build_out_frame.length > 8)
                build_out_frame.length = 8;
            break;
        default:
            if (step < build_out_frame.length + 6)
            {
                build_out_frame.data.bytes[step - 6] = in_byte;
            }
            else
            {
                state = IDLE;
                // this would be the checksum byte. Compute and compare.
                // temp8 = checksumCalc(buff, step);
                // if (temp8 == in_byte)
                //{
                //  toggleRXLED();
                // if(isConnected) {

                // canManager.displayFrame(build_out_frame);
                displayFrame(build_out_frame);

                //}
                //}
            }
            break;
        }
        step++;
        break;
    case SETUP_EXT_BUSES: // setup enable/listenonly/speed for SWCAN, Enable/Speed for LIN1, LIN2
        switch (step)
        {
        case 0:
            build_int = in_byte;
            break;
        case 1:
            build_int |= in_byte << 8;
            break;
        case 2:
            build_int |= in_byte << 16;
            break;
        case 3:
            build_int |= in_byte << 24;
            break;
        case 4:
            build_int = in_byte;
            break;
        case 5:
            build_int |= in_byte << 8;
            break;
        case 6:
            build_int |= in_byte << 16;
            break;
        case 7:
            build_int |= in_byte << 24;
            break;
        case 8:
            build_int = in_byte;
            break;
        case 9:
            build_int |= in_byte << 8;
            break;
        case 10:
            build_int |= in_byte << 16;
            break;
        case 11:
            build_int |= in_byte << 24;
            state = IDLE;
            // now, write out the new canbus settings to EEPROM
            // EEPROM.writeBytes(0, &settings, sizeof(settings));
            // EEPROM.commit();
            // setPromiscuousMode();
            break;
        }
        step++;
        break;
    }
}

// void GVRETProtocol::handleCommand(uint8_t cmd)
// {
//     switch (cmd)
//     {
//     case 0: // PROTO_BUILD_CAN_FRAME
//         state = BUILD_FRAME;
//         step = 0;
//         id = 0;
//         break;

//     case 5: // PROTO_SETUP_CANBUS
//         // minimal: enable bus (SavvyCAN will send this)
//         busEnabled = true;
//         state = IDLE;
//         break;

//     case 9: // KEEPALIVE
//         // respond minimal
//         Serial.write(0xF1);
//         Serial.write(0x09);
//         Serial.write(0xDE);
//         Serial.write(0xAD);
//         state = IDLE;
//         break;

//     default:
//         state = IDLE;
//         break;
//     }
// }

// void GVRETProtocol::buildFrame(uint8_t b)
// {
//     switch (step)
//     {
//     case 0:
//         id = b;
//         break;
//     case 1:
//         id |= (b << 8);
//         break;
//     case 2:
//         id |= (b << 16);
//         break;
//     case 3:
//         id |= (b << 24);
//         if (id & (1UL << 31))
//         {
//             id &= 0x7FFFFFFF;
//             extended = true;
//         }
//         break;

//     case 4:
//         // bus (ignore for now)
//         break;

//     case 5:
//         dlc = b & 0xF;
//         if (dlc > 8)
//             dlc = 8;
//         break;

//     default:
//         if (step < dlc + 6)
//         {
//             data[step - 6] = b;
//         }
//         else
//         {
//             // send frame
//             twai_message_t msg{};
//             msg.identifier = id;
//             msg.extd = extended;
//             msg.rtr = 0;
//             msg.data_length_code = dlc;

//             for (int i = 0; i < dlc; i++)
//                 msg.data[i] = data[i];

//             CANDriver::send(msg);

//             reset();
//             return;
//         }
//         break;
//     }

//     step++;
// }
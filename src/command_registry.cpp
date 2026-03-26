#include "Arduino.h"
#include <string.h>
#include <stdlib.h>
#include "command_registry.h"

CommandInfo commandTable[] = {
    {"start", "s", "start streaming"},
    {"stop", "x", "stop streaming"},
    {"fps", "f", "set FPS (e.g. fps 1000)"},
    {"baud", "b", "set CAN baud"},
    {"mode generator", "m0", "generator mode"},
    {"mode ecu", "m2", "ECU mode"},
    {"status", "", "show status"},
    {"help", "", "show this help"},
};

const int commandCount = sizeof(commandTable) / sizeof(CommandInfo);


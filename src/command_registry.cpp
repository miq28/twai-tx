#include "Arduino.h"
#include <string.h>
#include <stdlib.h>
#include "command_registry.h"

CommandInfo commandTable[] = {
    {"start", "s", "start streaming"},
    {"stop", "x", "stop streaming"},
    {"fps", "f", "set FPS (e.g. fps 1000)"},
    {"help", "", "show this help"},
    {"baud", "b", "set CAN baud (e.g. b500000)"},
    {"ext", "e", "ext 0/1  : standard / extended frame"},
    {"delay", "d", "d<number>: delay us (e.g. d100)"},
    {"lock ID", "i", "i<number>: lock ID (0-9), i-1 = cycle"},
    {"listen only", "l", "l0 / l1  : listen OFF / ON"},
    {"mode generator", "m0", "generator mode"},
    {"mode generator", "m1", "slow mode"},
    {"mode ecu", "m2", "ECU mode"},
    {"status", "", "show status"},
    {"reset", "r", "reeet defaults"},
    {"help", "", "show this help"},
};

int commandCount = sizeof(commandTable) / sizeof(CommandInfo);


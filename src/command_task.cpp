#include "command_queue.h"
#include "command_handler.h"
#include "Arduino.h"

void commandTask(void*)
{
    Command cmd;

    while (1)
    {
        if (cmdPop(cmd))
        {
            handleCommand(cmd);
        }
        else
        {
            vTaskDelay(1);
        }
    }
}
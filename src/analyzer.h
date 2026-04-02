#pragma once
#include "can_pipeline.h"

void analyzerPush(const CANRxItem& item);
void analyzerProcess();
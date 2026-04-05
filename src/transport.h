#pragma once

void transportInit();
void transportProcess();

// ===== TX handing
void transportWrite(const uint8_t* data, size_t len);
void transportFlush();

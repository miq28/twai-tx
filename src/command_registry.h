
#pragma once

struct CommandInfo
{
    const char *name;
    const char *alias;
    const char *help;
};

extern CommandInfo commandTable[];
extern int commandCount;   // ✅ remove const here
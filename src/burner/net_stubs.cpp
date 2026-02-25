#include "burner.h"

// Non-Windows SDL builds in this fork don't provide fightcade net runtime.
int kNetVersion = NET_VERSION;
int kNetGame = 0;
int kNetSpectator = 0;
int kNetLua = 1;
bool bFixDiagonals = false;
int nEnableSOCD = 0;

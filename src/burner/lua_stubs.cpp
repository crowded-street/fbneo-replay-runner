#include "burner.h"
#include "luaengine.h"

INT32 bRunaheadFrame = 0;

void CallRegisteredLuaMemHook(unsigned int, int, unsigned int, LuaMemHookType)
{
}

int FBA_LuaRerecordCountSkip()
{
	return 0;
}

void luasav_save(const char*)
{
}

void luasav_load(const char*)
{
}

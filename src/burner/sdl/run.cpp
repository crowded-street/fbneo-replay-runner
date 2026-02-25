// Run module
#include "burner.h"
#include "state.h"

#include <sys/time.h>
#include <vector>

static unsigned int nDoFPS = 0;
bool bAltPause = 0;

int bAlwaysDrawFrames = 0;

int counter;                                // General purpose variable used when debugging

static unsigned int nNormalLast = 0;        // Last value of GetTime()
static int          nNormalFrac = 0;        // Extra fraction we did

static bool bAppDoStep = 0;
bool        bAppDoFast = 0;
bool        bAppShowFPS = 0;
static int  nFastSpeed = 6;

UINT32 messageFrames = 0;
char lastMessage[MESSAGE_MAX_LENGTH];

/// Ingame gui
#ifdef BUILD_SDL2
extern SDL_Renderer* sdlRenderer;
extern void ingame_gui_start(SDL_Renderer* renderer);
#endif

/// Save States
#ifdef BUILD_SDL2
static char* szSDLSavePath = NULL;
#endif

int bDrvSaveAll = 0;

extern int nAcbVersion;
extern int nAcbLoadState;

static char gReplayStatePath[MAX_PATH] = { 0 };
static char gReplayInputsPath[MAX_PATH] = { 0 };
static bool gReplayEnabled = false;
static bool gReplayLoaded = false;
static bool gReplayFinished = false;
static std::vector<UINT8> gReplayInputData;
static size_t gReplayInputOffset = 0;

struct ReplayBinding {
	struct GameInp* pgi;
	UINT8 player;
	UINT8 bit;
};
static std::vector<ReplayBinding> gReplayBindings;

static const UINT8* gReplayScanPtr = NULL;
static INT32 gReplayScanRemaining = 0;
static bool gReplayScanFailed = false;

void ReplaySetStatePath(const char* path)
{
	snprintf(gReplayStatePath, MAX_PATH, "%s", path ? path : "");
}

void ReplaySetInputsPath(const char* path)
{
	snprintf(gReplayInputsPath, MAX_PATH, "%s", path ? path : "");
}

bool ReplayHasStatePath()
{
	return gReplayStatePath[0] != '\0';
}

bool ReplayHasInputsPath()
{
	return gReplayInputsPath[0] != '\0';
}

static bool ReplayIsEnabled()
{
	return gReplayEnabled;
}

static INT32 __cdecl ReplayWriteAcb(struct BurnArea* pba)
{
	if (pba->nLen > gReplayScanRemaining) {
		memset(pba->Data, 0, pba->nLen);
		if (gReplayScanRemaining > 0) {
			memcpy(pba->Data, gReplayScanPtr, gReplayScanRemaining);
		}
		gReplayScanPtr += gReplayScanRemaining;
		gReplayScanRemaining = 0;
		gReplayScanFailed = true;
		return 1;
	}

	memcpy(pba->Data, gReplayScanPtr, pba->nLen);
	gReplayScanPtr += pba->nLen;
	gReplayScanRemaining -= pba->nLen;
	return 0;
}

static bool ReplayReadFile(const char* path, std::vector<UINT8>& out)
{
	FILE* fp = fopen(path, "rb");
	if (fp == NULL) {
		return false;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	long size = ftell(fp);
	if (size < 0) {
		fclose(fp);
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return false;
	}

	out.resize((size_t)size);
	if (size > 0) {
		if (fread(out.data(), 1, (size_t)size, fp) != (size_t)size) {
			fclose(fp);
			out.clear();
			return false;
		}
	}

	fclose(fp);
	return true;
}

static bool ReplayLoadStateBlob(const std::vector<UINT8>& stateBlob)
{
	if (stateBlob.empty()) {
		printf("Replay state blob is empty\n");
		return false;
	}

	const UINT8* payload = stateBlob.data();
	INT32 payloadLen = (INT32)stateBlob.size();

	if (payloadLen >= (INT32)(6 * sizeof(INT32))) {
		const INT32* header = (const INT32*)payload;
		if (header[0] == 'GGPO') {
			INT32 headerSize = header[1];
			if (headerSize < (INT32)(2 * sizeof(INT32)) || headerSize > payloadLen) {
				printf("Replay state header size is invalid (%d)\n", headerSize);
				return false;
			}
			nAcbVersion = header[2];
			payload += headerSize;
			payloadLen -= headerSize;
		}
	}

	gReplayScanPtr = payload;
	gReplayScanRemaining = payloadLen;
	gReplayScanFailed = false;

	BurnAcb = ReplayWriteAcb;
	nAcbLoadState = 1;
	BurnAreaScan(ACB_FULLSCANL | ACB_WRITE, NULL);
	nAcbLoadState = 0;
	nAcbVersion = nBurnVer;

	if (gReplayScanFailed) {
		printf("Replay state blob is too small for this driver\n");
		return false;
	}

	BurnRecalcPal();
	return true;
}

static INT32 ReplayBitFromInfo(const char* szInfo)
{
	if (szInfo == NULL) return -1;

	if (strstr(szInfo, " start")) return 1;
	if (strstr(szInfo, " up")) return 2;
	if (strstr(szInfo, " down")) return 3;
	if (strstr(szInfo, " left")) return 4;
	if (strstr(szInfo, " right")) return 5;

	const char* fire = strstr(szInfo, " fire ");
	if (fire) {
		INT32 n = strtol(fire + 6, NULL, 10);
		if (n >= 1 && n <= 6) {
			return n + 5; // fire 1..6 => LP..HK bits 6..11
		}
	}

	return -1;
}

static void ReplayBuildBindings()
{
	gReplayBindings.clear();

	for (UINT32 i = 0; i < nGameInpCount; i++) {
		BurnInputInfo bii;
		memset(&bii, 0, sizeof(bii));
		if (BurnDrvGetInputInfo(&bii, i) != 0 || bii.pVal == NULL || bii.szInfo == NULL) {
			continue;
		}

		INT32 player = -1;
		if (strncmp(bii.szInfo, "p1 ", 3) == 0) player = 0;
		if (strncmp(bii.szInfo, "p2 ", 3) == 0) player = 1;
		if (player < 0) continue;

		INT32 bit = ReplayBitFromInfo(bii.szInfo);
		if (bit < 0) continue;

		struct GameInp* pgi = &GameInp[i];
		if (pgi->nInput != GIT_SWITCH) continue;

		ReplayBinding binding;
		binding.pgi = pgi;
		binding.player = (UINT8)player;
		binding.bit = (UINT8)bit;
		gReplayBindings.push_back(binding);
	}
}

static void ReplayApplyFrameInputs()
{
	if (gReplayFinished) {
		return;
	}

	UINT16 masks[2] = { 0, 0 };
	if ((gReplayInputOffset + 10) > gReplayInputData.size()) {
		gReplayFinished = true;
		return;
	}

	const UINT8* frame = &gReplayInputData[gReplayInputOffset];
	masks[0] = (UINT16)frame[0] | ((UINT16)frame[1] << 8);
	masks[1] = (UINT16)frame[5] | ((UINT16)frame[6] << 8);
	gReplayInputOffset += 10;

	for (size_t i = 0; i < gReplayBindings.size(); i++) {
		ReplayBinding& b = gReplayBindings[i];
		const bool pressed = ((masks[b.player] >> b.bit) & 1) != 0;
		b.pgi->Input.nVal = pressed ? 1 : 0;
		if (b.pgi->Input.pVal) {
			*b.pgi->Input.pVal = (UINT8)b.pgi->Input.nVal;
		}
	}
}

static bool ReplayInit()
{
	gReplayEnabled = ReplayHasStatePath() && ReplayHasInputsPath();
	gReplayLoaded = false;
	gReplayFinished = false;
	gReplayInputData.clear();
	gReplayInputOffset = 0;
	gReplayBindings.clear();

	if (!gReplayEnabled) {
		return true;
	}

	std::vector<UINT8> stateBlob;
	if (!ReplayReadFile(gReplayStatePath, stateBlob)) {
		printf("Failed to read replay state blob: %s\n", gReplayStatePath);
		return false;
	}
	if (!ReplayReadFile(gReplayInputsPath, gReplayInputData)) {
		printf("Failed to read replay inputs blob: %s\n", gReplayInputsPath);
		return false;
	}
	if ((gReplayInputData.size() % 10) != 0) {
		printf("Replay inputs blob size must be a multiple of 10 bytes, got %zu\n", gReplayInputData.size());
		return false;
	}
	if (!ReplayLoadStateBlob(stateBlob)) {
		return false;
	}

	ReplayBuildBindings();
	gReplayLoaded = true;
	printf("Replay loaded: %zu frames, %zu input bindings\n", gReplayInputData.size() / 10, gReplayBindings.size());
	return true;
}

// The automatic save
int StatedAuto(int bSave)
{
	static TCHAR szName[MAX_PATH] = _T("");
	int nRet;

#if defined(BUILD_SDL2) && !defined(SDL_WINDOWS)	
	if (szSDLSavePath == NULL)
	{
		szSDLSavePath = SDL_GetPrefPath("fbneo", "states");
	}

	snprintf(szName, MAX_PATH, "%s%s.fs", szSDLSavePath, BurnDrvGetText(DRV_NAME));

#else

	_stprintf(szName, _T("config/games/%s.fs"), BurnDrvGetText(DRV_NAME));

#endif

	if (bSave == 0)
	{
		printf("loading state %i %s\n", bDrvSaveAll, szName);
		nRet = BurnStateLoad(szName, bDrvSaveAll, NULL);		// Load ram
		if (nRet && bDrvSaveAll)
		{
			nRet = BurnStateLoad(szName, 0, NULL);				// Couldn't get all - okay just try the nvram
		}
	}
	else
	{
		printf("saving state %i %s\n", bDrvSaveAll, szName);
		nRet = BurnStateSave(szName, bDrvSaveAll);				// Save ram
	}

	return nRet;
}


/// End Save States

char fpsstring[20];

static time_t fpstimer;
static unsigned int nPreviousFrames;

static void DisplayFPSInit()
{
	nDoFPS = 0;
	fpstimer = 0;
	nPreviousFrames = nFramesRendered;
}

static void DisplayFPS()
{
	time_t temptime = clock();
	double fps = (double)(nFramesRendered - nPreviousFrames) * CLOCKS_PER_SEC / (temptime - fpstimer);
	if (bAppDoFast) {
		fps *= nFastSpeed + 1;
	}
	if (fpstimer && temptime - fpstimer > 0) { // avoid strange fps values
		sprintf(fpsstring, "%2.2lf", fps);
	}

	fpstimer = temptime;
	nPreviousFrames = nFramesRendered;
}


//crappy message system
void UpdateMessage(char* message)
{
	snprintf(lastMessage, MESSAGE_MAX_LENGTH, "%s", message);
	messageFrames = MESSAGE_MAX_FRAMES;
}

// define this function somewhere above RunMessageLoop()
void ToggleLayer(unsigned char thisLayer)
{
	nBurnLayer ^= thisLayer;                         // xor with thisLayer
	VidRedraw();
	VidPaint(0);
}


struct timeval start;

unsigned int GetTime(void)
{
	unsigned int ticks;
	struct timeval now;
	gettimeofday(&now, NULL);
	ticks = (now.tv_sec - start.tv_sec) * 1000000 + now.tv_usec - start.tv_usec;
	return ticks;
}

// With or without sound, run one frame.
// If bDraw is true, it's the last frame before we are up to date, and so we should draw the screen
static int RunFrame(int bDraw, int bPause)
{
	if (!bDrvOkay)
	{
		return 1;
	}

	if (bPause)
	{
		if (!ReplayIsEnabled()) {
			InputMake(false);
		}
		VidPaint(0);
	}
	else
	{
		nFramesEmulated++;
		nCurrentFrame++;
		if (ReplayIsEnabled()) {
			ReplayApplyFrameInputs();
		} else {
			InputMake(true);
		}
	}

	if (bDraw)
	{
		nFramesRendered++;
		if (VidFrame())
		{
		 	AudBlankSound();
		}
		VidPaint(0);                                              // paint the screen (no need to validate)
	}
	else
	{                                       // frame skipping
		pBurnDraw = NULL;                    // Make sure no image is drawn
		BurnDrvFrame();
	}

	if (bAppShowFPS) {
		if (nDoFPS < nFramesRendered) {
			DisplayFPS();
			nDoFPS = nFramesRendered + 30;
		}
	}

	return 0;
}

// Callback used when DSound needs more sound
static int RunGetNextSound(int bDraw)
{
	if (nAudNextSound == NULL)
	{
		return 1;
	}

	if (bRunPause)
	{
		if (bAppDoStep)
		{
			RunFrame(bDraw, 0);
			memset(nAudNextSound, 0, nAudSegLen << 2);                                        // Write silence into the buffer
		}
		else
		{
			RunFrame(bDraw, 1);
		}

		bAppDoStep = 0;                                                   // done one step
		return 0;
	}

	if (bAppDoFast)
	{                                            // do more frames
		for (int i = 0; i < nFastSpeed; i++)
		{
			RunFrame(0, 0);
		}
	}

	// Render frame with sound
	pBurnSoundOut = nAudNextSound;
	RunFrame(bDraw, 0);
	if (bAppDoStep)
	{
		memset(nAudNextSound, 0, nAudSegLen << 2);                // Write silence into the buffer
	}
	bAppDoStep = 0;                                              // done one step

	return 0;
}

int delay_ticks(int ticks)
{
//sdl_delay can take up to 10 - 15 ticks it doesnt guarentee below this
   int startTicks = 0;
   int endTicks = 0;
   int checkTicks = 0;

   startTicks=SDL_GetTicks();

   while (checkTicks <= ticks)
   {
      endTicks=SDL_GetTicks();
      checkTicks = endTicks - startTicks;
   }

   return ticks;
}
int RunIdle()
{
	int nTime, nCount;

	if (bAudPlaying)
	{
		// Run with sound
		AudSoundCheck();
		return 0;
	}

	// Run without sound
	nTime = GetTime() - nNormalLast;
	nCount = (nTime * nAppVirtualFps - nNormalFrac) / 100000;
	if (nCount <= 0) {						// No need to do anything for a bit
		//delay_ticks(2);
		return 0;
	}

	nNormalFrac += nCount * 100000;
	nNormalLast += nNormalFrac / nAppVirtualFps;
	nNormalFrac %= nAppVirtualFps;

	if (nCount > 100) {						// Limit frame skipping
		nCount = 100;
	}
	if (bRunPause) {
		if (bAppDoStep) {					// Step one frame
			nCount = 10;
		}
		else {
			RunFrame(1, 1);					// Paused
			return 0;
		}
	}
	bAppDoStep = 0;


	if (bAppDoFast)
	{									// do more frames
		for (int i = 0; i < nFastSpeed; i++)
		{
			RunFrame(0, 0);
		}
	}

	if (!bAlwaysDrawFrames)
	{
		for (int i = nCount / 10; i > 0; i--)
		{              // Mid-frames
			RunFrame(0, 0);
		}
	}
	RunFrame(1, 0);                                  // End-frame
	// temp added for SDLFBA
	//VidPaint(0);
	return 0;
}

int RunReset()
{
	// Reset the speed throttling code
	nNormalLast = 0; nNormalFrac = 0;
	if (!bAudPlaying)
	{
		// run without sound
		nNormalLast = GetTime();
	}
	return 0;
}

int RunInit()
{
	gettimeofday(&start, NULL);
	DisplayFPSInit();
	// Try to run with sound
	AudSetCallback(RunGetNextSound);
	AudSoundPlay();

	RunReset();
	if (!ReplayInit()) {
		return 1;
	}
	if (!ReplayIsEnabled()) {
		StatedAuto(0);
	}
	return 0;
}

int RunExit()
{
	nNormalLast = 0;
	if (!ReplayIsEnabled()) {
		StatedAuto(1);
	}
	return 0;
}

#ifndef BUILD_MACOS
// The main message loop
int RunMessageLoop()
{
	int quit = 0;

	RunInit();
	GameInpCheckMouse();                                                                     // Hide the cursor

	while (!quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:                                        /* Windows was closed */
				quit = 1;
				break;

			case SDL_KEYDOWN:                                                // need to find a nicer way of doing this...
				switch (event.key.keysym.sym)
				{
				case SDLK_F1:
					bAppDoFast = 1;
					break;
				case SDLK_F9:
					QuickState(0);
					break;
				case SDLK_F10:
					QuickState(1);
					break;
				case SDLK_F11:
					bAppShowFPS = !bAppShowFPS;
					break;
#ifdef BUILD_SDL2
				case SDLK_TAB:
					if (sdlRenderer) {
						ingame_gui_start(sdlRenderer);
					}
					break;
#endif
				default:
					break;
				}
				break;

			case SDL_KEYUP:                                                // need to find a nicer way of doing this...
				switch (event.key.keysym.sym)
				{
				case SDLK_F1:
					bAppDoFast = 0;
					break;

				case SDLK_F12:
					quit = 1;
					break;

				default:
					break;
				}
				break;
			}
		}
		RunIdle();
		if (ReplayIsEnabled() && gReplayFinished) {
			quit = 1;
		}
	}

	RunExit();

	return 0;
}

#endif

// Windows layer-independent code
// (c) EDuke32 developers and contributors. All rights reserved. ;)

#include "winbits.h"

#include "baselayer.h"
#include "build.h"
#include "cache1d.h"
#include "compat.h"
#include "osd.h"
#include "renderlayer.h"

#include <mmsystem.h>
#include <winnls.h>
#include <winternl.h>

#include <system_error>

#ifdef BITNESS64
# define EBACKTRACEDLL "ebacktrace1-64.dll"
#else
# define EBACKTRACEDLL "ebacktrace1.dll"
#endif

int32_t    win_priorityclass;
char       win_silentvideomodeswitch;
static int win_silentfocuschange;

static HANDLE  g_singleInstanceSemaphore = nullptr;
static int32_t win_togglecomposition;
static int32_t win_systemtimermode;
static int32_t win_performancemode;

static OSVERSIONINFOEX osv;
static FARPROC ntdll_wine_get_version;
static char const *enUSLayoutString = "00000409";

DWM_TIMING_INFO timingInfo;

static HMODULE hPOWRPROF;
static GUID *systemPowerSchemeGUID;

typedef DWORD(WINAPI *PFNPOWERGETACTIVESCHEME)(HKEY, GUID **);
typedef DWORD(WINAPI *PFNPOWERSETACTIVESCHEME)(HKEY, CONST GUID *);

static PFNPOWERGETACTIVESCHEME powrprof_PowerGetActiveScheme;
static PFNPOWERSETACTIVESCHEME powrprof_PowerSetActiveScheme;

void windowsSetupTimer(int const useNtTimer)
{
    if (ntdll_wine_get_version)
        return;

    typedef HRESULT(NTAPI* PFNSETTIMERRESOLUTION)(ULONG, BOOLEAN, PULONG);
    typedef HRESULT(NTAPI* PFNQUERYTIMERRESOLUTION)(PULONG, PULONG, PULONG);

    TIMECAPS timeCaps;

    if (timeGetDevCaps(&timeCaps, sizeof(TIMECAPS)) == MMSYSERR_NOERROR)
    {
#if defined RENDERTYPESDL && SDL_MAJOR_VERSION >= 2
        int const onBattery = (SDL_GetPowerInfo(NULL, NULL) == SDL_POWERSTATE_ON_BATTERY);
#else
        static constexpr int const onBattery = 0;
#endif
        static int     timePeriod;
        static ULONG   ntTimerRes;
        static HMODULE hNTDLL = GetModuleHandle("ntdll.dll");

        static PFNQUERYTIMERRESOLUTION ntdll_NtQueryTimerResolution;
        static PFNSETTIMERRESOLUTION   ntdll_NtSetTimerResolution;

        if (useNtTimer)
        {
            if (!onBattery)
            {
                ntdll_NtQueryTimerResolution = (PFNQUERYTIMERRESOLUTION) (void(*))GetProcAddress(hNTDLL, "NtQueryTimerResolution");
                ntdll_NtSetTimerResolution   = (PFNSETTIMERRESOLUTION)   (void(*))GetProcAddress(hNTDLL, "NtSetTimerResolution");

                if (ntdll_NtQueryTimerResolution == nullptr || ntdll_NtSetTimerResolution == nullptr)
                {
                    OSD_Printf("ERROR: unable to locate NtQueryTimerResolution or NtSetTimerResolution symbols in ntdll.dll!\n");
                    goto failsafe;
                }

                ULONG minRes, maxRes, actualRes;

                ntdll_NtQueryTimerResolution(&minRes, &maxRes, &actualRes);

                if (ntTimerRes != 0)
                {
                    if (ntTimerRes == actualRes)
                        return;

                    ntdll_NtSetTimerResolution(actualRes, FALSE, &actualRes);
                }

                ntdll_NtSetTimerResolution(maxRes, TRUE, &actualRes);

                ntTimerRes = actualRes;
                timePeriod = 0;

                if (!win_silentfocuschange)
                    OSD_Printf("Initialized %.1fms system timer\n", actualRes / 10000.0);

                return;
            }
            else if (!win_silentfocuschange)
                OSD_Printf("Low-latency timer mode not supported on battery power!\n");
        }
        else if (ntTimerRes != 0)
        {
            ntdll_NtSetTimerResolution(ntTimerRes, FALSE, &ntTimerRes);
            ntTimerRes = 0;
        }

failsafe:
        int const newPeriod = min(max(timeCaps.wPeriodMin, 1u << onBattery), timeCaps.wPeriodMax);
            
        if (timePeriod != 0)
        {
            if (timePeriod == newPeriod)
                return;

            timeEndPeriod(timePeriod);
        }

        timeBeginPeriod(newPeriod);

        timePeriod = newPeriod;
        ntTimerRes = 0;

        if (!win_silentfocuschange)
            OSD_Printf("Initialized %ums system timer\n", newPeriod);

        return;
    }

    OSD_Printf("ERROR: unable to configure system timer!\n");
}

//
// CheckWinVersion() -- check to see what version of Windows we happen to be running under
//
BOOL windowsGetVersion(void)
{
    HMODULE hNTDLL = GetModuleHandle("ntdll.dll");

    if (hNTDLL)
        ntdll_wine_get_version = GetProcAddress(hNTDLL, "wine_get_version");

    ZeroMemory(&osv, sizeof(osv));
    osv.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

    if (GetVersionEx((LPOSVERSIONINFOA)&osv)) return TRUE;

    osv.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if (GetVersionEx((LPOSVERSIONINFOA)&osv)) return TRUE;

    return FALSE;
}

static void windowsPrintVersion(void)
{
    const char *ver = "";

    switch (osv.dwPlatformId)
    {
        case VER_PLATFORM_WIN32_WINDOWS:
            if (osv.dwMinorVersion < 10)
                ver = "95";
            else if (osv.dwMinorVersion < 90)
                ver = "98";
            else
                ver = "ME";
            break;

        case VER_PLATFORM_WIN32_NT:
            switch (osv.dwMajorVersion)
            {
                case 5:
                    switch (osv.dwMinorVersion)
                    {
                        case 0: ver = "2000"; break;
                        case 1: ver = "XP"; break;
                        case 2: ver = osv.wProductType == VER_NT_WORKSTATION ? "XP x64" : "Server 2003"; break;
                    }
                    break;

                case 6:
                    {
                        static const char *client[] = { "Vista", "7", "8", "8.1" };
                        static const char *server[] = { "Server 2008", "Server 2008 R2", "Server 2012", "Server 2012 R2" };
                        ver = ((osv.wProductType == VER_NT_WORKSTATION) ? client : server)[osv.dwMinorVersion % ARRAY_SIZE(client)];
                    }
                    break;

                case 10:
                    switch (osv.wProductType)
                    {
                        case VER_NT_WORKSTATION: ver = "10"; break;
                        default: ver = "Server"; break;
                    }
                    break;
            }
            break;
    }

    char *buf = (char *)Xcalloc(1, 256);
    int len;

    if (ntdll_wine_get_version)
        len = Bsprintf(buf, "Wine %s, identifying as Windows %s", (char *)ntdll_wine_get_version(), ver);
    else
    {
        len = Bsprintf(buf, "Windows %s", ver);

        if (osv.dwPlatformId != VER_PLATFORM_WIN32_NT || osv.dwMajorVersion < 6)
        {
            Bstrcat(buf, " (UNSUPPORTED)");
            len = Bstrlen(buf);
        }
    }

    // service packs
    if (osv.szCSDVersion[0])
    {
        buf[len] = 32;
        Bstrcat(&buf[len], osv.szCSDVersion);
    }

    initprintf("Running on %s (build %lu.%lu.%lu)\n", buf, osv.dwMajorVersion, osv.dwMinorVersion, osv.dwBuildNumber);
    Xfree(buf);
}

//
// win_checkinstance() -- looks for another instance of a Build app
//
int windowsCheckAlreadyRunning(void)
{
    if (!g_singleInstanceSemaphore) return 1;
    return (WaitForSingleObject(g_singleInstanceSemaphore,0) != WAIT_TIMEOUT);
}


typedef void (*dllSetString)(const char*);

//
// win_open(), win_init(), win_setvideomode(), win_close() -- shared code
//
int windowsPreInit(void)
{
    if (!windowsGetVersion())
    {
        windowsShowError("This version of Windows is not supported.");
        return -1;
    }

    windowsGetSystemKeyboardLayout();
    windowsGetSystemKeyboardLayoutName();

#ifdef DEBUGGINGAIDS
    HMODULE ebacktrace = LoadLibraryA(EBACKTRACEDLL);
    if (ebacktrace)
    {
        dllSetString SetTechnicalName = (dllSetString)(void(*))GetProcAddress(ebacktrace, "SetTechnicalName");
        dllSetString SetProperName = (dllSetString)(void(*))GetProcAddress(ebacktrace, "SetProperName");

        if (SetTechnicalName)
            SetTechnicalName(AppTechnicalName);

        if (SetProperName)
            SetProperName(AppProperName);
    }
#endif

    g_singleInstanceSemaphore = CreateSemaphore(NULL, 1,1, WindowClass);

    return 0;
}

static int osdcmd_win_systemtimermode(osdcmdptr_t parm)
{
    int const r = osdcmd_cvar_set(parm);

    if (r != OSDCMD_OK)
        return r;

    windowsSetupTimer(win_systemtimermode);

    return OSDCMD_OK;
}

void windowsPlatformInit(void)
{
    static osdcvardata_t cvars_win[] = {
        { "win_togglecomposition", "disables Windows Vista/7 DWM composition", (void *)&win_togglecomposition, CVAR_BOOL, 0, 1 },

        { "win_priorityclass",
          "Windows process priority class:\n"
          "  -1: do not alter process priority\n"
          "   0: HIGH when game has focus, NORMAL when interacting with other programs\n"
          "   1: NORMAL when game has focus, IDLE when interacting with other programs",
          (void *)&win_priorityclass, CVAR_INT, -1, 1 },

        { "win_performancemode",
          "Windows performance mode:\n"
          "   0: do not alter performance mode\n"
          "   1: use HIGH PERFORMANCE power plan when game has focus",
          (void *)&win_performancemode, CVAR_BOOL, 0, 1 },

    };

    static osdcvardata_t win_timer_cvar = { "win_systemtimermode",
                                            "Windows timer interrupt resolution:\n"
                                            "   0: 1.0ms\n"
                                            "   1: 0.5ms low-latency"
#if defined RENDERTYPESDL && SDL_MAJOR_VERSION >= 2
                                            "\nThis option has no effect when running on battery power.",
#else
                                            ,
#endif
                                            (void *)&win_systemtimermode, CVAR_BOOL, 0, 1 };

    OSD_RegisterCvar(&win_timer_cvar, osdcmd_win_systemtimermode);

    for (int i=0; i<ARRAY_SSIZE(cvars_win); i++)
        OSD_RegisterCvar(&cvars_win[i], osdcmd_cvar_set);

    windowsPrintVersion();
    windowsSetupTimer(0);

    if (osv.dwMajorVersion >= 6)
    {
        if (!hPOWRPROF && (hPOWRPROF = GetModuleHandle("powrprof.dll")))
        {
            powrprof_PowerGetActiveScheme = (PFNPOWERGETACTIVESCHEME)(void(*))GetProcAddress(hPOWRPROF, "PowerGetActiveScheme");
            powrprof_PowerSetActiveScheme = (PFNPOWERSETACTIVESCHEME)(void(*))GetProcAddress(hPOWRPROF, "PowerSetActiveScheme");

            if (powrprof_PowerGetActiveScheme == nullptr || powrprof_PowerSetActiveScheme == nullptr)
                OSD_Printf("ERROR: unable to locate PowerGetActiveScheme or PowerSetActiveScheme symbols in powrprof.dll!\n");
            else if (!systemPowerSchemeGUID)
                powrprof_PowerGetActiveScheme(NULL, &systemPowerSchemeGUID);
        }
    }
}

typedef UINT D3DKMT_HANDLE;
typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;

typedef struct _D3DKMT_OPENADAPTERFROMHDC
{
    HDC           hDc;
    D3DKMT_HANDLE hAdapter;
    LUID          AdapterLuid;

    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;

typedef struct _D3DKMT_CLOSEADAPTER
{
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef struct _D3DKMT_WAITFORVERTICALBLANKEVENT
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;

    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} D3DKMT_WAITFORVERTICALBLANKEVENT;

typedef NTSTATUS(APIENTRY *PFND3DKMTOPENADAPTERFROMHDC)(D3DKMT_OPENADAPTERFROMHDC *);
typedef NTSTATUS(APIENTRY *PFND3DKMTCLOSEADAPTER)(D3DKMT_CLOSEADAPTER *);
typedef NTSTATUS(APIENTRY *PFND3DKMTWAITFORVERTICALBLANKEVENT)(D3DKMT_WAITFORVERTICALBLANKEVENT *);

typedef HRESULT(WINAPI *PFNDWMENABLECOMPOSITION)(UINT);
typedef HRESULT(WINAPI *PFNDWMGETCOMPOSITIONTIMINGINFO)(HWND, DWM_TIMING_INFO *);
typedef HRESULT(WINAPI *PFNDWMISCOMPOSITIONENABLED)(BOOL *);
typedef HRESULT(WINAPI *PFNDWMFLUSH)(void);

static HMODULE hDWMApi;
static PFNDWMFLUSH dwmapi_DwmFlush;
static PFNDWMISCOMPOSITIONENABLED dwmapi_DwmIsCompositionEnabled;

void windowsWaitForVBlank(void)
{
    // if we don't have these, we aren't going to have the WDDM functions below either, so bailing here is fine.
    if (osv.dwMajorVersion < 6 || !dwmapi_DwmFlush || !dwmapi_DwmIsCompositionEnabled)
        return;

    static int useDWMsync;

    // here comes the voodoo bullshit ;)
    static HMODULE hGDI32;
    static PFND3DKMTOPENADAPTERFROMHDC        gdi32_D3DKMTOpenAdapterFromHdc;
    static PFND3DKMTCLOSEADAPTER              gdi32_D3DKMTCloseAdapter;
    static PFND3DKMTWAITFORVERTICALBLANKEVENT gdi32_D3DKMTWaitForVBlank;

    if (!hGDI32 && (hGDI32 = GetModuleHandle("gdi32.dll")))
    {
        gdi32_D3DKMTOpenAdapterFromHdc = (PFND3DKMTOPENADAPTERFROMHDC)        (void(*))GetProcAddress(hGDI32, "D3DKMTOpenAdapterFromHdc");
        gdi32_D3DKMTCloseAdapter       = (PFND3DKMTCLOSEADAPTER)              (void(*))GetProcAddress(hGDI32, "D3DKMTCloseAdapter");
        gdi32_D3DKMTWaitForVBlank      = (PFND3DKMTWAITFORVERTICALBLANKEVENT) (void(*))GetProcAddress(hGDI32, "D3DKMTWaitForVerticalBlankEvent");
    }

    if (useDWMsync || !fullscreen || !gdi32_D3DKMTOpenAdapterFromHdc || !gdi32_D3DKMTCloseAdapter || !gdi32_D3DKMTWaitForVBlank)
    {
dwm:
        // if we don't have the better APIs but composition is enabled, this is sometimes good enough
        BOOL compositionEnabled = false;

        if (SUCCEEDED(dwmapi_DwmIsCompositionEnabled(&compositionEnabled)) && compositionEnabled && dwmapi_DwmFlush() != S_OK)
            OSD_Printf("debug: DWM flush FAILED!\n");

        return;
    }
    
    MONITORINFOEX mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(MonitorFromWindow(win_gethwnd(), MONITOR_DEFAULTTONULL), &mi);

    D3DKMT_OPENADAPTERFROMHDC activeAdapter = {};
    activeAdapter.hDc = CreateDC(mi.szDevice, mi.szDevice, nullptr, nullptr);

    if (activeAdapter.hDc == nullptr)
    {
        OSD_Printf("debug: CreateDC() FAILED! display: %s windowx: %d windowy: %d\n", mi.szDevice, windowx, windowy);
        useDWMsync = 1;
        goto dwm;
    }

    auto status = gdi32_D3DKMTOpenAdapterFromHdc(&activeAdapter);
    DeleteDC(activeAdapter.hDc);

    if (NT_SUCCESS(status))
    {
        D3DKMT_WAITFORVERTICALBLANKEVENT vBlankEvent = { activeAdapter.hAdapter, 0, activeAdapter.VidPnSourceId };

        if (NT_SUCCESS(status = gdi32_D3DKMTWaitForVBlank(&vBlankEvent)))
        {
            // the D3DKMT_CLOSEADAPTER struct only contains one member, and it's
            // the same as the first member in D3DKMT_WAITFORVERTICALBLANKEVENT
            if (NT_SUCCESS(status = gdi32_D3DKMTCloseAdapter((D3DKMT_CLOSEADAPTER *)&vBlankEvent)))
                return;
            else
                OSD_Printf("debug: D3DKMTCloseAdapter() FAILED! NTSTATUS: 0x%x\n", (unsigned)status);
        }
        else
            OSD_Printf("debug: D3DKMTWaitForVerticalBlankEvent() FAILED! NTSTATUS: 0x%x\n", (unsigned)status);
    }
    else
        OSD_Printf("debug: D3DKMTOpenAdapterFromHdc() FAILED! NTSTATUS: 0x%x\n", (unsigned)status);

    OSD_Printf("debug: D3DKMT failure, falling back to DWM sync\n");
    useDWMsync = 1;
    goto dwm;
}

void windowsDwmSetupComposition(int const compEnable)
{
    if (osv.dwMajorVersion < 6)
        return;

    static PFNDWMENABLECOMPOSITION        dwmapi_DwmEnableComposition;
    static PFNDWMGETCOMPOSITIONTIMINGINFO dwmapi_DwmGetCompositionTimingInfo;
    
    if (!hDWMApi && (hDWMApi = GetModuleHandle("dwmapi.dll")))
    {
        dwmapi_DwmEnableComposition        = (PFNDWMENABLECOMPOSITION)        (void(*))GetProcAddress(hDWMApi, "DwmEnableComposition");
        dwmapi_DwmFlush                    = (PFNDWMFLUSH)                    (void(*))GetProcAddress(hDWMApi, "DwmFlush");
        dwmapi_DwmGetCompositionTimingInfo = (PFNDWMGETCOMPOSITIONTIMINGINFO) (void(*))GetProcAddress(hDWMApi, "DwmGetCompositionTimingInfo");
        dwmapi_DwmIsCompositionEnabled     = (PFNDWMISCOMPOSITIONENABLED)     (void(*))GetProcAddress(hDWMApi, "DwmIsCompositionEnabled");
    }

    if (dwmapi_DwmGetCompositionTimingInfo)
    {
        timingInfo = {};
        timingInfo.cbSize = sizeof(DWM_TIMING_INFO);

        // the HWND parameter was deprecated in Windows 8.1 because DWM always syncs to the primary monitor's refresh...

        HRESULT result = dwmapi_DwmGetCompositionTimingInfo(nullptr, &timingInfo);

        if (FAILED(result))
            OSD_Printf("debug: DwmGetCompositionTimingInfo() FAILED! HRESULT: 0x%x (%s)\n", (unsigned)result, std::system_category().message(result).c_str());
    }

    if (win_togglecomposition && dwmapi_DwmEnableComposition && osv.dwMinorVersion < 2)
    {
        dwmapi_DwmEnableComposition(compEnable);

        if (!win_silentvideomodeswitch)
            OSD_Printf("%sabling DWM desktop composition...\n", (compEnable) ? "En" : "Dis");
    }
}

void windowsPlatformCleanup(void)
{
    if (g_singleInstanceSemaphore)
        CloseHandle(g_singleInstanceSemaphore);

    windowsSetKeyboardLayout(windowsGetSystemKeyboardLayoutName());

    if (systemPowerSchemeGUID)
    {
        powrprof_PowerSetActiveScheme(NULL, systemPowerSchemeGUID);
        LocalFree(systemPowerSchemeGUID);
    }
}


//
// GetWindowsErrorMsg() -- gives a pointer to a static buffer containing the Windows error message
//
LPTSTR windowsGetErrorMessage(DWORD code)
{
    static TCHAR lpMsgBuf[1024];

    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, code,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)lpMsgBuf, 1024, NULL);

    return lpMsgBuf;
}


// Keyboard layout switching

static char const * windowsDecodeKeyboardLayoutName(char const * keyboardLayout)
{
    int const   localeID = Bstrtol(keyboardLayout, NULL, 16);
    static char localeName[16];

    int const result = GetLocaleInfo(MAKELCID(localeID, SORT_DEFAULT), LOCALE_SNAME, localeName, ARRAY_SIZE(localeName));

    if (!result)
    {
        OSD_Printf("Error decoding name for locale ID %d: %s\n", localeID, windowsGetErrorMessage(GetLastError()));
        return keyboardLayout;
    }

    return localeName;
}

void windowsSetKeyboardLayout(char const *layout, int focusChanged /*= 0*/)
{
    char layoutName[KL_NAMELENGTH];
    
    GetKeyboardLayoutName(layoutName);

    if (!Bstrcmp(layoutName, layout))
        return;

    //if (!win_silentfocuschange)
    {
        if (focusChanged)
            OSD_Printf("Focus change: ");

        if (layout == enUSLayoutString)
            OSD_Printf("Loaded %s keyboard layout\n", windowsDecodeKeyboardLayoutName(layout));
        else
            OSD_Printf("Restored %s keyboard layout\n", windowsDecodeKeyboardLayoutName(layout));
    }

    static int enUSLoaded;
    static HKL enUSLayout;

    if (layout == enUSLayoutString)
    {
        if (enUSLoaded)
            ActivateKeyboardLayout(enUSLayout, KLF_SETFORPROCESS);
        else if ((enUSLayout = LoadKeyboardLayout(enUSLayoutString, KLF_ACTIVATE | KLF_SETFORPROCESS | KLF_SUBSTITUTE_OK)))
            enUSLoaded = true;
    }
    else
        ActivateKeyboardLayout(windowsGetSystemKeyboardLayout(), KLF_SETFORPROCESS);
}


char *windowsGetSystemKeyboardLayoutName(void)
{
    static char systemLayoutName[KL_NAMELENGTH];
    static int layoutSaved;

    if (!layoutSaved)
    {
        if (!GetKeyboardLayoutName(systemLayoutName))
            OSD_Printf("Error determining system keyboard layout: %s\n", windowsGetErrorMessage(GetLastError()));

        layoutSaved = true;
    }

    return systemLayoutName;
}

HKL windowsGetSystemKeyboardLayout(void)
{
    static HKL systemLayout;
    static int layoutSaved;

    if (!layoutSaved)
    {
        systemLayout = GetKeyboardLayout(0);
        layoutSaved  = true;
    }

    return systemLayout;
}

void windowsHandleFocusChange(int const appactive)
{
#ifndef DEBUGGINGAIDS
    win_silentfocuschange = true;
#endif

    if (appactive)
    {
        if (win_priorityclass != -1)
            SetPriorityClass(GetCurrentProcess(), win_priorityclass ? BELOW_NORMAL_PRIORITY_CLASS : HIGH_PRIORITY_CLASS);

        windowsSetupTimer(win_systemtimermode);
        windowsSetKeyboardLayout(enUSLayoutString, true);

        if (win_performancemode && systemPowerSchemeGUID)
            powrprof_PowerSetActiveScheme(NULL, &GUID_MIN_POWER_SAVINGS);
    }
    else
    {
        if (win_priorityclass != -1)
            SetPriorityClass(GetCurrentProcess(), win_priorityclass ? IDLE_PRIORITY_CLASS : ABOVE_NORMAL_PRIORITY_CLASS);

        windowsSetupTimer(0);
        windowsSetKeyboardLayout(windowsGetSystemKeyboardLayoutName(), true);

        if (systemPowerSchemeGUID)
            powrprof_PowerSetActiveScheme(NULL, systemPowerSchemeGUID);
    }

    win_silentfocuschange = false;
}

//
// ShowErrorBox() -- shows an error message box
//
void windowsShowError(const char *m)
{
    TCHAR msg[1024];

    wsprintf(msg, "%s: %s", m, windowsGetErrorMessage(GetLastError()));
    MessageBox(0, msg, apptitle, MB_OK|MB_ICONSTOP);
}


//
// Miscellaneous
//
int windowsGetCommandLine(char **argvbuf)
{
    int buildargc = 0;

    *argvbuf = Xstrdup(GetCommandLine());

    if (*argvbuf)
    {
        char quoted = 0, instring = 0, swallownext = 0;
        char *wp;
        for (const char *p = wp = *argvbuf; *p; p++)
        {
            if (*p == ' ')
            {
                if (instring)
                {
                    if (!quoted)
                    {
                        // end of a string
                        *(wp++) = 0;
                        instring = 0;
                    }
                    else
                        *(wp++) = *p;
                }
            }
            else if (*p == '"' && !swallownext)
            {
                if (instring)
                {
                    if (quoted && p[1] == ' ')
                    {
                        // end of a string
                        *(wp++) = 0;
                        instring = 0;
                    }
                    quoted = !quoted;
                }
                else
                {
                    instring = 1;
                    quoted = 1;
                    buildargc++;
                }
            }
            else if (*p == '\\' && p[1] == '"' && !swallownext)
                swallownext = 1;
            else
            {
                if (!instring)
                    buildargc++;

                instring = 1;
                *(wp++) = *p;
                swallownext = 0;
            }
        }
        *wp = 0;
    }

    return buildargc;
}

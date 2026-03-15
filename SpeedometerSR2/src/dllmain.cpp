#include "pch.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <cmath>
#include <stdio.h>
#include "MinHook.h"
#include "dllmain.h"

#pragma comment(lib, "d3d9.lib")

static DWORD WINAPI MainThread(LPVOID);

// ─── Memory helpers ───────────────────────────────────────────────────────────

static bool SafeRead4(DWORD addr, DWORD* out) {
    __try { *out = *(DWORD*)addr; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SafeReadF(DWORD addr, float* out) {
    __try { *out = *(float*)addr; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SafeReadStr(DWORD addr, char* buf, int maxLen) {
    __try { strncpy_s(buf, maxLen, (const char*)addr, maxLen - 1); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ─── Game data ────────────────────────────────────────────────────────────────

static const DWORD OBJ_TABLE = 0x02149C64;
static const DWORD VEHICLE_OFF = 0xD74;

static DWORD ResolveVehicle() {
    DWORD playerObj, handle, index, slotAddr, objPtr, storedHandle, state;
    if (!SafeRead4(0x021703D4, &playerObj) || playerObj == 0) return 0;
    if (!SafeRead4(playerObj + VEHICLE_OFF, &handle) || handle == 0) return 0;
    index = handle & 0xFFF;
    slotAddr = OBJ_TABLE + (index * 16);
    if (!SafeRead4(slotAddr, &objPtr) || objPtr == 0) return 0;
    if (!SafeRead4(objPtr + 0x44, &storedHandle) || storedHandle != handle) return 0;
    if (!SafeRead4(objPtr + 0x4C, &state) || state != 5) return 0;
    return objPtr;
}

static float GetSpeedMph(DWORD veh) {
    DWORD phys;
    float vx = 0, vy = 0, vz = 0;
    if (!SafeRead4(veh + 0x554, &phys) || phys == 0) return 0.0f;
    SafeReadF(phys + 0x1A0, &vx);
    SafeReadF(phys + 0x1A4, &vy);
    SafeReadF(phys + 0x1A8, &vz);
    return sqrtf(vx * vx + vy * vy + vz * vz) * 2.237f;
}

static bool GetVehicleName(DWORD veh, char* buf, int maxLen) {
    DWORD wheel;
    if (!SafeRead4(veh + 0x84E4, &wheel) || wheel == 0) return false;
    return SafeReadStr(wheel + 0x80, buf, maxLen);
}

// ─── D3D9 hook ────────────────────────────────────────────────────────────────

typedef HRESULT(APIENTRY* EndScene_t)(IDirect3DDevice9*);
typedef HRESULT(APIENTRY* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

static EndScene_t  oEndScene = nullptr;
static Present_t   oPresent = nullptr;
static ID3DXFont* gFont = nullptr;
static ID3DXSprite* gSprite = nullptr;
static bool        gFontReady = false;

static void InitFont(IDirect3DDevice9* dev) {
    if (gFontReady) return;
    if (FAILED(D3DXCreateSprite(dev, &gSprite))) return;
    if (SUCCEEDED(D3DXCreateFont(
        dev, 22, 0, FW_BOLD, 1, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Arial", &gFont)))
    {
        gFontReady = true;
    }
}

static void DrawString(const char* text, int x, int y, D3DCOLOR color) {
    if (!gFont || !gSprite) return;
    RECT shadow = { x + 1, y + 1, x + 600, y + 32 };
    RECT rc = { x,   y,   x + 600, y + 32 };
    gSprite->Begin(D3DXSPRITE_ALPHABLEND | D3DXSPRITE_SORT_TEXTURE);
    gFont->DrawTextA(gSprite, text, -1, &shadow, DT_LEFT, 0xFF000000);
    gFont->DrawTextA(gSprite, text, -1, &rc, DT_LEFT, color);
    gSprite->End();
}

static HRESULT APIENTRY HookedEndScene(IDirect3DDevice9* dev) {
    InitFont(dev);

    DrawString("TEST OVERLAY", 20, 20, 0xFFFF0000);

    DWORD veh = ResolveVehicle();
    if (veh != 0) {
        char nameBuf[64] = {};
        char speedBuf[32] = {};
        GetVehicleName(veh, nameBuf, 64);
        float mph = GetSpeedMph(veh);
        sprintf_s(speedBuf, "%.1f mph", mph);
        DrawString(nameBuf, 20, 46, 0xFFFFFFFF);
        DrawString(speedBuf, 20, 72, 0xFF00FF88);
    }

    return oEndScene(dev);
}

static HRESULT APIENTRY HookedPresent(IDirect3DDevice9* dev,
    const RECT* src, const RECT* dst, HWND hwnd, const RGNDATA* dirty)
{
    InitFont(dev);

    DrawString("TEST OVERLAY", 20, 20, 0xFFFF0000);

    DWORD veh = ResolveVehicle();
    if (veh != 0) {
        char nameBuf[64] = {};
        char speedBuf[32] = {};
        GetVehicleName(veh, nameBuf, 64);
        float mph = GetSpeedMph(veh);
        sprintf_s(speedBuf, "%.1f mph", mph);
        DrawString(nameBuf, 20, 46, 0xFFFFFFFF);
        DrawString(speedBuf, 20, 72, 0xFF00FF88);
    }

    return oPresent(dev, src, dst, hwnd, dirty);
}

// ─── Hook setup via MinHook ───────────────────────────────────────────────────

static void HookD3D() {
    // Wait for d3d9.dll
    HMODULE hD3D9 = NULL;
    for (int i = 0; i < 200; i++) {
        hD3D9 = GetModuleHandleA("d3d9.dll");
        if (hD3D9) break;
        Sleep(50);
    }
    if (!hD3D9) return;

    // Create temp device to get vtable addresses
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return;

    HWND hwnd = CreateWindowA("STATIC", "tmp", 0, 0, 0, 1, 1,
        NULL, NULL, NULL, NULL);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);

    if (SUCCEEDED(hr) && dev) {
        void** vtable = *(void***)dev;

        void* pEndScene = vtable[42];
        void* pPresent = vtable[17];

        FILE* f = nullptr;
        fopen_s(&f, "sr2hook_debug.txt", "w");
        if (f) {
            fprintf(f, "EndScene addr: 0x%X\n", (DWORD)pEndScene);
            fprintf(f, "Present  addr: 0x%X\n", (DWORD)pPresent);
            fclose(f);
        }

        MH_Initialize();
        MH_CreateHook(pEndScene, &HookedEndScene, (void**)&oEndScene);
        MH_CreateHook(pPresent, &HookedPresent, (void**)&oPresent);
        MH_EnableHook(MH_ALL_HOOKS);

        dev->Release();
    }

    d3d->Release();
    DestroyWindow(hwnd);
}

// ─── Entry point ──────────────────────────────────────────────────────────────

static DWORD WINAPI MainThread(LPVOID) {
    HookD3D();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
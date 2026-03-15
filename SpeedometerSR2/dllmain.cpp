#include "pch.h"
#include "GameMemory.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <cmath>
#include <stdio.h>
#include "MinHook.h"
#include "dllmain.h"
#include "Speedometer.h"

#pragma comment(lib, "d3d9.lib")

static DWORD WINAPI MainThread(LPVOID);

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
    DrawSpeedometer(dev, gSprite);
    return oEndScene(dev);
}

static HRESULT APIENTRY HookedPresent(IDirect3DDevice9* dev,
    const RECT* src, const RECT* dst, HWND hwnd, const RGNDATA* dirty)
{
    InitFont(dev);
    DrawSpeedometer(dev, gSprite);
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
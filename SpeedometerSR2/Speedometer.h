#pragma once
#include "GameMemory.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <string>
#include <unordered_map>
#include <cmath>

// ─── Config ──────────────────────────────────────────────────────────────────

struct SpeedometerConfig {
    float needlePivotX = 7.0f;
    float needlePivotY = 95.0f;
    float angleMin = 210.0f;
    float angleMax = 510.0f;
    float maxSpeed = 140.0f;
    float screenXPct = 0.78f;  // percentage of screen width
    float screenYPct = 0.85f;  // percentage of screen height
    float fade_speed = 2.0f;
};

static bool IsHudHidden() {
    __try {
        return *(BYTE*)0x0252737C != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool IsPaused() {
    __try {
        return *(BYTE*)0x00EBE860 == 20;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static float gAlpha = 0.0f;  // 0.0 = invisible, 1.0 = fully visible
static bool  gWasInVehicle = false;
static DWORD gLastTick = 0;

static std::unordered_map<std::string, SpeedometerConfig> gConfigs;
static SpeedometerConfig gDefaultConfig;

static void TrimString(std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

static void LoadConfig(const std::string& basePath) {
    std::string path = basePath + "textures\\speedometer.ini";
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "r");
    if (!f) return;

    char line[256];
    std::string section = "default";
    SpeedometerConfig cfg;

    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        TrimString(s);
        if (s.empty() || s[0] == ';') continue;

        if (s[0] == '[') {
            // Save previous section
            if (section == "default") gDefaultConfig = cfg;
            else                      gConfigs[section] = cfg;

            // Start new section — inherit from default
            section = s.substr(1, s.find(']') - 1);
            TrimString(section);
            cfg = gDefaultConfig;
            continue;
        }

        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        TrimString(key);
        TrimString(val);
        float v = (float)atof(val.c_str());

        if (key == "needle_pivot_x")  cfg.needlePivotX = v;
        else if (key == "needle_pivot_y")  cfg.needlePivotY = v;
        else if (key == "needle_angle_min") cfg.angleMin = v;
        else if (key == "needle_angle_max") cfg.angleMax = v;
        else if (key == "max_speed") cfg.maxSpeed = v;
        else if (key == "speedometer_x") cfg.screenXPct = v;
        else if (key == "speedometer_y") cfg.screenYPct = v;
        else if (key == "fade_speed") cfg.fade_speed = v;
    }

    // Save last section
    if (section == "default") gDefaultConfig = cfg;
    else                      gConfigs[section] = cfg;

    fclose(f);
}

static const SpeedometerConfig& GetConfig(const std::string& vehicleName) {
    auto it = gConfigs.find(vehicleName);
    return (it != gConfigs.end()) ? it->second : gDefaultConfig;
}

// ─── Texture cache ────────────────────────────────────────────────────────────

struct VehicleTextures {
    IDirect3DTexture9* background = nullptr;
    IDirect3DTexture9* needle = nullptr;
    D3DSURFACE_DESC    bgDesc = {};
    D3DSURFACE_DESC    ndDesc = {};
};

static std::unordered_map<std::string, VehicleTextures> gTextureCache;
static std::string gCurrentVehicle;
static std::string gBasePath;

static void GetBasePath() {
    if (!gBasePath.empty()) return;
    char path[MAX_PATH];
    HMODULE hMod = NULL;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&GetBasePath, &hMod);
    GetModuleFileNameA(hMod, path, MAX_PATH);
    char* p = strrchr(path, '\\');
    if (p) p[1] = '\0';
    gBasePath = path;
}

static VehicleTextures* GetOrLoadTextures(IDirect3DDevice9* dev, const std::string& vehicleName) {
    auto it = gTextureCache.find(vehicleName);
    if (it != gTextureCache.end()) return &it->second;

    VehicleTextures tex;
    std::string bgPath = gBasePath + "textures\\" + vehicleName + "_background.png";
    std::string ndPath = gBasePath + "textures\\" + vehicleName + "_needle.png";

    // Try vehicle-specific, fall back to default
    if (FAILED(D3DXCreateTextureFromFileA(dev, bgPath.c_str(), &tex.background))) {
        std::string defBg = gBasePath + "textures\\default_background.png";
        D3DXCreateTextureFromFileA(dev, defBg.c_str(), &tex.background);
    }
    if (FAILED(D3DXCreateTextureFromFileA(dev, ndPath.c_str(), &tex.needle))) {
        std::string defNd = gBasePath + "textures\\default_needle.png";
        D3DXCreateTextureFromFileA(dev, defNd.c_str(), &tex.needle);
    }

    // Get texture dimensions
    if (tex.background) {
        IDirect3DSurface9* surf = nullptr;
        tex.background->GetSurfaceLevel(0, &surf);
        if (surf) { surf->GetDesc(&tex.bgDesc); surf->Release(); }
    }
    if (tex.needle) {
        IDirect3DSurface9* surf = nullptr;
        tex.needle->GetSurfaceLevel(0, &surf);
        if (surf) { surf->GetDesc(&tex.ndDesc); surf->Release(); }
    }

    gTextureCache[vehicleName] = tex;
    return &gTextureCache[vehicleName];
}

// ─── Speedometer data ─────────────────────────────────────────────────────────

struct SpeedometerData {
    bool  inVehicle = false;
    float speedMph = 0.0f;
    float speedKmh = 0.0f;
    char  vehicleName[64] = {};
};

static SpeedometerData GetSpeedometerData() {
    SpeedometerData data = {};
    DWORD veh = ResolveVehicle();
    if (veh == 0) return data;

    data.inVehicle = true;
    data.speedMph = GetSpeedMph(veh);
    data.speedKmh = data.speedMph * 1.60934f;
    GetVehicleName(veh, data.vehicleName, 64);
    return data;
}

// ─── Main draw function ───────────────────────────────────────────────────────

static void UpdateFade(bool inVehicle) {
    DWORD now = GetTickCount();
    float dt = (gLastTick == 0) ? 0.0f : (now - gLastTick) / 1000.0f;
    gLastTick = now;
    const SpeedometerConfig& cfg = GetConfig("default");

    if (inVehicle) {
        gAlpha += cfg.fade_speed * dt;
        if (gAlpha > 1.0f) gAlpha = 1.0f;
    }
    else {
        gAlpha -= cfg.fade_speed * dt;
        if (gAlpha < 0.0f) gAlpha = 0.0f;
    }
}

static std::string gLastVehicleName;

static void DrawSpeedometer(IDirect3DDevice9* dev, ID3DXSprite* sprite) {
    if (!sprite) return;
    GetBasePath();
    static bool configLoaded = false;
    if (!configLoaded) {
        LoadConfig(gBasePath);
        configLoaded = true;
    }

    SpeedometerData spd = GetSpeedometerData();
    bool visible = spd.inVehicle && IsHudHidden() && !IsPaused() && spd.speedMph > 5.0f;
    UpdateFade(visible);

    if (gAlpha <= 0.0f) return;

    DWORD color = (DWORD)(gAlpha * 255) << 24 | 0x00FFFFFF;

    // Update last known vehicle name only when in vehicle
    if (spd.inVehicle && strlen(spd.vehicleName) > 0)
        gLastVehicleName = std::string(spd.vehicleName);

    // Use last known vehicle name during fade out
    std::string vehName = gLastVehicleName.empty()
        ? std::string(spd.vehicleName)
        : gLastVehicleName;

    VehicleTextures* tex = GetOrLoadTextures(dev, vehName);
    if (!tex) return;

    const SpeedometerConfig& cfg = GetConfig(vehName);

    // rest of function unchanged...

    // Get viewport dimensions
    D3DVIEWPORT9 vp;
    dev->GetViewport(&vp);

    float scale = min(vp.Width / 1440.0f, vp.Height / 900.0f);

    int screenX = (int)(vp.Width * cfg.screenXPct);
    int screenY = (int)(vp.Height * cfg.screenYPct);

    float needleAngleDeg = cfg.angleMin + (spd.speedMph * (cfg.angleMax - cfg.angleMin) / cfg.maxSpeed);
    float needleAngleRad = needleAngleDeg * (D3DX_PI / 180.0f);

    sprite->Begin(D3DXSPRITE_ALPHABLEND | D3DXSPRITE_SORT_TEXTURE);

    if (tex->background) {
        float bgW = (float)tex->bgDesc.Width * scale;
        float bgH = (float)tex->bgDesc.Height * scale;
        D3DXVECTOR2 bgPos((float)screenX - bgW * 0.5f,
            (float)screenY - bgH * 0.5f);
        D3DXVECTOR2 zero(0, 0);
        D3DXVECTOR2 scaleVec(scale, scale);
        D3DXMATRIX mat;
        D3DXMatrixTransformation2D(&mat, &zero, 0.0f, &scaleVec, &zero, 0.0f, &bgPos);
        sprite->Begin(D3DXSPRITE_ALPHABLEND);
        sprite->SetTransform(&mat);
        sprite->Draw(tex->background, nullptr, nullptr, nullptr, color);
        sprite->End();
    }

    if (tex->needle) {
        D3DXMATRIX matScale, matR, matT1, matT2, mat;

        // Move pivot to origin (unscaled pixel coordinates)
        D3DXMatrixTranslation(&matT1, -cfg.needlePivotX, -cfg.needlePivotY, 0.0f);

        // Scale uniformly
        D3DXMatrixScaling(&matScale, scale, scale, 1.0f);

        // Rotate
        D3DXMatrixRotationZ(&matR, needleAngleRad);

        // Move to screen position
        D3DXMatrixTranslation(&matT2, (float)screenX, (float)screenY, 0.0f);

        // Order: pivot to origin -> scale -> rotate -> move to screen
        mat = matT1 * matScale * matR * matT2;

        sprite->Begin(D3DXSPRITE_ALPHABLEND);
        sprite->SetTransform(&mat);
        sprite->Draw(tex->needle, nullptr, nullptr, nullptr, color);
        sprite->End();
    }

    // Reset transform
    D3DXMATRIX identity;
    D3DXMatrixIdentity(&identity);
    sprite->Begin(D3DXSPRITE_ALPHABLEND);
    sprite->SetTransform(&identity);
    sprite->End();
}
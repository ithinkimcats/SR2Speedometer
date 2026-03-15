#pragma once
#include <Windows.h>
#include <cmath>

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
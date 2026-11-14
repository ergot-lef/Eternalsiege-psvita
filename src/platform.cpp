// Eternal Siege Vita - plateforme : journal, entrées, horloge
#include "game.h"
#include <cstdarg>
#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/power.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

Input g_in;
static FILE* s_log = nullptr;

const char* dataDir() { return "ux0:data/EternalSiege"; }

void logf(const char* fmt, ...) {
#ifdef HOST_TEST
    { va_list a2; va_start(a2, fmt); vprintf(fmt, a2); va_end(a2); printf("\n"); }
#endif
    if (!s_log) {
        sceIoMkdir("ux0:data/EternalSiege", 0777);
        s_log = fopen("ux0:data/EternalSiege/log.txt", "w");
        if (!s_log) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(s_log, fmt, ap);
    va_end(ap);
    fputc('\n', s_log);
    fflush(s_log);
}

double timeNow() {
    return sceKernelGetProcessTimeWide() / 1000000.0;
}

void platformInit() {
    // Pleine puissance : CPU 444 MHz, GPU 222 MHz
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceIoMkdir("ux0:data/EternalSiege", 0777);
    memset(&g_in, 0, sizeof(g_in));
}

static float stickAxis(int v) {
    float f = (v - 128) / 127.0f;
    const float dead = 0.18f;
    if (fabsf(f) < dead) return 0;
    float s = f > 0 ? 1.0f : -1.0f;
    f = (fabsf(f) - dead) / (1 - dead);
    if (f > 1) f = 1;
    return s * f;
}

void inputUpdate() {
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t b = 0;
    if (pad.buttons & SCE_CTRL_CROSS) b |= B_CROSS;
    if (pad.buttons & SCE_CTRL_CIRCLE) b |= B_CIRCLE;
    if (pad.buttons & SCE_CTRL_SQUARE) b |= B_SQUARE;
    if (pad.buttons & SCE_CTRL_TRIANGLE) b |= B_TRIANGLE;
    if (pad.buttons & SCE_CTRL_LTRIGGER) b |= B_L;
    if (pad.buttons & SCE_CTRL_RTRIGGER) b |= B_R;
    if (pad.buttons & SCE_CTRL_START) b |= B_START;
    if (pad.buttons & SCE_CTRL_SELECT) b |= B_SELECT;
    if (pad.buttons & SCE_CTRL_UP) b |= B_UP;
    if (pad.buttons & SCE_CTRL_DOWN) b |= B_DOWN;
    if (pad.buttons & SCE_CTRL_LEFT) b |= B_LEFT;
    if (pad.buttons & SCE_CTRL_RIGHT) b |= B_RIGHT;
    g_in.pressed = b & ~g_in.held;
    g_in.released = g_in.held & ~b;
    g_in.held = b;
    g_in.lx = stickAxis(pad.lx);
    g_in.ly = stickAxis(pad.ly);
    g_in.rx = stickAxis(pad.rx);
    g_in.ry = stickAxis(pad.ry);
}

// Eternal Siege Vita - point d'entrée et boucle principale
#include "game.h"
#include <vitaGL.h>

// tas de 200 Mo pour le jeu (newlib)
extern "C" {
int _newlib_heap_size_user = 200 * 1024 * 1024;
}

Rng g_rng(0xC0FFEE);
float g_time = 0;
float g_dtReal = 0;

static void renderWorld() {
    Player& p = g_player;
    Camera& c = g_cam;
    // caméra : tête, balancement, secousses, recul
    float bob = sinf(p.bob * 2) * 0.05f * p.bobAmp;
    float sh = p.shake;
    c.pos = playerEye() + vec3(0, bob - p.landT * 0.3f, 0);
    if (p.dead) c.pos.y -= clampf(p.deadT, 0, 1) * 1.2f;
    c.yaw = p.yaw + (frand() - 0.5f) * sh * 0.05f;
    c.pitch = p.pitch + p.recoil * 0.05f + (frand() - 0.5f) * sh * 0.05f;
    c.roll = sinf(p.bob) * 0.006f * p.bobAmp + (p.dead ? clampf(p.deadT, 0, 1) * 0.6f : 0);
    c.fov = lerpf(1.2f, 0.62f, p.zoom);
    if (p.station >= 0 && g_stations[p.station].kind == ST_CANNON) c.fov = 0.8f;
    audioListener(c.pos, c.yaw);

    gfxBeginFrame();
    cameraApply();
    skyDraw(g_time);
    applySceneLighting();
    lightsApply();
    terrainDraw();
    castleDraw();
    // géométrie dynamique
    treesDraw(g_time, g_weather.wind);
    charsDraw();
    bodiesDraw();
    vehiclesDraw();
    stationsDraw();
    projsDraw();
    birdsDraw();
    grassDraw(g_time, g_weather.wind);
    batchesFlushOpaque();
    decalsDraw();
    shadowsDraw();
    waterDraw(g_time);
    fxDraw();
    rainUpdateDraw(g_dtReal);
    batchesFlushAlpha();
    playerDrawViewmodel();
}

int main() {
    logf("Eternal Siege Vita - démarrage");
    platformInit();
    gfxInit();
    texturesInit();
    audioInit();
    fxInit();
    weatherInit();
    g_rng = Rng((uint32_t)(timeNow() * 1000) | 1);
    // écran de chargement minimal
    begin2D();
    drawText(480, 260, "Chargement du champ de bataille...", 2, rgba(230, 220, 190), 1);
    end2D();
    gfxEndFrame();
    if (!gameLoad()) gameNew(false);
    logf("monde prêt");
    double last = timeNow();
    for (;;) {
        double now = timeNow();
        float dt = (float)(now - last);
        last = now;
        // la console en veille suspend le jeu : on borne le pas de temps
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.001f) dt = 0.001f;
        g_dtReal = dt;
        inputUpdate();
        if (g_in.pressed & B_START) { g_game.paused = !g_game.paused; g_game.menuSel = 0; }
        if (!g_game.paused) {
            if (g_in.pressed & B_SELECT) {
                g_game.timeSpeedIdx = (g_game.timeSpeedIdx + 1) % 6;
                char s[48];
                snprintf(s, sizeof(s), "Écoulement du temps x%d", (int)timeScale());
                gameMessage(s, "Années, jours et répits défilent plus vite", 2);
            }
            g_time += dt;
            gameUpdate(dt);
        } else {
            musicSet(0, g_game.era, true);
        }
        renderWorld();
        if (g_game.paused) menuUpdateDraw();
        else hudDraw();
        gfxEndFrame();
    }
    return 0;
}

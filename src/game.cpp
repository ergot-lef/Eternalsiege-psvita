// Eternal Siege Vita - ères, vagues, persistance, interface
#include "game.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <cstdlib>

GameState g_game;
extern char g_prompt[96];
const float g_timeSpeeds[6] = {1, 2, 4, 8, 16, 64};

const EraDef g_eras[9] = {
    {"Ère médiévale", 1000}, {"Moyen Âge tardif", 1300}, {"Renaissance", 1500},
    {"Ère baroque", 1650}, {"Ère industrielle", 1800}, {"Grande Guerre", 1914},
    {"Guerre froide", 1945}, {"Ère moderne", 1990}, {"Ère contemporaine", 2020},
};

float timeScale() { return g_timeSpeeds[clampi(g_game.timeSpeedIdx, 0, 5)]; }

int eraForYear(double y) {
    int e = 0;
    for (int i = 0; i < 9; i++) if (y >= g_eras[i].year) e = i;
    return e;
}

void gameMessage(const char* a, const char* b, float dur) {
    snprintf(g_game.msg, sizeof(g_game.msg), "%s", a);
    snprintf(g_game.sub, sizeof(g_game.sub), "%s", b);
    g_game.msgT = dur;
#ifdef HOST_TEST
    logf("MSG [%.0f] %s / %s", g_game.playTime, a, b);
#endif
}

void gameNew(bool keepHistory) {
    int resets = keepHistory ? g_game.resets + 1 : 0;
    int hc = keepHistory ? g_game.historyCount : 0;
    int hw[16], hy[16];
    memcpy(hw, g_game.histWave, sizeof(hw));
    memcpy(hy, g_game.histYear, sizeof(hy));
    int best = keepHistory ? g_game.bestWave : 0;
    int ts = keepHistory ? g_game.timeSpeedIdx : 0;
    if (keepHistory) {
        // archive l'ère qui vient de s'achever
        if (hc < 16) { hw[hc] = g_game.wave; hy[hc] = (int)g_game.year; hc++; }
        else { memmove(hw, hw + 1, sizeof(int) * 15); memmove(hy, hy + 1, sizeof(int) * 15); hw[15] = g_game.wave; hy[15] = (int)g_game.year; }
    }
    memset(&g_game, 0, sizeof(g_game));
    g_game.resets = resets;
    g_game.historyCount = hc;
    memcpy(g_game.histWave, hw, sizeof(hw));
    memcpy(g_game.histYear, hy, sizeof(hy));
    g_game.bestWave = best;
    g_game.timeSpeedIdx = ts;
    g_game.year = 1000;
#ifdef HOST_TEST
    if (getenv("YEAR")) g_game.year = atof(getenv("YEAR"));
#endif
    g_game.era = 0;
    g_game.wave = 0;
    g_game.waveState = WS_INTER;
    g_game.waveTimer = 45;
    g_game.siegeT = 30;
    g_game.airT = 60;
    terrainGen();
    terrainBuildMesh();
    castleGen();
    treesGen();
    propsGen();
    charsInit();
    fxInit();
    birdsInit();
    g_projs.clear();
    g_stuck.clear();
    g_vehicles.clear();
    stationsGen(0);
    weatherInit();
    playerInit();
    alliesSpawn();
    char sub[96];
    snprintf(sub, sizeof(sub), "Ère n°%d - An 1000", resets + 1);
    gameMessage(keepHistory ? "Une nouvelle ère commence" : "Défendez le château", sub, 6);
    sndPlay2D(S_BELL, 0.8f, 1);
}

// ---------------------------------------------------------------- Composition des vagues
static int pickType(int era) {
    struct W { int t, w; };
    static const W tbl[9][6] = {
        {{CT_SWORD, 35}, {CT_SPEAR, 25}, {CT_AXE, 15}, {CT_ARCHER, 25}, {CT_KNIGHT, 6}, {CT_SWORD, 0}},
        {{CT_SWORD, 25}, {CT_AXE, 10}, {CT_KNIGHT, 15}, {CT_CROSSBOW, 25}, {CT_ARCHER, 15}, {CT_PIKE, 10}},
        {{CT_PIKE, 25}, {CT_SWORD, 15}, {CT_ARQUEBUS, 30}, {CT_CROSSBOW, 15}, {CT_KNIGHT, 15}, {CT_SWORD, 0}},
        {{CT_MUSKET, 45}, {CT_GRENADIER, 20}, {CT_PIKE, 10}, {CT_SWORD, 25}, {CT_SWORD, 0}, {CT_SWORD, 0}},
        {{CT_RIFLE, 55}, {CT_MUSKET, 15}, {CT_GRENADIER, 15}, {CT_SWORD, 15}, {CT_SWORD, 0}, {CT_SWORD, 0}},
        {{CT_RIFLE, 50}, {CT_MG, 15}, {CT_GRENADIER, 15}, {CT_FLAMER, 10}, {CT_SWORD, 10}, {CT_SWORD, 0}},
        {{CT_ASSAULT, 50}, {CT_RPG, 15}, {CT_MG, 15}, {CT_GRENADIER, 10}, {CT_RIFLE, 10}, {CT_SWORD, 0}},
        {{CT_ASSAULT, 55}, {CT_RPG, 20}, {CT_MG, 15}, {CT_GRENADIER, 10}, {CT_SWORD, 0}, {CT_SWORD, 0}},
        {{CT_ASSAULT, 50}, {CT_RPG, 25}, {CT_MG, 15}, {CT_GRENADIER, 10}, {CT_SWORD, 0}, {CT_SWORD, 0}},
    };
    int total = 0;
    for (int i = 0; i < 6; i++) total += tbl[era][i].w;
    int r = g_rng.irange(0, total - 1);
    for (int i = 0; i < 6; i++) { if (r < tbl[era][i].w) return tbl[era][i].t; r -= tbl[era][i].w; }
    return CT_SWORD;
}

static vec3 spawnPoint() {
    float r = frand();
    float x, z;
    if (r < 0.75f || g_game.wave < 5) { x = frange(-70, 70); z = frange(112, 122); }
    else { x = frand() < 0.5f ? frange(-115, -95) : frange(95, 115); z = frange(80, 115); }
    return vec3(x, terrainHeight(x, z), z);
}

static void startWave() {
    GameState& g = g_game;
    g.wave++;
    if (g.wave > g.bestWave) g.bestWave = g.wave;
    g.waveState = WS_ASSAULT;
    g.waveTimer = 0;
    g.toSpawn = clampi((int)((10 + g.wave * 2) * (1 + g.era * 0.08f)), 10, 160);
    g.spawned = 0;
    g.killed = 0;
    g.spawnT = 2;
    g.siegeT = frange(8, 20);
    g.airT = frange(25, 45);
    char a[64], b[96];
    snprintf(a, sizeof(a), "Vague %d", g.wave);
    snprintf(b, sizeof(b), "%d assaillants approchent", g.toSpawn);
    gameMessage(a, b, 4);
    sndPlay2D(g.era >= 4 ? S_BELL : S_HORN, 1, 1);
    // engins de siège de l'ère
    int e = g.era;
    if (e <= 1 && (g.wave % 3 == 0 || g.wave > 8)) vehicleSpawn(VK_RAM, vec3(frange(-10, 10), 0, 118));
    if (e == 0 && g.wave >= 5) vehicleSpawn(VK_CATAPULT, vec3(frange(-50, 50), 0, 120));
    if (e == 1) { vehicleSpawn(VK_TREBUCHET, vec3(frange(-40, 40), 0, 122)); if (g.wave > 6) vehicleSpawn(VK_CATAPULT, vec3(frange(-60, 60), 0, 120)); }
    if (e >= 2 && e <= 4) {
        int n = 1 + (g.wave > 10) + (e >= 3);
        for (int i = 0; i < n; i++) vehicleSpawn(VK_CANNON, vec3(frange(-60, 60), 0, 120));
        if (e == 2 && frand() < 0.5f) vehicleSpawn(VK_RAM, vec3(frange(-10, 10), 0, 118));
    }
    if (e >= 5) {
        int n = 1 + (g.wave > 12) + (e >= 7);
        for (int i = 0; i < n; i++) vehicleSpawn(VK_TANK, vec3(frange(-60, 60), 0, 122));
    }
    for (auto& v : g_vehicles) if (v.alive && !v.wreck && v.kind <= VK_TANK) v.pos.y = terrainHeight(v.pos.x, v.pos.z);
}

static void directorUpdate(float dt) {
    GameState& g = g_game;
    float ts = timeScale();
    switch (g.waveState) {
    case WS_INTER:
        g.waveTimer -= dt * ts;
        if (g.waveTimer <= 0) startWave();
        break;
    case WS_ASSAULT: {
        g.waveTimer += dt;
        g.spawnT -= dt;
        int alive = countAlive(TEAM_ENEMY);
        if (g.spawned < g.toSpawn && g.spawnT <= 0 && alive < 55) {
            g.spawnT = frange(0.4f, 1.1f);
            vec3 p = spawnPoint();
            int group = g_rng.irange(1, 3);
            for (int k = 0; k < group && g.spawned < g.toSpawn; k++) {
                vec3 q = p + vec3(frange(-3, 3), 0, frange(-2, 2));
                q.y = terrainHeight(q.x, q.z);
                if (charSpawn(TEAM_ENEMY, pickType(g.era), q, g.era) >= 0) g.spawned++;
            }
        }
        // appuis : artillerie lointaine, aviation, drones
        g.siegeT -= dt;
        if (g.era >= 4 && g.siegeT <= 0) {
            g.siegeT = frange(5, 11) / (1 + g.era * 0.1f);
            vec3 tgt(frange(-30, 30), 18, frange(-30, 32));
            if (frand() < 0.3f) tgt = g_player.pos;
            tgt += vec3(frange(-6, 6), 0, frange(-6, 6));
            projSpawn(PK_SHELL, tgt + vec3(frange(-10, 10), 110, 60), vec3(0, -40, -22), TEAM_ENEMY, 0, false);
        }
        g.airT -= dt;
        if (g.era >= 5 && g.airT <= 0) {
            g.airT = frange(35, 70) / (1 + (g.era - 5) * 0.3f);
            if (g.era >= 7 && frand() < 0.6f) {
                int n = g.era == 8 ? g_rng.irange(4, 8) : g_rng.irange(2, 4);
                for (int i = 0; i < n; i++) vehicleSpawn(VK_DRONE, vec3(frange(-60, 60), 22, frange(110, 125)));
            } else if (g.era >= 6 && frand() < 0.4f) {
                vehicleSpawn(VK_HELI, vec3(frange(-50, 50), 30, 125));
            } else {
                vehicleSpawn(VK_PLANE, vec3(frange(-30, 30), 50, 140));
            }
            if (g.era == 8 && frand() < 0.4f) {
                vec3 tgt(frange(-20, 20), 15, frange(-20, 25));
                projSpawn(PK_MISSILE, tgt + vec3(0, 140, 90), vec3(0, -60, -38), TEAM_ENEMY, 0, false);
            }
        }
        int remaining = (g.toSpawn - g.spawned) + alive;
        if ((g.spawned >= g.toSpawn && alive <= g.toSpawn / 4) || g.waveTimer > 420) {
            g.waveState = WS_RETREAT;
            g.waveTimer = 0;
            for (auto& c : g_chars) if (c.alive && c.team == TEAM_ENEMY && c.state != CS_CLIMB) c.state = CS_FLEE;
            gameMessage("L'ennemi se replie !", "", 3);
            sndPlay2D(S_CHEER, 0.9f, 1);
        }
        (void)remaining;
        break;
    }
    case WS_RETREAT:
        g.waveTimer += dt;
        if (g.waveTimer > 20 || countAlive(TEAM_ENEMY) == 0) {
            g.waveState = WS_INTER;
            g.waveTimer = clampf(80 + g.wave * 0.5f, 80, 140);
            g_player.stone = 60;
            if (g.era >= 5) g_player.grenades = 4;
            alliesSpawn();
            // les ennemis restants disparaissent au loin
            for (auto& c : g_chars) if (c.alive && c.team == TEAM_ENEMY) { c.used = false; c.alive = false; }
            for (auto& v : g_vehicles) if (v.alive && !v.wreck) v.alive = false;
            gameMessage("Répit", "Réparez les murs (○) et préparez-vous", 4);
            gameSave();
        }
        break;
    case WS_FALLEN:
        g.fallenT += dt;
        if (g.fallenT > 9) gameNew(true);
        break;
    }
    // prise du donjon : des assaillants à l'intérieur font monter la jauge
    if (g.waveState != WS_FALLEN) {
        int inside = 0, defenders = 0;
        for (auto& c : g_chars)
            if (c.alive && fabsf(c.pos.x) < 8.5f && fabsf(c.pos.z) < 8.5f && c.pos.y < VOX0Y + 8) { if (c.team == TEAM_ENEMY) inside++; else defenders++; }
        if (!g_player.dead && fabsf(g_player.pos.x) < 12 && fabsf(g_player.pos.z) < 12) defenders += 2;
        float rate = (inside > 2 ? (inside - 2) * 0.6f : 0.0f) - defenders * 1.0f - 1.0f;
        g.keepCapture = clampf(g.keepCapture + rate * dt, 0, 100);
    }
    // chute du château
    if (g.waveState != WS_FALLEN && (keepIntegrity() < 0.55f || g.keepCapture >= 100)) {
        g.waveState = WS_FALLEN;
        g.fallenT = 0;
        char sub[96];
        snprintf(sub, sizeof(sub), "Tenu jusqu'à la vague %d, an %d. L'ère %d commence.", g.wave, (int)g.year, g.resets + 2);
        gameMessage("Le château est tombé", sub, 9);
        sndPlay2D(S_BELL, 1, 0.7f);
        sndPlay2D(S_COLLAPSE, 1, 0.6f);
    }
}

void gameUpdate(float dt) {
    GameState& g = g_game;
    float ts = timeScale();
    g.playTime += dt;
    g.msgT -= dt;
    // l'horloge historique : 1 heure réelle = 10 ans (accélérable)
    if (g.waveState != WS_FALLEN) g.year = fmin(2100.0, g.year + dt * ts * (10.0 / 3600.0));
    int era = eraForYear(g.year);
    if (era != g.era) {
        g.era = era;
        playerSetupLoadout(era);
        stationsGen(era);
        alliesSpawn();
        char sub[96];
        snprintf(sub, sizeof(sub), "An %d - nouvelles armes disponibles", (int)g.year);
        gameMessage(g_eras[era].name, sub, 6);
        sndPlay2D(S_BELL, 0.9f, 1.2f);
    }
    lightsReset();
    weatherUpdate(dt, ts);
    directorUpdate(dt);
    playerUpdate(dt);
    charsUpdate(dt);
    vehiclesUpdate(dt);
    projsUpdate(dt);
    bodiesUpdate(dt);
    castleUpdate(dt);
    treesUpdate(dt, g_weather.wind);
    birdsUpdate(dt);
    stationsUpdate(dt);
    fxUpdate(dt);
    terrainUpdate(dt);
    // musique : intensité selon la menace proche
    int near = 0;
    for (auto& c : g_chars) if (c.alive && c.team == TEAM_ENEMY && length(c.pos - g_player.pos) < 60) near++;
    float inten = g.waveState == WS_ASSAULT ? clampf(0.35f + near / 25.0f, 0, 1) : 0.0f;
    musicSet(inten, g.era, g.paused);
#ifdef HOST_TEST
    {
        static float dbgT = 0;
        dbgT += dt;
        extern long g_tris;
        static long lastTris = 0;
        if (dbgT > 10) {
            dbgT = 0;
            int nb = 0; for (auto& b : g_bodies) if (b.alive) nb++;
            int nv = 0; for (auto& v : g_vehicles) if (v.alive) nv++;
            logf("[t=%.0f] an %d ere %d vague %d etat %d ennemis %d allies %d corps %d vehicules %d chateau %.2f donjon %.2f tris/frame %ld joueur hp %.0f pos %.1f %.1f %.1f speed x%d",
                 g.playTime, (int)g.year, g.era, g.wave, g.waveState, countAlive(TEAM_ENEMY), countAlive(TEAM_ALLY), nb, nv,
                 castleIntegrity(), keepIntegrity(), (g_tris - lastTris) / 300, g_player.hp, g_player.pos.x, g_player.pos.y, g_player.pos.z, (int)timeScale());
            lastTris = g_tris;
            int shown = 0;
            for (auto& c : g_chars) if (c.alive && c.team == TEAM_ENEMY && shown < 6) { shown++; logf("   ennemi type %d etat %d pos %.1f %.1f %.1f stuck %.1f", c.type, c.state, c.pos.x, c.pos.y, c.pos.z, c.stuckT); }
        }
    }
#endif
    g.autosaveT += dt;
    if (g.autosaveT > 180) { g.autosaveT = 0; gameSave(); }
}

// ---------------------------------------------------------------- Sauvegarde
static const uint32_t SAVE_MAGIC = 0x45535631;  // "ESV1"
static const uint32_t SAVE_VER = 3;

bool gameSave() {
    char path[128];
    snprintf(path, sizeof(path), "%s/save.bin", dataDir());
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s/save.tmp", dataDir());
    FILE* f = fopen(tmp, "wb");
    if (!f) { logf("sauvegarde impossible"); return false; }
    fwrite(&SAVE_MAGIC, 4, 1, f);
    fwrite(&SAVE_VER, 4, 1, f);
    GameState gs = g_game;
    gs.paused = false;
    fwrite(&gs, sizeof(gs), 1, f);
    fwrite(&g_weather, sizeof(g_weather), 1, f);
    fwrite(&g_player.stone, sizeof(int), 1, f);
    fwrite(g_height, sizeof(float), TER_N * TER_N, f);
    castleSaveExtra(f);
    uint32_t nt = (uint32_t)g_trees.size();
    fwrite(&nt, 4, 1, f);
    if (nt) fwrite(g_trees.data(), sizeof(Tree), nt, f);
    uint32_t nd = (uint32_t)g_decals.size();
    fwrite(&nd, 4, 1, f);
    if (nd) fwrite(g_decals.data(), sizeof(Decal), nd, f);
    uint32_t nb = 0;
    for (auto& b : g_bodies) if (b.alive && !b.held) nb++;
    fwrite(&nb, 4, 1, f);
    for (auto& b : g_bodies) if (b.alive && !b.held) { Body c = b; c.sleeping = true; c.vel = vec3(0, 0, 0); c.angVel = vec3(0, 0, 0); c.burn = 0; fwrite(&c, sizeof(Body), 1, f); }
    uint32_t ns = (uint32_t)g_stuck.size();
    fwrite(&ns, 4, 1, f);
    if (ns) fwrite(g_stuck.data(), sizeof(Stuck), ns, f);
    fclose(f);
    sceIoRemove(path);
    sceIoRename(tmp, path);
    logf("sauvegarde ok (année %d, vague %d)", (int)g_game.year, g_game.wave);
    return true;
}

bool gameLoad() {
    char path[128];
    snprintf(path, sizeof(path), "%s/save.bin", dataDir());
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint32_t magic = 0, ver = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != SAVE_MAGIC || fread(&ver, 4, 1, f) != 1 || ver != SAVE_VER) { fclose(f); logf("sauvegarde incompatible"); return false; }
    GameState gs;
    if (fread(&gs, sizeof(gs), 1, f) != 1) { fclose(f); return false; }
    // reconstruit le monde puis applique l'état sauvegardé
    terrainGen();
    g_game = gs;
    Weather w;
    if (fread(&w, sizeof(w), 1, f) != 1) { fclose(f); return false; }
    int stone = 40;
    if (fread(&stone, sizeof(int), 1, f) != 1) { fclose(f); return false; }
    if (fread(g_height, sizeof(float), TER_N * TER_N, f) != (size_t)(TER_N * TER_N)) { fclose(f); return false; }
    terrainBuildMesh();
    castleLoadExtra(f);
    uint32_t nt = 0;
    if (fread(&nt, 4, 1, f) != 1 || nt > 2000) { fclose(f); return false; }
    g_trees.resize(nt);
    if (nt && fread(g_trees.data(), sizeof(Tree), nt, f) != nt) { fclose(f); return false; }
    fxInit();
    uint32_t nd = 0;
    if (fread(&nd, 4, 1, f) != 1 || nd > 5000) { fclose(f); return false; }
    g_decals.resize(nd);
    if (nd && fread(g_decals.data(), sizeof(Decal), nd, f) != nd) { fclose(f); return false; }
    bodiesInit();
    uint32_t nb = 0;
    if (fread(&nb, 4, 1, f) != 1 || nb > 400) { fclose(f); return false; }
    g_bodies.resize(nb);
    if (nb && fread(g_bodies.data(), sizeof(Body), nb, f) != nb) { fclose(f); return false; }
    uint32_t ns = 0;
    g_stuck.clear();
    if (fread(&ns, 4, 1, f) == 1 && ns <= 400) {
        g_stuck.resize(ns);
        if (ns && fread(g_stuck.data(), sizeof(Stuck), ns, f) != ns) g_stuck.clear();
    }
    fclose(f);
    charsInit();
    birdsInit();
    g_projs.clear();
    g_vehicles.clear();
    weatherInit();
    g_weather.timeOfDay = w.timeOfDay;
    g_weather.rain = w.rain; g_weather.rainTarget = w.rainTarget;
    g_weather.cloud = w.cloud; g_weather.cloudTarget = w.cloudTarget;
    g_weather.fog = w.fog; g_weather.fogTarget = w.fogTarget;
    stationsGen(g_game.era);
    playerInit();
    playerSetupLoadout(g_game.era);
    g_player.stone = stone;
    g_game.paused = false;
    g_game.era = eraForYear(g_game.year);
    if (g_game.waveState == WS_ASSAULT || g_game.waveState == WS_RETREAT) { g_game.waveState = WS_INTER; g_game.waveTimer = 40; }
    if (g_game.waveState == WS_FALLEN) { gameNew(true); return true; }
    alliesSpawn();
    char sub[96];
    snprintf(sub, sizeof(sub), "%s - an %d - vague %d", g_eras[g_game.era].name, (int)g_game.year, g_game.wave);
    gameMessage("La guerre reprend", sub, 5);
    logf("partie chargée");
    return true;
}

// ---------------------------------------------------------------- Interface
static void bar(float x, float y, float w, float h, float v, Color c) {
    draw2DRect(x - 1, y - 1, w + 2, h + 2, rgba(0, 0, 0, 150));
    draw2DRect(x, y, w * clampf(v, 0, 1), h, c);
}

void hudDraw() {
    GameState& g = g_game;
    Player& p = g_player;
    char s[128];
    begin2D();
    // voile rouge des blessures
    if (p.bloodScreen > 0.01f || p.hp < 35) {
        float a = clampf(p.bloodScreen * 0.6f + (p.hp < 35 ? (35 - p.hp) / 35.0f * 0.35f : 0), 0, 0.8f);
        Color c = rgbaf(0.5f, 0, 0, a);
        draw2DRect(0, 0, 960, 40, c); draw2DRect(0, 504, 960, 40, c);
        draw2DRect(0, 40, 50, 464, c); draw2DRect(910, 40, 50, 464, c);
        Color c2 = rgbaf(0.45f, 0, 0, a * 0.5f);
        draw2DRect(0, 40, 960, 30, c2); draw2DRect(0, 474, 960, 30, c2);
    }
    if (p.dead) draw2DRect(0, 0, 960, 544, rgba(20, 0, 0, (int)(clampf(p.deadT / 2, 0, 1) * 160)));
    // ère, année, vague
    snprintf(s, sizeof(s), "%s - An %d", g_eras[g.era].name, (int)g.year);
    drawText(16, 12, s, 2, rgba(240, 225, 190));
    switch (g.waveState) {
    case WS_INTER: { int t = (int)g.waveTimer; snprintf(s, sizeof(s), "Vague %d dans %d:%02d", g.wave + 1, t / 60, t % 60); break; }
    case WS_ASSAULT: snprintf(s, sizeof(s), "Vague %d - Assaut (%d)", g.wave, (g.toSpawn - g.spawned) + countAlive(TEAM_ENEMY)); break;
    case WS_RETREAT: snprintf(s, sizeof(s), "Vague %d - Repli ennemi", g.wave); break;
    default: snprintf(s, sizeof(s), "Le château est tombé"); break;
    }
    drawText(16, 40, s, 2, g.waveState == WS_ASSAULT ? rgba(255, 150, 120) : rgba(220, 220, 220));
    // intégrité du château
    drawText(944, 12, "Remparts", 1.5f, rgba(220, 220, 220), 2);
    bar(784, 32, 160, 8, castleIntegrity(), rgba(200, 190, 160));
    drawText(944, 46, "Donjon", 1.5f, rgba(220, 220, 220), 2);
    bar(784, 66, 160, 8, (keepIntegrity() - 0.55f) / 0.45f, rgba(220, 120, 80));
    if (g.keepCapture > 1) {
        drawText(480, 76, "Le donjon est envahi !", 2, rgba(255, 90, 70), 1);
        bar(400, 100, 160, 8, g.keepCapture / 100, rgba(230, 60, 40));
    }
    if (timeScale() > 1) {
        snprintf(s, sizeof(s), "Temps x%d", (int)timeScale());
        drawText(944, 84, s, 1.5f, rgba(255, 220, 120), 2);
    }
    // vie
    drawText(16, 498, "Vie", 1.5f, rgba(220, 220, 220));
    bar(60, 500, 180, 12, p.hp / 100.0f, p.hp < 35 ? rgba(220, 40, 40) : rgba(170, 30, 30));
    // arme
    if (!p.dead) {
        int w = p.weapons[p.cur];
        const WeaponDef& d = g_weaponDefs[w];
        if (p.held >= 0) snprintf(s, sizeof(s), "Objet en main");
        else if (p.reloadT > 0) snprintf(s, sizeof(s), "%s - rechargement", d.name);
        else if (d.mag > 1) snprintf(s, sizeof(s), "%s  %d/%d", d.name, p.mag[w], d.mag);
        else if (w == W_BOW && p.arrowFlaming) snprintf(s, sizeof(s), "%s - flèche enflammée", d.name);
        else snprintf(s, sizeof(s), "%s", d.name);
        drawText(944, 498, s, 2, rgba(240, 230, 200), 2);
        snprintf(s, sizeof(s), "Pierres %d%s", p.stone, p.grenades > 0 ? "" : "");
        if (p.grenades > 0) snprintf(s, sizeof(s), "Pierres %d   Grenades %d (haut)", p.stone, p.grenades);
        drawText(944, 474, s, 1.5f, rgba(200, 200, 200), 2);
        // réticule
        if (p.station < 0 || g_stations[p.station].kind != ST_CAULDRON) {
            Color cc = rgba(255, 255, 255, 200);
            float gap = 4 + (1 - p.zoom) * 3 + p.recoil * 12;
            draw2DRect(480 - gap - 6, 271, 6, 2, cc);
            draw2DRect(480 + gap, 271, 6, 2, cc);
            draw2DRect(479, 272 - gap - 6, 2, 6, cc);
            draw2DRect(479, 272 + gap, 2, 6, cc);
            if (w == W_BOW && p.drawT > 0) bar(440, 290, 80, 4, p.drawT, p.arrowFlaming ? rgba(255, 150, 50) : rgba(230, 230, 230));
            if (p.repairT > 0) bar(440, 300, 80, 4, p.repairT / 0.5f, rgba(160, 200, 255));
        }
        if (g_prompt[0]) drawText(480, 430, g_prompt, 2, rgba(255, 240, 200), 1);
    }
    // messages
    if (g.msgT > 0) {
        float a = clampf(g.msgT, 0, 1);
        drawText(480, 170, g.msg, 3.5f, rgba(255, 235, 190, (int)(255 * a)), 1);
        if (g.sub[0]) drawText(480, 222, g.sub, 2, rgba(230, 230, 230, (int)(255 * a)), 1);
    }
    end2D();
}

void menuUpdateDraw() {
    GameState& g = g_game;
    static int view = 0;  // 0 menu, 1 chroniques, 2 commandes
    const int N = 7;
    if (view == 0) {
        if (g_in.pressed & B_DOWN) g.menuSel = (g.menuSel + 1) % N;
        if (g_in.pressed & B_UP) g.menuSel = (g.menuSel + N - 1) % N;
        if (g_in.pressed & B_CROSS) {
            switch (g.menuSel) {
            case 0: g.paused = false; break;
            case 1: g.timeSpeedIdx = (g.timeSpeedIdx + 1) % 6; break;
            case 2: gameMessage(gameSave() ? "Partie sauvegardée" : "Échec de la sauvegarde", "", 2); g.paused = false; break;
            case 3: view = 1; break;
            case 4: view = 2; break;
            case 5: gameNew(false); g.paused = false; break;
            case 6: gameSave(); sceKernelExitProcess(0); break;
            }
        }
        if (g_in.pressed & B_CIRCLE) g.paused = false;
    } else if (g_in.pressed & (B_CIRCLE | B_CROSS)) view = 0;
    begin2D();
    draw2DRect(0, 0, 960, 544, rgba(0, 0, 0, 170));
    drawText(480, 50, "ETERNAL SIEGE", 4, rgba(240, 220, 170), 1);
    char s[128];
    if (view == 0) {
        char speed[48];
        snprintf(speed, sizeof(speed), "Vitesse du temps : x%d", (int)timeScale());
        const char* items[N] = {"Reprendre", speed, "Sauvegarder", "Chroniques des ères", "Commandes", "Nouvelle partie", "Sauvegarder et quitter"};
        for (int i = 0; i < N; i++) {
            Color c = i == g.menuSel ? rgba(255, 210, 120) : rgba(200, 200, 200);
            if (i == g.menuSel) draw2DRect(300, 128 + i * 44, 360, 36, rgba(80, 60, 30, 160));
            drawText(480, 136 + i * 44, items[i], 2.5f, c, 1);
        }
        snprintf(s, sizeof(s), "%s - An %d - Vague %d - Meilleure vague %d", g_eras[g.era].name, (int)g.year, g.wave, g.bestWave);
        drawText(480, 460, s, 1.5f, rgba(190, 190, 190), 1);
        drawText(480, 490, "Le temps s'arrête quand la console est en veille.", 1.5f, rgba(150, 150, 150), 1);
    } else if (view == 1) {
        drawText(480, 110, "Chroniques des ères passées", 2.5f, rgba(240, 220, 170), 1);
        if (g.historyCount == 0) drawText(480, 180, "Le château n'est encore jamais tombé.", 2, rgba(200, 200, 200), 1);
        for (int i = 0; i < g.historyCount && i < 9; i++) {
            snprintf(s, sizeof(s), "Ère n°%d : tombée à la vague %d, en l'an %d", g.historyCount - i, g.histWave[g.historyCount - 1 - i], g.histYear[g.historyCount - 1 - i]);
            drawText(480, 160 + i * 30, s, 2, rgba(210, 210, 210), 1);
        }
        drawText(480, 480, "○ : retour", 2, rgba(180, 180, 180), 1);
    } else {
        const char* lines[] = {
            "Stick gauche : se déplacer       Stick droit : regarder",
            "R : tirer / frapper / lancer     L : viser",
            "✕ : sauter      △ : changer d'arme",
            "□ : prendre un objet, utiliser canon / chaudron,",
            "     repousser une échelle",
            "○ (maintenir) : réparer les murs avec des pierres",
            "Haut : lancer une grenade (ères modernes)",
            "Arc près d'un brasero : flèche enflammée",
            "SELECT : accélérer le temps      START : menu",
        };
        for (int i = 0; i < 9; i++) drawText(80, 120 + i * 34, lines[i], 2, rgba(220, 220, 220));
        drawText(480, 480, "○ : retour", 2, rgba(180, 180, 180), 1);
    }
    end2D();
}

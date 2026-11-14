// Eternal Siege Vita - ciel, cycle jour/nuit, météo, pluie
#include "game.h"
#include <vitaGL.h>

Weather g_weather;
static Batch s_sky, s_clouds, s_add, s_rain;
static bool s_init = false;
struct Drop { vec3 p; float spd; };
static std::vector<Drop> s_drops;
static vec3 s_stars[220];

void weatherInit() {
    Weather& w = g_weather;
    memset(&w, 0, sizeof(w));
    w.timeOfDay = 0.33f;
    w.cloud = w.cloudTarget = 0.35f;
    w.wind = w.windTarget = 0.3f;
    w.windDir = 0.6f;
    w.fog = w.fogTarget = 0.1f;
    w.nextChange = 120;
    w.nextThunder = 5;
    if (!s_init) {
        s_sky.init(TEX_WHITE, BM_OPAQUE, false);
        s_clouds.init(TEX_CLOUDS, BM_ALPHA, false);
        s_add.init(TEX_SOFT, BM_ADD, false);
        s_rain.init(TEX_WHITE, BM_ALPHA, false);
        s_init = true;
        Rng r(99);
        for (int i = 0; i < 220; i++) {
            vec3 v = r.unit();
            if (v.y < 0.08f) v.y = -v.y + 0.08f;
            s_stars[i] = normalize(v);
        }
    }
    s_drops.resize(700);
    for (auto& d : s_drops) { d.p = vec3(frange(-25, 25), frange(0, 30), frange(-25, 25)); d.spd = frange(16, 22); }
}

static vec3 sunDir() {
    float a = (g_weather.timeOfDay - 0.25f) * 2 * PI;
    return normalize(vec3(cosf(a) * 0.8f, sinf(a), 0.35f));
}

void weatherUpdate(float dt, float ts) {
    Weather& w = g_weather;
    w.timeOfDay += dt * ts / (24 * 60.0f);
    if (w.timeOfDay >= 1) w.timeOfDay -= 1;
    w.nextChange -= dt * ts;
    if (w.nextChange <= 0) {
        w.nextChange = frange(180, 480);
        float r = frand();
        if (r < 0.35f) { w.rainTarget = 0; w.cloudTarget = frange(0.1f, 0.4f); w.fogTarget = 0.05f; w.windTarget = frange(0.1f, 0.4f); }
        else if (r < 0.55f) { w.rainTarget = 0; w.cloudTarget = frange(0.6f, 0.9f); w.fogTarget = 0.2f; w.windTarget = frange(0.3f, 0.6f); }
        else if (r < 0.75f) { w.rainTarget = frange(0.4f, 0.8f); w.cloudTarget = 0.9f; w.fogTarget = 0.35f; w.windTarget = frange(0.4f, 0.7f); }
        else if (r < 0.87f) { w.rainTarget = 1; w.cloudTarget = 1; w.fogTarget = 0.5f; w.windTarget = 1; }
        else { w.rainTarget = 0; w.cloudTarget = 0.7f; w.fogTarget = 0.9f; w.windTarget = 0.05f; }
        w.windDir += frange(-1, 1);
    }
    float k = clampf(dt * 0.05f * fmaxf(1, ts * 0.25f), 0, 1);
    w.rain = lerpf(w.rain, w.rainTarget, k);
    w.cloud = lerpf(w.cloud, w.cloudTarget, k);
    w.fog = lerpf(w.fog, w.fogTarget, k);
    w.wind = lerpf(w.wind, w.windTarget + sinf(g_time * 0.3f) * 0.1f, k);
    // éclairs pendant l'orage
    w.lightning = fmaxf(0, w.lightning - dt * 4);
    if (w.rain > 0.85f) {
        w.nextThunder -= dt;
        if (w.nextThunder <= 0) {
            w.nextThunder = frange(6, 25);
            w.lightning = 1;
            float delay = frange(0.3f, 1.0f);
            sndPlay2D(S_THUNDER, 0.7f + delay * 0.3f, frange(0.8f, 1.1f));
        }
    }
    vec3 sd = sunDir();
    float sunH = sd.y;
    w.daylight = smoothstepf(-0.12f, 0.2f, sunH);
    float dusk = smoothstepf(0.35f, 0.0f, fabsf(sunH)) * smoothstepf(-0.2f, 0.0f, sunH);
    float overcast = w.cloud * 0.6f + w.rain * 0.4f;
    // ciel
    float sr = lerpf(0.03f, 0.42f, w.daylight), sg = lerpf(0.04f, 0.6f, w.daylight), sb = lerpf(0.09f, 0.88f, w.daylight);
    sr = lerpf(sr, 0.95f, dusk * 0.6f); sg = lerpf(sg, 0.5f, dusk * 0.5f); sb = lerpf(sb, 0.35f, dusk * 0.5f);
    float gray = (sr + sg + sb) / 3;
    w.skyR = lerpf(sr, gray, overcast); w.skyG = lerpf(sg, gray, overcast); w.skyB = lerpf(sb, gray * 1.05f, overcast);
    float fogMix = clampf(w.fog + w.rain * 0.3f, 0, 1);
    w.fogR = lerpf(w.skyR * 0.9f + 0.05f * w.daylight, 0.55f * w.daylight + 0.05f, fogMix * 0.6f);
    w.fogG = lerpf(w.skyG * 0.9f + 0.05f * w.daylight, 0.57f * w.daylight + 0.05f, fogMix * 0.6f);
    w.fogB = lerpf(w.skyB * 0.9f + 0.05f * w.daylight, 0.6f * w.daylight + 0.07f, fogMix * 0.6f);
    // soleil / lune
    float sunI = w.daylight * (1 - overcast * 0.7f);
    w.sunR = lerpf(0.15f, 1.0f, sunI) + dusk * 0.3f;
    w.sunG = lerpf(0.17f, 0.93f, sunI) + dusk * 0.05f;
    w.sunB = lerpf(0.28f, 0.82f, sunI) - dusk * 0.2f;
    if (w.daylight < 0.1f) { w.sunR = 0.12f; w.sunG = 0.14f; w.sunB = 0.24f; }
    float amb = lerpf(0.10f, 0.42f, w.daylight) + overcast * 0.08f * w.daylight;
    w.ambR = amb * 0.9f; w.ambG = amb * 0.95f; w.ambB = amb * 1.12f;
    if (w.lightning > 0) {
        float l = w.lightning;
        w.ambR += l * 0.8f; w.ambG += l * 0.8f; w.ambB += l * 0.9f;
        w.skyR += l * 0.5f; w.skyG += l * 0.5f; w.skyB += l * 0.6f;
    }
}

void applySceneLighting() {
    Weather& w = g_weather;
    vec3 sd = sunDir();
    if (sd.y < 0) sd = normalize(vec3(-sd.x, -sd.y, sd.z));  // la lune prend le relais
    float pos[4] = {sd.x, sd.y, sd.z, 0};
    float dif[4] = {w.sunR, w.sunG, w.sunB, 1};
    float zero[4] = {0, 0, 0, 1};
    float amb[4] = {w.ambR, w.ambG, w.ambB, 1};
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightfv(GL_LIGHT0, GL_AMBIENT, zero);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    // brouillard
    float fogMix = clampf(w.fog + w.rain * 0.35f, 0, 1);
    float fs = lerpf(55, 6, fogMix), fe = lerpf(230, 60, fogMix);
    if (w.daylight < 0.2f) fe = fminf(fe, 140);
    float fc[4] = {w.fogR, w.fogG, w.fogB, 1};
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, fs);
    glFogf(GL_FOG_END, fe);
    glFogfv(GL_FOG_COLOR, fc);
}

void skyDraw(float t) {
    Weather& w = g_weather;
    glDisable(GL_FOG);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    vec3 c = g_cam.pos;
    // dôme : anneaux de couleur dégradée
    const int SEG = 16;
    float elev[5] = {-0.3f, 0.0f, 0.15f, 0.45f, 1.0f};
    Color cols[5];
    Color hz = rgbaf(w.fogR, w.fogG, w.fogB);
    Color zen = rgbaf(w.skyR * 0.75f, w.skyG * 0.8f, w.skyB * 0.95f);
    cols[0] = hz; cols[1] = hz;
    cols[2] = rgbaf(lerpf(w.fogR, w.skyR, 0.6f), lerpf(w.fogG, w.skyG, 0.6f), lerpf(w.fogB, w.skyB, 0.6f));
    cols[3] = rgbaf(w.skyR, w.skyG, w.skyB);
    cols[4] = zen;
    vec3 n(0, 1, 0);
    for (int r = 0; r < 4; r++)
        for (int s = 0; s < SEG; s++) {
            float a0 = s * 2 * PI / SEG, a1 = (s + 1) * 2 * PI / SEG;
            auto P = [&](float a, float e) {
                float ce = cosf(asinf(e));
                return c + vec3(cosf(a) * ce, e, sinf(a) * ce) * 200;
            };
            Vtx v[4];
            vec3 p[4] = {P(a0, elev[r]), P(a1, elev[r]), P(a1, elev[r + 1]), P(a0, elev[r + 1])};
            Color cc[4] = {cols[r], cols[r], cols[r + 1], cols[r + 1]};
            for (int q = 0; q < 4; q++) {
                v[q].x = p[q].x; v[q].y = p[q].y; v[q].z = p[q].z; v[q].u = 0.5f; v[q].v = 0.5f;
                v[q].nx = 0; v[q].ny = 1; v[q].nz = 0;
                v[q].r = cc[q].r; v[q].g = cc[q].g; v[q].b = cc[q].b; v[q].a = 255;
            }
            emitTri(s_sky, v[0], v[1], v[2]);
            emitTri(s_sky, v[0], v[2], v[3]);
        }
    batchFlush(s_sky);
    // étoiles
    float night = 1 - w.daylight;
    if (night > 0.05f && w.cloud < 0.95f) {
        Color sc = rgbaf(1, 1, 1, night * (1 - w.cloud));
        for (int i = 0; i < 220; i++) {
            float tw = 0.6f + 0.4f * sinf(t * 3 + i);
            emitBillboard(s_add, c + s_stars[i] * 190, 0.5f * tw, 0, sc, g_cam.right, g_cam.up);
        }
    }
    // soleil et lune
    vec3 sd = sunDir();
    if (sd.y > -0.1f) {
        float a = clampf((1 - w.cloud * 0.8f) * smoothstepf(-0.1f, 0.05f, sd.y), 0, 1);
        emitBillboard(s_add, c + sd * 180, 9, 0, rgbaf(1, 0.92f, 0.75f, a), g_cam.right, g_cam.up);
        emitBillboard(s_add, c + sd * 180, 28, 0, rgbaf(1, 0.8f, 0.5f, a * 0.35f), g_cam.right, g_cam.up);
    }
    if (sd.y < 0.1f) {
        vec3 md = normalize(vec3(-sd.x, -sd.y, sd.z));
        float a = clampf((1 - w.cloud * 0.8f) * smoothstepf(-0.1f, 0.05f, md.y), 0, 1);
        emitBillboard(s_add, c + md * 180, 6, 0, rgbaf(0.85f, 0.9f, 1, a), g_cam.right, g_cam.up);
        emitBillboard(s_add, c + md * 180, 18, 0, rgbaf(0.6f, 0.7f, 1, a * 0.2f), g_cam.right, g_cam.up);
    }
    batchFlush(s_add);
    // couche nuageuse
    float cb = 0.3f + w.daylight * 0.7f;
    Color cc = rgbaf(cb * (1 - w.rain * 0.45f), cb * (1 - w.rain * 0.43f), cb * (1 - w.rain * 0.4f), clampf(0.25f + w.cloud * 0.85f, 0, 1));
    float off = t * (0.004f + w.wind * 0.012f);
    for (int iz = -3; iz < 3; iz++)
        for (int ix = -3; ix < 3; ix++) {
            float x0 = c.x + ix * 60, z0 = c.z + iz * 60;
            float y0 = c.y + 70, y1 = c.y + 70;
            float fx0 = (ix) / 3.0f, fz0 = iz / 3.0f, fx1 = (ix + 1) / 3.0f, fz1 = (iz + 1) / 3.0f;
            auto A = [&](float fx, float fz) { return clampf(1.2f - sqrtf(fx * fx + fz * fz), 0, 1); };
            Vtx v[4];
            vec3 p[4] = {vec3(x0, y0, z0), vec3(x0 + 60, y0, z0), vec3(x0 + 60, y1, z0 + 60), vec3(x0, y1, z0 + 60)};
            float al[4] = {A(fx0, fz0), A(fx1, fz0), A(fx1, fz1), A(fx0, fz1)};
            for (int q = 0; q < 4; q++) {
                v[q].x = p[q].x; v[q].y = p[q].y; v[q].z = p[q].z;
                v[q].u = (p[q].x - c.x * 0.7f) / 180.0f + off; v[q].v = (p[q].z - c.z * 0.7f) / 180.0f + off * 0.3f;
                v[q].nx = 0; v[q].ny = -1; v[q].nz = 0;
                v[q].r = cc.r; v[q].g = cc.g; v[q].b = cc.b; v[q].a = (uint8_t)(cc.a * al[q]);
            }
            emitTri(s_clouds, v[0], v[1], v[2]);
            emitTri(s_clouds, v[0], v[2], v[3]);
        }
    batchFlush(s_clouds);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_FOG);
}

void rainUpdateDraw(float dt) {
    Weather& w = g_weather;
    sndLoop(LS_RAIN, S_RAIN, w.rain * 0.7f, 1);
    sndLoop(LS_WIND, S_WIND, 0.08f + w.wind * 0.4f, 0.8f + w.wind * 0.4f);
    if (w.rain < 0.03f) return;
    vec3 c = g_cam.pos;
    vec3 wv(cosf(w.windDir) * w.wind * 5, 0, sinf(w.windDir) * w.wind * 5);
    int n = (int)(s_drops.size() * w.rain);
    Color col = rgbaf(0.7f, 0.75f, 0.85f, 0.35f + w.daylight * 0.2f);
    for (int i = 0; i < n; i++) {
        Drop& d = s_drops[i];
        d.p += (vec3(0, -d.spd, 0) + wv) * dt;
        vec3 wp = c + d.p;
        float g = surfaceHeight(wp.x, wp.z, wp.y);
        if (wp.y < g || d.p.y < -12) {
            if (wp.y < g + 0.5f && fabsf(d.p.x) < 10 && fabsf(d.p.z) < 10 && frand() < 0.3f) {
                bool water = inRiver(vec3(wp.x, g, wp.z));
                spawnParticle(PT_SPLASH, vec3(wp.x, water ? WATER_Y : g + 0.02f, wp.z), vec3(0, 1.2f, 0), 0.06f, 0.25f, rgba(200, 210, 230, 140));
            }
            d.p = vec3(frange(-22, 22), frange(14, 22), frange(-22, 22));
            continue;
        }
        if (d.p.x > 22) d.p.x -= 44; if (d.p.x < -22) d.p.x += 44;
        if (d.p.z > 22) d.p.z -= 44; if (d.p.z < -22) d.p.z += 44;
        vec3 dir = normalize(vec3(wv.x, -d.spd, wv.z));
        vec3 a = wp, b = wp - dir * 0.6f;
        vec3 side = normalize(cross(dir, g_cam.fwd)) * 0.012f;
        emitQuad(s_rain, a - side, a + side, b + side, b - side, vec3(0, 1, 0), col, 0, 0, 1, 1);
    }
    glDisable(GL_CULL_FACE);
    batchFlush(s_rain);
    glEnable(GL_CULL_FACE);
}

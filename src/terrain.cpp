// Eternal Siege Vita - terrain, rivière, herbe
#include "game.h"
#include <vitaGL.h>

float g_height[TER_N * TER_N];
float g_heightBase[TER_N * TER_N];
static uint8_t s_dirtW[TER_N * TER_N];
static uint8_t s_rockW[TER_N * TER_N];
static uint8_t s_shade[TER_N * TER_N];
static StaticMesh s_grassMesh, s_dirtMesh, s_rockMesh;
static bool s_dirty = false;
static float s_rebuildT = 0;

static inline float gx(int i) { return -TER_HALF + i * TER_CELL; }

static float genHeight(float x, float z) {
    float h = 2.6f + (fbm(x * 0.02f, z * 0.02f, 4, 3) - 0.5f) * 2.5f;
    // plateau du château (super-ellipse)
    float d = powf(powf(fabsf(x), 4) + powf(fabsf(z), 4), 0.25f);
    float plateau = 1 - smoothstepf(40, 64, d);
    float slopeNoise = (fbm(x * 0.08f, z * 0.08f, 3, 9) - 0.5f) * 3.0f * plateau * (1 - plateau) * 4;
    h = lerpf(h, 10.0f, plateau) + slopeNoise;
    if (d < 41) h = 10.0f;
    // flancs de la vallée
    float side = smoothstepf(58, 125, fabsf(x));
    h += side * (20 + fbm(x * 0.03f, z * 0.03f, 4, 21) * 14);
    float north = smoothstepf(-62, -126, z);
    h += north * (16 + fbm(x * 0.03f, z * 0.03f, 4, 27) * 10);
    // rivière
    float rc = (RIVER_Z0 + RIVER_Z1) * 0.5f + sinf(x * 0.035f) * 2.0f;
    float rw = (RIVER_Z1 - RIVER_Z0) * 0.5f;
    float rd = fabsf(z - rc);
    float bed = -0.6f + rd * rd * 0.02f;
    float t = smoothstepf(rw + 6, rw - 1, rd);
    h = lerpf(h, fminf(h, bed), t);
    // gué sous le chemin
    if (fabsf(x) < 5 && t > 0) h = fmaxf(h, 0.6f);
    return h;
}

void terrainGen() {
    for (int j = 0; j < TER_N; j++)
        for (int i = 0; i < TER_N; i++) {
            float x = gx(i), z = gx(j);
            float h = genHeight(x, z);
            g_height[j * TER_N + i] = h;
            g_heightBase[j * TER_N + i] = h;
        }
    for (int j = 0; j < TER_N; j++)
        for (int i = 0; i < TER_N; i++) {
            float x = gx(i), z = gx(j);
            vec3 n = terrainNormal(x, z);
            float rock = smoothstepf(0.88f, 0.7f, n.y);
            float dirt = 0;
            // chemin vers la porte
            float pathX = sinf(z * 0.04f) * 4.0f;
            if (z > 30) dirt = fmaxf(dirt, smoothstepf(5, 2, fabsf(x - pathX)));
            // cour du château : terre battue
            if (fabsf(x) < 30 && fabsf(z) < 30) dirt = fmaxf(dirt, 0.75f);
            // berges boueuses
            float h = g_height[j * TER_N + i];
            if (h < WATER_Y + 0.6f && z > RIVER_Z0 - 8 && z < RIVER_Z1 + 8) dirt = fmaxf(dirt, 0.8f);
            dirt = fmaxf(dirt, smoothstepf(0.62f, 0.8f, fbm(x * 0.05f, z * 0.05f, 3, 33)) * 0.6f);
            s_rockW[j * TER_N + i] = (uint8_t)(rock * 255);
            s_dirtW[j * TER_N + i] = (uint8_t)(clampf(dirt, 0, 1) * 255);
            s_shade[j * TER_N + i] = (uint8_t)(200 + fbm(x * 0.1f, z * 0.1f, 2, 41) * 55);
        }
    s_dirty = true;
}

float terrainHeight(float x, float z) {
    float fx = (x + TER_HALF) / TER_CELL, fz = (z + TER_HALF) / TER_CELL;
    if (fx < 0) fx = 0; if (fz < 0) fz = 0;
    if (fx > TER_N - 1.001f) fx = TER_N - 1.001f;
    if (fz > TER_N - 1.001f) fz = TER_N - 1.001f;
    int i = (int)fx, j = (int)fz;
    float tx = fx - i, tz = fz - j;
    float a = g_height[j * TER_N + i], b = g_height[j * TER_N + i + 1];
    float c = g_height[(j + 1) * TER_N + i], d = g_height[(j + 1) * TER_N + i + 1];
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), tz);
}

vec3 terrainNormal(float x, float z) {
    float e = 1.0f;
    float hl = terrainHeight(x - e, z), hr = terrainHeight(x + e, z);
    float hd = terrainHeight(x, z - e), hu = terrainHeight(x, z + e);
    return normalize(vec3(hl - hr, 2 * e, hd - hu));
}

bool inRiver(const vec3& p) {
    return p.z > RIVER_Z0 - 6 && p.z < RIVER_Z1 + 6 && p.y < WATER_Y && terrainHeight(p.x, p.z) < WATER_Y;
}

void terrainDeform(const vec3& p, float radius, float depth) {
    int i0 = clampi((int)((p.x - radius + TER_HALF) / TER_CELL), 0, TER_N - 1);
    int i1 = clampi((int)((p.x + radius + TER_HALF) / TER_CELL) + 1, 0, TER_N - 1);
    int j0 = clampi((int)((p.z - radius + TER_HALF) / TER_CELL), 0, TER_N - 1);
    int j1 = clampi((int)((p.z + radius + TER_HALF) / TER_CELL) + 1, 0, TER_N - 1);
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            float dx = gx(i) - p.x, dz = gx(j) - p.z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d > radius) continue;
            float f = 1 - d / radius;
            float x = gx(i), z = gx(j);
            // pas de cratère sous les fondations du château
            if (fabsf(x) < 34 && fabsf(z) < 34) continue;
            g_height[j * TER_N + i] -= depth * f * f;
            int dw = s_dirtW[j * TER_N + i] + (int)(f * 255);
            s_dirtW[j * TER_N + i] = (uint8_t)(dw > 255 ? 255 : dw);
            int sh = s_shade[j * TER_N + i] - (int)(f * 80);
            s_shade[j * TER_N + i] = (uint8_t)(sh < 90 ? 90 : sh);
        }
    s_dirty = true;
}

static void buildMeshes() {
    s_grassMesh.cpu.clear(); s_dirtMesh.cpu.clear(); s_rockMesh.cpu.clear();
    s_grassMesh.cpu.reserve(128 * 128 * 6);
    std::vector<vec3> nrm(TER_N * TER_N);
    for (int j = 0; j < TER_N; j++)
        for (int i = 0; i < TER_N; i++) {
            int il = i > 0 ? i - 1 : i, ir = i < TER_N - 1 ? i + 1 : i;
            int jd = j > 0 ? j - 1 : j, ju = j < TER_N - 1 ? j + 1 : j;
            float dx = g_height[j * TER_N + il] - g_height[j * TER_N + ir];
            float dz = g_height[jd * TER_N + i] - g_height[ju * TER_N + i];
            nrm[j * TER_N + i] = normalize(vec3(dx, 2 * TER_CELL * 0.5f * ((ir - il) + (ju - jd)) * 0.5f, dz));
        }
    auto V = [&](int i, int j, float alpha, float uvs) {
        Vtx v;
        int k = j * TER_N + i;
        v.x = gx(i); v.y = g_height[k]; v.z = gx(j);
        v.u = v.x / uvs; v.v = v.z / uvs;
        v.nx = nrm[k].x; v.ny = nrm[k].y; v.nz = nrm[k].z;
        uint8_t s = s_shade[k];
        v.r = v.g = v.b = s;
        v.a = (uint8_t)(alpha * 255);
        return v;
    };
    for (int j = 0; j < TER_N - 1; j++)
        for (int i = 0; i < TER_N - 1; i++) {
            int ids[4][2] = {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}};
            int order[6] = {0, 3, 2, 0, 2, 1};
            for (int o = 0; o < 6; o++) s_grassMesh.cpu.push_back(V(ids[order[o]][0], ids[order[o]][1], 1, 3.0f));
            int dsum = 0, rsum = 0;
            for (int c = 0; c < 4; c++) {
                dsum += s_dirtW[ids[c][1] * TER_N + ids[c][0]];
                rsum += s_rockW[ids[c][1] * TER_N + ids[c][0]];
            }
            if (dsum > 8)
                for (int o = 0; o < 6; o++) {
                    int ci = ids[order[o]][0], cj = ids[order[o]][1];
                    s_dirtMesh.cpu.push_back(V(ci, cj, s_dirtW[cj * TER_N + ci] / 255.0f, 3.0f));
                }
            if (rsum > 8)
                for (int o = 0; o < 6; o++) {
                    int ci = ids[order[o]][0], cj = ids[order[o]][1];
                    s_rockMesh.cpu.push_back(V(ci, cj, s_rockW[cj * TER_N + ci] / 255.0f, 6.0f));
                }
        }
    s_grassMesh.upload(); s_dirtMesh.upload(); s_rockMesh.upload();
}

void terrainBuildMesh() { buildMeshes(); s_dirty = false; }

void terrainUpdate(float dt) {
    s_rebuildT -= dt;
    if (s_dirty && s_rebuildT <= 0) { buildMeshes(); s_dirty = false; s_rebuildT = 0.5f; }
}

void terrainDraw() {
    s_grassMesh.draw(TEX_GRASS, BM_OPAQUE, true);
    // couches de terre et roche par-dessus (fondu par alpha de sommet)
    glDepthFunc(GL_LEQUAL);
    s_dirtMesh.draw(TEX_DIRT, BM_ALPHA, true);
    s_rockMesh.draw(TEX_ROCK, BM_ALPHA, true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------- Eau
static Batch s_water;
static bool s_waterInit = false;

void waterDraw(float t) {
    if (!s_waterInit) { s_water.init(TEX_WATER, BM_ALPHA, false); s_waterInit = true; }
    const int NX = 48, NZ = 6;
    float z0 = RIVER_Z0 - 7, z1 = RIVER_Z1 + 7;
    Weather& w = g_weather;
    float br = 0.35f + w.daylight * 0.65f;
    Color c = rgbaf(clampf((w.skyR * 0.5f + 0.25f) * br, 0, 1), clampf((w.skyG * 0.5f + 0.3f) * br, 0, 1),
                    clampf((w.skyB * 0.5f + 0.32f) * br, 0, 1), 0.78f);
    vec3 n(0, 1, 0);
    float flow = t * 0.25f;
    for (int iz = 0; iz < NZ; iz++)
        for (int ix = 0; ix < NX; ix++) {
            float xa = -TER_HALF + ix * (TER_SIZE / NX), xb = xa + TER_SIZE / NX;
            float za = z0 + iz * (z1 - z0) / NZ, zb = za + (z1 - z0) / NZ;
            vec3 center((xa + xb) * 0.5f, WATER_Y, (za + zb) * 0.5f);
            if (!sphereVisible(center, 6)) continue;
            auto Y = [&](float x, float z) { return WATER_Y + sinf(x * 0.7f + t * 1.6f) * 0.04f + sinf(z * 0.9f + t * 1.1f) * 0.04f; };
            vec3 p0(xa, Y(xa, za), za), p1(xb, Y(xb, za), za), p2(xb, Y(xb, zb), zb), p3(xa, Y(xa, zb), zb);
            emitQuad(s_water, p0, p3, p2, p1, n, c, xa / 6 - flow, za / 6, xb / 6 - flow, zb / 6);
        }
    glDisable(GL_CULL_FACE);
    batchFlush(s_water);
    glEnable(GL_CULL_FACE);
}

// ---------------------------------------------------------------- Herbe
void grassDraw(float t, float wind) {
    Batch& b = g_batches[BA_GRASS];
    const float SP = 1.9f, R = 10.0f + 14.0f * g_quality;
    vec3 c = g_cam.pos;
    int i0 = (int)floorf((c.x - R) / SP), i1 = (int)floorf((c.x + R) / SP);
    int j0 = (int)floorf((c.z - R) / SP), j1 = (int)floorf((c.z + R) / SP);
    vec3 n(0, 1, 0);
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            float x = i * SP + hash2(i, j, 501) * SP, z = j * SP + hash2(i, j, 502) * SP;
            float dx = x - c.x, dz = z - c.z;
            float d2 = dx * dx + dz * dz;
            if (d2 > R * R) continue;
            if (dx * g_cam.fwd.x + dz * g_cam.fwd.z < -2.0f) continue;
            if (fabsf(x) > 126 || fabsf(z) > 126) continue;
            int gi = (int)((x + TER_HALF) / TER_CELL), gj = (int)((z + TER_HALF) / TER_CELL);
            if (gi < 0 || gj < 0 || gi >= TER_N || gj >= TER_N) continue;
            if (s_dirtW[gj * TER_N + gi] > 110 || s_rockW[gj * TER_N + gi] > 110) continue;
            float y = terrainHeight(x, z);
            if (y < WATER_Y + 0.1f) continue;
            if (voxAtWorld(vec3(x, y + 0.3f, z)) != M_AIR) continue;
            float s = 0.45f + hash2(i, j, 503) * 0.45f;
            float a = hash2(i, j, 504) * PI;
            float fade = 1 - smoothstepf(R * 0.7f, R, sqrtf(d2));
            float sh = (0.7f + hash2(i, j, 505) * 0.3f) * (0.35f + g_weather.daylight * 0.65f);
            Color col = rgbaf(sh, sh, sh * 0.9f, 1);
            float sway = sinf(t * 2.2f + x * 0.4f + z * 0.3f) * 0.12f * (0.3f + wind);
            vec3 sw(cosf(g_weather.windDir) * sway, 0, sinf(g_weather.windDir) * sway);
            float hh = s * fade;
            if (hh < 0.05f) continue;
            for (int k = 0; k < 2; k++) {
                float ang = a + k * PI * 0.5f;
                vec3 dir(cosf(ang) * s * 0.6f, 0, sinf(ang) * s * 0.6f);
                vec3 base(x, y - 0.05f, z);
                emitQuad(b, base - dir, base + dir, base + dir + vec3(0, hh, 0) + sw, base - dir + vec3(0, hh, 0) + sw, n, col, 0, 1, 1, 0);
            }
        }
}

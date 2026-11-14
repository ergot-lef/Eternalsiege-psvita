// Eternal Siege Vita - château en blocs de pierre destructibles
#include "game.h"
#include <vitaGL.h>

uint8_t g_vox[VX * VY * VZ];
uint8_t g_voxHp[VX * VY * VZ];
static uint8_t s_orig[VX * VY * VZ];
static const int CS = 16, NCX = VX / CS, NCZ = VZ / CS;
struct Chunk { StaticMesh stone, wood; bool dirty; };
static Chunk s_chunks[NCX * NCZ];
static int s_stoneCount = 0, s_keepCount = 0, s_stoneTotal = 1, s_keepTotal = 1;
static bool s_supportDirty = false;
static float s_supportT = 0, s_crackSndT = 0;
struct VoxFire { int i, j, k; float t; };
static std::vector<VoxFire> s_fires;
static uint8_t s_visit[VX * VY * VZ];
static int s_queue[VX * VY * VZ / 2];

static int maxHp(uint8_t m) {
    switch (m) {
    case M_STONE: return 250;
    case M_KEEP: return 255;
    case M_WOOD: return 220;
    case M_RUBBLE: return 110;
    }
    return 0;
}

uint8_t voxAt(int i, int j, int k) {
    if (!vinside(i, j, k)) return M_AIR;
    return g_vox[vidx(i, j, k)];
}

bool worldToVox(const vec3& p, int& i, int& j, int& k) {
    i = (int)floorf(p.x - VOX0X); j = (int)floorf(p.y - VOX0Y); k = (int)floorf(p.z - VOX0Z);
    return vinside(i, j, k);
}

uint8_t voxAtWorld(const vec3& p) {
    int i, j, k;
    if (!worldToVox(p, i, j, k)) return M_AIR;
    return g_vox[vidx(i, j, k)];
}

vec3 voxCenter(int i, int j, int k) { return vec3(VOX0X + i + 0.5f, VOX0Y + j + 0.5f, VOX0Z + k + 0.5f); }
bool solidAt(const vec3& p) { return voxAtWorld(p) != M_AIR; }

void castleMarkDirty(int i, int k) {
    int ci = i / CS, ck = k / CS;
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            int ni = (i + dx) / CS, nk = (k + dz) / CS;
            if (i + dx < 0 || k + dz < 0 || ni >= NCX || nk >= NCZ) continue;
            if (ni == ci && nk == ck && (dx || dz)) continue;
            s_chunks[nk * NCX + ni].dirty = true;
        }
    if (ci >= 0 && ck >= 0 && ci < NCX && ck < NCZ) s_chunks[ck * NCX + ci].dirty = true;
}

static void setv(int i, int j, int k, uint8_t m) {
    if (!vinside(i, j, k)) return;
    g_vox[vidx(i, j, k)] = m;
    g_voxHp[vidx(i, j, k)] = (uint8_t)maxHp(m);
}
static void box(int i0, int j0, int k0, int i1, int j1, int k1, uint8_t m) {
    for (int j = j0; j < j1; j++)
        for (int k = k0; k < k1; k++)
            for (int i = i0; i < i1; i++) setv(i, j, k, m);
}

static void stairs(int i, int k, int di, int dk, int j0, int n, int width) {
    // escalier : n marches montant dans la direction (di,dk), largeur perpendiculaire
    int pi = dk != 0 ? 1 : 0, pk = di != 0 ? 1 : 0;
    for (int s = 0; s < n; s++)
        for (int w = 0; w < width; w++)
            for (int j = 1; j <= j0 + s; j++)
                setv(i + di * s + pi * w, j, k + dk * s + pk * w, M_STONE);
}

static void tower(int i0, int k0, int size, int top) {
    box(i0, 0, k0, i0 + size, top, k0 + size, M_STONE);
    // intérieur creux au-dessus du chemin de ronde
    box(i0 + 2, 9, k0 + 2, i0 + size - 2, top - 1, k0 + size - 2, M_AIR);
    // marches intérieures vers la plate-forme
    for (int s = 0; s < top - 10; s++) setv(i0 + 2 + s % (size - 4), 9 + s, k0 + 2, M_STONE);
    // trémie au-dessus des marches
    for (int s = 0; s < size - 4; s++) setv(i0 + 2 + s, top - 1, k0 + 2, M_AIR);
    setv(i0 + 2, top - 1, k0 + 3, M_AIR);
    // merlons
    for (int a = 0; a < size; a++) {
        if (a % 2) continue;
        setv(i0 + a, top, k0, M_STONE); setv(i0 + a, top, k0 + size - 1, M_STONE);
        setv(i0, top, k0 + a, M_STONE); setv(i0 + size - 1, top, k0 + a, M_STONE);
    }
}

void castleGen() {
    memset(g_vox, 0, sizeof(g_vox));
    memset(g_voxHp, 0, sizeof(g_voxHp));
    const int A = 8, B = 72, T = 3, H = 9;
    // courtines
    box(A, 0, A, B, H, A + T, M_STONE);
    box(A, 0, B - T, B, H, B, M_STONE);
    box(A, 0, A, A + T, H, B, M_STONE);
    box(B - T, 0, A, B, H, B, M_STONE);
    // merlons extérieurs
    for (int a = A; a < B; a += 2) {
        setv(a, H, A, M_STONE); setv(a, H, B - 1, M_STONE);
        setv(A, H, a, M_STONE); setv(B - 1, H, a, M_STONE);
    }
    // tours d'angle
    tower(A - 4, A - 4, 10, 15);
    tower(B - 6, A - 4, 10, 15);
    tower(A - 4, B - 6, 10, 15);
    tower(B - 6, B - 6, 10, 15);
    // tours intermédiaires est/ouest
    tower(A - 3, 36, 8, 13);
    tower(B - 5, 36, 8, 13);
    // châtelet d'entrée (sud)
    box(32, 0, B - 5, 37, 13, B + 3, M_STONE);
    box(43, 0, B - 5, 48, 13, B + 3, M_STONE);
    for (int a = 32; a < 48; a += 2) setv(a, 13, B + 2, M_STONE);
    box(37, 7, B - 5, 43, 11, B + 3, M_STONE);
    // passage de la porte
    box(37, 1, B - 5, 43, 7, B + 3, M_AIR);
    // vantaux en bois (double épaisseur)
    box(37, 1, B + 1, 43, 7, B + 3, M_WOOD);
    // hourds en bois au-dessus de la porte
    box(36, 11, B + 2, 44, 12, B + 4, M_WOOD);
    // escaliers de la cour vers le chemin de ronde
    stairs(A + T, 20, 0, 1, 1, 8, 2);
    stairs(B - T - 2, 20, 0, 1, 1, 8, 2);
    stairs(24, B - T - 2, 1, 0, 1, 8, 2);
    stairs(48, B - T - 2, 1, 0, 1, 8, 2);
    // donjon
    const int K0 = 30, K1 = 50, KH = 22;
    box(K0, 0, K0, K1, KH, K1, M_KEEP);
    box(K0 + 2, 1, K0 + 2, K1 - 2, KH - 1, K1 - 2, M_AIR);
    box(K0 + 2, 8, K0 + 2, K1 - 2, 9, K1 - 2, M_KEEP);
    box(K0 + 2, 15, K0 + 2, K1 - 2, 16, K1 - 2, M_KEEP);
    box(38, 1, K1 - 2, 42, 5, K1, M_AIR);  // porte du donjon
    box(38, 1, K1 - 1, 42, 5, K1, M_WOOD);
    for (int a = K0; a < K1; a += 2) {
        setv(a, KH, K0, M_KEEP); setv(a, KH, K1 - 1, M_KEEP);
        setv(K0, KH, a, M_KEEP); setv(K1 - 1, KH, a, M_KEEP);
    }
    // meurtrières (fenêtres) du donjon
    for (int a = K0 + 4; a < K1 - 4; a += 5) {
        setv(a, 12, K0, M_AIR); setv(a, 12, K1 - 1, M_AIR);
        setv(K0, 12, a, M_AIR); setv(K1 - 1, 12, a, M_AIR);
        setv(a, 18, K0, M_AIR); setv(a, 18, K1 - 1, M_AIR);
    }
    memcpy(s_orig, g_vox, sizeof(g_vox));
    s_stoneCount = s_keepCount = 0;
    for (int n = 0; n < VX * VY * VZ; n++) {
        if (g_vox[n] == M_KEEP) s_keepCount++;
        else if (g_vox[n] != M_AIR) s_stoneCount++;
    }
    s_stoneTotal = s_stoneCount > 0 ? s_stoneCount : 1;
    s_keepTotal = s_keepCount > 0 ? s_keepCount : 1;
    s_fires.clear();
    for (auto& c : s_chunks) c.dirty = true;
    logf("château: %d blocs, donjon %d", s_stoneCount, s_keepCount);
}

float castleIntegrity() { return (float)s_stoneCount / s_stoneTotal; }
float keepIntegrity() { return (float)s_keepCount / s_keepTotal; }

// ---------------------------------------------------------------- Maillage
static const float AO[4] = {0.5f, 0.68f, 0.84f, 1.0f};

static inline int solidI(int i, int j, int k) {
    if (j < 0) return 1;
    return voxAt(i, j, k) != M_AIR ? 1 : 0;
}

static void buildChunk(int cx, int cz) {
    Chunk& ch = s_chunks[cz * NCX + cx];
    ch.stone.cpu.clear(); ch.wood.cpu.clear();
    static const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int j = 0; j < VY; j++)
        for (int k = cz * CS; k < cz * CS + CS; k++)
            for (int i = cx * CS; i < cx * CS + CS; i++) {
                uint8_t m = g_vox[vidx(i, j, k)];
                if (m == M_AIR) continue;
                float hpF = g_voxHp[vidx(i, j, k)] / (float)maxHp(m);
                float tint = 0.55f + hpF * 0.45f;
                float hh = hash2(i * 7 + j, k * 3 + j, 77) * 0.12f;
                float r = 1, g = 1, b = 1;
                if (m == M_KEEP) { r = 1.05f; g = 1.02f; b = 0.96f; }
                if (m == M_RUBBLE) { r = 0.68f; g = 0.64f; b = 0.6f; }
                if (m == M_WOOD) { r = 0.95f; g = 0.9f; b = 0.85f; }
                StaticMesh& mesh = (m == M_WOOD) ? ch.wood : ch.stone;
                for (int f = 0; f < 6; f++) {
                    int ni = i + dirs[f][0], nj = j + dirs[f][1], nk = k + dirs[f][2];
                    if (nj < 0) continue;
                    if (solidI(ni, nj, nk)) continue;
                    // axes du quad
                    int ax = f / 2;  // 0 x 1 y 2 z
                    int ua = ax == 0 ? 2 : 0, va = ax == 1 ? 2 : 1;
                    int sgn = (f % 2 == 0) ? 1 : -1;
                    vec3 nrm((float)dirs[f][0], (float)dirs[f][1], (float)dirs[f][2]);
                    vec3 corners[4];
                    float aoV[4];
                    int cu[4] = {0, 1, 1, 0}, cv[4] = {0, 0, 1, 1};
                    for (int c = 0; c < 4; c++) {
                        int p[3] = {i, j, k};
                        float fp[3] = {(float)i, (float)j, (float)k};
                        if (sgn > 0) fp[ax] += 1;
                        fp[ua] += cu[c]; fp[va] += cv[c];
                        corners[c] = vec3(VOX0X + fp[0], VOX0Y + fp[1], VOX0Z + fp[2]);
                        // AO : voisins de la couche extérieure
                        int o[3] = {p[0] + dirs[f][0], p[1] + dirs[f][1], p[2] + dirs[f][2]};
                        int du = cu[c] ? 1 : -1, dv = cv[c] ? 1 : -1;
                        int s1[3] = {o[0], o[1], o[2]}; s1[ua] += du;
                        int s2[3] = {o[0], o[1], o[2]}; s2[va] += dv;
                        int cc[3] = {o[0], o[1], o[2]}; cc[ua] += du; cc[va] += dv;
                        int a1 = solidI(s1[0], s1[1], s1[2]), a2 = solidI(s2[0], s2[1], s2[2]), a3 = solidI(cc[0], cc[1], cc[2]);
                        int ao = (a1 && a2) ? 0 : 3 - (a1 + a2 + a3);
                        aoV[c] = AO[ao];
                    }
                    float faceShade = (f == 2) ? 1.0f : (f == 3 ? 0.6f : 0.88f);
                    Vtx vv[4];
                    for (int c = 0; c < 4; c++) {
                        Vtx& v = vv[c];
                        v.x = corners[c].x; v.y = corners[c].y; v.z = corners[c].z;
                        float uc = (ua == 0 ? corners[c].x : corners[c].z), vc = (va == 1 ? corners[c].y : corners[c].z);
                        v.u = uc * 0.5f; v.v = -vc * 0.5f;
                        v.nx = nrm.x; v.ny = nrm.y; v.nz = nrm.z;
                        float s = aoV[c] * tint * faceShade + hh;
                        v.r = (uint8_t)clampf(s * r * 235, 0, 255);
                        v.g = (uint8_t)clampf(s * g * 235, 0, 255);
                        v.b = (uint8_t)clampf(s * b * 235, 0, 255);
                        v.a = 255;
                    }
                    // orientation CCW vers l'extérieur
                    bool flip = (ax == 2) ? (sgn < 0) : (sgn > 0);
                    if (!flip) {
                        mesh.cpu.push_back(vv[0]); mesh.cpu.push_back(vv[1]); mesh.cpu.push_back(vv[2]);
                        mesh.cpu.push_back(vv[0]); mesh.cpu.push_back(vv[2]); mesh.cpu.push_back(vv[3]);
                    } else {
                        mesh.cpu.push_back(vv[0]); mesh.cpu.push_back(vv[2]); mesh.cpu.push_back(vv[1]);
                        mesh.cpu.push_back(vv[0]); mesh.cpu.push_back(vv[3]); mesh.cpu.push_back(vv[2]);
                    }
                }
            }
    ch.stone.upload();
    ch.wood.upload();
    ch.dirty = false;
}

void castleDraw() {
    int rebuilt = 0;
    for (int cz = 0; cz < NCZ; cz++)
        for (int cx = 0; cx < NCX; cx++) {
            Chunk& ch = s_chunks[cz * NCX + cx];
            if (ch.dirty && rebuilt < 3) { buildChunk(cx, cz); rebuilt++; }
            vec3 c(VOX0X + cx * CS + CS * 0.5f, VOX0Y + VY * 0.5f, VOX0Z + cz * CS + CS * 0.5f);
            if (!sphereVisible(c, 20)) continue;
            ch.stone.draw(TEX_STONE, BM_OPAQUE, true);
            ch.wood.draw(TEX_PLANKS, BM_OPAQUE, true);
        }
}

// ---------------------------------------------------------------- Dégâts
static void removeVoxel(int i, int j, int k, const vec3& from, bool spawnDebris) {
    int n = vidx(i, j, k);
    uint8_t m = g_vox[n];
    if (m == M_AIR) return;
    g_vox[n] = M_AIR;
    g_voxHp[n] = 0;
    if (m == M_KEEP) s_keepCount--; else s_stoneCount--;
    vec3 c = voxCenter(i, j, k);
    castleMarkDirty(i, k);
    s_supportDirty = true;
    flowFieldDirty();
    if (spawnDebris) {
        vec3 away = normalize(c - from + vec3(0, 0.5f, 0));
        if (m == M_WOOD) {
            for (int q = 0; q < 2; q++) {
                int b = bodySpawn(BK_PLANK, c + vec3(frange(-0.3f, 0.3f), frange(-0.3f, 0.3f), 0), vec3(0.08f, 0.45f, 0.18f), rgba(200, 190, 175), BA_PLANKS, 6);
                if (b >= 0) { g_bodies[b].vel = away * frange(2, 6) + vec3(0, 2, 0); g_bodies[b].angVel = g_rng.unit() * 6; }
            }
            fxDust(c, 0.6f, 6, rgba(120, 100, 80));
        } else {
            int nd = g_rng.irange(1, 3);
            for (int q = 0; q < nd; q++) {
                float s = frange(0.12f, 0.3f);
                int b = bodySpawn(BK_DEBRIS, c + g_rng.unit() * 0.3f, vec3(s, s * 0.8f, s * 1.1f), rgba(210, 205, 195), BA_STONE, s * 20);
                if (b >= 0) { g_bodies[b].vel = away * frange(2, 7) + vec3(0, frange(1, 4), 0); g_bodies[b].angVel = g_rng.unit() * 8; }
            }
            fxDust(c, 0.8f, 8, rgba(150, 140, 125));
        }
    }
    // incendies sur le bloc retiré
    for (size_t f = 0; f < s_fires.size(); f++)
        if (s_fires[f].i == i && s_fires[f].j == j && s_fires[f].k == k) { s_fires[f] = s_fires.back(); s_fires.pop_back(); break; }
}

bool castleDamageVoxel(int i, int j, int k, float dmg, const vec3& from) {
    if (!vinside(i, j, k)) return false;
    int n = vidx(i, j, k);
    uint8_t m = g_vox[n];
    if (m == M_AIR) return false;
    if (j == 0) dmg *= 0.25f;  // fondations
    if (m == M_WOOD) dmg *= 1.8f;
    if (m == M_RUBBLE) dmg *= 1.5f;
    int hp = g_voxHp[n] - (int)dmg;
    if (hp <= 0) {
        removeVoxel(i, j, k, from, true);
        if (s_crackSndT <= 0) {
            sndPlay(m == M_WOOD ? S_WOOD_BREAK : S_STONE_CRACK, voxCenter(i, j, k), 1.0f, frange(0.85f, 1.1f));
            s_crackSndT = 0.08f;
        }
        return true;
    }
    uint8_t old = g_voxHp[n];
    g_voxHp[n] = (uint8_t)hp;
    // fissure visible : reconstruire le maillage par paliers
    if ((old * 4) / maxHp(m) != (hp * 4) / maxHp(m)) castleMarkDirty(i, k);
    return false;
}

void castleDamageSphere(const vec3& p, float radius, float dmg, bool explosive) {
    int i0 = (int)floorf(p.x - radius - VOX0X), i1 = (int)floorf(p.x + radius - VOX0X);
    int j0 = (int)floorf(p.y - radius - VOX0Y), j1 = (int)floorf(p.y + radius - VOX0Y);
    int k0 = (int)floorf(p.z - radius - VOX0Z), k1 = (int)floorf(p.z + radius - VOX0Z);
    if (i1 < 0 || j1 < 0 || k1 < 0 || i0 >= VX || j0 >= VY || k0 >= VZ) return;
    int destroyed = 0;
    for (int j = j0; j <= j1; j++)
        for (int k = k0; k <= k1; k++)
            for (int i = i0; i <= i1; i++) {
                if (!vinside(i, j, k)) continue;
                if (g_vox[vidx(i, j, k)] == M_AIR) continue;
                float d = length(voxCenter(i, j, k) - p);
                if (d > radius) continue;
                float f = 1 - d / radius;
                if (explosive) f = f * f * 1.4f + 0.1f;
                if (castleDamageVoxel(i, j, k, dmg * f * frange(0.7f, 1.3f), p)) destroyed++;
            }
    if (destroyed > 6) sndPlay(S_COLLAPSE, p, clampf(destroyed / 20.0f, 0.4f, 1.0f), frange(0.8f, 1.0f));
}

void castlePlace(int i, int j, int k, uint8_t m) {
    if (!vinside(i, j, k)) return;
    int n = vidx(i, j, k);
    if (g_vox[n] != M_AIR) return;
    g_vox[n] = m;
    g_voxHp[n] = (uint8_t)maxHp(m);
    if (m == M_KEEP) s_keepCount++; else s_stoneCount++;
    castleMarkDirty(i, k);
    flowFieldDirty();
}

void castleIgnite(int i, int j, int k) {
    if (voxAt(i, j, k) != M_WOOD) return;
    for (auto& f : s_fires) if (f.i == i && f.j == j && f.k == k) return;
    if (s_fires.size() > 80) return;
    VoxFire f = {i, j, k, 0};
    s_fires.push_back(f);
}

// ---------------------------------------------------------------- Effondrements
static void supportCheck() {
    memset(s_visit, 0, sizeof(s_visit));
    int qh = 0, qt = 0;
    const int QMAX = (int)(sizeof(s_queue) / sizeof(s_queue[0]));
    for (int k = 0; k < VZ; k++)
        for (int i = 0; i < VX; i++)
            for (int j = 0; j < 2; j++) {
                int n = vidx(i, j, k);
                if (g_vox[n] == M_AIR) continue;
                bool grounded = (j == 0);
                if (!grounded) {
                    vec3 c = voxCenter(i, j, k);
                    grounded = terrainHeight(c.x, c.z) > c.y - 0.6f;
                }
                if (grounded && qt < QMAX) { s_visit[n] = 1; s_queue[qt++] = n; }
            }
    static const int d6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    while (qh < qt) {
        int n = s_queue[qh++];
        int i = n % VX, k = (n / VX) % VZ, j = n / (VX * VZ);
        for (int d = 0; d < 6; d++) {
            int ni = i + d6[d][0], nj = j + d6[d][1], nk = k + d6[d][2];
            if (!vinside(ni, nj, nk)) continue;
            int m = vidx(ni, nj, nk);
            if (s_visit[m] || g_vox[m] == M_AIR) continue;
            s_visit[m] = 1;
            if (qt < QMAX) s_queue[qt++] = m;
        }
    }
    // blocs non soutenus : chute
    int fallen = 0;
    vec3 sum(0, 0, 0);
    for (int j = 0; j < VY; j++)
        for (int k = 0; k < VZ; k++)
            for (int i = 0; i < VX; i++) {
                int n = vidx(i, j, k);
                if (g_vox[n] == M_AIR || s_visit[n]) continue;
                vec3 c = voxCenter(i, j, k);
                uint8_t m = g_vox[n];
                removeVoxel(i, j, k, c - vec3(0, 1, 0), false);
                sum += c;
                if (fallen < 70) {
                    int b = bodySpawn(BK_BLOCK, c, vec3(0.48f, 0.48f, 0.48f), m == M_WOOD ? rgba(170, 150, 130) : rgba(225, 220, 210),
                                      m == M_WOOD ? BA_PLANKS : BA_STONE, 900);
                    if (b >= 0) {
                        g_bodies[b].vel = vec3(frange(-1, 1), frange(-1, 0.5f), frange(-1, 1));
                        g_bodies[b].angVel = g_rng.unit() * 1.5f;
                    }
                } else {
                    fxDust(c, 1.0f, 3, rgba(150, 140, 125));
                }
                fallen++;
            }
    if (fallen > 0) {
        vec3 c = sum / (float)fallen;
        sndPlay(S_COLLAPSE, c, clampf(0.5f + fallen / 30.0f, 0, 1), frange(0.7f, 0.95f));
        fxDust(c, 3 + sqrtf((float)fallen), 30, rgba(150, 140, 125));
        g_player.shake = fmaxf(g_player.shake, clampf(fallen / 40.0f, 0.1f, 0.8f) * clampf(30.0f / (1 + length(c - g_player.pos)), 0, 1));
        birdsScare(c);
    }
}

void castleUpdate(float dt) {
    s_crackSndT -= dt;
    s_supportT -= dt;
    if (s_supportDirty && s_supportT <= 0) {
        s_supportDirty = false;
        s_supportT = 0.3f;
        supportCheck();
    }
    // incendies des parties en bois
    for (size_t f = 0; f < s_fires.size();) {
        VoxFire& F = s_fires[f];
        F.t += dt;
        vec3 c = voxCenter(F.i, F.j, F.k);
        if (voxAt(F.i, F.j, F.k) != M_WOOD) { s_fires[f] = s_fires.back(); s_fires.pop_back(); continue; }
        if (frand() < dt * 12) fxFire(c + vec3(frange(-0.4f, 0.4f), frange(-0.2f, 0.5f), frange(-0.4f, 0.4f)), 0.5f, 1.0f);
        if (frand() < dt * 4) fxSmoke(c + vec3(0, 0.6f, 0), 0.6f, 1, 0.8f);
        lightAdd(c, 1.0f, 0.55f, 0.2f, 7, 1);
        float rainK = 1 - g_weather.rain * 0.6f;
        int n = vidx(F.i, F.j, F.k);
        if (frand() < dt * 3 * rainK) {
            int hp = g_voxHp[n] - 6;
            if (hp <= 0) { removeVoxel(F.i, F.j, F.k, c, true); continue; }
            g_voxHp[n] = (uint8_t)hp;
            if (hp % 30 < 6) castleMarkDirty(F.i, F.k);
        }
        // propagation
        if (frand() < dt * 0.25f * rainK) {
            static const int d6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            int d = g_rng.irange(0, 5);
            castleIgnite(F.i + d6[d][0], F.j + d6[d][1], F.k + d6[d][2]);
        }
        if (g_weather.rain > 0.7f && frand() < dt * 0.05f) { s_fires[f] = s_fires.back(); s_fires.pop_back(); continue; }
        f++;
    }
    if (!s_fires.empty()) {
        float best = 1e9f;
        for (auto& F : s_fires) best = fminf(best, length(voxCenter(F.i, F.j, F.k) - g_cam.pos));
        sndLoop(LS_FIRE, S_FIRE, clampf(1 - best / 30.0f, 0, 1) * 0.8f, 1);
    }
}

// ---------------------------------------------------------------- Requêtes
float surfaceHeight(float x, float z, float yRef) {
    float h = terrainHeight(x, z);
    int i = (int)floorf(x - VOX0X), k = (int)floorf(z - VOX0Z);
    if (i < 0 || k < 0 || i >= VX || k >= VZ) return h;
    int jStart = (int)floorf(yRef + 1.05f - VOX0Y);
    if (jStart >= VY) jStart = VY - 1;
    for (int j = jStart; j >= 0; j--) {
        if (g_vox[vidx(i, j, k)] != M_AIR) {
            float top = VOX0Y + j + 1;
            return top > h ? top : h;
        }
    }
    return h;
}

bool castleRaycast(const vec3& o, const vec3& d, float maxDist, float& tHit, int& hi, int& hj, int& hk, vec3& normal) {
    vec3 p = o - vec3(VOX0X, VOX0Y, VOX0Z);
    // rapprocher l'origine de la grille
    float tmin = 0, tmax = maxDist;
    float bmin[3] = {0, 0, 0}, bmax[3] = {(float)VX, (float)VY, (float)VZ};
    float po[3] = {p.x, p.y, p.z}, dd[3] = {d.x, d.y, d.z};
    for (int a = 0; a < 3; a++) {
        if (fabsf(dd[a]) < 1e-6f) {
            if (po[a] < bmin[a] || po[a] > bmax[a]) return false;
        } else {
            float t1 = (bmin[a] - po[a]) / dd[a], t2 = (bmax[a] - po[a]) / dd[a];
            if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
    }
    vec3 s = p + d * (tmin + 1e-4f);
    int i = (int)floorf(s.x), j = (int)floorf(s.y), k = (int)floorf(s.z);
    int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    float tdx = fabsf(d.x) > 1e-6f ? fabsf(1 / d.x) : 1e9f;
    float tdy = fabsf(d.y) > 1e-6f ? fabsf(1 / d.y) : 1e9f;
    float tdz = fabsf(d.z) > 1e-6f ? fabsf(1 / d.z) : 1e9f;
    float tmx = fabsf(d.x) > 1e-6f ? ((sx > 0 ? (i + 1 - s.x) : (s.x - i)) * tdx) : 1e9f;
    float tmy = fabsf(d.y) > 1e-6f ? ((sy > 0 ? (j + 1 - s.y) : (s.y - j)) * tdy) : 1e9f;
    float tmz = fabsf(d.z) > 1e-6f ? ((sz > 0 ? (k + 1 - s.z) : (s.z - k)) * tdz) : 1e9f;
    float t = tmin;
    int lastAxis = -1;
    for (int step = 0; step < 400; step++) {
        if (vinside(i, j, k) && g_vox[vidx(i, j, k)] != M_AIR) {
            tHit = t; hi = i; hj = j; hk = k;
            normal = vec3(0, 0, 0);
            if (lastAxis == 0) normal.x = (float)-sx;
            else if (lastAxis == 1) normal.y = (float)-sy;
            else if (lastAxis == 2) normal.z = (float)-sz;
            else normal = -d;
            return true;
        }
        if (tmx < tmy && tmx < tmz) { i += sx; t = tmin + tmx; tmx += tdx; lastAxis = 0; }
        else if (tmy < tmz) { j += sy; t = tmin + tmy; tmy += tdy; lastAxis = 1; }
        else { k += sz; t = tmin + tmz; tmz += tdz; lastAxis = 2; }
        if (t > tmax) return false;
        if (!vinside(i, j, k) && step > 2) {
            if (i < -1 || j < -1 || k < -1 || i > VX || j > VY || k > VZ) return false;
        }
    }
    return false;
}

bool castleRepairTarget(const vec3& o, const vec3& d, int& ri, int& rj, int& rk) {
    float t; int i, j, k; vec3 n;
    if (!castleRaycast(o, d, 5.0f, t, i, j, k, n)) return false;
    int idx = vidx(i, j, k);
    uint8_t m = g_vox[idx];
    if (g_voxHp[idx] < maxHp(m) - 20) { ri = i; rj = j; rk = k; return true; }
    // emplacements d'origine manquants autour du bloc visé
    for (int r = 1; r <= 2; r++)
        for (int dj = -r; dj <= r; dj++)
            for (int dk = -r; dk <= r; dk++)
                for (int di = -r; di <= r; di++) {
                    int a = i + di, b = j + dj, c = k + dk;
                    if (!vinside(a, b, c)) continue;
                    int q = vidx(a, b, c);
                    if (s_orig[q] != M_AIR && g_vox[q] == M_AIR) {
                        // pas à l'intérieur du joueur
                        vec3 cc = voxCenter(a, b, c);
                        vec3 pp = g_player.pos;
                        if (fabsf(cc.x - pp.x) < 0.9f && fabsf(cc.z - pp.z) < 0.9f && cc.y > pp.y - 0.5f && cc.y < pp.y + 2.3f) continue;
                        ri = a; rj = b; rk = c;
                        return true;
                    }
                }
    return false;
}

// rétablit un bloc à son matériau d'origine
void castleRepairCell(int i, int j, int k) {
    int q = vidx(i, j, k);
    if (g_vox[q] == M_AIR) castlePlace(i, j, k, s_orig[q] != M_AIR ? s_orig[q] : M_STONE);
    else { g_voxHp[q] = (uint8_t)maxHp(g_vox[q]); castleMarkDirty(i, k); }
}

void castleSaveExtra(FILE* f) {
    fwrite(g_vox, 1, sizeof(g_vox), f);
    fwrite(g_voxHp, 1, sizeof(g_voxHp), f);
}

void castleLoadExtra(FILE* f) {
    castleGen();  // plan d'origine et totaux
    size_t a = fread(g_vox, 1, sizeof(g_vox), f);
    size_t b = fread(g_voxHp, 1, sizeof(g_voxHp), f);
    (void)a; (void)b;
    s_stoneCount = s_keepCount = 0;
    for (int n = 0; n < VX * VY * VZ; n++) {
        if (g_vox[n] == M_KEEP) s_keepCount++;
        else if (g_vox[n] != M_AIR) s_stoneCount++;
    }
    for (auto& c : s_chunks) c.dirty = true;
    s_supportDirty = true;
}

// Eternal Siege Vita - soldats : modèle articulé, animation, IA, champ de flux
#include "game.h"
#include <queue>

std::vector<Character> g_chars;
const int MAX_CHARS = 110;

struct CharDef {
    const char* name;
    float hp, speed, range, dmg, reload;
    bool ranged, hitscan, melee;
    float spread;
};
static const CharDef s_defs[CT_COUNT] = {
    {"Épéiste", 60, 3.2f, 1.8f, 14, 1.1f, false, false, true, 0},
    {"Lancier", 60, 3.0f, 2.3f, 16, 1.3f, false, false, true, 0},
    {"Hachier", 70, 2.9f, 1.8f, 20, 1.5f, false, false, true, 0},
    {"Archer", 45, 3.1f, 60, 18, 2.8f, true, false, false, 0.05f},
    {"Arbalétrier", 50, 2.8f, 65, 32, 4.5f, true, false, false, 0.03f},
    {"Chevalier", 150, 2.6f, 1.9f, 26, 1.3f, false, false, true, 0},
    {"Piquier", 70, 2.8f, 2.6f, 18, 1.4f, false, false, true, 0},
    {"Arquebusier", 50, 2.9f, 60, 38, 7.0f, true, true, false, 0.06f},
    {"Mousquetaire", 55, 3.0f, 70, 42, 6.0f, true, true, false, 0.045f},
    {"Grenadier", 60, 3.1f, 26, 0, 6.0f, true, false, false, 0.08f},
    {"Fusilier", 60, 3.3f, 90, 34, 2.3f, true, true, false, 0.025f},
    {"Mitrailleur", 75, 2.6f, 90, 13, 0.11f, true, true, false, 0.04f},
    {"Lance-flammes", 85, 2.8f, 13, 6, 0.08f, true, false, false, 0.1f},
    {"Fantassin", 70, 3.6f, 90, 15, 0.14f, true, true, false, 0.03f},
    {"Lance-roquettes", 70, 3.0f, 85, 0, 7.5f, true, false, false, 0.02f},
};

// ---------------------------------------------------------------- Champ de flux
const int FG = 128;
static float s_flow[FG * FG];
static int8_t s_flowDir[FG * FG];
static uint8_t s_cellCost[FG * FG];
static bool s_flowDirty = true;
static float s_flowT = 0;
static const int D8[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

void flowFieldDirty() { s_flowDirty = true; }

static inline int cellOf(float v) { return clampi((int)((v + TER_HALF) / 2.0f), 0, FG - 1); }
static inline float cellCenter(int c) { return -TER_HALF + c * 2.0f + 1.0f; }

static int computeCost(int gi, int gj) {
    float cx = cellCenter(gi), cz = cellCenter(gj);
    int cost = 1;
    bool stone = false, wood = false, keep = false;
    for (int s = 0; s < 4; s++) {
        float x = cx + ((s & 1) ? 0.5f : -0.5f), z = cz + ((s & 2) ? 0.5f : -0.5f);
        float g = terrainHeight(x, z);
        for (float dy = 0.3f; dy < 2.0f; dy += 0.8f) {
            uint8_t m = voxAtWorld(vec3(x, g + dy, z));
            if (m == M_STONE) stone = true;
            else if (m == M_KEEP) keep = true;
            else if (m == M_WOOD) wood = true;
        }
    }
    if (keep) return 250;
    if (stone) return 70;
    if (wood) return 12;
    float h = terrainHeight(cx, cz);
    if (h < WATER_Y - 0.2f) cost += 3;
    float hs = fabsf(terrainHeight(cx + 1, cz) - terrainHeight(cx - 1, cz)) + fabsf(terrainHeight(cx, cz + 1) - terrainHeight(cx, cz - 1));
    if (hs > 2.5f) cost += 4;
    return cost;
}

static void buildFlow() {
    for (int j = 0; j < FG; j++)
        for (int i = 0; i < FG; i++) {
            float x = cellCenter(i), z = cellCenter(j);
            if (fabsf(x) < 45 && fabsf(z) < 45) s_cellCost[j * FG + i] = (uint8_t)computeCost(i, j);
            else s_cellCost[j * FG + i] = (uint8_t)computeCost(i, j);
            s_flow[j * FG + i] = 1e9f;
        }
    typedef std::pair<float, int> QE;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    // objectifs : anneau autour du donjon (cour intérieure)
    for (int j = 0; j < FG; j++)
        for (int i = 0; i < FG; i++) {
            float x = cellCenter(i), z = cellCenter(j);
            float m = fmaxf(fabsf(x), fabsf(z));
            if (m > 10 && m < 13.5f && s_cellCost[j * FG + i] < 50) { s_flow[j * FG + i] = 0; q.push(QE(0, j * FG + i)); }
        }
    while (!q.empty()) {
        QE e = q.top(); q.pop();
        int n = e.second;
        if (e.first > s_flow[n]) continue;
        int i = n % FG, j = n / FG;
        for (int d = 0; d < 8; d++) {
            int ni = i + D8[d][0], nj = j + D8[d][1];
            if (ni < 0 || nj < 0 || ni >= FG || nj >= FG) continue;
            int m = nj * FG + ni;
            float c = s_cellCost[m] * (d >= 4 ? 1.414f : 1.0f);
            if (d >= 4 && (s_cellCost[j * FG + ni] >= 50 || s_cellCost[nj * FG + i] >= 50)) continue;
            float nd = e.first + c;
            if (nd < s_flow[m]) { s_flow[m] = nd; q.push(QE(nd, m)); }
        }
    }
    for (int j = 0; j < FG; j++)
        for (int i = 0; i < FG; i++) {
            int best = -1;
            float bv = s_flow[j * FG + i];
            for (int d = 0; d < 8; d++) {
                int ni = i + D8[d][0], nj = j + D8[d][1];
                if (ni < 0 || nj < 0 || ni >= FG || nj >= FG) continue;
                if (s_flow[nj * FG + ni] < bv) { bv = s_flow[nj * FG + ni]; best = d; }
            }
            s_flowDir[j * FG + i] = (int8_t)best;
        }
}

static vec3 flowDirAt(const vec3& p) {
    int i = cellOf(p.x), j = cellOf(p.z);
    int d = s_flowDir[j * FG + i];
    if (d < 0) return vec3(-p.x, 0, -p.z) * 0.05f;
    vec3 target(cellCenter(i + D8[d][0]), 0, cellCenter(j + D8[d][1]));
    vec3 dir = target - vec3(p.x, 0, p.z);
    return normalize(dir);
}

// ---------------------------------------------------------------- Déplacement commun
static bool blockedAt(const vec3& p, float y0, float r, int* vi, int* vj, int* vk) {
    for (int s = 0; s < 4; s++) {
        float ox = (s & 1) ? r : -r, oz = (s & 2) ? r : -r;
        for (float dy = 0; dy < 1.7f; dy += 0.55f) {
            vec3 q(p.x + ox, y0 + dy, p.z + oz);
            int i, j, k;
            if (worldToVox(q, i, j, k) && g_vox[vidx(i, j, k)] != M_AIR) {
                if (vi) { *vi = i; *vj = j; *vk = k; }
                return true;
            }
        }
    }
    return false;
}

bool actorMove(vec3& pos, float& vy, const vec3& delta, float r, float dt, bool& onGround, int* vi, int* vj, int* vk) {
    bool blocked = false;
    if (onGround && vy < 0) vy = 0;
    // horizontal, axe par axe pour glisser le long des murs
    for (int axis = 0; axis < 2; axis++) {
        vec3 np = pos;
        if (axis == 0) np.x += delta.x; else np.z += delta.z;
        float d = axis == 0 ? delta.x : delta.z;
        if (fabsf(d) < 1e-6f) continue;
        float g = surfaceHeight(np.x, np.z, pos.y);
        float stepY = (g > pos.y && g - pos.y < 1.15f && onGround) ? g : pos.y;
        if (blockedAt(np, stepY + 0.05f, r, vi, vj, vk)) { blocked = true; continue; }
        // marche trop haute (relief)
        if (g - pos.y > 1.15f) { blocked = true; continue; }
        np.x = clampf(np.x, -125, 125); np.z = clampf(np.z, -125, 125);
        pos.x = np.x; pos.z = np.z;
        if (stepY > pos.y) pos.y = stepY;
    }
    // vertical
    vy -= 18.0f * dt;
    if (vy < -40) vy = -40;
    pos.y += vy * dt;
    float g = surfaceHeight(pos.x, pos.z, pos.y + 0.5f);
    onGround = false;
    if (pos.y <= g) {
        pos.y = g;
        onGround = true;
    }
    // la vitesse de chute est rendue à l'appelant pour les dégâts, puis remise à zéro au prochain pas

    // plafond
    int ci, cj, ck;
    if (vy > 0 && worldToVox(pos + vec3(0, 1.85f, 0), ci, cj, ck) && g_vox[vidx(ci, cj, ck)] != M_AIR) vy = 0;
    return blocked;
}

bool lineOfSight(const vec3& a, const vec3& b) {
    vec3 d = b - a;
    float l = length(d);
    if (l < 0.01f) return true;
    vec3 dir = d / l;
    float t; int i, j, k; vec3 n;
    if (castleRaycast(a, dir, l - 0.6f, t, i, j, k, n)) return false;
    for (int s = 1; s < 6; s++) {
        vec3 p = a + d * (s / 6.0f);
        if (terrainHeight(p.x, p.z) > p.y + 0.3f) return false;
    }
    return true;
}

vec3 ballisticAim(const vec3& from, const vec3& to, float v, float g) {
    vec3 d = to - from;
    float dx = sqrtf(d.x * d.x + d.z * d.z), dy = d.y;
    float v2 = v * v;
    float disc = v2 * v2 - g * (g * dx * dx + 2 * dy * v2);
    float ang;
    if (disc < 0) ang = PI * 0.25f;
    else ang = atanf((v2 - sqrtf(disc)) / (g * dx + 1e-4f));
    vec3 h = dx > 1e-4f ? vec3(d.x / dx, 0, d.z / dx) : vec3(0, 0, 1);
    return normalize(h * cosf(ang) + vec3(0, sinf(ang), 0));
}

// ---------------------------------------------------------------- Apparition
void charsInit() {
    g_chars.clear();
    g_chars.resize(MAX_CHARS);
    for (auto& c : g_chars) { c.used = false; c.alive = false; }
    s_flowDirty = true;
}

static Color eraTunic(int era, int team, Rng& r) {
    if (team == TEAM_ALLY) return rgba(40, 60, 140);
    switch (era) {
    case 0: { int p = r.irange(0, 3); Color c[4] = {rgba(120, 40, 35), rgba(70, 90, 50), rgba(110, 90, 60), rgba(90, 70, 50)}; return c[p]; }
    case 1: return r.chance(0.5f) ? rgba(140, 30, 30) : rgba(200, 190, 170);
    case 2: return r.chance(0.5f) ? rgba(180, 150, 40) : rgba(150, 40, 40);
    case 3: return rgba(200, 195, 185);
    case 4: return rgba(40, 45, 75);
    case 5: return rgba(95, 100, 85);
    case 6: return rgba(85, 90, 60);
    case 7: return rgba(150, 135, 100);
    default: return rgba(70, 72, 75);
    }
}

int charSpawn(int team, int type, const vec3& p, int era) {
    int slot = -1;
    for (int i = 0; i < MAX_CHARS; i++) if (!g_chars[i].used) { slot = i; break; }
    if (slot < 0) {
        // recycle le plus vieux cadavre
        float best = -1;
        for (int i = 0; i < MAX_CHARS; i++)
            if (!g_chars[i].alive && g_chars[i].deadT > best) { best = g_chars[i].deadT; slot = i; }
        if (slot < 0) return -1;
    }
    Character& c = g_chars[slot];
    memset(&c, 0, sizeof(c));
    c.used = true; c.alive = true;
    c.team = (uint8_t)team; c.type = (uint8_t)type; c.era = (uint8_t)era;
    c.pos = p;
    const CharDef& d = s_defs[type];
    c.hp = c.maxHp = d.hp * (1 + era * 0.05f);
    c.speed = d.speed * frange(0.85f, 1.15f);
    c.state = CS_WALK;
    c.yaw = PI;
    c.target = -2;
    c.ladder = -1;
    c.vehicle = -1;
    c.post = -1;
    c.reload = frange(0.5f, d.reload);
    c.animT = frange(0, 10);
    Rng r(g_rng.next());
    c.tunic = eraTunic(era, team, r);
    c.pants = scaleC(c.tunic, 0.6f);
    if (era == 4 && team == TEAM_ENEMY) c.pants = rgba(150, 35, 30);
    c.spawnT = 0;
    // échelles pour une partie des fantassins médiévaux
    if (team == TEAM_ENEMY && era <= 2 && (type == CT_SWORD || type == CT_AXE || type == CT_SPEAR) && frand() < 0.3f) c.ladder = -2;
    return slot;
}

int countAlive(int team) {
    int n = 0;
    for (auto& c : g_chars) if (c.alive && c.team == team) n++;
    return n;
}

void charsKillAll() {
    for (auto& c : g_chars) { c.used = false; c.alive = false; }
}

static const vec3 s_posts[] = {
    vec3(-6, 18, 30.2f), vec3(6, 18, 30.2f), vec3(-20, 18, 30.2f), vec3(20, 18, 30.2f),
    vec3(-30.4f, 18, 12), vec3(30.4f, 18, 12), vec3(-30.4f, 18, -12), vec3(30.4f, 18, -12),
};

void alliesSpawn() {
    int have[8] = {0};
    for (auto& c : g_chars) if (c.alive && c.team == TEAM_ALLY && c.post >= 0 && c.post < 8) have[c.post] = 1;
    int era = g_game.era;
    int type = CT_ARCHER;
    if (era == 1) type = CT_CROSSBOW;
    else if (era == 2) type = CT_ARQUEBUS;
    else if (era == 3) type = CT_MUSKET;
    else if (era == 4 || era == 5) type = CT_RIFLE;
    else if (era >= 6) type = CT_ASSAULT;
    for (int p = 0; p < 8; p++) {
        if (have[p]) continue;
        vec3 pos = s_posts[p];
        pos.y = surfaceHeight(pos.x, pos.z, 30);
        int c = charSpawn(TEAM_ALLY, type, pos, era);
        if (c >= 0) { g_chars[c].post = p; g_chars[c].state = CS_IDLE; g_chars[c].yaw = atan2f(pos.x, pos.z) + PI; }
    }
}

// ---------------------------------------------------------------- Dégâts et mort
static void dismember(Character& c, const vec3& dir, int cause) {
    int parts = cause == DC_EXPLOSION ? g_rng.irange(1, 3) : 1;
    for (int k = 0; k < parts; k++) {
        int bit = g_rng.irange(0, 4);  // 0 tête 1 bras G 2 bras D 3 jambe G 4 jambe D
        if (c.limbs & (1 << bit)) continue;
        c.limbs |= (uint8_t)(1 << bit);
        vec3 p = c.pos + vec3(0, bit == 0 ? 1.7f : (bit < 3 ? 1.3f : 0.5f), 0);
        vec3 half = bit == 0 ? vec3(0.11f, 0.13f, 0.12f) : (bit < 3 ? vec3(0.06f, 0.3f, 0.06f) : vec3(0.08f, 0.42f, 0.08f));
        int b = bodySpawn(BK_LIMB, p, half, bit == 0 ? rgba(220, 180, 150) : c.tunic, bit == 0 ? BA_SKIN : BA_CLOTH, 4);
        if (b >= 0) {
            g_bodies[b].vel = dir * frange(4, 9) + vec3(0, frange(3, 7), 0);
            g_bodies[b].angVel = g_rng.unit() * 12;
            g_bodies[b].bleed = 4;
        }
        fxBlood(p, dir, 14);
    }
}

void charDamage(int idx, float dmg, const vec3& from, const vec3& dir, int cause) {
    if (idx < 0 || idx >= MAX_CHARS) return;
    Character& c = g_chars[idx];
    if (!c.alive) return;
    if (c.type == CT_KNIGHT && (cause == DC_ARROW || cause == DC_BLADE)) dmg *= 0.6f;
    c.hp -= dmg;
    c.hitFlash = 0.15f;
    vec3 hp = c.pos + vec3(0, 1.2f, 0);
    if (cause != DC_FIRE) {
        fxBlood(hp, dir, cause == DC_EXPLOSION ? 16 : 6);
        c.bloodiness = fminf(1, c.bloodiness + 0.3f);
        if (cause == DC_BULLET || cause == DC_ARROW) sndPlay(S_HIT_FLESH, hp, 0.7f, frange(0.9f, 1.2f));
    }
    if (cause == DC_FIRE) c.burn = fmaxf(c.burn, 3);
    if (c.hp > 0) {
        if (c.screamCd <= 0 && frand() < 0.5f) { sndPlay(S_GRUNT, hp, 0.7f, frange(0.85f, 1.25f)); c.screamCd = 1.5f; }
        // le soldat sur une échelle peut tomber
        if (c.state == CS_CLIMB && dmg > 15 && frand() < 0.4f) { c.state = CS_WALK; c.vy = 1; c.pos += vec3(sinf(c.yaw), 0, cosf(c.yaw)) * -0.8f; }
        return;
    }
    // mort
#ifdef HOST_TEST
    logf("mort team %d type %d cause %d pos %.1f %.1f %.1f dmg %.0f etat %d", c.team, c.type, cause, c.pos.x, c.pos.y, c.pos.z, dmg, c.state);
#endif
    c.alive = false;
    c.state = CS_DEAD;
    c.deadT = 0;
    vec3 fdir = normalize(vec3(dir.x, 0, dir.z));
    if (length2(fdir) < 0.01f) fdir = vec3(sinf(c.yaw), 0, cosf(c.yaw));
    c.fallAxis = normalize(cross(vec3(0, 1, 0), fdir));
    c.fallAng = 0;
    c.fallVel = cause == DC_EXPLOSION ? 6 : 1.5f;
    c.vel = fdir * (cause == DC_EXPLOSION ? frange(4, 8) : 1.2f);
    c.vy = cause == DC_EXPLOSION ? frange(4, 8) : 0;
    if (frand() < 0.5f) {
        int s = S_SCREAM1 + g_rng.irange(0, 3);
        sndPlay(s, hp, 1.0f, frange(0.85f, 1.2f));
    }
    if ((cause == DC_EXPLOSION && frand() < 0.75f) || (cause == DC_CRUSH && frand() < 0.3f) || (cause == DC_BLADE && frand() < 0.15f)) dismember(c, fdir, cause);
    if (cause == DC_BLADE || cause == DC_EXPLOSION) fxBlood(hp, fdir, 12);
    // casque qui tombe
    if (frand() < 0.35f && c.era < 8) {
        int b = bodySpawn(BK_HELMET, c.pos + vec3(0, 1.8f, 0), vec3(0.13f, 0.08f, 0.14f), rgba(150, 150, 155), BA_METAL, 2);
        if (b >= 0) { g_bodies[b].vel = fdir * 2 + vec3(0, 2, 0); g_bodies[b].angVel = g_rng.unit() * 6; }
    }
    if (c.ladder >= 0 && c.ladder < (int)g_bodies.size()) c.ladder = -1;
    if (c.team == TEAM_ENEMY) { g_game.killed++; if (cause != DC_FALL) g_player.kills += (from.y > -999 ? 0 : 0); }
}

int charAtRay(const vec3& o, const vec3& d, float maxDist, float& tOut, bool& head, int ignoreTeam) {
    int best = -1;
    float bt = maxDist;
    head = false;
    for (int i = 0; i < MAX_CHARS; i++) {
        Character& c = g_chars[i];
        if (!c.alive || c.team == ignoreTeam) continue;
        vec3 rel = c.pos + vec3(0, 0.95f, 0) - o;
        float tc = dot(rel, d);
        if (tc < 0 || tc > bt + 1) continue;
        vec3 closest = o + d * tc;
        float dx = closest.x - c.pos.x, dz = closest.z - c.pos.z;
        // capsule verticale
        float a = d.x * d.x + d.z * d.z;
        float ox = o.x - c.pos.x, oz = o.z - c.pos.z;
        float r = 0.32f;
        float b = 2 * (ox * d.x + oz * d.z), cc = ox * ox + oz * oz - r * r;
        float disc = b * b - 4 * a * cc;
        (void)dx; (void)dz;
        if (disc < 0 || a < 1e-6f) continue;
        float t = (-b - sqrtf(disc)) / (2 * a);
        if (t < 0) t = 0;
        float y = o.y + d.y * t - c.pos.y;
        if (y < 0 || y > 1.85f) continue;
        if (t < bt) { bt = t; best = i; head = y > 1.5f; }
    }
    tOut = bt;
    return best;
}

// ---------------------------------------------------------------- IA
static void shootAt(Character& c, int idx, const vec3& tgt) {
    const CharDef& d = s_defs[c.type];
    vec3 muzzle = c.pos + vec3(0, 1.5f, 0) + vec3(sinf(c.yaw), 0, cosf(c.yaw)) * 0.4f;
    float spread = d.spread * (c.team == TEAM_ALLY ? 0.6f : 1.0f) * (1 + g_weather.rain * 0.3f);
    float dmgK = c.team == TEAM_ALLY ? 1.6f : 1.0f;
    switch (c.type) {
    case CT_ARCHER: case CT_CROSSBOW: {
        float spd = c.type == CT_ARCHER ? 42.0f : 55.0f;
        vec3 dir = ballisticAim(muzzle, tgt, spd, 9.8f);
        dir = normalize(dir + g_rng.unit() * spread);
        bool fire = c.team == TEAM_ENEMY && c.era <= 1 && frand() < 0.2f;
        projSpawn(c.type == CT_ARCHER ? PK_ARROW : PK_BOLT, muzzle, dir * spd, c.team, d.dmg * dmgK, fire);
        sndPlay(c.type == CT_ARCHER ? S_BOW : S_CROSSBOW, muzzle, 0.6f, frange(0.9f, 1.1f));
        break;
    }
    case CT_GRENADIER: {
        vec3 dir = ballisticAim(muzzle, tgt, 18, 9.8f);
        projSpawn(PK_GRENADE, muzzle, normalize(dir + g_rng.unit() * spread) * 18, c.team, 0, false);
        sndPlay(S_THROW, muzzle, 0.6f, 1);
        break;
    }
    case CT_RPG: {
        vec3 dir = normalize(tgt - muzzle + g_rng.unit() * spread * length(tgt - muzzle));
        projSpawn(PK_ROCKET, muzzle, dir * 45, c.team, 0, false);
        sndPlay(S_ROCKET, muzzle, 0.9f, 1);
        break;
    }
    case CT_FLAMER: {
        for (int k = 0; k < 2; k++) {
            vec3 dir = normalize(tgt - muzzle + g_rng.unit() * 1.2f);
            projSpawn(PK_OIL, muzzle, dir * 16, c.team, d.dmg, true);
        }
        break;
    }
    default: {
        vec3 dir = normalize(tgt - muzzle);
        dir = normalize(dir + g_rng.unit() * spread);
        hitscan(muzzle, dir, d.range * 1.3f, d.dmg * dmgK, c.team, c.era >= 4);
        int snd = (c.type == CT_ARQUEBUS || c.type == CT_MUSKET) ? S_MUSKET : (c.type == CT_MG ? S_MG : S_RIFLE);
        sndPlay(snd, muzzle, c.type == CT_MG || c.type == CT_ASSAULT ? 0.55f : 0.8f, frange(0.92f, 1.08f));
        if (c.type == CT_ARQUEBUS || c.type == CT_MUSKET) fxSmoke(muzzle + vec3(sinf(c.yaw), 0, cosf(c.yaw)) * 0.6f, 0.6f, 2, 0.1f);
        spawnParticle(PT_FLASH, muzzle + vec3(sinf(c.yaw), 0, cosf(c.yaw)) * 0.5f, vec3(0, 0, 0), 0.25f, 0.05f, rgba(255, 220, 150));
        break;
    }
    }
    (void)idx;
}

static bool findTarget(Character& c, int self, vec3& tp) {
    const CharDef& d = s_defs[c.type];
    float best = d.range;
    int bi = -2;
    vec3 eye = c.pos + vec3(0, 1.5f, 0);
    if (c.team == TEAM_ENEMY) {
        if (!g_player.dead) {
            float l = length(g_player.pos - c.pos);
            if (l < best * 1.05f && (d.melee || lineOfSight(eye, g_player.pos + vec3(0, 1.5f, 0)))) { best = l * 0.8f; bi = -1; }
        }
    }
    int start = g_rng.irange(0, MAX_CHARS - 1);
    int checks = 0;
    for (int n = 0; n < MAX_CHARS && checks < 12; n++) {
        int i = (start + n) % MAX_CHARS;
        if (i == self) continue;
        Character& o = g_chars[i];
        if (!o.alive || o.team == c.team) continue;
        float l = length(o.pos - c.pos);
        if (l >= best) continue;
        checks++;
        if (!d.melee && !lineOfSight(eye, o.pos + vec3(0, 1.3f, 0))) continue;
        best = l; bi = i;
    }
    c.target = bi;
    if (bi == -2) return false;
    tp = bi == -1 ? g_player.pos + vec3(0, 1.2f, 0) : g_chars[bi].pos + vec3(0, 1.1f, 0);
    return true;
}

static void placeLadder(Character& c, int vi, int vj, int vk) {
    (void)vj;
    vec3 wc = voxCenter(vi, 0, vk);
    // hauteur du mur à cet endroit
    int top = 0;
    for (int j = 0; j < VY; j++) if (g_vox[vidx(vi, j, vk)] != M_AIR) top = j;
    float wallTop = VOX0Y + top + 1;
    float ground = terrainHeight(c.pos.x, c.pos.z);
    float h = wallTop - ground;
    if (h < 2 || h > 13) { c.ladder = -1; return; }
    vec3 out = normalize(vec3(c.pos.x - wc.x, 0, c.pos.z - wc.z));
    float len = h + 0.8f;
    float tilt = 0.22f;
    vec3 base = vec3(c.pos.x, ground, c.pos.z);
    vec3 mid = base + vec3(0, len * 0.5f * cosf(tilt), 0) - out * (len * 0.5f * sinf(tilt));
    int b = bodySpawn(BK_LADDER, mid, vec3(0.3f, len * 0.5f, 0.05f), rgba(200, 180, 150), BA_WOOD, 20);
    if (b < 0) { c.ladder = -1; return; }
    float yaw = atan2f(out.x, out.z);
    g_bodies[b].rot = Mat4::rotY(yaw) * Mat4::rotX(-tilt);
    g_bodies[b].sleeping = true;
    g_bodies[b].hp = 80;
    c.ladder = b;
    c.state = CS_CLIMB;
    c.climbTop = wallTop;
    c.yaw = yaw + PI;
    sndPlay(S_HIT_WOOD, c.pos, 0.8f, 0.7f);
}

static void updateChar(Character& c, int idx, float dt) {
    const CharDef& d = s_defs[c.type];
    c.animT += dt;
    c.reload -= dt;
    c.screamCd -= dt;
    c.hitFlash -= dt;
    c.spawnT += dt;
    // feu
    if (c.burn > 0) {
        c.burn -= dt * (0.6f + g_weather.rain);
        if (inRiver(c.pos + vec3(0, 0.5f, 0))) c.burn = 0;
        if (frand() < dt * 25) fxFire(c.pos + vec3(frange(-0.3f, 0.3f), frange(0.3f, 1.7f), frange(-0.3f, 0.3f)), 0.5f, 1);
        lightAdd(c.pos + vec3(0, 1, 0), 1, 0.5f, 0.15f, 6, 1);
        c.hp -= dt * 14;
        if (c.screamCd <= 0) { sndPlay(S_SCREAM1 + g_rng.irange(0, 3), c.pos + vec3(0, 1.6f, 0), 0.9f, frange(1.0f, 1.3f)); c.screamCd = frange(1.0f, 2.0f); }
        if (c.hp <= 0) { c.hp = 1; charDamage(idx, 10, c.pos, vec3(frange(-1, 1), 0, frange(-1, 1)), DC_FIRE); return; }
        if (c.state != CS_CLIMB) c.state = CS_FLEE;
    }
    vec3 move(0, 0, 0);
    bool onG = c.grounded;
    vec3 tp;
    switch (c.state) {
    case CS_IDLE: {  // allié à son poste
        if (c.post >= 0) {
            vec3 post = s_posts[c.post];
            vec3 to = vec3(post.x - c.pos.x, 0, post.z - c.pos.z);
            if (length(to) > 0.6f) move = normalize(to) * c.speed * 0.6f * dt;
        }
        if (c.reload <= 0) {
            c.reload = 0.6f;
            if (findTarget(c, idx, tp)) {
                c.yaw = atan2f(tp.x - c.pos.x, tp.z - c.pos.z);
                c.state = CS_SHOOT;
                c.actT = 0.5f;
            }
        }
        break;
    }
    case CS_WALK: case CS_FLEE: {
        vec3 dir;
        if (c.state == CS_FLEE) {
            if (c.burn > 0) dir = normalize(vec3(sinf(c.animT * 1.7f + idx), 0, cosf(c.animT * 1.3f + idx)));
            else dir = vec3(c.pos.x * 0.02f, 0, 1);
            dir = normalize(dir);
            if (c.burn <= 0 && c.pos.z > 118) { c.used = false; c.alive = false; return; }
            if (c.burn <= 0 && g_game.waveState != WS_RETREAT) c.state = CS_WALK;
        } else if (c.onWall) {
            // sur le chemin de ronde : vers les défenseurs ou vers la cour
            vec3 goal = (!g_player.dead && length(g_player.pos - c.pos) < 30) ? g_player.pos : vec3(0, 0, 0);
            dir = normalize(vec3(goal.x - c.pos.x, 0, goal.z - c.pos.z));
        } else if (fabsf(c.pos.x) < 14 && fabsf(c.pos.z) < 14) {
            // au pied du donjon : on l'investit par la porte ou on sape ses murs
            float a = (idx * 2.399f);
            vec3 goal = (idx % 3 == 0) ? vec3(0, 0, 0) : vec3(cosf(a) * 9.5f, 0, sinf(a) * 9.5f);
            dir = normalize(vec3(goal.x - c.pos.x, 0, goal.z - c.pos.z));
        } else {
            dir = flowDirAt(c.pos);
        }
        // séparation
        vec3 sep(0, 0, 0);
        for (int n = 0; n < 6; n++) {
            int j = (idx + n * 7 + 1) % MAX_CHARS;
            Character& o = g_chars[j];
            if (!o.alive || j == idx) continue;
            vec3 dd = c.pos - o.pos;
            dd.y = 0;
            float l = length(dd);
            if (l < 0.9f && l > 0.01f) sep += dd / l * (0.9f - l) * 2;
        }
        float spd = c.speed * (inRiver(c.pos + vec3(0, 0.4f, 0)) ? 0.5f : 1.0f) * (c.state == CS_FLEE ? 1.25f : 1.0f);
        vec3 v = normalize(dir + sep * 0.8f) * spd;
        move = v * dt;
        if (length2(dir) > 0.01f) {
            float ty = atan2f(dir.x, dir.z);
            c.yaw += wrapAngle(ty - c.yaw) * clampf(dt * 6, 0, 1);
        }
        // cibles
        if (c.reload <= 0 && c.state == CS_WALK) {
            c.reload = 0.4f + frand() * 0.4f;
            if (findTarget(c, idx, tp)) {
                float l = length(tp - c.pos);
                if (d.melee && l < d.range + 0.3f) { c.state = CS_ATTACK; c.actT = 0; }
                else if (!d.melee) {
                    // les tireurs s'arrêtent pour tirer quand ils sont à portée
                    float stopDist = d.range * (c.team == TEAM_ENEMY ? frange(0.55f, 0.9f) : 1);
                    if (l < stopDist) { c.state = CS_SHOOT; c.actT = 0; }
                }
            } else if (!d.melee && c.team == TEAM_ENEMY) {
                // tir de harcèlement sur les créneaux
                float dc = fmaxf(fabsf(c.pos.x), fabsf(c.pos.z));
                if (dc < 55 && frand() < 0.3f) {
                    c.target = -3;
                    c.state = CS_SHOOT;
                    c.actT = 0;
                }
            }
        }
        break;
    }
    case CS_ATTACK: {
        c.actT += dt;
        vec3 target;
        bool valid = true;
        if (c.target == -1) target = g_player.pos;
        else if (c.target >= 0 && g_chars[c.target].alive) target = g_chars[c.target].pos;
        else valid = false;
        if (!valid) { c.state = CS_WALK; break; }
        vec3 to = target - c.pos;
        c.yaw = atan2f(to.x, to.z);
        float l = length(to);
        if (l > d.range + 0.8f) { c.state = CS_WALK; break; }
        if (c.actT > d.reload * 0.55f && c.actT - dt <= d.reload * 0.55f) {
            sndPlay(S_SWORD_SWING, c.pos + vec3(0, 1.3f, 0), 0.6f, frange(0.9f, 1.2f));
            if (l < d.range + 0.3f) {
                if (c.target == -1) playerDamage(d.dmg * frange(0.8f, 1.2f), c.pos + vec3(0, 1.3f, 0));
                else charDamage(c.target, d.dmg * frange(0.8f, 1.2f), c.pos, normalize(to), DC_BLADE);
                sndPlay(S_SWORD_HIT, target + vec3(0, 1.2f, 0), 0.7f, frange(0.9f, 1.1f));
            }
        }
        if (c.actT > d.reload) c.actT = 0;
        break;
    }
    case CS_SHOOT: {
        c.actT += dt;
        vec3 tgt;
        bool valid = true;
        if (c.target == -1) tgt = g_player.pos + vec3(0, 1.2f, 0);
        else if (c.target >= 0 && g_chars[c.target].alive) tgt = g_chars[c.target].pos + vec3(0, 1.1f, 0);
        else if (c.target == -3) tgt = vec3(c.pos.x * 0.45f, 19, c.pos.z > 0 ? 31 : -31) + vec3(frange(-3, 3), frange(0, 2), 0);
        else valid = false;
        if (!valid) { c.state = c.team == TEAM_ALLY ? CS_IDLE : CS_WALK; break; }
        vec3 to = tgt - c.pos;
        c.yaw += wrapAngle(atan2f(to.x, to.z) - c.yaw) * clampf(dt * 8, 0, 1);
        bool autoW = (c.type == CT_MG || c.type == CT_ASSAULT || c.type == CT_FLAMER);
        float aimT = autoW ? 0.5f : 0.9f;
        if (c.actT > aimT && c.reload <= 0) {
            shootAt(c, idx, tgt);
            if (autoW) {
                c.burst++;
                c.reload = d.reload;
                int burstLen = c.type == CT_FLAMER ? 25 : (c.type == CT_MG ? 18 : 6);
                if (c.burst >= burstLen) { c.burst = 0; c.reload = frange(1.5f, 3.0f); c.actT = 0; if (frand() < 0.3f) c.state = c.team == TEAM_ALLY ? CS_IDLE : CS_WALK; }
            } else {
                c.reload = d.reload * frange(0.85f, 1.2f);
                c.actT = 0;
                if (c.team == TEAM_ENEMY && frand() < 0.35f) c.state = CS_WALK;
                else if (c.team == TEAM_ALLY) c.state = CS_IDLE;
            }
        }
        break;
    }
    case CS_CLIMB: {
        // vérifie que l'échelle tient toujours
        bool ok = c.ladder >= 0 && c.ladder < (int)g_bodies.size() && g_bodies[c.ladder].alive && g_bodies[c.ladder].sleeping;
        if (!ok) {
            c.state = CS_WALK; c.ladder = -1; c.vy = 2;
            c.pos += vec3(-sinf(c.yaw), 0, -cosf(c.yaw)) * 1.2f;
            sndPlay(S_SCREAM1 + g_rng.irange(0, 3), c.pos + vec3(0, 1.5f, 0), 0.9f, frange(1.0f, 1.3f));
            break;
        }
        c.pos.y += dt * 1.3f;
        c.pos += vec3(sinf(c.yaw), 0, cosf(c.yaw)) * dt * 0.06f;
        if (c.pos.y >= c.climbTop - 0.1f) {
            c.pos += vec3(sinf(c.yaw), 0, cosf(c.yaw)) * 1.6f;
            c.pos.y = surfaceHeight(c.pos.x, c.pos.z, c.climbTop + 0.5f);
            c.state = CS_WALK;
            c.onWall = true;
            c.ladder = -1;
        }
        c.vy = 0;
        return;
    }
    }
    // déplacement physique
    int vi = -1, vj = -1, vk = -1;
    bool wasOnGround = true;
    float oldVy = c.vy;
    bool blocked = actorMove(c.pos, c.vy, move, 0.28f, dt, onG, &vi, &vj, &vk);
    (void)wasOnGround;
    bool landed = onG && !c.grounded;
    c.grounded = onG;
    if (landed && oldVy < -13) { charDamage(idx, (-oldVy - 13) * 9, c.pos, vec3(0, -1, 0), DC_FALL); if (!c.alive) return; }
    if (c.onWall && c.pos.y < 14) c.onWall = false;
    if (blocked && c.state == CS_WALK && c.team == TEAM_ENEMY) {
        c.stuckT += dt;
        if (vi >= 0) {
            uint8_t m = voxAt(vi, vj, vk);
            if (c.ladder == -2 && m == M_STONE) { placeLadder(c, vi, vj, vk); return; }
            // frappe le mur ou la porte
            if (d.melee) {
                c.actT += dt;
                if (c.actT > d.reload) {
                    c.actT = 0;
                    float dmg = (m == M_WOOD ? 2.6f : 4.0f) * (c.type == CT_AXE ? 2.0f : 1.0f) * (c.type == CT_KNIGHT ? 1.5f : 1.0f);
                    castleDamageVoxel(vi, vj, vk, dmg, c.pos);
                    sndPlay(m == M_WOOD ? S_HIT_WOOD : S_CLANG, voxCenter(vi, vj, vk), 0.5f, frange(0.8f, 1.1f));
                    if (m != M_WOOD) fxSparks(voxCenter(vi, vj, vk), vec3(0, 1, 0), 2);
                    // frappe aussi le bloc au-dessus pour ouvrir une brèche
                    castleDamageVoxel(vi, vj + 1, vk, dmg * 0.5f, c.pos);
                }
            } else if (c.stuckT > 4) {
                c.target = -3; c.state = CS_SHOOT; c.stuckT = 0;
            }
        }
    } else c.stuckT = 0;
    c.vel = move / fmaxf(dt, 1e-4f);
}

void charsUpdate(float dt) {
    s_flowT -= dt;
    if (s_flowDirty && s_flowT <= 0) { buildFlow(); s_flowDirty = false; s_flowT = 1.0f; }
    for (int i = 0; i < MAX_CHARS; i++) {
        Character& c = g_chars[i];
        if (!c.used) continue;
        if (c.alive) { updateChar(c, i, dt); continue; }
        // cadavre : chute puis immobile
        c.deadT += dt;
        if (c.fallAng < PI * 0.5f) {
            c.fallVel += dt * 7;
            c.fallAng = fminf(PI * 0.5f, c.fallAng + c.fallVel * dt);
            if (c.fallAng >= PI * 0.5f) {
                sndPlay(S_LAND, c.pos, 0.5f, frange(0.8f, 1.1f));
                if (!inRiver(c.pos + vec3(0, 0.3f, 0))) decalAdd(DC_POOL, c.pos.x + c.vel.x * 0.1f, c.pos.z + c.vel.z * 0.1f, 0.2f, frange(0.8f, 1.4f));
            }
        }
        bool og = c.grounded;
        c.vel *= (1 - clampf(dt * 3, 0, 1));
        actorMove(c.pos, c.vy, c.vel * dt, 0.2f, dt, og, nullptr, nullptr, nullptr);
        c.grounded = og;
        if (inRiver(c.pos + vec3(0, 0.3f, 0))) { c.pos.x += dt * 1.2f; c.pos.y = WATER_Y - 0.1f; }
        if (c.deadT > 600) c.used = false;
    }
}

// ---------------------------------------------------------------- Rendu
static void part(Batch& b, const Mat4& m, const vec3& pivot, const Mat4& rot, const vec3& off, const vec3& half, Color c, Mat4* out = nullptr) {
    Mat4 pm = m * Mat4::translate(pivot) * rot;
    if (out) *out = pm;
    emitBox(b, pm * Mat4::translate(off), half, c);
}

static void drawWeapon(const Mat4& hand, int type, int era) {
    Batch& W = g_batches[BA_WOOD];
    Batch& M = g_batches[BA_METAL];
    Color wc = rgba(190, 170, 140), mc = rgba(200, 200, 205), dk = rgba(70, 70, 75);
    switch (type) {
    case CT_SWORD: case CT_KNIGHT:
        emitBox(M, hand * Mat4::translate(vec3(0, -0.05f, 0.45f)), vec3(0.02f, 0.006f, 0.4f), mc);
        emitBox(M, hand * Mat4::translate(vec3(0, -0.05f, 0.04f)), vec3(0.09f, 0.015f, 0.015f), dk);
        if (type == CT_KNIGHT) emitBox(M, hand * Mat4::translate(vec3(-0.45f, -0.1f, 0.05f)), vec3(0.03f, 0.3f, 0.22f), rgba(160, 40, 40));
        break;
    case CT_AXE:
        emitBox(W, hand * Mat4::translate(vec3(0, -0.05f, 0.3f)), vec3(0.025f, 0.025f, 0.45f), wc);
        emitBox(M, hand * Mat4::translate(vec3(0, 0.05f, 0.68f)), vec3(0.015f, 0.12f, 0.09f), mc);
        break;
    case CT_SPEAR: case CT_PIKE: {
        float L = type == CT_PIKE ? 2.4f : 1.2f;
        emitBox(W, hand * Mat4::translate(vec3(0, -0.05f, L * 0.5f)), vec3(0.02f, 0.02f, L), wc);
        emitBox(M, hand * Mat4::translate(vec3(0, -0.05f, L * 1.5f)), vec3(0.03f, 0.01f, 0.12f), mc);
        break;
    }
    case CT_ARCHER:
        emitBox(W, hand * Mat4::translate(vec3(0, 0, 0.1f)) * Mat4::rotX(0.0f), vec3(0.015f, 0.7f, 0.02f), wc);
        break;
    case CT_CROSSBOW:
        emitBox(W, hand * Mat4::translate(vec3(0, 0, 0.25f)), vec3(0.03f, 0.04f, 0.35f), wc);
        emitBox(W, hand * Mat4::translate(vec3(0, 0.02f, 0.55f)), vec3(0.35f, 0.02f, 0.03f), wc);
        break;
    case CT_ARQUEBUS: case CT_MUSKET:
        emitBox(W, hand * Mat4::translate(vec3(0, 0, 0.1f)), vec3(0.03f, 0.05f, 0.35f), wc);
        emitBox(M, hand * Mat4::translate(vec3(0, 0.02f, 0.65f)), vec3(0.015f, 0.015f, 0.55f), dk);
        break;
    case CT_RIFLE:
        emitBox(W, hand * Mat4::translate(vec3(0, 0, 0.15f)), vec3(0.03f, 0.05f, 0.4f), wc);
        emitBox(M, hand * Mat4::translate(vec3(0, 0.02f, 0.6f)), vec3(0.012f, 0.012f, 0.35f), dk);
        break;
    case CT_MG: case CT_ASSAULT:
        emitBox(M, hand * Mat4::translate(vec3(0, 0, 0.25f)), vec3(0.035f, 0.07f, 0.4f), dk);
        emitBox(M, hand * Mat4::translate(vec3(0, -0.12f, 0.2f)), vec3(0.02f, 0.08f, 0.04f), dk);
        break;
    case CT_RPG:
        emitCylinder(M, hand * Mat4::translate(vec3(0, 0.08f, -0.4f)) * Mat4::rotX(PI * 0.5f), 0.05f, 1.1f, 6, rgba(80, 90, 70), true);
        break;
    case CT_FLAMER:
        emitBox(M, hand * Mat4::translate(vec3(0, 0, 0.35f)), vec3(0.025f, 0.025f, 0.4f), dk);
        break;
    case CT_GRENADIER:
        emitBox(W, hand * Mat4::translate(vec3(0, 0, 0.15f)), vec3(0.03f, 0.05f, 0.4f), wc);
        break;
    }
    (void)era;
}

static void drawHelmet(const Mat4& head, int era, int type, Color tunic) {
    Batch& M = g_batches[BA_METAL];
    Batch& C = g_batches[BA_CLOTH];
    Color mc = rgba(175, 175, 180);
    switch (era) {
    case 0:
        emitBox(M, head * Mat4::translate(vec3(0, 0.22f, 0)), vec3(0.125f, 0.08f, 0.135f), mc);
        emitBox(M, head * Mat4::translate(vec3(0, 0.1f, 0.13f)), vec3(0.015f, 0.07f, 0.01f), mc);
        break;
    case 1:
        emitCone(M, head * Mat4::translate(vec3(0, 0.16f, 0)), 0.15f, 0.22f, 6, mc);
        if (type == CT_KNIGHT) emitBox(M, head * Mat4::translate(vec3(0, 0.12f, 0.02f)), vec3(0.13f, 0.14f, 0.14f), mc);
        break;
    case 2:
        emitBox(M, head * Mat4::translate(vec3(0, 0.22f, 0)), vec3(0.2f, 0.015f, 0.12f), mc);
        emitCone(M, head * Mat4::translate(vec3(0, 0.22f, 0)), 0.13f, 0.14f, 6, mc);
        break;
    case 3:
        emitCone(C, head * Mat4::translate(vec3(0, 0.2f, 0)), 0.2f, 0.1f, 3, rgba(35, 30, 30));
        break;
    case 4:
        emitCylinder(C, head * Mat4::translate(vec3(0, 0.18f, 0)), 0.12f, 0.12f, 6, scaleC(tunic, 1.2f), true);
        break;
    default: {
        Color hc = era == 5 ? rgba(90, 95, 85) : (era == 6 ? rgba(85, 90, 65) : (era == 7 ? rgba(140, 125, 95) : rgba(60, 62, 65)));
        emitBox(M, head * Mat4::translate(vec3(0, 0.2f, 0)), vec3(0.15f, 0.07f, 0.16f), hc);
        emitBox(M, head * Mat4::translate(vec3(0, 0.13f, 0)), vec3(0.17f, 0.015f, 0.18f), hc);
        break;
    }
    }
}

static void drawChar(Character& c, int idx) {
    vec3 center = c.pos + vec3(0, 0.9f, 0);
    if (!sphereVisible(center, 1.6f)) return;
    float dist = length(center - g_cam.pos);
    if (dist > 140) return;
    bool lod = dist > 45;
    Mat4 m;
    if (c.state == CS_DEAD) m = Mat4::translate(c.pos) * Mat4::rotAxis(c.fallAxis, c.fallAng) * Mat4::rotY(c.yaw);
    else m = Mat4::translate(c.pos) * Mat4::rotY(c.yaw);
    float day = 1.0f;
    Color tun = c.tunic, pan = c.pants;
    if (c.burn > 0 || c.bloodiness > 0) {
        float k = 1 - c.bloodiness * 0.3f;
        tun = rgba((int)(tun.r * (1 - c.bloodiness * 0.1f)), (int)(tun.g * k), (int)(tun.b * k));
    }
    if (c.hitFlash > 0) tun = rgba(255, 120, 120);
    Color skin = rgba(225, 185, 155), boot = rgba(80, 60, 45), leather = rgba(130, 95, 60);
    (void)day;
    Batch& CL = g_batches[BA_CLOTH];
    Batch& SK = g_batches[BA_SKIN];
    Batch& LE = g_batches[BA_LEATHER];
    Batch& ME = g_batches[BA_METAL];
    // paramètres d'animation
    float speed = length(vec3(c.vel.x, 0, c.vel.z));
    float ph = c.animT * (5.5f + speed * 0.8f);
    float walk = clampf(speed / 3.0f, 0, 1);
    float legL = sinf(ph) * 0.55f * walk, legR = -legL;
    float kneeL = fmaxf(0, -sinf(ph - 0.6f)) * 0.9f * walk, kneeR = fmaxf(0, sinf(ph - 0.6f)) * 0.9f * walk;
    float armL = -legL * 0.7f, armR = legR * 0.7f * -1;
    float elbowL = 0.25f, elbowR = 0.25f;
    float lean = walk * 0.12f;
    float armLz = 0.08f, armRz = -0.08f;
    const CharDef& d = s_defs[c.type];
    bool gun = !d.melee && c.type != CT_ARCHER && c.type != CT_GRENADIER;
    switch (c.state) {
    case CS_ATTACK: {
        float t = c.actT / fmaxf(0.1f, d.reload);
        float sw = t < 0.5f ? -t * 2 * 2.4f : -2.4f + (t - 0.5f) * 2 * 3.4f;
        armR = sw; elbowR = 0.3f; lean = 0.15f;
        armL = -0.5f;
        break;
    }
    case CS_SHOOT:
        if (c.type == CT_ARCHER) { armL = -1.5f; elbowL = 0; armR = -1.4f; elbowR = 1.6f; }
        else if (c.type == CT_GRENADIER) { armR = c.actT < 0.7f ? -2.6f : -0.5f; elbowR = 0.4f; }
        else { armL = -1.35f; elbowL = 0.6f; armR = -1.2f; elbowR = 1.2f; armLz = 0.35f; }
        break;
    case CS_CLIMB: {
        float cph = c.animT * 5;
        armL = -2.6f + sinf(cph) * 0.4f; armR = -2.6f - sinf(cph) * 0.4f;
        legL = -0.6f + sinf(cph) * 0.5f; legR = -0.6f - sinf(cph) * 0.5f;
        kneeL = 0.9f; kneeR = 0.9f; elbowL = elbowR = 0.5f;
        break;
    }
    case CS_FLEE:
        if (c.burn > 0) { armL = -2.5f + sinf(c.animT * 14) * 0.8f; armR = -2.5f + cosf(c.animT * 13) * 0.8f; armLz = 0.6f; armRz = -0.6f; }
        break;
    case CS_DEAD:
        armL = -0.3f; armR = 0.2f; armLz = 0.9f; armRz = -1.1f; legL = 0.1f; legR = -0.2f;
        break;
    default:
        if (gun && walk < 0.2f) { armL = -1.0f; armR = -0.9f; elbowL = 0.9f; elbowR = 1.2f; }
        if (c.state == CS_IDLE) lean = 0;
        break;
    }
    if (gun && c.state == CS_WALK) { armL = -0.9f; armR = -0.8f; elbowL = 0.9f; elbowR = 1.1f; }
    // bassin et jambes
    float bob = fabsf(sinf(ph)) * 0.04f * walk;
    Mat4 hip = m * Mat4::translate(vec3(0, bob, 0));
    emitBox(CL, hip * Mat4::translate(vec3(0, 0.95f, 0)), vec3(0.17f, 0.1f, 0.11f), pan);
    for (int s = -1; s <= 1; s += 2) {
        if (c.limbs & (1 << (s < 0 ? 3 : 4))) continue;
        float lg = s < 0 ? legL : legR, kn = s < 0 ? kneeL : kneeR;
        Mat4 up;
        part(CL, hip, vec3(s * 0.1f, 0.9f, 0), Mat4::rotX(-lg), vec3(0, -0.22f, 0), vec3(0.075f, 0.22f, 0.08f), pan, &up);
        if (!lod) {
            Mat4 lo;
            part(LE, up, vec3(0, -0.44f, 0), Mat4::rotX(kn), vec3(0, -0.2f, 0), vec3(0.065f, 0.21f, 0.07f), boot, &lo);
            emitBox(LE, lo * Mat4::translate(vec3(0, -0.42f, 0.05f)), vec3(0.06f, 0.035f, 0.12f), boot);
        } else {
            emitBox(LE, up * Mat4::translate(vec3(0, -0.66f, 0.02f)), vec3(0.065f, 0.22f, 0.08f), boot);
        }
    }
    // torse
    Mat4 torso = hip * Mat4::translate(vec3(0, 1.03f, 0)) * Mat4::rotX(lean);
    emitBox(CL, torso * Mat4::translate(vec3(0, 0.28f, 0)), vec3(0.21f, 0.29f, 0.13f), tun);
    if (!lod) {
        emitBox(LE, torso * Mat4::translate(vec3(0, 0.04f, 0)), vec3(0.215f, 0.035f, 0.135f), leather);
        if (c.type == CT_KNIGHT || (c.era == 1 && c.team == TEAM_ENEMY && c.type == CT_SWORD))
            emitBox(ME, torso * Mat4::translate(vec3(0, 0.33f, 0)), vec3(0.22f, 0.22f, 0.14f), rgba(170, 170, 175));
        if (c.era >= 5 && c.team == TEAM_ENEMY) emitBox(LE, torso * Mat4::translate(vec3(0, 0.15f, -0.17f)), vec3(0.15f, 0.15f, 0.06f), scaleC(tun, 0.7f));
    }
    // tête
    if (!(c.limbs & 1)) {
        Mat4 head = torso * Mat4::translate(vec3(0, 0.6f, 0));
        emitBox(SK, head * Mat4::translate(vec3(0, 0.12f, 0)), vec3(0.105f, 0.125f, 0.115f), skin);
        drawHelmet(head, c.era, c.type, c.tunic);
    } else {
        emitBox(g_batches[BA_WHITE], torso * Mat4::translate(vec3(0, 0.6f, 0)), vec3(0.07f, 0.03f, 0.07f), rgba(120, 10, 10));
    }
    // bras
    Mat4 handR;
    bool hasR = false;
    for (int s = -1; s <= 1; s += 2) {
        if (c.limbs & (1 << (s < 0 ? 1 : 2))) continue;
        float a = s < 0 ? armL : armR, e = s < 0 ? elbowL : elbowR, z = s < 0 ? armLz : armRz;
        Mat4 up;
        part(CL, torso, vec3(s * 0.27f, 0.52f, 0), Mat4::rotZ(z) * Mat4::rotX(a), vec3(0, -0.15f, 0), vec3(0.06f, 0.16f, 0.065f), tun, &up);
        if (lod) continue;
        Mat4 lo;
        part(CL, up, vec3(0, -0.3f, 0), Mat4::rotX(-e), vec3(0, -0.14f, 0), vec3(0.05f, 0.14f, 0.055f), tun, &lo);
        emitBox(SK, lo * Mat4::translate(vec3(0, -0.31f, 0)), vec3(0.045f, 0.05f, 0.045f), skin);
        if (s > 0) { handR = lo * Mat4::translate(vec3(0, -0.31f, 0)) * Mat4::rotX(PI * 0.5f); hasR = true; }
    }
    if (hasR && c.state != CS_DEAD) drawWeapon(handR, c.type, c.era);
    if (c.state != CS_DEAD && dist < 40) shadowBlob(c.pos, 0.45f, 0.45f);
    (void)idx;
}

void charsDraw() {
    for (int i = 0; i < MAX_CHARS; i++) {
        Character& c = g_chars[i];
        if (!c.used) continue;
        drawChar(c, i);
    }
}

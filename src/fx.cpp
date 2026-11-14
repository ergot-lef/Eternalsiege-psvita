// Eternal Siege Vita - particules, décalques (sang, brûlures), explosions
#include "game.h"
#include <vitaGL.h>

static std::vector<Particle> s_parts;
static int s_partNext = 0;
const int MAX_PARTS = 2600;
std::vector<Decal> g_decals;
static int s_decalNext = 0;
const int MAX_DECALS = 640;
static Batch s_bSmoke, s_bFire, s_bSpark, s_bSoft, s_bDecal[5], s_bShadow, s_bDebris;
struct Blob { vec3 p; float r, s; };
static std::vector<Blob> s_blobs;
struct TLight { vec3 p; float life, maxLife, radius; float r, g, b; };
static std::vector<TLight> s_tlights;

void fxInit() {
    s_parts.assign(MAX_PARTS, Particle());
    for (auto& p : s_parts) p.life = 0;
    g_decals.clear();
    s_decalNext = 0;
    s_bSmoke.init(TEX_SMOKE, BM_ALPHA, false);
    s_bFire.init(TEX_FIRE, BM_ADD, false);
    s_bSpark.init(TEX_SPARK, BM_ADD, false);
    s_bSoft.init(TEX_SOFT, BM_ALPHA, false);
    s_bDebris.init(TEX_WHITE, BM_ALPHA, false);
    s_bDecal[DC_BLOOD].init(TEX_BLOOD, BM_ALPHA, false);
    s_bDecal[DC_POOL].init(TEX_POOL, BM_ALPHA, false);
    s_bDecal[DC_SCORCH].init(TEX_SCORCH, BM_ALPHA, false);
    s_bDecal[DC_CRATER].init(TEX_SCORCH, BM_ALPHA, false);
    s_bDecal[DC_MUD].init(TEX_SMOKE, BM_ALPHA, false);
    s_bShadow.init(TEX_SOFT, BM_ALPHA, false);
    s_tlights.clear();
}

void spawnParticle(int type, const vec3& p, const vec3& v, float size, float life, Color c) {
    // réduit la charge si le point est loin et hors champ
    vec3 d = p - g_cam.pos;
    float dist2 = length2(d);
    if (dist2 > 120 * 120) return;
    if (dist2 > 30 * 30 && dot(d, g_cam.fwd) < 0) return;
    Particle& q = s_parts[s_partNext];
    s_partNext = (s_partNext + 1) % MAX_PARTS;
    q.pos = p; q.vel = v; q.size = size; q.life = life; q.maxLife = life;
    q.col = c; q.type = (uint8_t)type;
    q.rot = frange(0, 6.28f); q.rotV = frange(-1, 1);
    q.grow = 0; q.gravity = 0; q.drag = 0.5f;
    switch (type) {
    case PT_SMOKE: q.grow = size * 1.2f; q.gravity = -0.6f; q.drag = 0.8f; break;
    case PT_DUST: q.grow = size * 0.9f; q.gravity = 0.2f; q.drag = 1.5f; break;
    case PT_FIRE: q.grow = -size * 0.4f; q.gravity = -2.5f; q.drag = 1.0f; break;
    case PT_FLASH: q.grow = size * 3; break;
    case PT_EMBER: q.gravity = -1.2f; q.drag = 0.4f; q.rotV = 0; break;
    case PT_SPARK: q.gravity = 9.8f; q.drag = 0.3f; break;
    case PT_BLOOD: q.gravity = 9.8f; q.drag = 0.2f; break;
    case PT_SPLASH: q.gravity = 6; q.drag = 0.4f; q.grow = size * 2; break;
    case PT_DEBRIS: q.gravity = 9.8f; q.drag = 0.2f; break;
    case PT_TRACER: q.gravity = 0; q.drag = 0; break;
    }
}

void fxBlood(const vec3& p, const vec3& dir, int amount) {
    for (int i = 0; i < amount; i++) {
        vec3 v = dir * frange(1, 4) + g_rng.unit() * 1.6f + vec3(0, frange(0.5f, 2.5f), 0);
        spawnParticle(PT_BLOOD, p, v, frange(0.04f, 0.1f), frange(0.6f, 1.4f), rgba(110 + g_rng.irange(0, 40), 5, 5, 230));
    }
    spawnParticle(PT_DUST, p, dir * 0.6f, 0.25f, 0.5f, rgba(120, 10, 10, 120));
}

void fxDust(const vec3& p, float radius, int amount, Color c) {
    for (int i = 0; i < amount; i++) {
        vec3 o = g_rng.unit() * radius * 0.6f;
        spawnParticle(PT_DUST, p + o, o * 0.8f + vec3(0, frange(0.2f, 1.2f), 0), frange(0.3f, 0.7f) * (0.5f + radius * 0.4f), frange(1.5f, 4), c);
    }
}

void fxSparks(const vec3& p, const vec3& n, int amount) {
    for (int i = 0; i < amount; i++)
        spawnParticle(PT_SPARK, p, (n + g_rng.unit() * 0.8f) * frange(2, 7), 0.04f, frange(0.2f, 0.5f), rgba(255, 210, 120));
}

void fxSplash(const vec3& p, float s) {
    for (int i = 0; i < (int)(6 + s * 14); i++)
        spawnParticle(PT_SPLASH, vec3(p.x, WATER_Y, p.z), vec3(frange(-1, 1) * s, frange(2, 5) * s, frange(-1, 1) * s), 0.1f + s * 0.1f, frange(0.5f, 1.0f), rgba(200, 215, 230, 170));
    sndPlay(S_SPLASH, p, clampf(s, 0.3f, 1), frange(0.8f, 1.2f) / (0.6f + s * 0.4f));
}

void fxFire(const vec3& p, float radius, float intensity) {
    spawnParticle(PT_FIRE, p, vec3(frange(-0.3f, 0.3f), frange(0.5f, 1.5f), frange(-0.3f, 0.3f)), radius * frange(0.5f, 0.9f), frange(0.5f, 1.0f), rgba(255, 255, 255, (int)(200 * clampf(intensity, 0.2f, 1))));
    if (frand() < 0.15f) spawnParticle(PT_EMBER, p, vec3(frange(-1, 1), frange(1, 3), frange(-1, 1)), 0.03f, frange(1, 2.5f), rgba(255, 160, 60));
}

void fxSmoke(const vec3& p, float radius, float amount, float dark) {
    int n = (int)(amount + frand());
    for (int i = 0; i < n; i++) {
        int c = (int)(lerpf(150, 40, dark));
        spawnParticle(PT_SMOKE, p + g_rng.unit() * radius * 0.3f, vec3(frange(-0.3f, 0.3f), frange(0.8f, 1.6f), frange(-0.3f, 0.3f)), radius * frange(0.5f, 0.9f), frange(3, 6), rgba(c, c, c, 150));
    }
}

void fxTracer(const vec3& a, const vec3& b) {
    Particle& q = s_parts[s_partNext];
    s_partNext = (s_partNext + 1) % MAX_PARTS;
    q.pos = a; q.vel = b - a; q.life = 0.06f; q.maxLife = 0.06f; q.size = 0.03f; q.type = PT_TRACER;
    q.col = rgba(255, 220, 150, 200); q.grow = 0; q.gravity = 0; q.drag = 0; q.rot = 0; q.rotV = 0;
}

static void addTLight(const vec3& p, float radius, float life, float r, float g, float b) {
    if (s_tlights.size() > 24) s_tlights.erase(s_tlights.begin());
    TLight l; l.p = p; l.radius = radius; l.life = life; l.maxLife = life; l.r = r; l.g = g; l.b = b;
    s_tlights.push_back(l);
}

void fxUpdate(float dt) {
    vec3 wind(cosf(g_weather.windDir) * g_weather.wind, 0, sinf(g_weather.windDir) * g_weather.wind);
    for (auto& q : s_parts) {
        if (q.life <= 0) continue;
        q.life -= dt;
        if (q.type == PT_TRACER) continue;
        q.vel.y -= q.gravity * dt;
        q.vel *= (1 - clampf(q.drag * dt, 0, 1));
        if (q.type == PT_SMOKE || q.type == PT_DUST || q.type == PT_FIRE || q.type == PT_EMBER) q.pos += wind * dt * 2.2f;
        q.pos += q.vel * dt;
        q.size += q.grow * dt;
        if (q.size < 0.01f) q.life = 0;
        q.rot += q.rotV * dt;
        if (q.type == PT_BLOOD || q.type == PT_DEBRIS || q.type == PT_SPARK) {
            float g = surfaceHeight(q.pos.x, q.pos.z, q.pos.y + 0.2f);
            if (q.pos.y < g) {
                if (q.type == PT_BLOOD) {
                    if (frand() < 0.25f && !inRiver(q.pos)) decalAdd(DC_BLOOD, q.pos.x, q.pos.z, frange(0.15f, 0.45f));
                    q.life = 0;
                } else {
                    q.pos.y = g; q.vel.y *= -0.3f; q.vel.x *= 0.5f; q.vel.z *= 0.5f;
                }
            }
        }
    }
    for (size_t i = 0; i < s_tlights.size();) {
        s_tlights[i].life -= dt;
        if (s_tlights[i].life <= 0) { s_tlights.erase(s_tlights.begin() + i); continue; }
        TLight& l = s_tlights[i];
        float f = l.life / l.maxLife;
        lightAdd(l.p, l.r * f, l.g * f, l.b * f, l.radius, 4);
        i++;
    }
    decalsUpdate(dt);
}

void fxDraw() {
    vec3 R = g_cam.right, U = g_cam.up;
    float light = 0.25f + g_weather.daylight * 0.75f;
    for (auto& q : s_parts) {
        if (q.life <= 0) continue;
        float f = q.life / q.maxLife;
        if (!sphereVisible(q.pos, q.size + 0.5f)) continue;
        Color c = q.col;
        switch (q.type) {
        case PT_SMOKE: case PT_DUST: {
            float a = f * (q.type == PT_SMOKE ? 1.0f : 0.8f) * smoothstepf(0, 0.1f, 1 - f + 0.05f);
            c = rgba((int)(c.r * light), (int)(c.g * light), (int)(c.b * light), (int)(c.a * a));
            emitBillboard(s_bSmoke, q.pos, q.size, q.rot, c, R, U);
            break;
        }
        case PT_FIRE: case PT_FLASH: {
            Color cc = rgba(c.r, (int)(c.g * (0.6f + f * 0.4f)), (int)(c.b * f), (int)(c.a * f));
            emitBillboard(s_bFire, q.pos, q.size, q.rot, cc, R, U);
            break;
        }
        case PT_EMBER: case PT_SPARK:
            emitBillboard(s_bSpark, q.pos, q.size * 2.5f, 0, rgba(c.r, c.g, c.b, (int)(255 * f)), R, U);
            break;
        case PT_BLOOD: case PT_SPLASH:
            emitBillboard(s_bSoft, q.pos, q.size, q.rot, rgba((int)(c.r * light), (int)(c.g * light), (int)(c.b * light), (int)(c.a * fminf(1, f * 2))), R, U);
            break;
        case PT_DEBRIS:
            emitBillboard(s_bDebris, q.pos, q.size, q.rot, rgba((int)(c.r * light), (int)(c.g * light), (int)(c.b * light), 255), R, U);
            break;
        case PT_TRACER: {
            vec3 a = q.pos, b = q.pos + q.vel;
            vec3 dir = normalize(b - a);
            vec3 side = normalize(cross(dir, g_cam.fwd)) * 0.025f;
            emitQuad(s_bSpark, a - side, a + side, b + side, b - side, vec3(0, 1, 0), rgba(255, 230, 160, 220), 0.5f, 0, 0.5f, 1);
            break;
        }
        }
    }
    glDisable(GL_CULL_FACE);
    batchFlush(s_bSoft);
    batchFlush(s_bDebris);
    batchFlush(s_bSmoke);
    glDisable(GL_FOG);
    batchFlush(s_bFire);
    batchFlush(s_bSpark);
    glEnable(GL_FOG);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

// ---------------------------------------------------------------- Décalques
void decalAdd(int type, float x, float z, float size, float growTo) {
    if (fabsf(x) > 127 || fabsf(z) > 127) return;
    Decal d;
    d.x = x; d.z = z; d.size = size; d.rot = frange(0, 6.28f); d.alpha = 1; d.growTo = growTo; d.type = (uint8_t)type;
    if ((int)g_decals.size() < MAX_DECALS) g_decals.push_back(d);
    else { g_decals[s_decalNext] = d; s_decalNext = (s_decalNext + 1) % MAX_DECALS; }
}

void decalsUpdate(float dt) {
    for (auto& d : g_decals) {
        if (d.growTo > d.size) d.size = fminf(d.growTo, d.size + dt * 0.12f);
        if ((d.type == DC_BLOOD || d.type == DC_POOL) && g_weather.rain > 0.3f) d.alpha = fmaxf(0.35f, d.alpha - dt * 0.002f * g_weather.rain);
    }
}

void decalsDraw() {
    Color tint;
    float light = 0.3f + g_weather.daylight * 0.7f;
    vec3 n(0, 1, 0);
    for (auto& d : g_decals) {
        vec3 c(d.x, 0, d.z);
        float dx = d.x - g_cam.pos.x, dz = d.z - g_cam.pos.z;
        if (dx * dx + dz * dz > 90 * 90) continue;
        float base = surfaceHeight(d.x, d.z, 40);
        c.y = base;
        if (!sphereVisible(c, d.size)) continue;
        float s = d.size;
        float cs = cosf(d.rot) * s, sn = sinf(d.rot) * s;
        vec3 ax(cs, 0, sn), az(-sn, 0, cs);
        switch (d.type) {
        case DC_CRATER: tint = rgbaf(0.6f * light, 0.55f * light, 0.5f * light, d.alpha); break;
        case DC_MUD: tint = rgbaf(0.25f * light, 0.18f * light, 0.1f * light, d.alpha * 0.8f); break;
        default: tint = rgbaf(light, light, light, d.alpha); break;
        }
        // 3x3 sommets suivant le relief
        vec3 P[3][3];
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 3; i++) {
                vec3 p = c + ax * (i - 1.0f) + az * (j - 1.0f);
                float h = surfaceHeight(p.x, p.z, base + 0.5f);
                if (fabsf(h - base) > 1.2f) h = base;  // évite de déborder sur les murs
                p.y = h + 0.05f;
                P[j][i] = p;
            }
        Batch& b = s_bDecal[d.type];
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                emitQuad(b, P[j][i], P[j + 1][i], P[j + 1][i + 1], P[j][i + 1], n, tint, i * 0.5f, j * 0.5f, (i + 1) * 0.5f, (j + 1) * 0.5f);
    }
    glDisable(GL_CULL_FACE);
    batchFlush(s_bDecal[DC_CRATER]);
    batchFlush(s_bDecal[DC_SCORCH]);
    batchFlush(s_bDecal[DC_MUD]);
    batchFlush(s_bDecal[DC_POOL]);
    batchFlush(s_bDecal[DC_BLOOD]);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

void shadowBlob(const vec3& p, float radius, float strength) {
    if (s_blobs.size() > 300) return;
    Blob b; b.p = p; b.r = radius; b.s = strength;
    s_blobs.push_back(b);
}

void shadowsDraw() {
    float k = 0.3f + g_weather.daylight * 0.5f;
    vec3 n(0, 1, 0);
    for (auto& b : s_blobs) {
        float y = surfaceHeight(b.p.x, b.p.z, b.p.y + 0.3f) + 0.06f;
        float h = b.p.y - y;
        float a = b.s * k * clampf(1 - h / 6.0f, 0, 1);
        if (a <= 0.02f) continue;
        vec3 c(b.p.x, y, b.p.z);
        float r = b.r * (1 + h * 0.1f);
        emitQuad(s_bShadow, c + vec3(-r, 0, -r), c + vec3(-r, 0, r), c + vec3(r, 0, r), c + vec3(r, 0, -r), n, rgbaf(0, 0, 0, a), 0, 0, 1, 1);
    }
    s_blobs.clear();
    glDisable(GL_CULL_FACE);
    batchFlush(s_bShadow);
    glEnable(GL_CULL_FACE);
}

// ---------------------------------------------------------------- Explosion
void explosion(const vec3& p, float power, int team) {
    (void)team;
    float r = 2.0f + power * 3.0f;
    // visuel
    spawnParticle(PT_FLASH, p, vec3(0, 0, 0), r * 0.6f, 0.15f, rgba(255, 240, 200));
    for (int i = 0; i < (int)(8 + power * 14); i++) {
        vec3 v = g_rng.unit() * frange(1, 4) * (0.5f + power);
        v.y = fabsf(v.y) + 1;
        spawnParticle(PT_FIRE, p + g_rng.unit() * r * 0.25f, v, r * frange(0.25f, 0.5f), frange(0.4f, 0.9f), rgba(255, 255, 255, 230));
    }
    for (int i = 0; i < (int)(6 + power * 12); i++) {
        vec3 v = g_rng.unit() * frange(0.5f, 2.5f);
        v.y = fabsf(v.y) + 0.8f;
        spawnParticle(PT_SMOKE, p + g_rng.unit() * r * 0.3f, v, r * frange(0.3f, 0.55f), frange(4, 9), rgba(55, 52, 50, 200));
    }
    fxSparks(p, vec3(0, 1, 0), (int)(10 + power * 15));
    float ground = terrainHeight(p.x, p.z);
    bool nearGround = p.y - ground < 2.5f;
    if (nearGround) {
        for (int i = 0; i < (int)(10 + power * 20); i++)
            spawnParticle(PT_DEBRIS, p + vec3(0, 0.2f, 0), vec3(frange(-4, 4), frange(4, 12), frange(-4, 4)) * (0.5f + power * 0.5f), frange(0.06f, 0.18f), frange(1, 3), rgba(70, 55, 40));
        if (inRiver(vec3(p.x, ground, p.z))) fxSplash(p, 1.5f + power);
        else {
            decalAdd(DC_SCORCH, p.x, p.z, r * 0.8f);
            if (power > 0.6f) { decalAdd(DC_CRATER, p.x, p.z, r * 0.5f); terrainDeform(p, r * 0.8f, power * 1.2f); }
        }
    }
    addTLight(p + vec3(0, 1, 0), r * 4, 0.35f + power * 0.2f, 2.5f, 1.6f, 0.7f);
    float d = length(p - g_cam.pos);
    sndPlay(power > 0.6f ? S_EXPLO_BIG : S_EXPLO_SMALL, p, 1.0f, frange(0.85f, 1.1f));
    g_player.shake = fmaxf(g_player.shake, clampf((power * 25) / (d + 2), 0, 1.2f));
    birdsScare(p);
    // dégâts
    castleDamageSphere(p, r * 0.9f, 180 * power + 60, true);
    for (size_t i = 0; i < g_chars.size(); i++) {
        Character& c = g_chars[i];
        if (!c.alive) continue;
        vec3 dd = c.pos + vec3(0, 1, 0) - p;
        float l = length(dd);
        if (l > r * 1.6f) continue;
        float dmg = (1 - l / (r * 1.6f)) * 220 * power + 10;
        charDamage((int)i, dmg, p, normalize(dd), DC_EXPLOSION);
    }
    float pl = length(g_player.pos + vec3(0, 1, 0) - p);
    if (pl < r * 1.5f) playerDamage((1 - pl / (r * 1.5f)) * 120 * power, p);
    bodiesImpulseSphere(p, r * 1.8f, 9 * power + 3);
    for (size_t i = 0; i < g_bodies.size(); i++) {
        Body& b = g_bodies[i];
        if (!b.alive) continue;
        float l = length(b.pos - p);
        if (l < r * 1.2f) bodyDamage((int)i, (1 - l / (r * 1.2f)) * 150 * power + 5, p, true);
    }
    for (size_t i = 0; i < g_trees.size(); i++) {
        Tree& t = g_trees[i];
        if (t.state != 0) continue;
        float l = dist2D(t.pos, p);
        if (l < r * 1.3f) {
            treeDamage((int)i, (1 - l / (r * 1.3f)) * 150 * power, p);
            if (frand() < 0.3f * power) t.burn = fmaxf(t.burn, 0.5f);
        }
    }
    for (size_t i = 0; i < g_vehicles.size(); i++) {
        Vehicle& v = g_vehicles[i];
        if (!v.alive || v.wreck) continue;
        float l = length(v.pos - p);
        if (l < r * 1.5f) vehicleDamage((int)i, (1 - l / (r * 1.5f)) * 300 * power, true);
    }
}

// Eternal Siege Vita - arbres et oiseaux
#include "game.h"

std::vector<Tree> g_trees;
static std::vector<Bird> s_birds;
static float s_cawT = 4;

void treesGen() {
    g_trees.clear();
    Rng r(4242);
    int attempts = 0;
    while (g_trees.size() < 300 && attempts < 20000) {
        attempts++;
        float x = r.range(-124, 124), z = r.range(-124, 124);
        float d = powf(powf(fabsf(x), 4) + powf(fabsf(z), 4), 0.25f);
        if (d < 60) continue;
        // densité : forêts sur les flancs et au nord, rare dans la plaine
        float dens = 0.04f;
        if (fabsf(x) > 52) dens = 0.9f;
        if (z < -55) dens = 0.85f;
        if (z > 85 && fabsf(x) > 30) dens = fmaxf(dens, 0.5f);
        dens *= 0.4f + fbm(x * 0.03f, z * 0.03f, 3, 55) * 1.2f;
        if (r.f() > dens) continue;
        if (z > RIVER_Z0 - 8 && z < RIVER_Z1 + 8) continue;
        if (fabsf(x - sinf(z * 0.04f) * 4) < 8 && z > 30) continue;
        float h = terrainHeight(x, z);
        if (h < WATER_Y + 0.5f) continue;
        bool ok = true;
        for (auto& t : g_trees)
            if ((t.pos.x - x) * (t.pos.x - x) + (t.pos.z - z) * (t.pos.z - z) < 16) { ok = false; break; }
        if (!ok) continue;
        Tree t;
        t.pos = vec3(x, h, z);
        t.kind = r.f() < (fabsf(x) > 70 || z < -70 ? 0.6f : 0.25f) ? 1 : 0;
        t.height = t.kind ? r.range(9, 15) : r.range(7, 11);
        t.radius = t.kind ? r.range(0.22f, 0.35f) : r.range(0.28f, 0.45f);
        t.hp = 60 + t.radius * 200;
        t.burn = 0; t.charred = 0;
        t.fallAng = 0; t.fallVel = 0; t.fallYaw = 0;
        t.state = 0;
        t.phase = r.range(0, 6.28f);
        g_trees.push_back(t);
    }
    logf("arbres: %d", (int)g_trees.size());
}

void treeDamage(int idx, float dmg, const vec3& from) {
    if (idx < 0 || idx >= (int)g_trees.size()) return;
    Tree& t = g_trees[idx];
    if (t.state != 0) return;
    t.hp -= dmg;
    if (frand() < 0.5f) {
        vec3 p = t.pos + vec3(0, t.height * 0.7f, 0);
        for (int i = 0; i < 4; i++)
            spawnParticle(PT_DEBRIS, p + g_rng.unit() * 2, vec3(frange(-1, 1), frange(-2, 0), frange(-1, 1)), 0.12f, 3, rgba(60, 90, 30));
    }
    if (t.hp <= 0) {
        t.state = 1;
        vec3 away = t.pos - from;
        t.fallYaw = atan2f(away.x, away.z) + frange(-0.3f, 0.3f);
        t.fallVel = 0.05f;
        sndPlay(S_TREE_FALL, t.pos + vec3(0, 3, 0), 1, frange(0.9f, 1.1f));
    }
}

int treeAtRay(const vec3& o, const vec3& d, float maxDist, float& tOut) {
    int best = -1;
    float bt = maxDist;
    for (size_t i = 0; i < g_trees.size(); i++) {
        Tree& t = g_trees[i];
        if (t.state != 0) continue;
        // rayon contre cylindre vertical
        float ox = o.x - t.pos.x, oz = o.z - t.pos.z;
        float a = d.x * d.x + d.z * d.z;
        if (a < 1e-6f) continue;
        float b = 2 * (ox * d.x + oz * d.z);
        float c = ox * ox + oz * oz - t.radius * t.radius * 1.4f;
        float disc = b * b - 4 * a * c;
        if (disc < 0) continue;
        float tt = (-b - sqrtf(disc)) / (2 * a);
        if (tt < 0 || tt > bt) continue;
        float y = o.y + d.y * tt;
        if (y < t.pos.y - 0.5f || y > t.pos.y + t.height * 0.6f) continue;
        bt = tt; best = (int)i;
    }
    tOut = bt;
    return best;
}

void treesUpdate(float dt, float wind) {
    (void)wind;
    for (size_t i = 0; i < g_trees.size(); i++) {
        Tree& t = g_trees[i];
        if (t.state == 1) {
            t.fallVel += (1.2f * sinf(t.fallAng) + 0.25f) * dt;
            t.fallAng += t.fallVel * dt * 1.6f;
            if (t.fallAng >= PI * 0.48f) {
                t.fallAng = PI * 0.48f;
                t.state = 2;
                vec3 dir(sinf(t.fallYaw), 0, cosf(t.fallYaw));
                vec3 mid = t.pos + dir * (t.height * 0.5f);
                sndPlay(S_COLLAPSE, mid, 0.9f, 1.4f);
                fxDust(mid, t.height * 0.4f, 25, rgba(110, 100, 80));
                for (int k = 0; k < 20; k++)
                    spawnParticle(PT_DEBRIS, t.pos + dir * frange(2, t.height) + vec3(0, 1, 0), vec3(frange(-2, 2), frange(1, 4), frange(-2, 2)), 0.15f, 4, rgba(50, 80, 25));
                // écrase ce qui se trouve dessous
                for (size_t c = 0; c < g_chars.size(); c++) {
                    Character& ch = g_chars[c];
                    if (!ch.alive) continue;
                    vec3 rel = ch.pos - t.pos;
                    float along = rel.x * dir.x + rel.z * dir.z;
                    float side = fabsf(rel.x * dir.z - rel.z * dir.x);
                    if (along > 0 && along < t.height && side < 1.8f) charDamage((int)c, 200, mid + vec3(0, 3, 0), vec3(0, -1, 0), DC_CRUSH);
                }
                vec3 pr = g_player.pos - t.pos;
                float along = pr.x * dir.x + pr.z * dir.z;
                float side = fabsf(pr.x * dir.z - pr.z * dir.x);
                if (along > 0 && along < t.height && side < 1.5f) playerDamage(60, mid + vec3(0, 3, 0));
                bodiesImpulseSphere(mid, t.height * 0.5f, 6);
                birdsScare(mid);
                float d = length(mid - g_player.pos);
                g_player.shake = fmaxf(g_player.shake, clampf(8 / (d + 1), 0, 0.5f));
            }
        }
        if (t.burn > 0) {
            float rainK = 1 - g_weather.rain * 0.7f;
            t.burn -= dt * (0.02f + g_weather.rain * 0.12f);
            t.charred = fminf(1, t.charred + dt * 0.04f * rainK);
            if (t.state == 0) t.hp -= dt * 1.5f;
            if (t.hp <= 0 && t.state == 0) treeDamage((int)i, 1, t.pos + vec3(frange(-1, 1), 0, frange(-1, 1)));
            vec3 top = t.pos + vec3(0, t.state == 0 ? t.height * 0.7f : 1.0f, 0);
            if (t.state == 2) {
                vec3 dir(sinf(t.fallYaw), 0, cosf(t.fallYaw));
                top = t.pos + dir * frange(1, t.height) + vec3(0, 0.6f, 0);
            }
            if (frand() < dt * 20 * t.burn) fxFire(top + g_rng.unit() * (t.state == 0 ? 2.0f : 0.8f), 1.2f, t.burn);
            if (frand() < dt * 6 * t.burn) fxSmoke(top + vec3(0, 2, 0), 1.5f, 1, 0.9f);
            lightAdd(top, 1.0f, 0.5f, 0.15f, 12 * t.burn + 3, 1.5f);
            // propagation
            if (frand() < dt * 0.3f * t.burn * rainK) {
                for (size_t j = 0; j < g_trees.size(); j++) {
                    if (j == i || g_trees[j].burn > 0 || g_trees[j].charred >= 1) continue;
                    float dx = g_trees[j].pos.x - t.pos.x, dz = g_trees[j].pos.z - t.pos.z;
                    if (dx * dx + dz * dz < 49) { g_trees[j].burn = 0.6f; break; }
                }
            }
            if (t.charred >= 1 && t.burn > 0.3f) t.burn = 0.3f;
        }
    }
}

void treesDraw(float tm, float wind) {
    Batch& bark = g_batches[BA_BARK];
    Batch& leaves = g_batches[BA_LEAVES];
    float day = 0.35f + g_weather.daylight * 0.65f;
    for (size_t i = 0; i < g_trees.size(); i++) {
        Tree& t = g_trees[i];
        if (t.state == 3) continue;
        vec3 center = t.pos + vec3(0, t.height * 0.5f, 0);
        if (!sphereVisible(center, t.height * 0.7f + (t.state ? t.height : 0))) continue;
        float d = length(center - g_cam.pos);
        if (d > 95 + 75 * g_quality) continue;
        Mat4 m;
        if (t.state == 0) {
            float sw = sinf(tm * 1.3f + t.phase) * (0.008f + wind * 0.03f);
            float sw2 = cosf(tm * 1.1f + t.phase * 1.7f) * (0.006f + wind * 0.02f);
            m = Mat4::translate(t.pos) * Mat4::rotZ(sw) * Mat4::rotX(sw2);
        } else {
            m = Mat4::translate(t.pos) * Mat4::rotY(t.fallYaw) * Mat4::rotX(t.fallAng);
        }
        bool lod = d > 40 + 30 * g_quality;
        float ch = 1 - t.charred * 0.8f;
        Color trunkC = rgbaf(0.85f * ch, 0.8f * ch, 0.75f * ch);
        emitCylinder(bark, m * Mat4::translate(vec3(0, -0.4f, 0)), t.radius, t.height * (t.kind ? 0.85f : 0.6f), lod ? 4 : 7, trunkC, false, 1, 0.35f);
        if (t.charred >= 1) continue;
        float lc = (1 - t.charred) * day;
        Color leafC = t.kind ? rgbaf(0.55f * lc, 0.75f * lc, 0.55f * lc) : rgbaf(0.85f * lc, 0.95f * lc, 0.65f * lc);
        if (t.kind == 1) {
            // sapin : cônes empilés
            int tiers = lod ? 2 : 4;
            for (int k = 0; k < tiers; k++) {
                float f = (float)k / tiers;
                float y = t.height * (0.25f + f * 0.62f);
                float r = t.height * 0.28f * (1 - f * 0.75f);
                emitCone(leaves, m * Mat4::translate(vec3(0, y, 0)) * Mat4::rotY(k * 0.7f), r, t.height * 0.32f, lod ? 5 : 8, leafC);
            }
        } else {
            // feuillu : grappes de cartes croisées
            int n = lod ? 3 : 6;
            for (int k = 0; k < n; k++) {
                float a = hash2((int)i, k, 91) * 6.28f;
                float rr = (k == 0) ? 0 : t.height * 0.22f;
                float y = t.height * (0.62f + hash2((int)i, k, 92) * 0.3f);
                float s = t.height * (0.28f + hash2((int)i, k, 93) * 0.12f);
                vec3 c(cosf(a) * rr, y, sinf(a) * rr);
                for (int q = 0; q < 2; q++) {
                    float qa = a + q * PI * 0.5f;
                    vec3 dx(cosf(qa) * s, 0, sinf(qa) * s), dy(0, s, 0);
                    vec3 pc = m.point(c);
                    vec3 DX = m.dir(dx), DY = m.dir(dy);
                    emitQuad(leaves, pc - DX - DY, pc + DX - DY, pc + DX + DY, pc - DX + DY, vec3(0, 1, 0), leafC, 0, 1, 1, 0);
                }
                // carte horizontale pour le volume vu du dessous
                if (!lod && k < 3) {
                    vec3 pc = m.point(c);
                    vec3 DX = m.dir(vec3(s, 0, 0)), DZ = m.dir(vec3(0, 0, s));
                    emitQuad(leaves, pc - DX - DZ, pc + DX - DZ, pc + DX + DZ, pc - DX + DZ, vec3(0, 1, 0), scaleC(leafC, 0.85f), 0, 1, 1, 0);
                }
            }
            // branches maîtresses
            if (!lod)
                for (int k = 0; k < 3; k++) {
                    float a = k * 2.1f + t.phase;
                    Mat4 bm = m * Mat4::translate(vec3(0, t.height * 0.45f, 0)) * Mat4::rotY(a) * Mat4::rotZ(0.9f);
                    emitCylinder(bark, bm, t.radius * 0.35f, t.height * 0.35f, 4, trunkC, false, 1, 0.5f);
                }
        }
        if (t.state == 0 && d < 40) shadowBlob(vec3(t.pos.x, t.pos.y, t.pos.z), t.height * 0.3f, 0.35f);
    }
}

// ---------------------------------------------------------------- Oiseaux
void birdsInit() {
    s_birds.clear();
    for (int i = 0; i < 26; i++) {
        Bird b;
        float a = frange(0, 6.28f), r = frange(20, 60);
        b.pos = vec3(cosf(a) * r, frange(30, 50), sinf(a) * r);
        b.vel = vec3(-sinf(a), 0, cosf(a)) * frange(5, 8);
        b.flap = frange(0, 6);
        b.scared = 0;
        s_birds.push_back(b);
    }
}

void birdsScare(const vec3& p) {
    for (auto& b : s_birds)
        if (length(b.pos - p) < 60) b.scared = 6;
    sndPlay(S_CROW, p + vec3(0, 20, 0), 0.6f, frange(0.9f, 1.2f));
}

void birdsUpdate(float dt) {
    for (size_t i = 0; i < s_birds.size(); i++) {
        Bird& b = s_birds[i];
        b.flap += dt * (b.scared > 0 ? 18 : 8);
        if (b.scared > 0) {
            b.scared -= dt;
            vec3 out = normalize(vec3(b.pos.x, 0, b.pos.z)) * 14 + vec3(0, 5, 0);
            b.vel = lerp(b.vel, out, dt * 1.5f);
        } else {
            // vol en cercle au-dessus du champ de bataille, cohésion légère
            vec3 toC = vec3(0, 38, 20) - b.pos;
            vec3 tang = normalize(vec3(-b.pos.z + 20, 0, b.pos.x)) * 7;
            b.vel = lerp(b.vel, tang + toC * 0.05f, dt * 0.6f);
            vec3 sep(0, 0, 0);
            for (size_t j = 0; j < s_birds.size(); j++) {
                if (i == j) continue;
                vec3 dd = b.pos - s_birds[j].pos;
                float l = length(dd);
                if (l < 3 && l > 0.01f) sep += dd / l;
            }
            b.vel += sep * dt * 3;
        }
        b.pos += b.vel * dt;
        if (b.pos.y < 20) b.vel.y += dt * 4;
        if (length(vec3(b.pos.x, 0, b.pos.z)) > 140) b.vel += -normalize(vec3(b.pos.x, 0, b.pos.z)) * dt * 6;
    }
    s_cawT -= dt;
    if (s_cawT < 0 && !s_birds.empty()) {
        s_cawT = frange(4, 14);
        sndPlay(S_CROW, s_birds[g_rng.irange(0, (int)s_birds.size() - 1)].pos, 0.5f, frange(0.85f, 1.15f));
    }
}

void birdsDraw() {
    Batch& b = g_batches[BA_LEATHER];
    Color c = rgba(25, 25, 28);
    for (auto& bd : s_birds) {
        if (!sphereVisible(bd.pos, 1)) continue;
        vec3 f = normalize(bd.vel);
        Mat4 m = lookDir(bd.pos, f);
        emitBox(b, m, vec3(0.08f, 0.07f, 0.22f), c);
        float fl = sinf(bd.flap) * 0.7f;
        for (int s = -1; s <= 1; s += 2) {
            Mat4 wm = m * Mat4::rotZ(fl * s) * Mat4::translate(vec3(s * 0.35f, 0, 0));
            emitBox(b, wm, vec3(0.3f, 0.01f, 0.12f), c);
        }
    }
}

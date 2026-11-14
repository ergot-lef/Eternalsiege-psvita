// Eternal Siege Vita - objets physiques : débris, tonneaux, caisses, échelles...
#include "game.h"

std::vector<Body> g_bodies;
const int MAX_BODIES = 320;

static bool flammable(int k) {
    return k == BK_PLANK || k == BK_BARREL || k == BK_POWDER || k == BK_CRATE || k == BK_HAY ||
           k == BK_LOG || k == BK_LADDER || k == BK_CART;
}
static bool floats(int k) {
    return flammable(k) || k == BK_LIMB;
}

void bodiesInit() {
    g_bodies.clear();
    g_bodies.reserve(MAX_BODIES);
}

void bodyWake(Body& b) { b.sleeping = false; b.sleepT = 0; }

int bodySpawn(int kind, const vec3& p, const vec3& half, Color col, int batch, float mass) {
    int slot = -1;
    for (size_t i = 0; i < g_bodies.size(); i++) if (!g_bodies[i].alive) { slot = (int)i; break; }
    if (slot < 0 && (int)g_bodies.size() < MAX_BODIES) { g_bodies.push_back(Body()); slot = (int)g_bodies.size() - 1; }
    if (slot < 0) {
        // recycle un débris endormi
        float best = -1;
        for (size_t i = 0; i < g_bodies.size(); i++) {
            Body& b = g_bodies[i];
            if (b.held || !(b.kind == BK_DEBRIS || b.kind == BK_PLANK || b.kind == BK_LIMB || b.kind == BK_HELMET)) continue;
            float s = b.life + (b.sleeping ? 100 : 0);
            if (s > best) { best = s; slot = (int)i; }
        }
        if (slot < 0) return -1;
    }
    Body& b = g_bodies[slot];
    b.alive = true; b.sleeping = false; b.held = false;
    b.kind = (uint8_t)kind;
    b.pos = p; b.vel = vec3(0, 0, 0); b.angVel = vec3(0, 0, 0);
    b.rot.identity();
    b.half = half; b.mass = mass;
    b.hp = 40 + mass * 0.6f;
    if (kind == BK_BLOCK) b.hp = 400;
    if (kind == BK_POWDER) b.hp = 25;
    if (kind == BK_BOULDER) b.hp = 2000;
    b.burn = 0; b.life = 0; b.sleepT = 0; b.wet = 0; b.charred = 0; b.bleed = 0;
    b.col = col; b.batch = batch;
    b.ladderClimbers = 0;
    return slot;
}

void bodiesImpulseSphere(const vec3& p, float radius, float force) {
    for (auto& b : g_bodies) {
        if (!b.alive || b.held) continue;
        vec3 d = b.pos - p;
        float l = length(d);
        if (l > radius) continue;
        float f = (1 - l / radius) * force * 30 / (b.mass * 0.15f + 3);
        bodyWake(b);
        b.vel += normalize(d + vec3(0, 0.6f, 0)) * f;
        b.angVel += g_rng.unit() * f * 0.8f;
    }
}

static void breakBody(int idx, const vec3& from) {
    Body& b = g_bodies[idx];
    vec3 p = b.pos;
    int kind = b.kind;
    b.alive = false;
    bool burning = b.burn > 0;
    vec3 away = normalize(p - from + vec3(0, 0.5f, 0));
    switch (kind) {
    case BK_POWDER:
        explosion(p, 1.25f, -1);
        break;
    case BK_CRATE: case BK_BARREL: case BK_CART: {
        sndPlay(S_WOOD_BREAK, p, 1, frange(0.9f, 1.15f));
        int n = kind == BK_CART ? 6 : 4;
        for (int i = 0; i < n; i++) {
            int q = bodySpawn(BK_PLANK, p + g_rng.unit() * 0.3f, vec3(0.04f, frange(0.25f, 0.45f), 0.08f), rgba(210, 195, 175), kind == BK_BARREL ? BA_WOOD : BA_PLANKS, 3);
            if (q >= 0) {
                g_bodies[q].vel = (away + g_rng.unit() * 0.7f) * frange(2, 6);
                g_bodies[q].angVel = g_rng.unit() * 10;
                if (burning) g_bodies[q].burn = 0.5f;
            }
        }
        fxDust(p, 0.6f, 6, rgba(130, 110, 85));
        for (int i = 0; i < 8; i++) spawnParticle(PT_DEBRIS, p, g_rng.unit() * 4 + vec3(0, 2, 0), 0.05f, 2, rgba(120, 95, 60));
        break;
    }
    case BK_HAY:
        for (int i = 0; i < 25; i++) spawnParticle(PT_DEBRIS, p + g_rng.unit() * 0.5f, g_rng.unit() * 3 + vec3(0, 2, 0), 0.06f, 3, rgba(200, 170, 90));
        if (burning) fxFire(p, 1.5f, 1);
        break;
    case BK_BLOCK: case BK_BOULDER:
        sndPlay(S_STONE_CRACK, p, 1, frange(0.8f, 1.0f));
        for (int i = 0; i < 4; i++) {
            float s = frange(0.12f, 0.25f);
            int q = bodySpawn(BK_DEBRIS, p + g_rng.unit() * 0.3f, vec3(s, s, s), b.col, BA_STONE, s * 20);
            if (q >= 0) { g_bodies[q].vel = (away + g_rng.unit()) * frange(2, 5); g_bodies[q].angVel = g_rng.unit() * 8; }
        }
        fxDust(p, 1, 8, rgba(150, 140, 125));
        break;
    default:
        fxDust(p, 0.3f, 3, rgba(120, 110, 100));
        break;
    }
}

void bodyDamage(int idx, float dmg, const vec3& from, bool fire) {
    if (idx < 0 || idx >= (int)g_bodies.size()) return;
    Body& b = g_bodies[idx];
    if (!b.alive) return;
    bodyWake(b);
    if (fire && flammable(b.kind) && b.wet < 0.5f) b.burn = fmaxf(b.burn, 0.4f);
    if (b.kind == BK_LOG || b.kind == BK_LADDER) dmg *= 0.3f;
    b.hp -= dmg;
    if (b.hp <= 0) breakBody(idx, from);
}

int bodyAtRay(const vec3& o, const vec3& d, float maxDist, float& tOut) {
    int best = -1;
    float bt = maxDist;
    for (size_t i = 0; i < g_bodies.size(); i++) {
        Body& b = g_bodies[i];
        if (!b.alive || b.held) continue;
        vec3 rel = b.pos - o;
        float r = length(b.half);
        float tc = dot(rel, d);
        if (tc < -r || tc > bt + r) continue;
        if (length2(rel - d * tc) > r * r) continue;
        // test OBB précis dans le repère local (rot orthonormée : transposée)
        vec3 X(b.rot.m[0], b.rot.m[1], b.rot.m[2]), Y(b.rot.m[4], b.rot.m[5], b.rot.m[6]), Z(b.rot.m[8], b.rot.m[9], b.rot.m[10]);
        vec3 lo(dot(-rel, X), dot(-rel, Y), dot(-rel, Z)), ld(dot(d, X), dot(d, Y), dot(d, Z));
        float tmin = 0, tmax = bt;
        float ho[3] = {lo.x, lo.y, lo.z}, hd[3] = {ld.x, ld.y, ld.z}, hh[3] = {b.half.x * 1.1f, b.half.y * 1.1f, b.half.z * 1.1f};
        bool hit = true;
        for (int a = 0; a < 3 && hit; a++) {
            if (fabsf(hd[a]) < 1e-6f) { if (fabsf(ho[a]) > hh[a]) hit = false; continue; }
            float t1 = (-hh[a] - ho[a]) / hd[a], t2 = (hh[a] - ho[a]) / hd[a];
            if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) hit = false;
        }
        if (hit && tmin < bt) { bt = tmin; best = (int)i; }
    }
    tOut = bt;
    return best;
}

static void orthonormalize(Mat4& m) {
    vec3 X(m.m[0], m.m[1], m.m[2]), Y(m.m[4], m.m[5], m.m[6]);
    X = normalize(X);
    Y = normalize(Y - X * dot(X, Y));
    vec3 Z = cross(X, Y);
    m.m[0] = X.x; m.m[1] = X.y; m.m[2] = X.z;
    m.m[4] = Y.x; m.m[5] = Y.y; m.m[6] = Y.z;
    m.m[8] = Z.x; m.m[9] = Z.y; m.m[10] = Z.z;
}

static float groundAt(const vec3& p) {
    return surfaceHeight(p.x, p.z, p.y);
}

static void stepBody(Body& b, float dt) {
    float invM = 1.0f / b.mass;
    float I = b.mass * (b.half.x * b.half.x + b.half.y * b.half.y + b.half.z * b.half.z) * 0.4f + 0.01f;
    float invI = 1.0f / I;
    // eau
    bool water = b.pos.y < WATER_Y + 0.1f && inRiver(vec3(b.pos.x, WATER_Y - 0.5f, b.pos.z));
    if (water) {
        if (b.wet < 1 && length(b.vel) > 3) fxSplash(b.pos, clampf(b.mass / 80.0f, 0.3f, 1.5f));
        b.wet = 1;
        b.burn = 0;
        float sub = clampf((WATER_Y - (b.pos.y - b.half.y)) / (b.half.y * 2 + 0.01f), 0, 1);
        if (floats(b.kind)) b.vel.y += 14.0f * sub * dt;
        else b.vel.y += 3.0f * sub * dt;
        b.vel *= (1 - clampf(1.5f * dt, 0, 1));
        b.angVel *= (1 - clampf(1.5f * dt, 0, 1));
        b.vel.x += 1.6f * dt * (floats(b.kind) ? 1.0f : 0.2f);  // courant
    } else if (b.wet > 0) b.wet -= dt * 0.02f;
    b.vel.y -= 9.8f * dt;
    b.pos += b.vel * dt;
    float w = length(b.angVel);
    if (w > 1e-4f) {
        b.rot = Mat4::rotAxis(b.angVel / w, w * dt) * b.rot;
        orthonormalize(b.rot);
    }
    // contacts : coins de la boîte
    int contacts = 0;
    float maxPen = 0;
    for (int c = 0; c < 8; c++) {
        vec3 lc((c & 1) ? b.half.x : -b.half.x, (c & 2) ? b.half.y : -b.half.y, (c & 4) ? b.half.z : -b.half.z);
        vec3 r = b.rot.dir(lc);
        vec3 p = b.pos + r;
        float g = groundAt(p);
        vec3 n(0, 1, 0);
        float pen = g - p.y;
        // mur : pousse horizontalement hors du bloc
        if (pen > 0.6f) {
            int i, j, k;
            if (worldToVox(p, i, j, k) && g_vox[vidx(i, j, k)] != M_AIR) {
                vec3 cc = voxCenter(i, j, k);
                vec3 dd = p - cc;
                if (fabsf(dd.x) > fabsf(dd.z)) { n = vec3(dd.x > 0 ? 1.0f : -1.0f, 0, 0); pen = 0.5f - fabsf(dd.x); }
                else { n = vec3(0, 0, dd.z > 0 ? 1.0f : -1.0f); pen = 0.5f - fabsf(dd.z); }
                pen = fmaxf(pen, 0) + 0.01f;
            } else continue;
        }
        if (pen <= 0) continue;
        if (n.y > 0.5f) n = terrainNormal(p.x, p.z);
        contacts++;
        if (pen > maxPen) maxPen = pen;
        vec3 vc = b.vel + cross(b.angVel, r);
        float vn = dot(vc, n);
        if (vn < 0) {
            float e = (b.kind == BK_BOULDER || b.kind == BK_DEBRIS) ? 0.25f : 0.15f;
            if (vn < -4 && b.life > 0.1f) {
                if (b.kind == BK_BLOCK || b.kind == BK_BOULDER || b.kind == BK_DEBRIS) {
                    if (vn < -6 && frand() < 0.4f) sndPlay(S_HIT_STONE, p, clampf(-vn / 15, 0.2f, 1), frange(0.7f, 1.2f));
                    if (b.kind != BK_DEBRIS) fxDust(p, 0.5f, 2, rgba(140, 130, 115));
                } else if (frand() < 0.4f) sndPlay(S_HIT_WOOD, p, clampf(-vn / 12, 0.2f, 0.8f), frange(0.8f, 1.2f));
            }
            vec3 rn = cross(r, n);
            float k = invM + dot(rn * invI, rn);
            float j = -(1 + e) * vn / k;
            vec3 J = n * j;
            b.vel += J * invM;
            b.angVel += cross(r, J) * invI;
            // frottement
            vec3 vt = vc - n * vn;
            float vtl = length(vt);
            if (vtl > 1e-4f) {
                vec3 t = vt / vtl;
                vec3 rt = cross(r, t);
                float kt = invM + dot(rt * invI, rt);
                float jt = fminf(vtl / kt, 0.6f * j);
                vec3 JT = t * -jt;
                b.vel += JT * invM;
                b.angVel += cross(r, JT) * invI;
            }
        }
        b.pos += n * (pen * 0.25f);
    }
    if (contacts > 0) {
        b.vel *= (1 - clampf(0.4f * dt, 0, 1));
        b.angVel *= (1 - clampf(0.9f * dt, 0, 1));
    }
    // limites du monde
    b.pos.x = clampf(b.pos.x, -126, 126);
    b.pos.z = clampf(b.pos.z, -126, 126);
    if (b.pos.y < -20) b.alive = false;
    // sommeil
    if (contacts > 0 && length2(b.vel) < 0.04f && length2(b.angVel) < 0.06f && !water) {
        b.sleepT += dt;
        if (b.sleepT > 0.7f) { b.sleeping = true; b.vel = vec3(0, 0, 0); b.angVel = vec3(0, 0, 0); }
    } else b.sleepT = 0;
    // dégâts de chute pour les personnages touchés
    float sp = length(b.vel);
    if (sp > 6 && b.mass > 15) {
        float r = length(b.half) + 0.4f;
        for (size_t i = 0; i < g_chars.size(); i++) {
            Character& c = g_chars[i];
            if (!c.alive) continue;
            vec3 d = c.pos + vec3(0, 1, 0) - b.pos;
            if (length2(d) < r * r) charDamage((int)i, sp * b.mass * 0.05f, b.pos, normalize(b.vel), DC_CRUSH);
        }
        vec3 d = g_player.pos + vec3(0, 1, 0) - b.pos;
        if (length2(d) < r * r && !b.held && b.life > 0.3f) playerDamage(sp * b.mass * 0.03f, b.pos);
        // les rochers et blocs lancés abîment les murs
        if (sp > 10 && (b.kind == BK_BOULDER || b.kind == BK_BLOCK)) {
            int i, j, k;
            vec3 ahead = b.pos + normalize(b.vel) * (b.half.x + 0.3f);
            if (worldToVox(ahead, i, j, k) && g_vox[vidx(i, j, k)] != M_AIR) {
                castleDamageSphere(ahead, 1.5f, sp * b.mass * 0.02f, false);
                b.vel *= 0.5f;
            }
        }
    }
}

static void bodyPairs(float dt) {
    (void)dt;
    for (size_t i = 0; i < g_bodies.size(); i++) {
        Body& a = g_bodies[i];
        if (!a.alive || a.sleeping || a.held) continue;
        float ra = (a.half.x + a.half.y + a.half.z) * 0.42f;
        for (size_t j = 0; j < g_bodies.size(); j++) {
            if (i == j) continue;
            Body& b = g_bodies[j];
            if (!b.alive || b.held) continue;
            float rb = (b.half.x + b.half.y + b.half.z) * 0.42f;
            vec3 d = b.pos - a.pos;
            float l2 = length2(d);
            float rr = ra + rb;
            if (l2 >= rr * rr || l2 < 1e-6f) continue;
            float l = sqrtf(l2);
            vec3 n = d / l;
            float pen = rr - l;
            float wa = b.mass / (a.mass + b.mass), wb = a.mass / (a.mass + b.mass);
            a.pos -= n * pen * wa * 0.5f;
            b.pos += n * pen * wb * 0.5f;
            float vn = dot(b.vel - a.vel, n);
            if (vn < 0) {
                float j2 = -(1.2f) * vn / (1 / a.mass + 1 / b.mass);
                a.vel -= n * (j2 / a.mass);
                b.vel += n * (j2 / b.mass);
                if (b.sleeping && fabsf(vn) > 0.5f) bodyWake(b);
                if (fabsf(vn) > 7) {
                    bodyDamage((int)j, fabsf(vn) * a.mass * 0.05f, a.pos, false);
                    bodyDamage((int)i, fabsf(vn) * b.mass * 0.05f, b.pos, false);
                }
            }
        }
    }
}

void bodiesUpdate(float dt) {
    for (size_t i = 0; i < g_bodies.size(); i++) {
        Body& b = g_bodies[i];
        if (!b.alive) continue;
        b.life += dt;
        if (!b.sleeping && !b.held) stepBody(b, dt);
        if (!b.alive) continue;
        // feu
        if (b.burn > 0) {
            b.burn = fminf(1.2f, b.burn + dt * 0.08f) - g_weather.rain * dt * 0.1f;
            b.charred = fminf(1, b.charred + dt * 0.03f);
            if (frand() < dt * 14 * b.burn) fxFire(b.pos + g_rng.unit() * length(b.half) * 0.6f + vec3(0, b.half.y * 0.6f, 0), 0.4f + length(b.half) * 0.6f, b.burn);
            if (frand() < dt * 3 * b.burn) fxSmoke(b.pos + vec3(0, 1, 0), 0.8f, 1, 0.85f);
            lightAdd(b.pos + vec3(0, 0.5f, 0), 1.0f, 0.5f, 0.15f, 5 + length(b.half) * 5 * b.burn, 1);
            float rate = b.kind == BK_HAY ? 8.0f : 3.0f;
            b.hp -= dt * rate * b.burn;
            if (b.kind == BK_POWDER && b.burn > 0.5f) b.hp -= dt * 15;
            // propagation aux objets et arbres voisins
            if (frand() < dt * 0.8f * b.burn) {
                for (size_t j = 0; j < g_bodies.size(); j++) {
                    Body& o = g_bodies[j];
                    if (!o.alive || j == i || o.burn > 0 || !flammable(o.kind) || o.wet > 0.3f) continue;
                    if (length2(o.pos - b.pos) < 4.0f) { o.burn = 0.3f; bodyWake(o); break; }
                }
                for (auto& t : g_trees)
                    if (t.state != 3 && t.burn <= 0 && t.charred < 1 && dist2D(t.pos, b.pos) < 2.5f) t.burn = 0.4f;
                for (size_t c = 0; c < g_chars.size(); c++)
                    if (g_chars[c].alive && length2(g_chars[c].pos - b.pos) < 2.2f) g_chars[c].burn = fmaxf(g_chars[c].burn, 2.5f);
                int vi, vj, vk;
                if (worldToVox(b.pos + vec3(0, 0.5f, 0), vi, vj, vk))
                    for (int dd = -1; dd <= 1; dd++) castleIgnite(vi + dd, vj, vk), castleIgnite(vi, vj, vk + dd);
            }
            if (b.burn <= 0) b.burn = 0;
            if (b.hp <= 0) { breakBody((int)i, b.pos - vec3(0, 1, 0)); continue; }
        }
        // sang qui goutte des membres
        if (b.bleed > 0) {
            b.bleed -= dt;
            if (frand() < dt * 15) spawnParticle(PT_BLOOD, b.pos, b.vel * 0.3f + vec3(0, -0.5f, 0), 0.05f, 0.8f, rgba(120, 5, 5, 220));
        }
        // blocs endormis : deviennent des gravats du château (talus, rampes)
        if (b.sleeping && b.kind == BK_BLOCK) {
            int vi, vj, vk;
            if (worldToVox(b.pos, vi, vj, vk) && g_vox[vidx(vi, vj, vk)] == M_AIR) {
                castlePlace(vi, vj, vk, M_RUBBLE);
                b.alive = false;
                continue;
            } else if (worldToVox(b.pos + vec3(0, 1, 0), vi, vj, vk) && g_vox[vidx(vi, vj, vk)] == M_AIR) {
                castlePlace(vi, vj, vk, M_RUBBLE);
                b.alive = false;
                continue;
            }
        }
        // durée de vie des petits débris
        if (b.sleeping && (b.kind == BK_DEBRIS) && b.life > 50) b.alive = false;
        if (b.sleeping && (b.kind == BK_PLANK || b.kind == BK_LIMB || b.kind == BK_HELMET) && b.life > 180) b.alive = false;
    }
    bodyPairs(dt);
}

// ---------------------------------------------------------------- Rendu
void bodiesDraw() {
    for (size_t i = 0; i < g_bodies.size(); i++) {
        Body& b = g_bodies[i];
        if (!b.alive) continue;
        float r = length(b.half);
        if (!sphereVisible(b.pos, r)) continue;
        float d = length(b.pos - g_cam.pos);
        if (d > 70 + 40 * g_quality || (r < 0.3f && d > 25 + 25 * g_quality)) continue;
        Mat4 m = Mat4::translate(b.pos) * b.rot;
        float ch = 1 - b.charred * 0.75f;
        Color c = scaleC(b.col, ch);
        Batch& B = g_batches[b.batch];
        switch (b.kind) {
        case BK_BARREL: case BK_POWDER: {
            Mat4 cm = m * Mat4::translate(vec3(0, -b.half.y, 0));
            emitCylinder(g_batches[BA_WOOD], cm, b.half.x, b.half.y * 2, 9, c, true, 2, 0.6f);
            for (int k = 0; k < 2; k++)
                emitCylinder(g_batches[BA_METAL], cm * Mat4::translate(vec3(0, b.half.y * (0.35f + k * 1.2f), 0)), b.half.x * 1.03f, 0.06f, 9, rgba(90, 90, 95), false);
            if (b.kind == BK_POWDER)
                emitCylinder(g_batches[BA_CLOTH], cm * Mat4::translate(vec3(0, b.half.y * 0.9f, 0)), b.half.x * 1.04f, b.half.y * 0.25f, 9, rgba(160, 30, 25), false);
            break;
        }
        case BK_LOG: {
            emitCylinder(g_batches[BA_BARK], m * Mat4::translate(vec3(0, -b.half.y, 0)), b.half.x, b.half.y * 2, 7, c, true, 1, 0.3f);
            break;
        }
        case BK_LADDER: {
            for (int s = -1; s <= 1; s += 2)
                emitBox(g_batches[BA_WOOD], m * Mat4::translate(vec3(s * b.half.x, 0, 0)), vec3(0.05f, b.half.y, 0.05f), c);
            int rungs = (int)(b.half.y * 2 / 0.4f);
            for (int k = 0; k < rungs; k++)
                emitBox(g_batches[BA_WOOD], m * Mat4::translate(vec3(0, -b.half.y + 0.2f + k * 0.4f, 0)), vec3(b.half.x, 0.03f, 0.03f), c);
            break;
        }
        case BK_CART: {
            emitBox(g_batches[BA_PLANKS], m * Mat4::translate(vec3(0, 0.1f, 0)), vec3(b.half.x, b.half.y * 0.4f, b.half.z), c);
            emitBox(g_batches[BA_PLANKS], m * Mat4::translate(vec3(0, b.half.y * 0.5f, b.half.z)), vec3(b.half.x, b.half.y * 0.4f, 0.04f), c);
            emitBox(g_batches[BA_PLANKS], m * Mat4::translate(vec3(0, b.half.y * 0.5f, -b.half.z)), vec3(b.half.x, b.half.y * 0.4f, 0.04f), c);
            for (int s = -1; s <= 1; s += 2)
                emitCylinder(g_batches[BA_WOOD], m * Mat4::translate(vec3(s * (b.half.x + 0.05f), -b.half.y * 0.3f, 0)) * Mat4::rotZ(PI * 0.5f), b.half.y * 0.75f, 0.1f, 10, c, true);
            emitBox(g_batches[BA_WOOD], m * Mat4::translate(vec3(0, 0, b.half.z + 0.8f)), vec3(0.05f, 0.05f, 0.8f), c);
            break;
        }
        case BK_HAY:
            emitBox(B, m, b.half, c, 0.8f);
            break;
        case BK_LIMB:
            emitBox(B, m, b.half, c, 1);
            emitBox(g_batches[BA_WHITE], m * Mat4::translate(vec3(0, b.half.y, 0)), vec3(b.half.x * 0.9f, 0.02f, b.half.z * 0.9f), rgba(120, 10, 10));
            break;
        default:
            emitBox(B, m, b.half, c, b.kind == BK_BLOCK ? 0.5f : 1.0f);
            break;
        }
        if (d < 35 && r > 0.3f) shadowBlob(b.pos, r * 0.9f, 0.4f);
    }
}

// ---------------------------------------------------------------- Mise en place des objets
static void place(int kind, float x, float z, float yaw, float yOff = 0) {
    float y = surfaceHeight(x, z, 40);
    vec3 half; Color c = rgba(220, 210, 195); int batch = BA_PLANKS; float mass = 30;
    switch (kind) {
    case BK_CRATE: half = vec3(0.4f, 0.4f, 0.4f); mass = 25; break;
    case BK_BARREL: half = vec3(0.32f, 0.45f, 0.32f); batch = BA_WOOD; mass = 40; break;
    case BK_POWDER: half = vec3(0.3f, 0.42f, 0.3f); batch = BA_WOOD; mass = 35; c = rgba(200, 185, 170); break;
    case BK_HAY: half = vec3(0.6f, 0.45f, 0.8f); batch = BA_STRAW; mass = 30; c = rgba(235, 220, 170); break;
    case BK_CART: half = vec3(0.8f, 0.5f, 1.3f); mass = 140; c = rgba(200, 185, 165); break;
    case BK_BOULDER: half = vec3(0.3f, 0.26f, 0.3f); batch = BA_ROCK; mass = 70; c = rgba(200, 195, 190); break;
    case BK_LOG: half = vec3(0.25f, 1.6f, 0.25f); batch = BA_BARK; mass = 120; c = rgba(220, 210, 200); break;
    case BK_LADDER: half = vec3(0.3f, 3.2f, 0.05f); batch = BA_WOOD; mass = 20; break;
    case BK_SANDBAG: half = vec3(0.4f, 0.15f, 0.25f); batch = BA_CLOTH; mass = 30; c = rgba(180, 160, 120); break;
    }
    int b = bodySpawn(kind, vec3(x, y + half.y + 0.02f + yOff, z), half, c, batch, mass);
    if (b < 0) return;
    g_bodies[b].rot = Mat4::rotY(yaw);
    if (kind == BK_LOG || kind == BK_LADDER) g_bodies[b].rot = Mat4::rotY(yaw) * Mat4::rotZ(PI * 0.5f);
    g_bodies[b].sleeping = true;
}

void propsGen() {
    bodiesInit();
    Rng r(777);
    // cour : réserves, charrettes, foin, poudre
    for (int i = 0; i < 4; i++) place(BK_CRATE, -18 + i * 0.85f, -18, 0);
    for (int i = 0; i < 3; i++) place(BK_CRATE, -18 + i * 0.85f + 0.4f, -18, 0, 0.82f);
    place(BK_CRATE, -17.5f, -18, 0.3f, 1.64f);
    for (int i = 0; i < 5; i++) place(BK_BARREL, 18 + (i % 3) * 0.7f, -16 + (i / 3) * 0.7f, r.range(0, 3));
    for (int i = 0; i < 4; i++) place(BK_POWDER, -22 + (i % 2) * 0.65f, 18 + (i / 2) * 0.65f, 0);
    place(BK_HAY, 20, 16, 0.2f); place(BK_HAY, 21.4f, 16.2f, 0.1f); place(BK_HAY, 20.7f, 16.1f, 0.15f, 0.9f);
    place(BK_CART, 14, 22, 0.4f);
    place(BK_CART, -12, -22, 1.3f);
    for (int i = 0; i < 6; i++) place(BK_CRATE, r.range(-24, 24), r.range(-24, 24) * 0.3f + (r.f() < 0.5f ? 22 : -22), r.range(0, 3));
    // pierres à jeter depuis le chemin de ronde, au-dessus de la porte
    for (int i = 0; i < 8; i++) place(BK_BOULDER, -6 + i * 1.6f, 29.5f, r.range(0, 3));
    for (int i = 0; i < 4; i++) place(BK_BOULDER, -29.5f, -10 + i * 6, r.range(0, 3));
    for (int i = 0; i < 4; i++) place(BK_BOULDER, 29.5f, -10 + i * 6, r.range(0, 3));
    // plaine : ferme abandonnée, champs de foin, charrettes
    for (int i = 0; i < 9; i++) place(BK_HAY, -40 + r.range(-12, 12), 92 + r.range(-8, 8), r.range(0, 3));
    for (int i = 0; i < 7; i++) place(BK_HAY, 42 + r.range(-10, 10), 45 + r.range(-6, 6), r.range(0, 3));
    place(BK_CART, 12, 100, 2.4f);
    place(BK_CART, -8, 52, 0.8f);
    for (int i = 0; i < 5; i++) place(BK_CRATE, 15 + r.range(-2, 2), 98 + r.range(-2, 2), r.range(0, 3));
    for (int i = 0; i < 4; i++) place(BK_BARREL, -30 + r.range(-2, 2), 50 + r.range(-2, 2), r.range(0, 3));
    for (int i = 0; i < 6; i++) place(BK_LOG, r.range(-80, 80), r.range(35, 110), r.range(0, 3));
    logf("objets: %d", (int)g_bodies.size());
}

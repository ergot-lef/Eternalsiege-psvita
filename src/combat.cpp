// Eternal Siege Vita - projectiles, tirs, engins de siège, véhicules, postes fixes
#include "game.h"

std::vector<Proj> g_projs;
std::vector<Stuck> g_stuck;
std::vector<Vehicle> g_vehicles;
std::vector<Station> g_stations;
static int s_stuckNext = 0;
struct GroundFire { vec3 p; float life, r; };
static std::vector<GroundFire> s_gfires;

static void addStuck(const vec3& p, const vec3& dir, bool flaming) {
    Stuck s; s.pos = p; s.dir = dir; s.life = flaming ? 8 : 0; s.flaming = flaming;
    if (g_stuck.size() < 260) g_stuck.push_back(s);
    else { g_stuck[s_stuckNext] = s; s_stuckNext = (s_stuckNext + 1) % 260; }
}

static void groundFire(const vec3& p, float r, float life) {
    if (s_gfires.size() > 40) s_gfires.erase(s_gfires.begin());
    GroundFire g; g.p = p; g.r = r; g.life = life;
    s_gfires.push_back(g);
    decalAdd(DC_SCORCH, p.x, p.z, r);
}

void projSpawn(int kind, const vec3& p, const vec3& v, int team, float dmg, bool flaming) {
    Proj* slot = nullptr;
    for (auto& q : g_projs) if (!q.alive) { slot = &q; break; }
    if (!slot) {
        if (g_projs.size() >= 300) return;
        g_projs.push_back(Proj());
        slot = &g_projs.back();
    }
    Proj& q = *slot;
    q.alive = true; q.kind = (uint8_t)kind; q.team = (uint8_t)team; q.flaming = flaming;
    q.pos = p; q.vel = v; q.dmg = dmg; q.life = 0; q.fuse = kind == PK_GRENADE ? 2.6f : 0;
    if (kind == PK_SHELL || kind == PK_BOMB) {
        float d = length(p - g_player.pos);
        if (d > 30) sndPlay(S_WHISTLE, g_player.pos + normalize(p - g_player.pos) * 15, 0.8f, frange(0.9f, 1.1f));
    }
}

// rayon contre le joueur (capsule)
static bool rayPlayer(const vec3& o, const vec3& d, float maxD, float& t) {
    if (g_player.dead) return false;
    float ox = o.x - g_player.pos.x, oz = o.z - g_player.pos.z;
    float a = d.x * d.x + d.z * d.z;
    if (a < 1e-6f) return false;
    float b = 2 * (ox * d.x + oz * d.z), c = ox * ox + oz * oz - 0.35f * 0.35f;
    float disc = b * b - 4 * a * c;
    if (disc < 0) return false;
    float tt = (-b - sqrtf(disc)) / (2 * a);
    if (tt < 0 || tt > maxD) return false;
    float y = o.y + d.y * tt - g_player.pos.y;
    if (y < 0 || y > 1.85f) return false;
    t = tt;
    return true;
}

static float terrainRay(const vec3& o, const vec3& d, float maxD) {
    float step = 1.0f;
    for (float t = 0; t < maxD; t += step) {
        vec3 p = o + d * t;
        if (p.y < terrainHeight(p.x, p.z)) {
            // affine
            float lo = t - step, hi = t;
            for (int i = 0; i < 6; i++) {
                float m = (lo + hi) * 0.5f;
                vec3 q = o + d * m;
                if (q.y < terrainHeight(q.x, q.z)) hi = m; else lo = m;
            }
            return hi;
        }
    }
    return 1e9f;
}

void hitscan(const vec3& o, const vec3& d, float range, float dmg, int team, bool tracer) {
    float best = range;
    int what = 0, idx = -1;
    bool head = false;
    float t;
    int ci = charAtRay(o, d, best, t, head, team);
    if (ci >= 0 && t < best) { best = t; what = 1; idx = ci; }
    if (team != TEAM_PLAYER && team != TEAM_ALLY && rayPlayer(o, d, best, t)) { best = t; what = 2; }
    int bi = bodyAtRay(o, d, best, t);
    if (bi >= 0 && t < best) { best = t; what = 3; idx = bi; }
    int ti = treeAtRay(o, d, best, t);
    if (ti >= 0 && t < best) { best = t; what = 4; idx = ti; }
    int vi = vehicleAtRay(o, d, best, t);
    if (vi >= 0 && t < best) { best = t; what = 5; idx = vi; }
    int hi, hj, hk; vec3 n;
    if (castleRaycast(o, d, best, t, hi, hj, hk, n) && t < best) { best = t; what = 6; }
    float tt = terrainRay(o, d, best);
    if (tt < best) { best = tt; what = 7; }
    vec3 hp = o + d * best;
    if (tracer) fxTracer(o + d * 1.0f, hp);
    switch (what) {
    case 1: charDamage(idx, head ? dmg * 2.2f : dmg, o, d, DC_BULLET); break;
    case 2: playerDamage(dmg * 0.7f, o); break;
    case 3: bodyDamage(idx, dmg, o, false); fxDust(hp, 0.2f, 2, rgba(130, 110, 90)); sndPlay(S_HIT_WOOD, hp, 0.4f, 1.2f); break;
    case 4: treeDamage(idx, dmg * 0.3f, o); for (int i = 0; i < 4; i++) spawnParticle(PT_DEBRIS, hp, -d * 2 + g_rng.unit() * 2, 0.04f, 1.5f, rgba(150, 120, 80)); break;
    case 5: vehicleDamage(idx, dmg * 0.4f, false); fxSparks(hp, -d, 5); sndPlay(S_CLANG, hp, 0.5f, frange(1.2f, 1.5f)); break;
    case 6: castleDamageVoxel(hi, hj, hk, dmg * 0.15f, o); fxDust(hp, 0.25f, 3, rgba(160, 150, 135)); fxSparks(hp, n, 2); sndPlay(S_HIT_STONE, hp, 0.35f, frange(1.1f, 1.4f)); break;
    case 7:
        if (inRiver(hp + vec3(0, 0.1f, 0))) fxSplash(hp, 0.2f);
        else { fxDust(hp, 0.2f, 3, rgba(110, 95, 75)); for (int i = 0; i < 3; i++) spawnParticle(PT_DEBRIS, hp, vec3(frange(-1, 1), frange(1, 3), frange(-1, 1)), 0.04f, 1, rgba(80, 65, 50)); }
        break;
    }
}

// ---------------------------------------------------------------- Projectiles
static void projImpact(Proj& q, const vec3& p, const vec3& n, int hitWhat, int idx) {
    switch (q.kind) {
    case PK_ARROW: case PK_BOLT:
        if (hitWhat == 0 || hitWhat == 3 || hitWhat == 4 || hitWhat == 6) addStuck(p - normalize(q.vel) * 0.15f, normalize(q.vel), q.flaming);
        if (q.flaming) {
            if (hitWhat == 3) bodyDamage(idx, 1, p, true);
            if (hitWhat == 4 && idx >= 0) g_trees[idx].burn = fmaxf(g_trees[idx].burn, 0.5f);
            if (hitWhat == 6) { int i, j, k; if (worldToVox(p - n * 0.3f, i, j, k)) castleIgnite(i, j, k); }
            if (hitWhat == 0) groundFire(p, 0.5f, 4);
        }
        if (hitWhat == 0) { fxDust(p, 0.1f, 1, rgba(100, 85, 65)); }
        if (hitWhat == 6) sndPlay(S_HIT_STONE, p, 0.3f, 1.4f);
        if (hitWhat == 3 || hitWhat == 4) sndPlay(S_HIT_WOOD, p, 0.4f, 1.3f);
        break;
    case PK_BOULDER: case PK_BALL: {
        float big = q.kind == PK_BOULDER ? 1.0f : 0.7f;
        if (hitWhat == 6) castleDamageSphere(p, 2.0f * big + 0.4f, 260 * big, false);
        sndPlay(S_COLLAPSE, p, 0.8f, 1.2f);
        fxDust(p, 1.5f, 12, rgba(140, 130, 115));
        bodiesImpulseSphere(p, 4, 4);
        for (size_t c = 0; c < g_chars.size(); c++)
            if (g_chars[c].alive && length(g_chars[c].pos + vec3(0, 1, 0) - p) < 2.0f) charDamage((int)c, 150, p, normalize(q.vel), DC_CRUSH);
        if (length(g_player.pos + vec3(0, 1, 0) - p) < 1.8f) playerDamage(70, p);
        if (hitWhat == 0 && !inRiver(p)) { decalAdd(DC_CRATER, p.x, p.z, 1.2f * big); terrainDeform(p, 2.0f * big, 0.5f); }
        // le boulet devient un objet qui roule
        float s = q.kind == PK_BOULDER ? 0.45f : 0.16f;
        int b = bodySpawn(BK_BOULDER, p + n * (s + 0.1f), vec3(s, s * 0.9f, s), q.kind == PK_BOULDER ? rgba(200, 195, 185) : rgba(60, 60, 65),
                          q.kind == PK_BOULDER ? BA_ROCK : BA_METAL, q.kind == PK_BOULDER ? 400 : 60);
        if (b >= 0) {
            vec3 v = q.vel * 0.25f;
            v = v - n * (2 * dot(v, n));
            g_bodies[b].vel = v;
            g_bodies[b].angVel = g_rng.unit() * 3;
        }
        g_player.shake = fmaxf(g_player.shake, clampf(6 / (length(p - g_player.pos) + 1), 0, 0.6f));
        break;
    }
    case PK_SHELL: explosion(p, 1.0f, q.team); break;
    case PK_BOMB: explosion(p, 1.6f, q.team); break;
    case PK_ROCKET: explosion(p, 0.9f, q.team); break;
    case PK_MISSILE: explosion(p, 2.0f, q.team); break;
    case PK_GRENADE: explosion(p, 0.6f, q.team); break;
    case PK_OIL:
        groundFire(p, 1.0f, 6);
        for (int i = 0; i < 3; i++) fxFire(p + g_rng.unit() * 0.5f, 0.8f, 1);
        if (hitWhat == 3) bodyDamage(idx, 2, p, true);
        if (hitWhat == 6) { int i, j, k; if (worldToVox(p - n * 0.3f, i, j, k)) castleIgnite(i, j, k); }
        for (auto& t : g_trees) if (t.state != 3 && dist2D(t.pos, p) < 2) t.burn = fmaxf(t.burn, 0.5f);
        break;
    }
}

void projsUpdate(float dt) {
    for (auto& q : g_projs) {
        if (!q.alive) continue;
        q.life += dt;
        if (q.life > 20) { q.alive = false; continue; }
        float g = 9.8f;
        if (q.kind == PK_ROCKET || q.kind == PK_MISSILE) g = 1.0f;
        if (q.kind == PK_OIL) g = 6.0f;
        q.vel.y -= g * dt;
        if (q.kind == PK_ARROW || q.kind == PK_BOLT) {
            vec3 wv(cosf(g_weather.windDir), 0, sinf(g_weather.windDir));
            q.vel += wv * g_weather.wind * 0.8f * dt;
        }
        // traînées
        if (q.kind == PK_ROCKET || q.kind == PK_MISSILE) {
            spawnParticle(PT_SMOKE, q.pos, vec3(0, 0.3f, 0), 0.35f, 2.5f, rgba(180, 180, 180, 160));
            spawnParticle(PT_FIRE, q.pos, vec3(0, 0, 0), 0.25f, 0.1f, rgba(255, 255, 255));
            lightAdd(q.pos, 1, 0.6f, 0.3f, 6, 1);
        }
        if (q.flaming && (q.kind == PK_ARROW || q.kind == PK_BOLT)) {
            if (frand() < 0.7f) spawnParticle(PT_FIRE, q.pos, vec3(0, 0.3f, 0), 0.18f, 0.25f, rgba(255, 255, 255, 220));
            lightAdd(q.pos, 1, 0.55f, 0.2f, 4, 0.6f);
        }
        if (q.kind == PK_OIL) {
            if (frand() < 0.8f) spawnParticle(PT_FIRE, q.pos, q.vel * 0.2f, 0.45f, 0.3f, rgba(255, 255, 255, 230));
            lightAdd(q.pos, 1, 0.5f, 0.15f, 5, 0.8f);
        }
        if (q.kind == PK_GRENADE) {
            q.fuse -= dt;
            if (q.fuse <= 0) { q.alive = false; projImpact(q, q.pos, vec3(0, 1, 0), 0, -1); continue; }
        }
        vec3 delta = q.vel * dt;
        float len = length(delta);
        if (len < 1e-5f) continue;
        vec3 dir = delta / len;
        vec3 o = q.pos;
        float best = len;
        int what = -1, idx = -1;
        bool head = false;
        float t;
        vec3 n(0, 1, 0);
        int ci = charAtRay(o, dir, best, t, head, q.team == TEAM_PLAYER ? TEAM_ALLY : q.team);
        if (ci >= 0 && q.team == TEAM_PLAYER && g_chars[ci].team == TEAM_ALLY) ci = -1;
        if (ci >= 0 && t < best) { best = t; what = 1; idx = ci; }
        if (q.team == TEAM_ENEMY && rayPlayer(o, dir, best, t)) { best = t; what = 2; }
        int bi = bodyAtRay(o, dir, best, t);
        if (bi >= 0 && t < best && !(q.kind == PK_GRENADE)) { best = t; what = 3; idx = bi; }
        int ti = treeAtRay(o, dir, best, t);
        if (ti >= 0 && t < best) { best = t; what = 4; idx = ti; }
        int vi = vehicleAtRay(o, dir, best, t);
        if (vi >= 0 && t < best) { best = t; what = 5; idx = vi; }
        int hi, hj, hk; vec3 vn;
        if (castleRaycast(o, dir, best, t, hi, hj, hk, vn) && t < best) { best = t; what = 6; n = vn; }
        vec3 np = o + dir * best;
        float gh = terrainHeight(np.x, np.z);
        if (what < 0 && np.y < gh) {
            what = 0; n = terrainNormal(np.x, np.z);
            // remonte le point d'impact
            for (int i = 0; i < 5; i++) { best *= 0.85f; np = o + dir * best; if (np.y >= terrainHeight(np.x, np.z)) break; }
            np.y = fmaxf(np.y, gh);
        }
        if (what < 0) {
            // eau
            if (q.pos.y > WATER_Y && np.y <= WATER_Y && inRiver(vec3(np.x, WATER_Y - 0.5f, np.z))) {
                fxSplash(np, q.kind == PK_BOULDER || q.kind == PK_BALL ? 1.5f : 0.4f);
                if (q.kind == PK_ARROW || q.kind == PK_BOLT || q.kind == PK_OIL) { q.alive = false; continue; }
            }
            q.pos = np;
            continue;
        }
        if (q.kind == PK_GRENADE && what == 0) {
            // rebond
            q.pos = np + n * 0.1f;
            q.vel = (q.vel - n * (2 * dot(q.vel, n))) * 0.35f;
            continue;
        }
        q.alive = false;
        switch (what) {
        case 1:
            if (q.kind == PK_ARROW || q.kind == PK_BOLT) {
                charDamage(idx, head ? q.dmg * 2.5f : q.dmg, o, dir, DC_ARROW);
                if (q.flaming) g_chars[idx].burn = fmaxf(g_chars[idx].burn, 3);
            } else if (q.kind == PK_OIL) {
                g_chars[idx].burn = fmaxf(g_chars[idx].burn, 4);
                charDamage(idx, q.dmg, o, dir, DC_FIRE);
            } else projImpact(q, np, n, 1, idx);
            break;
        case 2:
            if (q.kind == PK_ARROW || q.kind == PK_BOLT) playerDamage(q.dmg * 0.8f, o);
            else if (q.kind == PK_OIL) playerDamage(5, o);
            else projImpact(q, np, n, 2, -1);
            break;
        case 3:
            if (q.kind == PK_ARROW || q.kind == PK_BOLT) bodyDamage(idx, q.dmg * 0.5f, o, q.flaming);
            projImpact(q, np, n, 3, idx);
            break;
        case 4:
            if (q.kind == PK_ARROW || q.kind == PK_BOLT) treeDamage(idx, 2, o);
            projImpact(q, np, n, 4, idx);
            break;
        case 5:
            if (q.kind == PK_ARROW || q.kind == PK_BOLT) { vehicleDamage(idx, q.dmg * 0.2f, false); sndPlay(S_HIT_WOOD, np, 0.4f, 1.2f); }
            else projImpact(q, np, n, 5, idx);
            break;
        case 6:
            if (q.kind == PK_ARROW || q.kind == PK_BOLT) castleDamageVoxel(hi, hj, hk, 2, o);
            projImpact(q, np, n, 6, -1);
            break;
        default:
            projImpact(q, np, n, 0, -1);
            break;
        }
    }
    // flèches plantées
    for (auto& s : g_stuck) {
        if (s.flaming) {
            s.life -= dt;
            if (s.life <= 0) s.flaming = false;
            if (frand() < dt * 8) spawnParticle(PT_FIRE, s.pos, vec3(0, 0.4f, 0), 0.15f, 0.3f, rgba(255, 255, 255, 200));
        }
    }
    // feux au sol (huile, flammes)
    for (size_t i = 0; i < s_gfires.size();) {
        GroundFire& f = s_gfires[i];
        f.life -= dt * (1 + g_weather.rain * 2);
        if (f.life <= 0) { s_gfires.erase(s_gfires.begin() + i); continue; }
        float h = surfaceHeight(f.p.x, f.p.z, f.p.y + 1);
        vec3 p(f.p.x, h, f.p.z);
        if (frand() < dt * 18) fxFire(p + vec3(frange(-f.r, f.r), 0.2f, frange(-f.r, f.r)), 0.7f, clampf(f.life / 3, 0.3f, 1));
        if (frand() < dt * 2) fxSmoke(p + vec3(0, 1, 0), 0.8f, 1, 0.9f);
        lightAdd(p + vec3(0, 0.5f, 0), 1, 0.5f, 0.15f, 5 + f.r * 2, 1);
        for (size_t c = 0; c < g_chars.size(); c++) {
            Character& ch = g_chars[c];
            if (ch.alive && dist2D(ch.pos, p) < f.r + 0.4f && fabsf(ch.pos.y - h) < 1.5f) ch.burn = fmaxf(ch.burn, 3);
        }
        if (dist2D(g_player.pos, p) < f.r + 0.3f && fabsf(g_player.pos.y - h) < 1.5f) playerDamage(dt * 25, p + vec3(0, -1, 0));
        i++;
    }
}

void projsDraw() {
    Batch& W = g_batches[BA_WOOD];
    Batch& M = g_batches[BA_METAL];
    Batch& R = g_batches[BA_ROCK];
    for (auto& q : g_projs) {
        if (!q.alive || q.kind == PK_OIL) continue;
        if (!sphereVisible(q.pos, 1)) continue;
        Mat4 m = lookDir(q.pos, q.vel);
        switch (q.kind) {
        case PK_ARROW: case PK_BOLT:
            emitBox(W, m, vec3(0.01f, 0.01f, q.kind == PK_ARROW ? 0.38f : 0.22f), rgba(200, 180, 140));
            emitBox(g_batches[BA_WHITE], m * Mat4::translate(vec3(0, 0, -0.3f)), vec3(0.03f, 0.005f, 0.06f), rgba(220, 220, 210));
            break;
        case PK_BOULDER: emitBox(R, m * Mat4::rotY(q.life * 3), vec3(0.45f, 0.4f, 0.45f), rgba(200, 195, 185)); break;
        case PK_BALL: emitBox(M, m, vec3(0.15f, 0.15f, 0.15f), rgba(50, 50, 55)); break;
        case PK_GRENADE: emitBox(M, m * Mat4::rotX(q.life * 8), vec3(0.05f, 0.05f, 0.08f), rgba(70, 80, 60)); break;
        default: emitCylinder(M, m * Mat4::rotX(PI * 0.5f) * Mat4::translate(vec3(0, -0.4f, 0)), 0.08f, 0.8f, 6, rgba(80, 85, 75), true); break;
        }
    }
    for (auto& s : g_stuck) {
        if (length2(s.pos - g_cam.pos) > 50 * 50) continue;
        if (!sphereVisible(s.pos, 0.5f)) continue;
        Mat4 m = lookDir(s.pos, s.dir);
        emitBox(W, m * Mat4::translate(vec3(0, 0, -0.2f)), vec3(0.01f, 0.01f, 0.25f), rgba(190, 170, 130));
        emitBox(g_batches[BA_WHITE], m * Mat4::translate(vec3(0, 0, -0.42f)), vec3(0.03f, 0.005f, 0.05f), rgba(210, 210, 200));
    }
}

// ---------------------------------------------------------------- Véhicules
int vehicleSpawn(int kind, const vec3& p) {
    int slot = -1;
    for (size_t i = 0; i < g_vehicles.size(); i++) if (!g_vehicles[i].alive) { slot = (int)i; break; }
    if (slot < 0 && g_vehicles.size() < 18) { g_vehicles.push_back(Vehicle()); slot = (int)g_vehicles.size() - 1; }
    if (slot < 0) {
        float best = -1;
        for (size_t i = 0; i < g_vehicles.size(); i++)
            if (g_vehicles[i].wreck && g_vehicles[i].stateT > best) { best = g_vehicles[i].stateT; slot = (int)i; }
        if (slot < 0) return -1;
    }
    Vehicle& v = g_vehicles[slot];
    memset(&v, 0, sizeof(v));
    v.alive = true; v.kind = (uint8_t)kind; v.pos = p; v.yaw = PI;
    switch (kind) {
    case VK_RAM: v.hp = 450; break;
    case VK_CATAPULT: v.hp = 300; break;
    case VK_TREBUCHET: v.hp = 500; break;
    case VK_CANNON: v.hp = 260; break;
    case VK_TANK: v.hp = g_game.era >= 6 ? 1600 : 900; break;
    case VK_PLANE: v.hp = g_game.era >= 6 ? 400 : 160; v.pos.y = frange(45, 60); v.vel = vec3(frange(-5, 5), 0, -48); v.yaw = PI; break;
    case VK_HELI: v.hp = 600; v.pos.y = 30; break;
    case VK_DRONE: v.hp = 25; v.pos.y = 22; break;
    }
    v.maxHp = v.hp;
    v.reload = frange(3, 6);
    return slot;
}

int vehicleAtRay(const vec3& o, const vec3& d, float maxDist, float& tOut) {
    int best = -1;
    float bt = maxDist;
    for (size_t i = 0; i < g_vehicles.size(); i++) {
        Vehicle& v = g_vehicles[i];
        if (!v.alive || v.wreck) continue;
        float r = v.kind == VK_DRONE ? 0.6f : (v.kind == VK_PLANE ? 4.0f : (v.kind == VK_HELI ? 3.5f : 2.2f));
        vec3 c = v.pos + vec3(0, (v.kind == VK_PLANE || v.kind == VK_HELI || v.kind == VK_DRONE) ? 0 : 1.2f, 0);
        vec3 rel = c - o;
        float tc = dot(rel, d);
        if (tc < 0 || tc > bt) continue;
        if (length2(rel - d * tc) > r * r) continue;
        float t = tc - sqrtf(fmaxf(0, r * r - length2(rel - d * tc)));
        if (t < bt) { bt = t; best = (int)i; }
    }
    tOut = bt;
    return best;
}

void vehicleDamage(int idx, float dmg, bool explosive) {
    if (idx < 0 || idx >= (int)g_vehicles.size()) return;
    Vehicle& v = g_vehicles[idx];
    if (!v.alive || v.wreck) return;
    if ((v.kind == VK_TANK) && !explosive) dmg *= 0.05f;
    v.hp -= dmg;
    if (v.hp > 0) return;
    v.wreck = true;
    v.stateT = 0;
    v.burn = 1;
    if (v.kind == VK_PLANE || v.kind == VK_HELI) { v.vel.y = -2; return; }  // chute avant explosion
    explosion(v.pos + vec3(0, 1, 0), v.kind == VK_TANK ? 1.4f : 0.8f, -1);
    if (v.kind == VK_RAM || v.kind == VK_CATAPULT || v.kind == VK_TREBUCHET || v.kind == VK_CANNON) {
        for (int i = 0; i < 6; i++) {
            int b = bodySpawn(BK_PLANK, v.pos + vec3(frange(-1, 1), 1.5f, frange(-1, 1)), vec3(0.06f, frange(0.4f, 0.9f), 0.12f), rgba(160, 140, 120), BA_PLANKS, 8);
            if (b >= 0) { g_bodies[b].vel = g_rng.unit() * 6 + vec3(0, 4, 0); g_bodies[b].angVel = g_rng.unit() * 8; g_bodies[b].burn = 0.5f; }
        }
    }
    if (v.kind == VK_DRONE) v.alive = false;
}

static vec3 castleTarget(bool keepToo) {
    // point visé sur le château : porte, murs sud, tours, parfois le donjon
    float r = frand();
    if (keepToo && r < 0.25f) return vec3(frange(-9, 9), frange(12, 28), frange(-9, 9) + 10);
    if (r < 0.5f) return vec3(frange(-4, 4), frange(11, 16), 33);
    if (r < 0.8f) return vec3(frange(-30, 30), frange(11, 19), 31);
    return vec3(frand() < 0.5f ? -31 : 31, frange(11, 19), frange(-25, 30));
}

static void vehicleMoveToward(Vehicle& v, float dt, float speed, float stopDist) {
    vec3 to = vec3(-v.pos.x, 0, 25 - v.pos.z);
    float d = length(to);
    if (d < stopDist) { v.vel = vec3(0, 0, 0); return; }
    vec3 dir = to / d;
    v.yaw += wrapAngle(atan2f(dir.x, dir.z) - v.yaw) * clampf(dt * 1.5f, 0, 1);
    vec3 f(sinf(v.yaw), 0, cosf(v.yaw));
    v.pos += f * speed * dt;
    v.pos.y = terrainHeight(v.pos.x, v.pos.z);
    v.vel = f * speed;
    v.anim += dt * speed;
}

void vehiclesUpdate(float dt) {
    float nearestEngine = 1e9f, nearestPlane = 1e9f, nearestDrone = 1e9f;
    for (size_t i = 0; i < g_vehicles.size(); i++) {
        Vehicle& v = g_vehicles[i];
        if (!v.alive) continue;
        v.stateT += dt;
        v.reload -= dt;
        if (v.burn > 0) {
            v.burn -= dt * 0.015f;
            if (frand() < dt * 10 * v.burn) fxFire(v.pos + vec3(frange(-1, 1), frange(0.5f, 2), frange(-1, 1)), 1.0f, v.burn);
            if (frand() < dt * 5 * v.burn) fxSmoke(v.pos + vec3(0, 2.5f, 0), 1.6f, 1, 1.0f);
            lightAdd(v.pos + vec3(0, 1.5f, 0), 1, 0.5f, 0.15f, 8 * v.burn + 2, 1.2f);
        }
        if (v.wreck) {
            if (v.kind == VK_PLANE || v.kind == VK_HELI) {
                // chute en vrille
                if (v.pos.y > terrainHeight(v.pos.x, v.pos.z) + 0.5f) {
                    v.vel.y -= 9.8f * dt;
                    v.pos += v.vel * dt;
                    v.yaw += dt * 2.5f;
                    v.anim += dt * 3;
                    if (frand() < dt * 30) fxSmoke(v.pos, 1.2f, 1, 1.0f);
                    if (frand() < dt * 20) fxFire(v.pos, 1.0f, 1);
                    // percute le château ?
                    int a, b, c;
                    if (worldToVox(v.pos, a, b, c) && g_vox[vidx(a, b, c)] != M_AIR) v.pos.y = -100;
                } else if (v.anim < 1000) {
                    v.pos.y = fmaxf(v.pos.y, terrainHeight(v.pos.x, v.pos.z));
                    explosion(v.pos + vec3(0, 1, 0), 1.6f, -1);
                    v.anim = 1000;
                    v.vel = vec3(0, 0, 0);
                }
                if (v.pos.y < -50) { explosion(vec3(v.pos.x, terrainHeight(v.pos.x, v.pos.z), v.pos.z), 1.6f, -1); v.alive = false; }
            }
            if (v.stateT > 240 && length(v.pos - g_cam.pos) > 30) v.alive = false;
            continue;
        }
        float dPlayer = length(v.pos - g_player.pos);
        switch (v.kind) {
        case VK_RAM: {
            vec3 f(sinf(v.yaw), 0, cosf(v.yaw));
            vec3 front = v.pos + f * 2.6f + vec3(0, 1.0f, 0);
            int a, b, c;
            bool hit = false;
            for (int k = 0; k < 3 && !hit; k++) {
                vec3 p = front + vec3(0, k * 0.8f, 0);
                if (worldToVox(p, a, b, c) && g_vox[vidx(a, b, c)] != M_AIR) hit = true;
            }
            if (!hit) {
                vec3 dir = vec3(-v.pos.x * 0.05f, 0, 1) * -1.0f;
                vec3 to = vec3(0, 0, 33) - v.pos;
                if (length(to) > 1) dir = normalize(vec3(to.x, 0, to.z));
                if (v.pos.z < 37) dir = vec3(0, 0, -1);
                v.yaw += wrapAngle(atan2f(dir.x, dir.z) - v.yaw) * clampf(dt * 1.2f, 0, 1);
                v.pos += vec3(sinf(v.yaw), 0, cosf(v.yaw)) * 1.3f * dt;
                v.pos.y = surfaceHeight(v.pos.x, v.pos.z, v.pos.y + 0.5f);
                v.anim += dt * 1.3f;
                v.turret = 0;
            } else if (v.reload <= 0) {
                v.reload = 2.8f;
                v.turret = 1;  // déclenche l'animation du coup
                castleDamageSphere(front + vec3(0, 0.6f, 0), 1.6f, 95, false);
                sndPlay(S_RAM, front, 1, frange(0.9f, 1.05f));
                fxDust(front, 0.8f, 6, rgba(120, 100, 80));
                g_player.shake = fmaxf(g_player.shake, clampf(4 / (dPlayer + 1), 0, 0.3f));
            }
            v.turret = fmaxf(0, v.turret - dt * 1.5f);
            break;
        }
        case VK_CATAPULT: case VK_TREBUCHET: case VK_CANNON: {
            float stop = v.kind == VK_TREBUCHET ? 95 : (v.kind == VK_CATAPULT ? 75 : 68);
            vehicleMoveToward(v, dt, 1.0f, stop);
            if (length(vec3(v.pos.x, 0, v.pos.z - 25)) <= stop + 0.5f && v.reload <= 0) {
                v.reload = v.kind == VK_TREBUCHET ? 15 : (v.kind == VK_CATAPULT ? 10 : 8);
                v.anim = 0;
                v.turret = 1;
                vec3 tgt = castleTarget(g_game.waveState == WS_ASSAULT && castleIntegrity() < 0.8f);
                vec3 muzzle = v.pos + vec3(0, v.kind == VK_TREBUCHET ? 9.0f : (v.kind == VK_CATAPULT ? 3.0f : 1.2f), 0);
                float dx = dist2D(muzzle, tgt);
                float spd = v.kind == VK_CANNON ? 75 : sqrtf(9.8f * dx) * 1.08f;
                vec3 dir = ballisticAim(muzzle, tgt, spd, 9.8f);
                dir = normalize(dir + g_rng.unit() * 0.012f);
                projSpawn(v.kind == VK_CANNON ? PK_BALL : PK_BOULDER, muzzle, dir * spd, TEAM_ENEMY, 0, false);
                if (v.kind == VK_CANNON) {
                    sndPlay(S_CANNON, muzzle, 1, frange(0.9f, 1.05f));
                    fxSmoke(muzzle + dir * 1.5f, 1.5f, 6, 0.2f);
                    spawnParticle(PT_FLASH, muzzle + dir * 1.2f, vec3(0, 0, 0), 1.2f, 0.12f, rgba(255, 220, 150));
                } else sndPlay(S_WHOOSH, muzzle, 1, 0.5f);
            }
            v.turret = fmaxf(0, v.turret - dt * 0.8f);
            break;
        }
        case VK_TANK: {
            vehicleMoveToward(v, dt, 2.2f, 48);
            nearestEngine = fminf(nearestEngine, dPlayer);
            vec3 tgt = (!g_player.dead && dPlayer < 90 && frand() < 0.5f) ? g_player.pos + vec3(0, 1, 0) : castleTarget(true);
            float ty = atan2f(tgt.x - v.pos.x, tgt.z - v.pos.z) - v.yaw;
            v.turret += wrapAngle(ty - v.turret) * clampf(dt * 1.5f, 0, 1);
            if (v.reload <= 0) {
                v.reload = frange(5, 8);
                vec3 muzzle = v.pos + vec3(0, 2.2f, 0) + vec3(sinf(v.yaw + v.turret), 0, cosf(v.yaw + v.turret)) * 3.2f;
                vec3 dir = ballisticAim(muzzle, tgt, 90, 9.8f);
                projSpawn(PK_SHELL, muzzle, normalize(dir + g_rng.unit() * 0.01f) * 90, TEAM_ENEMY, 0, false);
                sndPlay(S_CANNON, muzzle, 1, 1.2f);
                fxSmoke(muzzle, 1.2f, 4, 0.3f);
                spawnParticle(PT_FLASH, muzzle, vec3(0, 0, 0), 1.0f, 0.1f, rgba(255, 220, 150));
            }
            break;
        }
        case VK_PLANE: {
            v.pos += v.vel * dt;
            v.anim += dt * 40;
            nearestPlane = fminf(nearestPlane, dPlayer);
            if (fabsf(v.pos.z - 10) < 30 && v.reload <= 0) {
                v.reload = 0.35f;
                projSpawn(PK_BOMB, v.pos - vec3(0, 1, 0), v.vel * 0.9f, TEAM_ENEMY, 0, false);
            }
            if (v.pos.z < -160) v.alive = false;
            break;
        }
        case VK_HELI: {
            vec3 hover(v.pos.x * 0.98f, 28, 62);
            v.pos = lerp(v.pos, hover, clampf(dt * 0.3f, 0, 1));
            v.anim += dt * 25;
            nearestPlane = fminf(nearestPlane, dPlayer);
            vec3 tgt = (!g_player.dead && frand() < 0.6f) ? g_player.pos + vec3(0, 1, 0) : castleTarget(true);
            v.yaw += wrapAngle(atan2f(tgt.x - v.pos.x, tgt.z - v.pos.z) - v.yaw) * clampf(dt, 0, 1);
            if (v.reload <= 0 && v.pos.z < 75) {
                v.reload = frange(2.5f, 4.5f);
                vec3 dir = normalize(tgt - v.pos + g_rng.unit() * 2);
                projSpawn(PK_ROCKET, v.pos + dir * 3, dir * 50, TEAM_ENEMY, 0, false);
                sndPlay(S_ROCKET, v.pos, 1, 1);
            }
            break;
        }
        case VK_DRONE: {
            v.anim += dt * 60;
            nearestDrone = fminf(nearestDrone, dPlayer);
            vec3 tgt = (!g_player.dead && dPlayer < 60) ? g_player.pos + vec3(0, 1, 0) : castleTarget(true);
            vec3 to = tgt - v.pos;
            float l = length(to);
            float spd = l < 25 ? 22 : 14;
            vec3 want = normalize(to + (l > 25 ? vec3(0, 15 - v.pos.y + tgt.y, 0) * 0.1f : vec3(0, 0, 0))) * spd;
            v.vel = lerp(v.vel, want, clampf(dt * 2, 0, 1));
            v.pos += v.vel * dt;
            v.yaw = atan2f(v.vel.x, v.vel.z);
            int a, b, c;
            if (l < 1.5f || v.pos.y < terrainHeight(v.pos.x, v.pos.z) + 0.3f || (worldToVox(v.pos, a, b, c) && g_vox[vidx(a, b, c)] != M_AIR)) {
                explosion(v.pos, 0.9f, TEAM_ENEMY);
                v.alive = false;
            }
            break;
        }
        }
        // l'huile bouillante et le feu brûlent les engins en bois
        if (v.kind <= VK_CANNON && v.burn > 0.2f) v.hp -= dt * 12;
        if (v.hp <= 0 && !v.wreck) vehicleDamage((int)i, 1, true);
    }
    sndLoop(LS_ENGINE, S_ENGINE, clampf(1 - nearestEngine / 80, 0, 0.8f), 1);
    sndLoop(LS_PLANE, S_PLANE, clampf(1 - nearestPlane / 120, 0, 0.9f), 1);
    sndLoop(LS_DRONE, S_DRONE, clampf(1 - nearestDrone / 40, 0, 0.7f), 1);
}

static void crewman(const Mat4& m, float ph, Color c) {
    Batch& CL = g_batches[BA_CLOTH];
    Batch& SK = g_batches[BA_SKIN];
    float lg = sinf(ph) * 0.5f;
    emitBox(CL, m * Mat4::translate(vec3(0, 1.25f, 0)) * Mat4::rotX(0.3f), vec3(0.2f, 0.3f, 0.12f), c);
    emitBox(SK, m * Mat4::translate(vec3(0, 1.7f, 0.12f)), vec3(0.1f, 0.12f, 0.11f), rgba(220, 180, 150));
    for (int s = -1; s <= 1; s += 2)
        emitBox(CL, m * Mat4::translate(vec3(s * 0.1f, 0.9f, 0)) * Mat4::rotX(lg * s) * Mat4::translate(vec3(0, -0.43f, 0)), vec3(0.07f, 0.43f, 0.08f), scaleC(c, 0.6f));
    emitBox(CL, m * Mat4::translate(vec3(0, 1.35f, 0.35f)), vec3(0.25f, 0.06f, 0.25f), c);
}

void vehiclesDraw() {
    Batch& W = g_batches[BA_WOOD];
    Batch& P = g_batches[BA_PLANKS];
    Batch& M = g_batches[BA_METAL];
    for (auto& v : g_vehicles) {
        if (!v.alive) continue;
        if (!sphereVisible(v.pos, 8)) continue;
        if (length(v.pos - g_cam.pos) > 200) continue;
        float ch = v.wreck ? 0.3f : 1.0f;
        Color wc = rgbaf(0.8f * ch, 0.72f * ch, 0.62f * ch), mc = rgbaf(0.55f * ch, 0.57f * ch, 0.5f * ch);
        Mat4 m = Mat4::translate(v.pos) * Mat4::rotY(v.yaw);
        switch (v.kind) {
        case VK_RAM: {
            emitBox(P, m * Mat4::translate(vec3(0, 0.6f, 0)), vec3(0.9f, 0.1f, 2.0f), wc);
            for (int s = -1; s <= 1; s += 2) {
                emitBox(P, m * Mat4::translate(vec3(s * 0.55f, 2.0f, 0)) * Mat4::rotZ(-s * 0.7f), vec3(0.7f, 0.06f, 2.1f), scaleC(wc, 0.85f));
                for (int k = -1; k <= 1; k += 2) emitBox(W, m * Mat4::translate(vec3(s * 0.85f, 1.3f, k * 1.8f)), vec3(0.06f, 0.7f, 0.06f), wc);
                for (int k = -1; k <= 1; k += 2)
                    emitCylinder(W, m * Mat4::translate(vec3(s * 1.0f, 0.4f, k * 1.4f)) * Mat4::rotZ(PI * 0.5f), 0.4f, 0.12f, 8, wc, true);
            }
            float swing = sinf(v.turret * PI) * 0.6f;
            emitCylinder(g_batches[BA_BARK], m * Mat4::translate(vec3(0, 1.3f, -2.0f + swing)) * Mat4::rotX(PI * 0.5f), 0.22f, 4.4f, 7, wc, true);
            emitBox(M, m * Mat4::translate(vec3(0, 1.3f, 2.5f + swing)), vec3(0.28f, 0.28f, 0.15f), mc);
            if (!v.wreck)
                for (int k = 0; k < 4; k++)
                    crewman(m * Mat4::translate(vec3((k % 2 ? 0.5f : -0.5f), 0, (k < 2 ? -1.2f : 0.6f))), v.anim * 6 + k, rgba(110, 60, 45));
            break;
        }
        case VK_CATAPULT: {
            emitBox(P, m * Mat4::translate(vec3(0, 0.5f, 0)), vec3(1.0f, 0.15f, 1.8f), wc);
            for (int s = -1; s <= 1; s += 2) emitBox(W, m * Mat4::translate(vec3(s * 0.8f, 1.3f, 0.3f)), vec3(0.1f, 0.8f, 0.1f), wc);
            float arm = v.turret > 0 ? -1.2f + (1 - v.turret) * 0.0f : 0.4f;
            arm = v.turret > 0.7f ? lerpf(0.4f, -1.2f, (1 - v.turret) / 0.3f) : (v.turret > 0 ? -1.2f + (0.7f - v.turret) * 2.3f : 0.4f);
            Mat4 am = m * Mat4::translate(vec3(0, 1.9f, 0.3f)) * Mat4::rotX(arm);
            emitBox(W, am * Mat4::translate(vec3(0, 0, -1.2f)), vec3(0.09f, 0.09f, 1.6f), wc);
            emitBox(P, am * Mat4::translate(vec3(0, 0.15f, -2.7f)), vec3(0.35f, 0.12f, 0.35f), wc);
            for (int s = -1; s <= 1; s += 2)
                for (int k = -1; k <= 1; k += 2) emitCylinder(W, m * Mat4::translate(vec3(s * 1.1f, 0.45f, k * 1.3f)) * Mat4::rotZ(PI * 0.5f), 0.45f, 0.12f, 8, wc, true);
            break;
        }
        case VK_TREBUCHET: {
            emitBox(P, m * Mat4::translate(vec3(0, 0.3f, 0)), vec3(2.0f, 0.2f, 3.0f), wc);
            for (int s = -1; s <= 1; s += 2) {
                emitBox(W, m * Mat4::translate(vec3(s * 1.2f, 3.6f, 0.8f)) * Mat4::rotX(0.3f), vec3(0.15f, 3.6f, 0.15f), wc);
                emitBox(W, m * Mat4::translate(vec3(s * 1.2f, 3.6f, -0.8f)) * Mat4::rotX(-0.3f), vec3(0.15f, 3.6f, 0.15f), wc);
            }
            float arm = v.turret > 0 ? lerpf(-0.9f, 1.3f, clampf((1 - v.turret) * 4, 0, 1)) : -0.9f;
            Mat4 am = m * Mat4::translate(vec3(0, 7.0f, 0)) * Mat4::rotX(arm);
            emitBox(W, am * Mat4::translate(vec3(0, 0, -2.5f)), vec3(0.15f, 0.15f, 4.5f), wc);
            emitBox(P, am * Mat4::translate(vec3(0, -1.0f, 1.6f)), vec3(0.8f, 0.8f, 0.8f), scaleC(wc, 0.7f));
            break;
        }
        case VK_CANNON: {
            float recoil = v.turret * 0.5f;
            emitBox(P, m * Mat4::translate(vec3(0, 0.55f, -recoil)), vec3(0.4f, 0.2f, 1.2f), wc);
            emitCylinder(M, m * Mat4::translate(vec3(0, 1.0f, -0.6f - recoil)) * Mat4::rotX(PI * 0.5f - 0.12f), 0.2f, 2.6f, 9, rgba(70, 70, 72), true);
            for (int s = -1; s <= 1; s += 2) emitCylinder(W, m * Mat4::translate(vec3(s * 0.55f, 0.6f, 0.2f - recoil)) * Mat4::rotZ(PI * 0.5f), 0.6f, 0.12f, 10, wc, true);
            if (!v.wreck) crewman(m * Mat4::translate(vec3(0.9f, 0, -1.6f)), v.stateT, rgba(150, 40, 40));
            break;
        }
        case VK_TANK: {
            Color tc = g_game.era == 5 ? rgbaf(0.45f * ch, 0.43f * ch, 0.35f * ch) : rgbaf(0.32f * ch, 0.36f * ch, 0.25f * ch);
            emitBox(M, m * Mat4::translate(vec3(0, 1.1f, 0)), vec3(1.5f, 0.6f, 3.0f), tc);
            for (int s = -1; s <= 1; s += 2) emitBox(M, m * Mat4::translate(vec3(s * 1.55f, 0.6f, 0)), vec3(0.35f, 0.6f, 3.2f), rgbaf(0.18f * ch, 0.18f * ch, 0.17f * ch));
            Mat4 tm = m * Mat4::translate(vec3(0, 2.0f, -0.2f)) * Mat4::rotY(v.turret);
            emitBox(M, tm, vec3(0.9f, 0.35f, 1.1f), tc);
            emitCylinder(M, tm * Mat4::translate(vec3(0, 0.05f, 0.9f)) * Mat4::rotX(PI * 0.5f), 0.1f, 2.6f, 6, tc, true);
            break;
        }
        case VK_PLANE: {
            Mat4 pm = Mat4::translate(v.pos) * Mat4::rotY(v.yaw) * Mat4::rotZ(v.wreck ? v.stateT * 2 : sinf(v.stateT) * 0.1f);
            Color pc = g_game.era == 5 ? rgbaf(0.55f * ch, 0.5f * ch, 0.35f * ch) : rgbaf(0.45f * ch, 0.48f * ch, 0.5f * ch);
            emitBox(M, pm, vec3(0.6f, 0.6f, 3.5f), pc);
            emitBox(M, pm * Mat4::translate(vec3(0, 0, 0.6f)), vec3(5.5f, 0.07f, 0.9f), pc);
            if (g_game.era == 5) emitBox(M, pm * Mat4::translate(vec3(0, 1.1f, 0.6f)), vec3(5.0f, 0.07f, 0.8f), pc);
            emitBox(M, pm * Mat4::translate(vec3(0, 0.4f, -3.2f)), vec3(1.8f, 0.05f, 0.5f), pc);
            emitBox(M, pm * Mat4::translate(vec3(0, 0.9f, -3.2f)), vec3(0.05f, 0.6f, 0.5f), pc);
            emitBox(M, pm * Mat4::translate(vec3(0, 0, 3.6f)) * Mat4::rotZ(v.anim), vec3(1.2f, 0.08f, 0.04f), rgba(40, 40, 40));
            break;
        }
        case VK_HELI: {
            Mat4 hm = Mat4::translate(v.pos) * Mat4::rotY(v.yaw) * Mat4::rotX(0.1f);
            Color hc = rgbaf(0.3f * ch, 0.34f * ch, 0.26f * ch);
            emitBox(M, hm, vec3(1.0f, 1.0f, 2.2f), hc);
            emitBox(M, hm * Mat4::translate(vec3(0, 0.3f, -3.8f)), vec3(0.2f, 0.25f, 2.0f), hc);
            emitBox(M, hm * Mat4::translate(vec3(0, 1.2f, 0)) * Mat4::rotY(v.anim), vec3(5.5f, 0.04f, 0.18f), rgba(30, 30, 30));
            emitBox(M, hm * Mat4::translate(vec3(0, 1.2f, 0)) * Mat4::rotY(v.anim + PI * 0.5f), vec3(5.5f, 0.04f, 0.18f), rgba(30, 30, 30));
            break;
        }
        case VK_DRONE: {
            Mat4 dm = Mat4::translate(v.pos) * Mat4::rotY(v.yaw);
            emitBox(M, dm, vec3(0.2f, 0.08f, 0.25f), rgba(40, 40, 45));
            for (int k = 0; k < 4; k++) {
                float a = k * PI * 0.5f + PI * 0.25f;
                vec3 o(cosf(a) * 0.35f, 0.05f, sinf(a) * 0.35f);
                emitBox(M, dm * Mat4::translate(o * 0.5f) * Mat4::rotY(-a), vec3(0.18f, 0.02f, 0.02f), rgba(40, 40, 45));
                emitBox(M, dm * Mat4::translate(o + vec3(0, 0.05f, 0)) * Mat4::rotY(v.anim + k), vec3(0.16f, 0.005f, 0.02f), rgba(60, 60, 60));
            }
            lightAdd(v.pos, 1, 0.1f, 0.1f, 2, 0.3f);
            break;
        }
        }
        if (v.kind <= VK_TANK) shadowBlob(v.pos, 2.0f, 0.5f);
    }
}

// ---------------------------------------------------------------- Postes fixes
void stationsGen(int era) {
    g_stations.clear();
    auto add = [&](int k, vec3 p, float yaw) {
        Station s; s.kind = (uint8_t)k; s.pos = p; s.yaw = yaw; s.pitch = 0; s.cooldown = 0; s.active = false;
        s.pos.y = surfaceHeight(p.x, p.z, p.y + 2);
        g_stations.push_back(s);
    };
    // braseros sur le chemin de ronde
    add(ST_BRAZIER, vec3(-3, 18, 29.6f), 0);
    add(ST_BRAZIER, vec3(-24, 18, 29.6f), 0);
    add(ST_BRAZIER, vec3(24, 18, 29.6f), 0);
    add(ST_BRAZIER, vec3(-29.6f, 18, 0), 0);
    add(ST_BRAZIER, vec3(29.6f, 18, 0), 0);
    add(ST_BRAZIER, vec3(0, 10, 12), 0);
    // chaudron d'huile au-dessus de la porte
    add(ST_CAULDRON, vec3(0, 20, 34.5f), 0);
    if (era >= 2) {
        add(ST_CANNON, vec3(-12, 18, 30), 0);
        add(ST_CANNON, vec3(12, 18, 30), 0);
        add(ST_CANNON, vec3(-31.5f, 24, 31.5f), -0.6f);
        add(ST_CANNON, vec3(31.5f, 24, 31.5f), 0.6f);
    }
    if (era >= 5) {
        add(ST_MG_NEST, vec3(-6, 18, 30.2f), 0);
        add(ST_MG_NEST, vec3(6, 18, 30.2f), 0);
    }
}

void stationsUpdate(float dt) {
    for (auto& s : g_stations) {
        s.cooldown -= dt;
        if (s.kind == ST_BRAZIER) {
            vec3 f = s.pos + vec3(0, 1.1f, 0);
            if (g_weather.rain < 0.9f) {
                if (frand() < dt * 14) fxFire(f + vec3(frange(-0.2f, 0.2f), 0, frange(-0.2f, 0.2f)), 0.45f, 1);
                if (frand() < dt * 2) fxSmoke(f + vec3(0, 0.6f, 0), 0.4f, 1, 0.6f);
                lightAdd(f, 1.0f, 0.6f, 0.25f, 9, 2 - g_weather.daylight);
            }
        }
    }
}

void stationsDraw() {
    Batch& M = g_batches[BA_METAL];
    Batch& W = g_batches[BA_WOOD];
    for (auto& s : g_stations) {
        if (!sphereVisible(s.pos, 2)) continue;
        Mat4 m = Mat4::translate(s.pos) * Mat4::rotY(s.yaw);
        switch (s.kind) {
        case ST_BRAZIER:
            emitCylinder(M, m, 0.05f, 0.9f, 4, rgba(60, 60, 60), false);
            emitCylinder(M, m * Mat4::translate(vec3(0, 0.85f, 0)), 0.35f, 0.25f, 8, rgba(70, 65, 60), true);
            break;
        case ST_CAULDRON: {
            float tilt = s.cooldown > 17 ? (20 - s.cooldown) * 0.5f : 0;
            emitBox(W, m * Mat4::translate(vec3(-0.7f, 0.6f, 0)), vec3(0.06f, 0.6f, 0.06f), rgba(150, 130, 100));
            emitBox(W, m * Mat4::translate(vec3(0.7f, 0.6f, 0)), vec3(0.06f, 0.6f, 0.06f), rgba(150, 130, 100));
            emitBox(W, m * Mat4::translate(vec3(0, 1.2f, 0)), vec3(0.8f, 0.05f, 0.05f), rgba(150, 130, 100));
            emitCylinder(M, m * Mat4::translate(vec3(0, 1.1f, 0)) * Mat4::rotX(tilt) * Mat4::translate(vec3(0, -0.6f, 0)), 0.45f, 0.6f, 9, rgba(55, 55, 55), true);
            if (s.cooldown <= 0 && frand() < 0.2f) spawnParticle(PT_SMOKE, s.pos + vec3(0, 1.2f, 0), vec3(0, 0.5f, 0), 0.2f, 1.5f, rgba(200, 200, 200, 80));
            break;
        }
        case ST_CANNON: {
            Mat4 cm = Mat4::translate(s.pos) * Mat4::rotY(s.yaw);
            emitBox(W, cm * Mat4::translate(vec3(0, 0.35f, 0)), vec3(0.4f, 0.18f, 0.8f), rgba(140, 115, 85));
            float rec = s.cooldown > 3 ? (s.cooldown - 3) * 0.4f : 0;
            emitCylinder(M, cm * Mat4::translate(vec3(0, 0.75f, -0.4f - rec)) * Mat4::rotX(PI * 0.5f - s.pitch), 0.17f, 2.0f, 9, rgba(65, 65, 68), true);
            for (int k = -1; k <= 1; k += 2) emitCylinder(W, cm * Mat4::translate(vec3(k * 0.48f, 0.38f, 0.1f)) * Mat4::rotZ(PI * 0.5f), 0.38f, 0.1f, 9, rgba(140, 115, 85), true);
            break;
        }
        case ST_MG_NEST: {
            for (int k = -2; k <= 2; k++)
                emitBox(g_batches[BA_CLOTH], m * Mat4::translate(vec3(k * 0.45f, 0.15f, 0.6f)), vec3(0.22f, 0.15f, 0.18f), rgba(170, 150, 110));
            Mat4 gm = Mat4::translate(s.pos + vec3(0, 1.0f, 0)) * Mat4::rotY(s.yaw) * Mat4::rotX(-s.pitch);
            emitBox(M, gm, vec3(0.06f, 0.08f, 0.45f), rgba(45, 45, 48));
            emitBox(M, gm * Mat4::translate(vec3(0, 0, 0.6f)), vec3(0.02f, 0.02f, 0.3f), rgba(45, 45, 48));
            emitBox(M, m * Mat4::translate(vec3(0, 0.5f, 0)), vec3(0.03f, 0.5f, 0.03f), rgba(45, 45, 48));
            break;
        }
        }
    }
}

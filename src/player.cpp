// Eternal Siege Vita - joueur à la première personne
#include "game.h"
#include <vitaGL.h>

Player g_player;
char g_prompt[96];
static float s_vy = 0;
static float s_lookVelX = 0, s_lookVelY = 0;
static float s_fireHeld = 0;

const WeaponDef g_weaponDefs[W_COUNT] = {
    // nom, chargeur, rechargement, cadence, dégâts, dispersion, portée, mêlée, projectile, auto
    {"Épée", 0, 0, 0.55f, 45, 0, 2.4f, true, false, false},
    {"Arc", 0, 0, 0.25f, 0, 0, 0, false, true, false},
    {"Arbalète", 1, 2.6f, 0.2f, 70, 0.004f, 0, false, true, false},
    {"Arquebuse", 1, 5.0f, 0.3f, 85, 0.03f, 90, false, false, false},
    {"Mousquet", 1, 4.0f, 0.3f, 80, 0.018f, 110, false, false, false},
    {"Fusil", 5, 3.0f, 1.0f, 65, 0.006f, 150, false, false, false},
    {"Mitrailleuse", 60, 4.0f, 0.09f, 24, 0.025f, 150, false, false, true},
    {"Fusil d'assaut", 30, 2.4f, 0.085f, 28, 0.012f, 150, false, false, true},
    {"Lance-roquettes", 1, 3.5f, 0.5f, 0, 0.002f, 0, false, true, false},
};

vec3 playerEye() { return g_player.pos + vec3(0, 1.62f, 0); }

void playerSetupLoadout(int era) {
    Player& p = g_player;
    p.nWeapons = 0;
    auto add = [&](int w) { p.weapons[p.nWeapons++] = w; };
    switch (era) {
    case 0: add(W_BOW); add(W_SWORD); break;
    case 1: add(W_CROSSBOW); add(W_BOW); add(W_SWORD); break;
    case 2: add(W_ARQUEBUS); add(W_BOW); add(W_SWORD); break;
    case 3: add(W_MUSKET); add(W_SWORD); break;
    case 4: add(W_RIFLE); add(W_SWORD); break;
    case 5: add(W_RIFLE); add(W_MG); add(W_SWORD); break;
    case 6: add(W_ASSAULT); add(W_ROCKET); add(W_SWORD); break;
    default: add(W_ASSAULT); add(W_ROCKET); add(W_MG); break;
    }
    p.cur = 0;
    for (int w = 0; w < W_COUNT; w++) p.mag[w] = g_weaponDefs[w].mag;
    p.grenades = era >= 5 ? 4 : 0;
    p.reloadT = 0;
}

void playerInit() {
    memset(&g_player, 0, sizeof(g_player));
    g_player.held = -1;
    g_player.station = -1;
    g_player.stone = 40;
    playerSetupLoadout(0);
    playerSpawn();
}

void playerSpawn() {
    Player& p = g_player;
    p.pos = vec3(frange(-2, 2), 10, 15);
    p.pos.y = surfaceHeight(p.pos.x, p.pos.z, 12);
    p.vel = vec3(0, 0, 0);
    p.yaw = 0; p.pitch = 0.05f;
    p.hp = 100;
    p.dead = false;
    p.deadT = 0;
    p.held = -1;
    p.station = -1;
    p.bloodScreen = 0;
    p.hurtT = 0;
    s_vy = 0;
    if (g_game.era >= 5) p.grenades = 4;
}

void playerDamage(float dmg, const vec3& from) {
    Player& p = g_player;
    if (p.dead || dmg <= 0) return;
    p.hp -= dmg;
    p.hurtT = 0.4f;
    p.bloodScreen = fminf(1, p.bloodScreen + dmg / 50.0f);
    p.shake = fmaxf(p.shake, clampf(dmg / 60.0f, 0.05f, 0.5f));
    fxBlood(p.pos + vec3(0, 1.3f, 0), normalize(p.pos - from), 4);
    if (dmg > 8) sndPlay2D(S_GRUNT, 0.8f, frange(0.8f, 0.95f));
    if (p.hp <= 0) {
        p.dead = true;
        p.deadT = 0;
        p.hp = 0;
        if (p.held >= 0 && p.held < (int)g_bodies.size()) g_bodies[p.held].held = false;
        p.held = -1;
        p.station = -1;
        sndPlay2D(S_SCREAM1 + g_rng.irange(0, 3), 1, 0.9f);
        gameMessage("Vous êtes tombé", "Un renfort arrive...", 4);
    }
}

static int curWeapon() { return g_player.weapons[g_player.cur]; }

static void meleeAttack() {
    Player& p = g_player;
    vec3 eye = playerEye();
    vec3 f = g_cam.fwd;
    bool hitSomething = false;
    for (size_t i = 0; i < g_chars.size(); i++) {
        Character& c = g_chars[i];
        if (!c.alive || c.team == TEAM_ALLY) continue;
        vec3 to = c.pos + vec3(0, 1.1f, 0) - eye;
        float l = length(to);
        if (l > 2.6f) continue;
        if (dot(to / l, f) < 0.55f) continue;
        charDamage((int)i, 45 * frange(0.9f, 1.2f), eye, normalize(to), DC_BLADE);
        sndPlay(S_SWORD_HIT, c.pos + vec3(0, 1.2f, 0), 0.9f, frange(0.9f, 1.1f));
        hitSomething = true;
    }
    float t;
    int ti = treeAtRay(eye, f, 2.4f, t);
    if (ti >= 0) {
        treeDamage(ti, 22, eye);
        sndPlay(S_HIT_WOOD, eye + f * t, 0.9f, frange(0.8f, 1.0f));
        for (int k = 0; k < 6; k++) spawnParticle(PT_DEBRIS, eye + f * t, -f * 2 + g_rng.unit() * 2, 0.05f, 1.5f, rgba(170, 140, 90));
        hitSomething = true;
    }
    int bi = bodyAtRay(eye, f, 2.4f, t);
    if (bi >= 0) {
        bodyDamage(bi, 30, eye, false);
        g_bodies[bi].vel += f * 3;
        bodyWake(g_bodies[bi]);
        sndPlay(S_HIT_WOOD, eye + f * t, 0.8f, 1);
        hitSomething = true;
    }
    int hi, hj, hk; vec3 n;
    if (!hitSomething && castleRaycast(eye, f, 2.4f, t, hi, hj, hk, n)) {
        castleDamageVoxel(hi, hj, hk, voxAt(hi, hj, hk) == M_WOOD ? 20 : 3, eye);
        fxSparks(eye + f * t, n, 6);
        sndPlay(S_CLANG, eye + f * t, 0.8f, frange(0.9f, 1.2f));
    }
    (void)p;
}

static void fireWeapon() {
    Player& p = g_player;
    int w = curWeapon();
    const WeaponDef& d = g_weaponDefs[w];
    vec3 eye = playerEye();
    vec3 f = g_cam.fwd;
    float spread = d.spread * (p.zoom > 0.5f ? 0.4f : 1.0f) * (length(p.vel) > 1 ? 1.8f : 1.0f);
    vec3 dir = normalize(f + g_rng.unit() * spread);
    vec3 muzzle = eye + f * 0.6f + g_cam.right * 0.15f - g_cam.up * 0.12f;
    switch (w) {
    case W_SWORD:
        p.swingT = 0.001f;
        sndPlay2D(S_SWORD_SWING, 0.8f, frange(0.9f, 1.15f));
        break;
    case W_CROSSBOW:
        projSpawn(PK_BOLT, eye + f * 0.5f, dir * 60, TEAM_PLAYER, d.dmg, p.arrowFlaming);
        sndPlay2D(S_CROSSBOW, 0.9f, 1);
        p.recoil = 0.25f;
        p.arrowFlaming = false;
        break;
    case W_ROCKET:
        projSpawn(PK_ROCKET, muzzle + f * 0.5f, dir * 45, TEAM_PLAYER, 0, false);
        sndPlay2D(S_ROCKET, 1, 1);
        fxSmoke(eye - f * 1.0f, 1.0f, 4, 0.1f);
        p.recoil = 0.6f;
        break;
    default: {
        hitscan(eye, dir, d.range, d.dmg, TEAM_PLAYER, w >= W_RIFLE);
        int snd = (w == W_ARQUEBUS || w == W_MUSKET) ? S_MUSKET : (w == W_MG ? S_MG : S_RIFLE);
        sndPlay2D(snd, 1, frange(0.95f, 1.05f));
        spawnParticle(PT_FLASH, muzzle + f * 0.3f, vec3(0, 0, 0), 0.3f, 0.05f, rgba(255, 220, 150));
        lightAdd(muzzle, 1.5f, 1.1f, 0.6f, 6, 3);
        if (w == W_ARQUEBUS || w == W_MUSKET) fxSmoke(muzzle + f * 0.6f, 0.7f, 4, 0.1f);
        p.recoil = (w == W_MG || w == W_ASSAULT) ? 0.12f : 0.45f;
        p.pitch += (w == W_MG || w == W_ASSAULT) ? 0.006f : 0.03f;
        break;
    }
    }
    if (d.mag > 0) {
        p.mag[w]--;
        if (p.mag[w] <= 0) { p.reloadT = d.reload; sndPlay2D(S_RELOAD, 0.7f, 1); }
    }
    p.cooldown = d.fireDelay;
}

static int findStation() {
    vec3 eye = playerEye();
    int best = -1;
    float bd = 2.2f;
    for (size_t i = 0; i < g_stations.size(); i++) {
        Station& s = g_stations[i];
        if (s.kind == ST_BRAZIER) continue;
        float d = length(s.pos + vec3(0, 0.8f, 0) - eye);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

static int findLadder() {
    for (size_t i = 0; i < g_bodies.size(); i++) {
        Body& b = g_bodies[i];
        if (!b.alive || b.kind != BK_LADDER || !b.sleeping) continue;
        vec3 top = b.pos + b.rot.dir(vec3(0, b.half.y, 0));
        if (length(top - playerEye()) < 2.4f) return (int)i;
    }
    return -1;
}

static int findPickup() {
    float t;
    int bi = bodyAtRay(playerEye(), g_cam.fwd, 2.6f, t);
    if (bi >= 0) {
        Body& b = g_bodies[bi];
        if (b.mass <= 90 && b.kind != BK_LADDER && b.kind != BK_CART) return bi;
    }
    return -1;
}

static bool nearBrazier() {
    for (auto& s : g_stations)
        if (s.kind == ST_BRAZIER && length(s.pos + vec3(0, 1, 0) - playerEye()) < 2.3f && g_weather.rain < 0.9f) return true;
    return false;
}

static void updateStation(float dt) {
    Player& p = g_player;
    Station& s = g_stations[p.station];
    if (g_in.pressed & (B_CIRCLE | B_SQUARE)) { s.active = false; p.station = -1; return; }
    if (s.kind == ST_CAULDRON) {
        if (s.cooldown <= 0) {
            vec3 lip = s.pos + vec3(0, 1.0f, 1.2f);
            for (int i = 0; i < 26; i++)
                projSpawn(PK_OIL, lip + vec3(frange(-0.6f, 0.6f), 0, frange(0, 0.5f)), vec3(frange(-1.5f, 1.5f), frange(-1, 1), frange(1, 3.5f)), TEAM_PLAYER, 25, true);
            sndPlay(S_IGNITE, lip, 1, 0.7f);
            sndPlay(S_SPLASH, lip, 0.8f, 0.6f);
            s.cooldown = 20;
            gameMessage("Huile bouillante !", "", 2);
        } else {
            gameMessage("Le chaudron chauffe...", "", 1.5f);
        }
        p.station = -1;
        return;
    }
    // canon ou mitrailleuse : visée au stick droit
    s.yaw -= g_in.rx * dt * 1.2f;
    s.pitch = clampf(s.pitch - g_in.ry * dt * 0.8f, -0.45f, 0.5f);
    p.yaw = s.yaw + PI;
    p.pitch = s.pitch;
    vec3 f(-sinf(s.yaw + PI) * cosf(s.pitch), sinf(s.pitch), -cosf(s.yaw + PI) * cosf(s.pitch));
    f = normalize(vec3(sinf(s.yaw) * cosf(s.pitch), sinf(s.pitch), cosf(s.yaw) * cosf(s.pitch)));
    p.yaw = atan2f(-f.x, -f.z);
    vec3 muzzle = s.pos + vec3(0, 1.0f, 0) + f * 2.0f;
    if (s.kind == ST_CANNON && (g_in.pressed & B_R) && s.cooldown <= 0) {
        s.cooldown = 4.5f;
        if (g_game.era >= 5) projSpawn(PK_SHELL, muzzle, f * 80, TEAM_PLAYER, 0, false);
        else projSpawn(PK_BALL, muzzle, f * 70, TEAM_PLAYER, 0, false);
        sndPlay2D(S_CANNON, 1, frange(0.95f, 1.05f));
        fxSmoke(muzzle + f, 1.5f, 8, 0.15f);
        spawnParticle(PT_FLASH, muzzle + f * 0.5f, vec3(0, 0, 0), 1.3f, 0.12f, rgba(255, 220, 150));
        lightAdd(muzzle, 2, 1.4f, 0.7f, 12, 4);
        p.shake = 0.5f;
    }
    if (s.kind == ST_MG_NEST && (g_in.held & B_R) && s.cooldown <= 0) {
        s.cooldown = 0.08f;
        hitscan(muzzle, normalize(f + g_rng.unit() * 0.02f), 160, 26, TEAM_PLAYER, true);
        sndPlay2D(S_MG, 0.9f, frange(0.95f, 1.05f));
        spawnParticle(PT_FLASH, muzzle, vec3(0, 0, 0), 0.3f, 0.04f, rgba(255, 220, 150));
        p.shake = fmaxf(p.shake, 0.08f);
    }
    snprintf(g_prompt, sizeof(g_prompt), "R : Tirer   ○ : Quitter");
}

void playerUpdate(float dt) {
    Player& p = g_player;
    g_prompt[0] = 0;
    p.hurtT -= dt;
    p.bloodScreen = fmaxf(0, p.bloodScreen - dt * 0.25f);
    p.shake = fmaxf(0, p.shake - dt * 1.5f);
    p.recoil = fmaxf(0, p.recoil - dt * 3);
    if (p.dead) {
        p.deadT += dt;
        p.pos.y = fmaxf(p.pos.y - dt * 2, surfaceHeight(p.pos.x, p.pos.z, p.pos.y) - 1.2f);
        if (p.deadT > 6) { playerSpawn(); gameMessage("Renfort", "Vous rejoignez la défense", 3); }
        return;
    }
    if (p.hp < 100) p.hp = fminf(100, p.hp + dt * 2.0f);
    if (p.station >= 0) { updateStation(dt); return; }
    // regard (accélération progressive)
    float sens = p.zoom > 0.5f ? 1.1f : 2.6f;
    float tx = g_in.rx * fabsf(g_in.rx) * sens, ty = g_in.ry * fabsf(g_in.ry) * sens * 0.75f;
    s_lookVelX = lerpf(s_lookVelX, tx, clampf(dt * 14, 0, 1));
    s_lookVelY = lerpf(s_lookVelY, ty, clampf(dt * 14, 0, 1));
    p.yaw -= s_lookVelX * dt;
    p.pitch = clampf(p.pitch - s_lookVelY * dt, -1.45f, 1.45f);
    // déplacement
    vec3 fwd(-sinf(p.yaw), 0, -cosf(p.yaw)), right(cosf(p.yaw), 0, -sinf(p.yaw));
    vec3 wish = fwd * -g_in.ly + right * g_in.lx;
    float wl = length(wish);
    if (wl > 1) wish = wish / wl;
    p.inWater = inRiver(p.pos + vec3(0, 0.4f, 0));
    float speed = (wl > 0.9f && p.zoom < 0.5f) ? 5.6f : 3.8f;
    if (p.inWater) speed *= 0.5f;
    if (p.held >= 0) speed *= 0.75f;
    vec3 target = wish * speed;
    float acc = p.onGround ? 12.0f : 3.0f;
    p.vel.x = lerpf(p.vel.x, target.x, clampf(dt * acc, 0, 1));
    p.vel.z = lerpf(p.vel.z, target.z, clampf(dt * acc, 0, 1));
    if ((g_in.pressed & B_CROSS) && p.onGround) { s_vy = 6.2f; p.onGround = false; sndPlay2D(S_STEP, 0.6f, 0.8f); }
    float oldVy = s_vy;
    bool wasGround = p.onGround;
    actorMove(p.pos, s_vy, p.vel * dt, 0.3f, dt, p.onGround, nullptr, nullptr, nullptr);
    // pousse les objets légers
    for (auto& b : g_bodies) {
        if (!b.alive || b.held) continue;
        vec3 d = b.pos - p.pos;
        if (d.y < -0.5f || d.y > 1.8f) continue;
        d.y = 0;
        float l = length(d), r = 0.35f + fmaxf(b.half.x, b.half.z);
        if (l < r && l > 0.01f) {
            vec3 n = d / l;
            if (b.mass < 150) { bodyWake(b); b.vel += n * (r - l) * 8 + p.vel * 0.1f; }
            else { p.pos -= n * (r - l); }
        }
    }
    if (!wasGround && p.onGround) {
        if (oldVy < -13) playerDamage((-oldVy - 13) * 7, p.pos - vec3(0, 2, 0));
        if (oldVy < -4) { sndPlay2D(S_LAND, 0.7f, 1); p.landT = 0.25f; }
        if (p.inWater) fxSplash(p.pos, 0.6f);
    }
    p.landT = fmaxf(0, p.landT - dt);
    // pas et balancement de tête
    float hs = length(vec3(p.vel.x, 0, p.vel.z));
    if (p.onGround && hs > 0.5f) {
        p.bob += dt * hs * 2.2f;
        p.stepT -= dt * hs;
        if (p.stepT <= 0) {
            p.stepT = 2.2f;
            if (p.inWater) fxSplash(p.pos, 0.25f);
            else sndPlay2D(S_STEP, 0.35f, frange(0.85f, 1.15f));
        }
    }
    p.bobAmp = lerpf(p.bobAmp, p.onGround ? clampf(hs / 5, 0, 1) : 0, clampf(dt * 8, 0, 1));
    p.zoom = lerpf(p.zoom, (g_in.held & B_L) ? 1.0f : 0.0f, clampf(dt * 10, 0, 1));
    // objet tenu
    if (p.held >= 0) {
        Body& b = g_bodies[p.held];
        if (!b.alive) { p.held = -1; }
        else {
            vec3 hold = playerEye() + g_cam.fwd * (1.1f + length(b.half)) - vec3(0, 0.3f, 0);
            b.vel = (hold - b.pos) * 12;
            b.pos = lerp(b.pos, hold, clampf(dt * 15, 0, 1));
            b.rot = Mat4::rotY(p.yaw);
            b.sleeping = false;
            snprintf(g_prompt, sizeof(g_prompt), "R : Lancer   □ : Poser");
            if (g_in.pressed & B_R) {
                b.held = false;
                b.vel = g_cam.fwd * (18 - b.mass * 0.1f) + vec3(0, 2, 0);
                b.angVel = g_rng.unit() * 4;
                b.life = 0;
                p.held = -1;
                sndPlay2D(S_THROW, 0.8f, 1);
                p.cooldown = 0.4f;
            } else if (g_in.pressed & B_SQUARE) {
                b.held = false;
                b.vel = p.vel;
                p.held = -1;
            }
            return;
        }
    }
    // armes
    int w = curWeapon();
    const WeaponDef& d = g_weaponDefs[w];
    p.cooldown -= dt;
    p.switchT = fmaxf(0, p.switchT - dt * 3);
    if (p.reloadT > 0) {
        p.reloadT -= dt;
        if (p.reloadT <= 0) p.mag[w] = d.mag;
    }
    if (g_in.pressed & B_TRIANGLE) {
        p.cur = (p.cur + 1) % p.nWeapons;
        p.switchT = 1;
        p.drawT = 0;
        p.reloadT = 0;
        int nw = curWeapon();
        if (g_weaponDefs[nw].mag > 0 && p.mag[nw] <= 0) p.reloadT = g_weaponDefs[nw].reload;
        sndPlay2D(S_RELOAD, 0.4f, 1.3f);
    }
    if (p.swingT > 0) {
        float prev = p.swingT;
        p.swingT += dt * 2.2f;
        if (prev < 0.35f && p.swingT >= 0.35f) meleeAttack();
        if (p.swingT >= 1) p.swingT = 0;
    }
    if (w == W_BOW) {
        if (g_in.held & B_R) {
            if (p.drawT == 0) sndPlay2D(S_BOW, 0.25f, 0.6f);
            p.drawT = fminf(1, p.drawT + dt * 1.4f);
            if (nearBrazier() && !p.arrowFlaming) { p.arrowFlaming = true; sndPlay2D(S_IGNITE, 0.7f, 1.2f); }
        } else if (p.drawT > 0) {
            if (p.drawT > 0.15f && p.cooldown <= 0) {
                vec3 dir = normalize(g_cam.fwd + g_rng.unit() * (1 - p.drawT) * 0.03f);
                projSpawn(PK_ARROW, playerEye() + g_cam.fwd * 0.5f, dir * (18 + p.drawT * 34), TEAM_PLAYER, 18 + p.drawT * 42, p.arrowFlaming);
                sndPlay2D(S_BOW, 0.9f, frange(0.95f, 1.05f));
                p.arrowFlaming = false;
                p.cooldown = 0.3f;
            }
            p.drawT = 0;
        }
    } else if (p.reloadT <= 0 && p.cooldown <= 0 && p.switchT < 0.3f) {
        bool trig = d.automatic ? (g_in.held & B_R) != 0 : (g_in.pressed & B_R) != 0;
        if (w == W_CROSSBOW && nearBrazier() && !p.arrowFlaming) { p.arrowFlaming = true; sndPlay2D(S_IGNITE, 0.7f, 1.2f); }
        if (trig && (d.mag == 0 || p.mag[w] > 0)) fireWeapon();
    }
    (void)s_fireHeld;
    // grenade
    if ((g_in.pressed & B_UP) && p.grenades > 0) {
        p.grenades--;
        projSpawn(PK_GRENADE, playerEye() + g_cam.fwd * 0.6f, g_cam.fwd * 16 + vec3(0, 3, 0), TEAM_PLAYER, 0, false);
        sndPlay2D(S_THROW, 0.8f, 1);
    }
    // interactions
    int st = findStation();
    int lad = findLadder();
    int pick = findPickup();
    int ri, rj, rk;
    bool canRepair = castleRepairTarget(playerEye(), g_cam.fwd, ri, rj, rk);
    if (st >= 0) {
        const char* names[] = {"", "Verser l'huile", "Utiliser le canon", "Utiliser la mitrailleuse"};
        snprintf(g_prompt, sizeof(g_prompt), "□ : %s", names[g_stations[st].kind]);
        if (g_in.pressed & B_SQUARE) { p.station = st; g_stations[st].active = true; }
    } else if (lad >= 0) {
        snprintf(g_prompt, sizeof(g_prompt), "□ : Repousser l'échelle");
        if (g_in.pressed & B_SQUARE) {
            Body& b = g_bodies[lad];
            vec3 out = b.pos - p.pos; out.y = 0; out = normalize(out);
            bodyWake(b);
            b.vel = out * 4 + vec3(0, 1, 0);
            b.angVel = cross(vec3(0, 1, 0), out) * 1.5f;
            sndPlay(S_HIT_WOOD, b.pos, 1, 0.7f);
            sndPlay(S_SCREAM2, b.pos, 0.8f, 1.1f);
        }
    } else if (pick >= 0) {
        snprintf(g_prompt, sizeof(g_prompt), "□ : Prendre");
        if (g_in.pressed & B_SQUARE) { p.held = pick; g_bodies[pick].held = true; bodyWake(g_bodies[pick]); }
    } else if (canRepair) {
        snprintf(g_prompt, sizeof(g_prompt), "○ (maintenir) : Réparer  [pierres %d]", p.stone);
    } else if (w == W_BOW && nearBrazier()) {
        snprintf(g_prompt, sizeof(g_prompt), "R : Bander l'arc pour enflammer la flèche");
    }
    if ((g_in.held & B_CIRCLE) && canRepair && p.stone > 0) {
        p.repairT += dt;
        if (p.repairT > 0.5f) {
            p.repairT = 0;
            castleRepairCell(ri, rj, rk);
            p.stone--;
            sndPlay(S_HIT_STONE, voxCenter(ri, rj, rk), 0.8f, 0.8f);
            fxDust(voxCenter(ri, rj, rk), 0.5f, 4, rgba(150, 140, 125));
        }
    } else p.repairT = 0;
}

// ---------------------------------------------------------------- Arme en main
void playerDrawViewmodel() {
    Player& p = g_player;
    if (p.dead || p.station >= 0) return;
    glClear(GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    Mat4 pr = Mat4::perspective(1.05f, 960.0f / 544.0f, 0.02f, 10);
    glLoadMatrixf(pr.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_FOG);
    float bob = sinf(p.bob * 2) * 0.012f * p.bobAmp, bobx = cosf(p.bob) * 0.015f * p.bobAmp;
    float sw = p.switchT;
    vec3 base(0.22f + bobx, -0.24f + bob - sw * 0.3f - p.landT * 0.1f, -0.45f + p.recoil * 0.12f);
    if (p.zoom > 0) base = lerp(base, vec3(0.0f, -0.16f + bob * 0.3f, -0.4f), p.zoom * 0.8f);
    Mat4 m = Mat4::translate(base) * Mat4::rotX(p.recoil * 0.3f);
    Color skin = rgba(220, 180, 150), wood = rgba(190, 165, 130), metal = rgba(150, 150, 155), dark = rgba(55, 55, 58);
    Batch& W = g_batches[BA_WOOD];
    Batch& M = g_batches[BA_METAL];
    Batch& S = g_batches[BA_SKIN];
    Batch& L = g_batches[BA_LEATHER];
    int w = curWeapon();
    if (p.held >= 0) {
        // mains tendues
        emitBox(S, Mat4::translate(vec3(-0.25f, -0.25f, -0.5f)), vec3(0.04f, 0.04f, 0.12f), skin);
        emitBox(S, Mat4::translate(vec3(0.25f, -0.25f, -0.5f)), vec3(0.04f, 0.04f, 0.12f), skin);
    } else
        switch (w) {
        case W_SWORD: {
            float t = p.swingT;
            float a = t > 0 ? (t < 0.35f ? -t / 0.35f * 1.2f : -1.2f + (t - 0.35f) / 0.65f * 2.6f) : 0;
            Mat4 sm = m * Mat4::translate(vec3(0.05f, 0, 0)) * Mat4::rotZ(t > 0 ? -0.9f * sinf(t * PI) : 0) * Mat4::rotX(0.9f + (t > 0 ? -a : 0));
            emitBox(L, sm * Mat4::translate(vec3(0, -0.05f, 0)), vec3(0.018f, 0.07f, 0.018f), rgba(90, 60, 40));
            emitBox(M, sm * Mat4::translate(vec3(0, 0.03f, 0)), vec3(0.08f, 0.01f, 0.015f), dark);
            emitBox(M, sm * Mat4::translate(vec3(0, 0.38f, 0)), vec3(0.022f, 0.35f, 0.005f), metal);
            emitBox(S, sm * Mat4::translate(vec3(0, -0.06f, 0.02f)), vec3(0.045f, 0.05f, 0.05f), skin);
            break;
        }
        case W_BOW: {
            Mat4 bm = Mat4::translate(vec3(-0.12f + bobx, -0.12f + bob, -0.5f)) * Mat4::rotZ(0.25f);
            emitBox(W, bm, vec3(0.012f, 0.45f, 0.015f), wood);
            emitBox(W, bm * Mat4::translate(vec3(0, 0.4f, 0.04f)) * Mat4::rotX(0.25f), vec3(0.01f, 0.1f, 0.012f), wood);
            emitBox(W, bm * Mat4::translate(vec3(0, -0.4f, 0.04f)) * Mat4::rotX(-0.25f), vec3(0.01f, 0.1f, 0.012f), wood);
            float pull = p.drawT * 0.25f;
            emitBox(g_batches[BA_WHITE], bm * Mat4::translate(vec3(0, 0.22f, 0.07f + pull * 0.5f)) * Mat4::rotX(-0.6f * (0.2f + p.drawT)), vec3(0.002f, 0.24f, 0.002f), rgba(220, 220, 200));
            emitBox(g_batches[BA_WHITE], bm * Mat4::translate(vec3(0, -0.22f, 0.07f + pull * 0.5f)) * Mat4::rotX(0.6f * (0.2f + p.drawT)), vec3(0.002f, 0.24f, 0.002f), rgba(220, 220, 200));
            emitBox(S, bm, vec3(0.04f, 0.05f, 0.04f), skin);
            if (p.cooldown < 0.1f) {
                Mat4 am = bm * Mat4::rotZ(-0.25f) * Mat4::translate(vec3(0.0f, 0, -0.2f + pull));
                emitBox(W, am, vec3(0.006f, 0.006f, 0.38f), rgba(200, 180, 140));
                emitBox(M, am * Mat4::translate(vec3(0, 0, -0.39f)), vec3(0.012f, 0.004f, 0.03f), metal);
                emitBox(S, am * Mat4::translate(vec3(0, 0, 0.36f)), vec3(0.035f, 0.04f, 0.04f), skin);
                if (p.arrowFlaming) {
                    vec3 tip = g_cam.pos + g_cam.fwd * 0.9f + g_cam.right * -0.1f - g_cam.up * 0.1f;
                    if (frand() < 0.5f) spawnParticle(PT_FIRE, tip, vec3(0, 0.4f, 0), 0.08f, 0.2f, rgba(255, 255, 255, 220));
                    lightAdd(tip, 1, 0.6f, 0.2f, 3, 3);
                }
            }
            break;
        }
        case W_CROSSBOW: {
            emitBox(W, m * Mat4::translate(vec3(0, 0, -0.1f)), vec3(0.03f, 0.035f, 0.3f), wood);
            emitBox(W, m * Mat4::translate(vec3(0, 0.03f, -0.36f)), vec3(0.28f, 0.015f, 0.025f), wood);
            if (p.reloadT <= 0) emitBox(W, m * Mat4::translate(vec3(0, 0.05f, -0.2f)), vec3(0.006f, 0.006f, 0.18f), rgba(200, 180, 140));
            emitBox(S, m * Mat4::translate(vec3(0, -0.05f, 0.12f)), vec3(0.04f, 0.05f, 0.05f), skin);
            break;
        }
        case W_ROCKET:
            emitCylinder(M, m * Mat4::translate(vec3(0, 0.02f, 0.3f)) * Mat4::rotX(-PI * 0.5f), 0.055f, 0.9f, 8, rgba(80, 90, 70), true);
            emitBox(S, m * Mat4::translate(vec3(0, -0.07f, -0.05f)), vec3(0.04f, 0.05f, 0.05f), skin);
            break;
        default: {
            bool modern = w >= W_RIFLE;
            float reloadDip = p.reloadT > 0 ? sinf(clampf(p.reloadT / g_weaponDefs[w].reload, 0, 1) * PI) * 0.15f : 0;
            Mat4 gm = m * Mat4::translate(vec3(0, -reloadDip, 0)) * Mat4::rotZ(reloadDip * 2);
            if (modern && w != W_RIFLE) {
                emitBox(M, gm * Mat4::translate(vec3(0, 0, -0.1f)), vec3(0.03f, 0.05f, 0.25f), dark);
                emitBox(M, gm * Mat4::translate(vec3(0, -0.07f, -0.05f)), vec3(0.018f, 0.06f, 0.03f), dark);
                emitBox(M, gm * Mat4::translate(vec3(0, 0.01f, -0.45f)), vec3(0.01f, 0.01f, 0.14f), dark);
                emitBox(M, gm * Mat4::translate(vec3(0, 0.065f, -0.1f)), vec3(0.01f, 0.015f, 0.05f), dark);
            } else {
                emitBox(W, gm * Mat4::translate(vec3(0, -0.01f, -0.05f)), vec3(0.025f, 0.035f, 0.32f), wood);
                emitBox(M, gm * Mat4::translate(vec3(0, 0.02f, -0.45f)), vec3(0.01f, 0.01f, 0.3f), dark);
            }
            emitBox(S, gm * Mat4::translate(vec3(0, -0.06f, 0.1f)), vec3(0.04f, 0.05f, 0.05f), skin);
            emitBox(S, gm * Mat4::translate(vec3(-0.05f, -0.05f, -0.3f)), vec3(0.04f, 0.04f, 0.05f), skin);
            break;
        }
        }
    batchesFlushOpaque();
    glEnable(GL_FOG);
}

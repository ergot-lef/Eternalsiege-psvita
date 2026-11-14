// Eternal Siege Vita - audio : sons synthétisés, mixage 3D, musique procédurale
#include "game.h"
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

static const int SR = 24000;     // fréquence des échantillons générés
static const int OUT_SR = 48000; // fréquence de sortie
static const int GRAIN = 512;

struct Sample { std::vector<int16_t> d; bool loop; };
static Sample s_snd[S_COUNT];

struct Voice { int id; float pos, step, vl, vr; bool active; };
static const int NV = 40;
static Voice s_voices[NV];

struct Cmd { int id; float vl, vr, step; };
static const int QN = 128;
static Cmd s_queue[QN];
static volatile int s_qHead = 0, s_qTail = 0;

struct LoopV { volatile int id; volatile float target, pitch; float vol, pos; };
static LoopV s_loops[LS_COUNT];

static vec3 s_lPos;
static float s_lYaw;
static volatile float s_musicInt = 0, s_musicTarget = 0;
static volatile int s_musicEra = 0;
static volatile bool s_musicPaused = false;
static int s_port = -1;

// ---------------------------------------------------------------- Synthèse
static Rng s_ar(31337);
static inline float nz() { return s_ar.f() * 2 - 1; }

struct OnePole { float z = 0; float lp(float x, float a) { z += a * (x - z); return z; } };
struct BiquadBP {
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(float f, float q) {
        float w = 2 * PI * f / SR, al = sinf(w) / (2 * q), c = cosf(w);
        float a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    float run(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

static std::vector<float> buf(float sec) { return std::vector<float>((size_t)(sec * SR), 0.0f); }

static void store(int id, std::vector<float>& f, bool loop, float gain = 1.0f) {
    float peak = 1e-4f;
    for (float v : f) peak = fmaxf(peak, fabsf(v));
    float k = gain / peak;
    s_snd[id].d.resize(f.size());
    for (size_t i = 0; i < f.size(); i++) s_snd[id].d[i] = (int16_t)clampf(f[i] * k * 30000, -32767, 32767);
    // fondu de bouclage
    if (loop) {
        size_t n = f.size(), fade = SR / 20;
        for (size_t i = 0; i < fade && i < n; i++) {
            float t = (float)i / fade;
            int a = s_snd[id].d[i], b = s_snd[id].d[n - fade + i];
            s_snd[id].d[n - fade + i] = (int16_t)(b * (1 - t) + a * t);
        }
    }
    s_snd[id].loop = loop;
}

static inline float env(float t, float a, float d) { return t < a ? t / a : expf(-(t - a) / d); }

static void genNoiseHit(int id, float dur, float lpA, float decay, float sub, float subF, float gain = 1) {
    auto f = buf(dur);
    OnePole lp, lp2;
    float ph = 0;
    for (size_t i = 0; i < f.size(); i++) {
        float t = (float)i / SR;
        float n = lp2.lp(lp.lp(nz(), lpA), lpA);
        float s = sinf(ph) * sub * expf(-t / (decay * 0.4f));
        ph += 2 * PI * subF * (1 - t * 0.3f) / SR;
        f[i] = (n * 2.5f + s) * env(t, 0.003f, decay);
    }
    store(id, f, false, gain);
}

static void genMetal(int id, float base, float dur, float gain) {
    auto f = buf(dur);
    float parts[5] = {1.0f, 2.31f, 3.17f, 4.43f, 5.9f};
    for (size_t i = 0; i < f.size(); i++) {
        float t = (float)i / SR;
        float s = 0;
        for (int p = 0; p < 5; p++) s += sinf(2 * PI * base * parts[p] * t) * expf(-t * (3 + p * 2)) / (1 + p * 0.4f);
        s += nz() * expf(-t * 60) * 0.6f;
        f[i] = s;
    }
    store(id, f, false, gain);
}

static void genWhoosh(int id, float dur, float f0, float f1, float gain) {
    auto f = buf(dur);
    BiquadBP bp;
    for (size_t i = 0; i < f.size(); i++) {
        float t = (float)i / f.size();
        if (i % 32 == 0) bp.set(f0 + (f1 - f0) * t, 1.2f);
        f[i] = bp.run(nz()) * sinf(t * PI);
    }
    store(id, f, false, gain);
}

static void genPluck(std::vector<float>& f, size_t off, float freq, float dur, float damp, float amp) {
    int n = (int)(SR / freq);
    if (n < 2) n = 2;
    std::vector<float> dl(n);
    for (int i = 0; i < n; i++) dl[i] = nz();
    int idx = 0;
    size_t len = (size_t)(dur * SR);
    for (size_t i = 0; i < len && off + i < f.size(); i++) {
        int nx = (idx + 1) % n;
        float v = dl[idx];
        dl[idx] = (dl[idx] + dl[nx]) * 0.5f * damp;
        idx = nx;
        f[off + i] += v * amp;
    }
}

static void genVoice(int id, float dur, float p0, float p1, float p2, float f1, float f2, float breath) {
    auto f = buf(dur);
    BiquadBP a, b, c;
    a.set(f1, 4); b.set(f2, 5); c.set(2600, 6);
    float ph = 0;
    for (size_t i = 0; i < f.size(); i++) {
        float t = (float)i / f.size();
        float pitch = t < 0.3f ? lerpf(p0, p1, t / 0.3f) : lerpf(p1, p2, (t - 0.3f) / 0.7f);
        pitch *= 1 + sinf(t * dur * 2 * PI * 6.5f) * 0.03f + nz() * 0.01f;
        ph += pitch / SR;
        ph -= floorf(ph);
        float saw = ph * 2 - 1;
        float src = saw + nz() * breath;
        float e = env(t * dur, 0.05f, dur * 0.45f) * (t < 0.85f ? 1 : (1 - t) / 0.15f);
        f[i] = (a.run(src) * 1.0f + b.run(src) * 0.7f + c.run(src) * 0.25f) * e;
    }
    store(id, f, false, 0.9f);
}

static void genAll() {
    // armes de trait
    {
        auto f = buf(0.5f);
        genPluck(f, 0, 98, 0.45f, 0.995f, 0.8f);
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] += nz() * expf(-t * 80) * 0.6f; }
        store(S_BOW, f, false, 0.8f);
    }
    genWhoosh(S_WHOOSH, 0.5f, 400, 1800, 0.6f);
    genWhoosh(S_SWORD_SWING, 0.28f, 2200, 700, 0.6f);
    genWhoosh(S_THROW, 0.3f, 600, 1400, 0.5f);
    {
        auto f = buf(0.25f);
        BiquadBP bp; bp.set(700, 1.5f);
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = sinf(2 * PI * 75 * t) * expf(-t * 25) + bp.run(nz()) * expf(-t * 30) * 1.5f; }
        store(S_HIT_FLESH, f, false, 0.8f);
    }
    {
        auto f = buf(0.35f);
        BiquadBP bp; bp.set(520, 3);
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = bp.run(nz() * expf(-t * 40)) * 3 + sinf(2 * PI * 210 * t) * expf(-t * 22) * 0.6f; }
        store(S_HIT_WOOD, f, false, 0.8f);
    }
    {
        auto f = buf(0.25f);
        BiquadBP bp; bp.set(1800, 2);
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = bp.run(nz()) * expf(-t * 45); }
        store(S_HIT_STONE, f, false, 0.7f);
    }
    genMetal(S_SWORD_HIT, 1150, 0.8f, 0.7f);
    genMetal(S_CLANG, 620, 0.6f, 0.7f);
    {
        auto f = buf(0.35f);
        BiquadBP bp; bp.set(900, 2);
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = bp.run(nz()) * expf(-t * 30) + sinf(2 * PI * 140 * t) * expf(-t * 40) * 0.8f; }
        store(S_CROSSBOW, f, false, 0.8f);
    }
    {
        auto f = buf(0.3f);
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            float c1 = t < 0.02f ? nz() * expf(-t * 300) : 0;
            float c2 = (t > 0.15f && t < 0.17f) ? nz() * expf(-(t - 0.15f) * 300) : 0;
            f[i] = c1 + c2 + sinf(2 * PI * 2400 * t) * (expf(-t * 120) + (t > 0.15f ? expf(-(t - 0.15f) * 120) : 0)) * 0.3f;
        }
        store(S_RELOAD, f, false, 0.6f);
    }
    // détonations
    genNoiseHit(S_EXPLO_BIG, 3.2f, 0.06f, 0.9f, 1.4f, 42, 1.0f);
    genNoiseHit(S_EXPLO_SMALL, 1.6f, 0.1f, 0.45f, 1.0f, 60, 0.9f);
    genNoiseHit(S_CANNON, 2.2f, 0.08f, 0.6f, 1.6f, 48, 1.0f);
    {
        auto f = buf(1.4f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            f[i] = nz() * expf(-t * 90) * 1.2f + lp.lp(nz(), 0.12f) * 3 * expf(-t * 3.5f) + sinf(2 * PI * 70 * t) * expf(-t * 10) * 0.7f;
        }
        store(S_MUSKET, f, false, 0.95f);
    }
    {
        auto f = buf(0.8f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            f[i] = nz() * expf(-t * 140) * 1.5f + lp.lp(nz(), 0.2f) * 2 * expf(-t * 7) + sinf(2 * PI * 90 * t) * expf(-t * 18) * 0.5f;
        }
        store(S_RIFLE, f, false, 0.9f);
    }
    {
        auto f = buf(0.22f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = nz() * expf(-t * 150) * 1.3f + lp.lp(nz(), 0.25f) * 1.5f * expf(-t * 18); }
        store(S_MG, f, false, 0.8f);
    }
    {
        auto f = buf(1.6f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = lp.lp(nz(), 0.3f) * (t < 0.1f ? t * 10 : expf(-(t - 0.1f) * 1.5f)) + nz() * expf(-t * 50) * 0.5f; }
        store(S_ROCKET, f, false, 0.8f);
    }
    // destructions
    {
        auto f = buf(0.9f);
        BiquadBP bp; bp.set(1500, 2);
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            float crack = (s_ar.f() < 0.004f * expf(-t * 4)) ? nz() * 6 : 0;
            f[i] = bp.run(crack + nz() * 0.3f * expf(-t * 8)) + sinf(2 * PI * 160 * t) * expf(-t * 12) * 0.4f;
        }
        store(S_WOOD_BREAK, f, false, 0.8f);
    }
    {
        auto f = buf(0.6f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            float g = (s_ar.f() < 0.01f * expf(-t * 6)) ? nz() * 4 : 0;
            f[i] = lp.lp(g + nz() * expf(-t * 12), 0.35f);
        }
        store(S_STONE_CRACK, f, false, 0.8f);
    }
    {
        auto f = buf(3.0f);
        OnePole lp, lp2;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            float bump = (s_ar.f() < 0.002f) ? nz() * 8 : 0;
            f[i] = lp2.lp(lp.lp(nz() * 2 + bump, 0.05f), 0.08f) * env(t, 0.05f, 1.1f);
        }
        store(S_COLLAPSE, f, false, 1.0f);
    }
    {
        auto f = buf(1.8f);
        float ph = 0;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            float jit = sinf(t * 70 + sinf(t * 13) * 3);
            ph += (55 + jit * 8) / SR;
            ph -= floorf(ph);
            f[i] = (ph * 2 - 1) * env(t, 0.2f, 0.7f) * (0.5f + 0.5f * (s_ar.f() < 0.02f ? 1 : 0.6f));
        }
        OnePole lp;
        for (auto& v : f) v = lp.lp(v, 0.15f);
        store(S_TREE_FALL, f, false, 0.7f);
    }
    {
        auto f = buf(0.6f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = sinf(2 * PI * 55 * t) * expf(-t * 9) * 1.5f + lp.lp(nz(), 0.2f) * expf(-t * 10) + nz() * expf(-t * 60) * 0.5f; }
        store(S_RAM, f, false, 1.0f);
    }
    {
        auto f = buf(0.8f);
        BiquadBP bp;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            if (i % 64 == 0) bp.set(2500 - t * 2000, 1);
            float bub = sinf(2 * PI * (400 + 600 * fmodf(t * 7, 1.0f)) * t) * 0.2f * expf(-t * 3);
            f[i] = bp.run(nz()) * env(t, 0.01f, 0.2f) + bub;
        }
        store(S_SPLASH, f, false, 0.7f);
    }
    // boucles d'ambiance
    {
        auto f = buf(2.0f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) {
            float crack = s_ar.f() < 0.003f ? nz() * 5 : 0;
            f[i] = lp.lp(nz(), 0.05f) * 1.5f + crack;
        }
        store(S_FIRE, f, true, 0.6f);
    }
    {
        auto f = buf(2.0f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) {
            float n = nz();
            float drop = s_ar.f() < 0.02f ? nz() * 2 : 0;
            f[i] = (n - lp.lp(n, 0.3f)) + drop * 0.5f;
        }
        store(S_RAIN, f, true, 0.5f);
    }
    {
        auto f = buf(4.0f);
        BiquadBP bp;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / f.size();
            if (i % 64 == 0) bp.set(300 + 200 * sinf(t * 2 * PI * 2) + 100 * sinf(t * 2 * PI * 5), 0.8f);
            f[i] = bp.run(nz()) * (0.7f + 0.3f * sinf(t * 2 * PI * 3));
        }
        store(S_WIND, f, true, 0.5f);
    }
    {
        auto f = buf(4.5f);
        OnePole lp, lp2;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            float crack = t < 0.3f ? nz() * expf(-t * 15) * 2 : 0;
            f[i] = lp2.lp(lp.lp(nz() * 3, 0.04f), 0.06f) * env(t, 0.1f, 1.4f) * (1 + 0.5f * sinf(t * 9)) + crack;
        }
        store(S_THUNDER, f, false, 1.0f);
    }
    {
        auto f = buf(2.0f);
        OnePole lp;
        float ph = 0;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            ph += (32 + sinf(t * 2 * PI * 4) * 3) / SR;
            ph -= floorf(ph);
            f[i] = lp.lp((ph * 2 - 1) + nz() * 0.6f, 0.08f) * (0.8f + 0.2f * sinf(t * 2 * PI * 16));
        }
        store(S_ENGINE, f, true, 0.7f);
    }
    {
        auto f = buf(2.0f);
        OnePole lp;
        float ph = 0;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            ph += 86.0f / SR; ph -= floorf(ph);
            f[i] = lp.lp((ph < 0.3f ? 1.0f : -0.4f) + nz() * 0.3f, 0.2f) * (0.85f + 0.15f * sinf(t * 2 * PI * 3));
        }
        store(S_PLANE, f, true, 0.6f);
    }
    {
        auto f = buf(1.0f);
        float ph = 0, ph2 = 0;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            ph += (210 + sinf(t * 2 * PI * 7) * 6) / SR; ph -= floorf(ph);
            ph2 += 423.0f / SR; ph2 -= floorf(ph2);
            f[i] = (ph * 2 - 1) * 0.6f + (ph2 * 2 - 1) * 0.3f;
        }
        OnePole lp;
        for (auto& v : f) v = lp.lp(v, 0.4f);
        store(S_DRONE, f, true, 0.5f);
    }
    {
        auto f = buf(1.6f);
        float ph = 0;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / f.size();
            ph += (1900 - 1300 * t) / SR;
            f[i] = sinf(2 * PI * ph) * (t < 0.1f ? t * 10 : 1) * (0.6f + 0.4f * t);
        }
        store(S_WHISTLE, f, false, 0.45f);
    }
    {
        auto f = buf(0.5f);
        BiquadBP bp; bp.set(900, 0.8f);
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = bp.run(nz()) * env(t, 0.05f, 0.15f) + (s_ar.f() < 0.01f ? nz() : 0); }
        store(S_IGNITE, f, false, 0.6f);
    }
    // voix
    genVoice(S_SCREAM1, 1.2f, 420, 560, 300, 800, 1250, 0.3f);
    genVoice(S_SCREAM2, 0.9f, 500, 650, 380, 650, 1100, 0.4f);
    genVoice(S_SCREAM3, 1.4f, 300, 480, 220, 520, 950, 0.35f);
    genVoice(S_SCREAM4, 1.0f, 380, 600, 260, 900, 1400, 0.5f);
    genVoice(S_GRUNT, 0.28f, 150, 170, 120, 500, 900, 0.4f);
    {
        // clameur de victoire : plusieurs voix superposées
        auto f = buf(2.2f);
        for (int v = 0; v < 9; v++) {
            BiquadBP a; a.set(700 + v * 30, 4);
            float ph = 0, p0 = 170 + s_ar.f() * 120, st = s_ar.f() * 0.3f;
            for (size_t i = 0; i < f.size(); i++) {
                float t = (float)i / SR - st;
                if (t < 0) continue;
                ph += p0 * (1 + sinf(t * 2 * PI * 5) * 0.02f) * (t < 0.4f ? 1 + t * 0.4f : 1.16f) / SR;
                ph -= floorf(ph);
                f[i] += a.run(ph * 2 - 1) * env(t, 0.08f, 0.7f);
            }
        }
        store(S_CHEER, f, false, 0.7f);
    }
    {
        // cor de guerre
        auto f = buf(2.8f);
        OnePole lp;
        float ph = 0;
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR;
            float pitch = 108 * (t < 0.3f ? 0.94f + t * 0.2f : 1.0f) * (1 + sinf(t * 2 * PI * 4.5f) * 0.006f);
            ph += pitch / SR; ph -= floorf(ph);
            float s = (ph * 2 - 1) + 0.4f * ((fmodf(ph * 2, 1.0f)) * 2 - 1);
            float e = t < 0.35f ? t / 0.35f : (t > 2.2f ? (2.8f - t) / 0.6f : 1);
            f[i] = lp.lp(s, 0.06f + e * 0.05f) * e;
        }
        store(S_HORN, f, false, 0.9f);
    }
    {
        auto f = buf(3.5f);
        float parts[5] = {1, 2.76f, 5.4f, 8.93f, 13.3f};
        for (size_t i = 0; i < f.size(); i++) {
            float t = (float)i / SR, s = 0;
            for (int p = 0; p < 5; p++) s += sinf(2 * PI * 196 * parts[p] * t) * expf(-t * (0.8f + p * 0.9f)) / (1 + p);
            f[i] = s;
        }
        store(S_BELL, f, false, 0.7f);
    }
    {
        auto f = buf(0.7f);
        for (int rep = 0; rep < 2; rep++) {
            float ph = 0;
            size_t off = (size_t)(rep * 0.3f * SR);
            BiquadBP a; a.set(1300, 3);
            for (size_t i = 0; i < (size_t)(0.22f * SR) && off + i < f.size(); i++) {
                float t = (float)i / SR;
                ph += (650 - t * 900) / SR; ph -= floorf(ph);
                f[off + i] += a.run((ph * 2 - 1) + nz() * 0.6f) * env(t, 0.01f, 0.08f);
            }
        }
        store(S_CROW, f, false, 0.5f);
    }
    {
        auto f = buf(0.6f);
        for (int k = 0; k < 4; k++) {
            float ph = 0;
            size_t off = (size_t)(k * 0.12f * SR);
            for (size_t i = 0; i < (size_t)(0.08f * SR) && off + i < f.size(); i++) {
                float t = (float)i / SR;
                ph += (3200 + sinf(t * 300) * 600) / SR;
                f[off + i] += sinf(2 * PI * ph) * sinf(t / 0.08f * PI);
            }
        }
        store(S_BIRDS, f, false, 0.4f);
    }
    {
        auto f = buf(0.12f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = lp.lp(nz(), 0.3f) * expf(-t * 40) + sinf(2 * PI * 90 * t) * expf(-t * 50) * 0.5f; }
        store(S_STEP, f, false, 0.5f);
    }
    {
        auto f = buf(0.3f);
        OnePole lp;
        for (size_t i = 0; i < f.size(); i++) { float t = (float)i / SR; f[i] = sinf(2 * PI * 60 * t) * expf(-t * 20) + lp.lp(nz(), 0.2f) * expf(-t * 25); }
        store(S_LAND, f, false, 0.7f);
    }
}

// ---------------------------------------------------------------- Musique procédurale
struct Pluck { std::vector<float> dl; int idx; float damp, amp; bool on; };
static Pluck s_pl[6];
static int s_plNext = 0;
static double s_mt = 0;
static float s_dronePh[3] = {0, 0, 0};
static OnePole s_droneLp;
static int s_beat = -1, s_mNote = 7;
static float s_drumEnv = 0, s_drumPh = 0, s_drumF = 60, s_snareEnv = 0, s_brassPh = 0, s_brassF = 73.4f, s_brassEnv = 0;
static OnePole s_brassLp, s_snareLp;
static Rng s_mr(2024);
static const float DORIAN[7] = {0, 2, 3, 5, 7, 9, 10};

static float noteHz(int deg, int oct) {
    int o = oct + deg / 7;
    int d = ((deg % 7) + 7) % 7;
    float semis = DORIAN[d] + 12 * (o - 4) + 2;  // ré
    return 261.63f * powf(2.0f, semis / 12.0f);
}

static void pluckNote(float hz, float amp) {
    Pluck& p = s_pl[s_plNext];
    s_plNext = (s_plNext + 1) % 6;
    int n = (int)(OUT_SR / hz);
    if (n < 2) n = 2;
    p.dl.assign(n, 0);
    for (int i = 0; i < n; i++) p.dl[i] = s_mr.f() * 2 - 1;
    p.idx = 0; p.damp = 0.996f; p.amp = amp; p.on = true;
}

static void musicStep(float* L, float* R, int n) {
    float inten = s_musicInt;
    float tempo = 80 + inten * 36;
    float beatLen = 60.0f / tempo / 2;  // croches
    float vol = s_musicPaused ? 0.35f : 1.0f;
    int era = s_musicEra;
    for (int i = 0; i < n; i++) {
        s_mt += 1.0 / OUT_SR;
        int beat = (int)(s_mt / beatLen);
        if (beat != s_beat) {
            s_beat = beat;
            int b8 = beat % 16;
            // mélodie (calme) ou ostinato (combat)
            if (s_mr.f() < (inten < 0.4f ? 0.45f : 0.3f) && (b8 % 2 == 0 || s_mr.f() < 0.3f)) {
                s_mNote += s_mr.irange(-2, 2);
                if (s_mNote < 2) s_mNote = 4;
                if (s_mNote > 14) s_mNote = 11;
                pluckNote(noteHz(s_mNote, 4), 0.16f * (1 - inten * 0.4f));
                if (s_mr.f() < 0.25f) pluckNote(noteHz(s_mNote - 2, 4), 0.09f);
            }
            if (inten > 0.25f) {
                if (b8 == 0 || b8 == 8 || (b8 == 6 && inten > 0.6f) || (b8 == 14 && inten > 0.5f)) { s_drumEnv = 1; s_drumF = 62; }
                if (b8 == 4 || b8 == 12) { s_drumEnv = 0.7f; s_drumF = 95; }
                if (era >= 3 && (b8 % 2 == 1) && s_mr.f() < inten) s_snareEnv = 0.5f;
                if (b8 % 4 == 0) {
                    static const int pat[4] = {0, 4, 0, -1};
                    s_brassF = noteHz(pat[(beat / 4) % 4], 2);
                    s_brassEnv = 1;
                }
            }
        }
        float s = 0;
        // bourdon
        float df[3] = {73.42f, 73.9f, 110.0f};
        float dr = 0;
        for (int k = 0; k < 3; k++) {
            s_dronePh[k] += df[k] / OUT_SR;
            s_dronePh[k] -= floorf(s_dronePh[k]);
            dr += s_dronePh[k] * 2 - 1;
        }
        s += s_droneLp.lp(dr, 0.01f) * 0.07f * (1 - inten * 0.5f);
        // cordes pincées
        for (auto& p : s_pl) {
            if (!p.on) continue;
            int nn = (int)p.dl.size();
            int nx = (p.idx + 1) % nn;
            float v = p.dl[p.idx];
            p.dl[p.idx] = (p.dl[p.idx] + p.dl[nx]) * 0.5f * p.damp;
            p.idx = nx;
            s += v * p.amp;
            p.amp *= 0.99998f;
            if (p.amp < 0.002f) p.on = false;
        }
        // tambours
        if (s_drumEnv > 0.001f) {
            s_drumPh += s_drumF * (1 + s_drumEnv * 0.8f) / OUT_SR;
            s += sinf(2 * PI * s_drumPh) * s_drumEnv * 0.45f * inten;
            s_drumEnv *= 0.99955f;
        }
        if (s_snareEnv > 0.001f) {
            float nn = s_mr.f() * 2 - 1;
            s += (nn - s_snareLp.lp(nn, 0.2f)) * s_snareEnv * 0.25f * inten;
            s_snareEnv *= 0.9993f;
        }
        // cuivres graves
        if (s_brassEnv > 0.001f && inten > 0.3f) {
            s_brassPh += s_brassF / OUT_SR;
            s_brassPh -= floorf(s_brassPh);
            float b = s_brassLp.lp(s_brassPh * 2 - 1, 0.03f + s_brassEnv * 0.03f);
            s += b * s_brassEnv * 0.22f * inten;
            s_brassEnv *= 0.99993f;
        }
        s *= vol * 0.8f;
        L[i] += s;
        R[i] += s;
    }
}

// ---------------------------------------------------------------- Mixage
static int16_t s_out[2][GRAIN * 2];
static float s_mixL[GRAIN], s_mixR[GRAIN];

static void mixVoice(Voice& v, int n) {
    Sample& sm = s_snd[v.id];
    int len = (int)sm.d.size();
    if (len == 0) { v.active = false; return; }
    for (int i = 0; i < n; i++) {
        int p = (int)v.pos;
        if (p >= len - 1) { v.active = false; return; }
        float fr = v.pos - p;
        float s = (sm.d[p] * (1 - fr) + sm.d[p + 1] * fr) / 32768.0f;
        s_mixL[i] += s * v.vl;
        s_mixR[i] += s * v.vr;
        v.pos += v.step;
    }
}

static int audioThread(SceSize, void*) {
    int cur = 0;
    for (;;) {
        for (int i = 0; i < GRAIN; i++) { s_mixL[i] = 0; s_mixR[i] = 0; }
        // nouvelles commandes
        while (s_qTail != s_qHead) {
            Cmd c = s_queue[s_qTail];
            s_qTail = (s_qTail + 1) % QN;
            int slot = -1;
            float quietest = 1e9f;
            for (int v = 0; v < NV; v++) {
                if (!s_voices[v].active) { slot = v; break; }
                float l = s_voices[v].vl + s_voices[v].vr;
                if (l < quietest) { quietest = l; slot = v; }
            }
            if (slot < 0) continue;
            Voice& v = s_voices[slot];
            v.id = c.id; v.pos = 0; v.step = c.step; v.vl = c.vl; v.vr = c.vr; v.active = true;
        }
        for (int v = 0; v < NV; v++) if (s_voices[v].active) mixVoice(s_voices[v], GRAIN);
        // boucles
        for (int l = 0; l < LS_COUNT; l++) {
            LoopV& lv = s_loops[l];
            int id = lv.id;
            if (id < 0 || id >= S_COUNT) continue;
            Sample& sm = s_snd[id];
            int len = (int)sm.d.size();
            if (len < 2) continue;
            float step = (float)SR / OUT_SR * lv.pitch;
            for (int i = 0; i < GRAIN; i++) {
                lv.vol += (lv.target - lv.vol) * 0.0005f;
                if (lv.vol < 0.001f) { lv.pos += step; continue; }
                int p = (int)lv.pos;
                if (p >= len - 1) { lv.pos -= len - 1; p = (int)lv.pos; }
                float s = sm.d[p] / 32768.0f;
                s_mixL[i] += s * lv.vol;
                s_mixR[i] += s * lv.vol;
                lv.pos += step;
            }
            if (lv.pos >= len - 1) lv.pos -= len - 1;
        }
        // musique
        s_musicInt += (s_musicTarget - s_musicInt) * 0.002f;
        musicStep(s_mixL, s_mixR, GRAIN);
        // compression douce et sortie
        for (int i = 0; i < GRAIN; i++) {
            float l = s_mixL[i] * 0.7f, r = s_mixR[i] * 0.7f;
            l = l / (1 + fabsf(l) * 0.6f);
            r = r / (1 + fabsf(r) * 0.6f);
            s_out[cur][i * 2] = (int16_t)clampf(l * 32767, -32767, 32767);
            s_out[cur][i * 2 + 1] = (int16_t)clampf(r * 32767, -32767, 32767);
        }
        sceAudioOutOutput(s_port, s_out[cur]);
        cur ^= 1;
    }
    return 0;
}

void audioInit() {
    genAll();
    for (int v = 0; v < NV; v++) s_voices[v].active = false;
    for (int l = 0; l < LS_COUNT; l++) { s_loops[l].id = -1; s_loops[l].target = 0; s_loops[l].vol = 0; s_loops[l].pos = 0; s_loops[l].pitch = 1; }
    s_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, OUT_SR, SCE_AUDIO_OUT_MODE_STEREO);
    if (s_port < 0) { logf("audio: port impossible (%x)", s_port); return; }
    int vols[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(s_port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vols);
    SceUID th = sceKernelCreateThread("audio", audioThread, 0x10000100 - 10, 0x20000, 0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    logf("audio ok");
}

void audioListener(const vec3& pos, float yaw) { s_lPos = pos; s_lYaw = yaw; }

static void push(int id, float vl, float vr, float pitch) {
    if (id < 0 || id >= S_COUNT) return;
    int nh = (s_qHead + 1) % QN;
    if (nh == s_qTail) return;
    Cmd& c = s_queue[s_qHead];
    c.id = id; c.vl = vl; c.vr = vr; c.step = (float)SR / OUT_SR * pitch;
    s_qHead = nh;
}

void sndPlay(int id, const vec3& pos, float vol, float pitch) {
    vec3 d = pos - s_lPos;
    float dist = length(d);
    float att = vol / (1 + dist * 0.08f + dist * dist * 0.0015f);
    if (att < 0.01f) return;
    // panoramique selon l'orientation de l'auditeur
    vec3 right(cosf(s_lYaw), 0, -sinf(s_lYaw));
    float pan = dist > 0.5f ? dot(d / dist, right) : 0;
    float vl = att * (0.6f - pan * 0.4f), vr = att * (0.6f + pan * 0.4f);
    // les sons lointains arrivent plus graves (légèrement)
    push(id, vl, vr, pitch * (dist > 60 ? 0.96f : 1.0f));
}

void sndPlay2D(int id, float vol, float pitch) { push(id, vol * 0.6f, vol * 0.6f, pitch); }

void sndLoop(int slot, int id, float vol, float pitch) {
    if (slot < 0 || slot >= LS_COUNT) return;
    s_loops[slot].id = id;
    s_loops[slot].target = vol;
    s_loops[slot].pitch = pitch;
}

void musicSet(float intensity, int era, bool paused) {
    s_musicTarget = intensity;
    s_musicEra = era;
    s_musicPaused = paused;
}

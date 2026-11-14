// Eternal Siege Vita - petite bibliothèque mathématique
#pragma once
#include <cmath>
#include <cstdint>

#ifndef PI
#define PI 3.14159265358979f
#endif

struct vec3 {
    float x, y, z;
    vec3() : x(0), y(0), z(0) {}
    vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    vec3 operator+(const vec3& o) const { return vec3(x + o.x, y + o.y, z + o.z); }
    vec3 operator-(const vec3& o) const { return vec3(x - o.x, y - o.y, z - o.z); }
    vec3 operator*(float s) const { return vec3(x * s, y * s, z * s); }
    vec3 operator/(float s) const { return vec3(x / s, y / s, z / s); }
    vec3 operator-() const { return vec3(-x, -y, -z); }
    vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) {
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float length(const vec3& a) { return sqrtf(dot(a, a)); }
inline float length2(const vec3& a) { return dot(a, a); }
inline vec3 normalize(const vec3& a) {
    float l = length(a);
    return l > 1e-6f ? a / l : vec3(0, 1, 0);
}
inline float dist2D(const vec3& a, const vec3& b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}
inline vec3 lerp(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
inline float smoothstepf(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
inline float wrapAngle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

// Matrice 4x4 colonne-major (format OpenGL)
struct Mat4 {
    float m[16];
    Mat4() { identity(); }
    void identity() {
        for (int i = 0; i < 16; i++) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1;
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    vec3 point(const vec3& p) const {
        return vec3(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                    m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                    m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
    }
    vec3 dir(const vec3& p) const {
        return vec3(m[0] * p.x + m[4] * p.y + m[8] * p.z,
                    m[1] * p.x + m[5] * p.y + m[9] * p.z,
                    m[2] * p.x + m[6] * p.y + m[10] * p.z);
    }
    static Mat4 translate(const vec3& t) {
        Mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static Mat4 scale(const vec3& s) {
        Mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }
    static Mat4 rotY(float a) {
        Mat4 r; float c = cosf(a), s = sinf(a);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }
    static Mat4 rotX(float a) {
        Mat4 r; float c = cosf(a), s = sinf(a);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }
    static Mat4 rotZ(float a) {
        Mat4 r; float c = cosf(a), s = sinf(a);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }
    static Mat4 rotAxis(vec3 ax, float a) {
        ax = normalize(ax);
        float c = cosf(a), s = sinf(a), t = 1 - c;
        Mat4 r;
        r.m[0] = t * ax.x * ax.x + c;        r.m[4] = t * ax.x * ax.y - s * ax.z; r.m[8] = t * ax.x * ax.z + s * ax.y;
        r.m[1] = t * ax.x * ax.y + s * ax.z; r.m[5] = t * ax.y * ax.y + c;        r.m[9] = t * ax.y * ax.z - s * ax.x;
        r.m[2] = t * ax.x * ax.z - s * ax.y; r.m[6] = t * ax.y * ax.z + s * ax.x; r.m[10] = t * ax.z * ax.z + c;
        return r;
    }
    // Base orthonormée : colonnes = axes X, Y, Z, position
    static Mat4 basis(const vec3& X, const vec3& Y, const vec3& Z, const vec3& P) {
        Mat4 r;
        r.m[0] = X.x; r.m[1] = X.y; r.m[2] = X.z;
        r.m[4] = Y.x; r.m[5] = Y.y; r.m[6] = Y.z;
        r.m[8] = Z.x; r.m[9] = Z.y; r.m[10] = Z.z;
        r.m[12] = P.x; r.m[13] = P.y; r.m[14] = P.z;
        return r;
    }
    static Mat4 perspective(float fovy, float aspect, float n, float f) {
        Mat4 r;
        float t = 1.0f / tanf(fovy * 0.5f);
        for (int i = 0; i < 16; i++) r.m[i] = 0;
        r.m[0] = t / aspect; r.m[5] = t;
        r.m[10] = (f + n) / (n - f); r.m[11] = -1;
        r.m[14] = 2 * f * n / (n - f);
        return r;
    }
    static Mat4 ortho(float l, float rr, float b, float t, float n, float f) {
        Mat4 r;
        r.m[0] = 2 / (rr - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
        r.m[12] = -(rr + l) / (rr - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
        return r;
    }
    // Vue FPS : yaw autour de Y, pitch autour de X
    static Mat4 viewFPS(const vec3& eye, float yaw, float pitch, float roll) {
        Mat4 r = rotZ(-roll) * rotX(-pitch) * rotY(-yaw) * translate(-eye);
        return r;
    }
};

// Orientation à partir d'une direction (axe Z local = dir)
inline Mat4 lookDir(const vec3& pos, vec3 fwd, vec3 up = vec3(0, 1, 0)) {
    fwd = normalize(fwd);
    if (fabsf(dot(fwd, up)) > 0.99f) up = vec3(1, 0, 0);
    vec3 X = normalize(cross(up, fwd));
    vec3 Y = cross(fwd, X);
    return Mat4::basis(X, Y, fwd, pos);
}

// Générateur aléatoire rapide
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 12345) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
    bool chance(float p) { return f() < p; }
    vec3 unit() {
        for (;;) {
            vec3 v(range(-1, 1), range(-1, 1), range(-1, 1));
            float l = length2(v);
            if (l > 0.01f && l <= 1) return v / sqrtf(l);
        }
    }
};
extern Rng g_rng;
inline float frand() { return g_rng.f(); }
inline float frange(float a, float b) { return g_rng.range(a, b); }

// Bruit de valeur 2D lissé
inline float hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFF) / 65535.0f;
}
inline float vnoise(float x, float y, uint32_t seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(ix, iy, seed), b = hash2(ix + 1, iy, seed);
    float c = hash2(ix, iy + 1, seed), d = hash2(ix + 1, iy + 1, seed);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
inline float fbm(float x, float y, int oct, uint32_t seed) {
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; i++) {
        s += vnoise(x, y, seed + i * 17) * a;
        n += a; a *= 0.5f; x *= 2.03f; y *= 2.03f;
    }
    return s / n;
}

struct Color { uint8_t r, g, b, a; };
inline Color rgba(int r, int g, int b, int a = 255) {
    Color c; c.r = (uint8_t)clampi(r, 0, 255); c.g = (uint8_t)clampi(g, 0, 255);
    c.b = (uint8_t)clampi(b, 0, 255); c.a = (uint8_t)clampi(a, 0, 255); return c;
}
inline Color rgbaf(float r, float g, float b, float a = 1) {
    return rgba((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
}
inline Color scaleC(Color c, float s) { return rgba((int)(c.r * s), (int)(c.g * s), (int)(c.b * s), c.a); }

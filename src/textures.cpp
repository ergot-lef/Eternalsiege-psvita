// Eternal Siege Vita - textures (photos CC0 converties à la compilation + procédurales)
#include "game.h"

// ---------------------------------------------------------------- Chargement .tex
// Format : "ETX1" + largeur(u16) + hauteur(u16) + RGB
static bool loadTex(const char* name, std::vector<uint8_t>& out, int& w, int& h) {
    char path[128];
    snprintf(path, sizeof(path), "app0:tex/%s.tex", name);
    FILE* f = fopen(path, "rb");
    if (!f) { logf("texture absente: %s", path); return false; }
    char magic[4];
    uint16_t dim[2];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "ETX1", 4) != 0 || fread(dim, 2, 2, f) != 2) { fclose(f); return false; }
    w = dim[0]; h = dim[1];
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) { fclose(f); return false; }
    std::vector<uint8_t> rgb(w * h * 3);
    size_t rd = fread(rgb.data(), 1, rgb.size(), f);
    fclose(f);
    if (rd != rgb.size()) return false;
    out.resize(w * h * 4);
    for (int i = 0; i < w * h; i++) {
        out[i * 4] = rgb[i * 3]; out[i * 4 + 1] = rgb[i * 3 + 1]; out[i * 4 + 2] = rgb[i * 3 + 2]; out[i * 4 + 3] = 255;
    }
    return true;
}

// ---------------------------------------------------------------- Générateurs de secours
typedef void (*GenFn)(int x, int y, int w, uint8_t* px);

static std::vector<uint8_t> genTex(int w, int h, GenFn fn) {
    std::vector<uint8_t> d(w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) fn(x, y, w, &d[(y * w + x) * 4]);
    return d;
}

static float tnoise(float x, float y, float per, int oct, uint32_t seed) {
    // bruit raccordable : on moyenne sur la période
    float s = 0, a = 0.5f, n = 0, f = 1;
    for (int i = 0; i < oct; i++) {
        float p = per * f;
        float fx = x * f, fy = y * f;
        // interpolation périodique
        int ix = (int)floorf(fx), iy = (int)floorf(fy);
        float tx = fx - ix, ty = fy - iy;
        tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
        int P = (int)p;
        auto H = [&](int a2, int b2) { return hash2(((a2 % P) + P) % P, ((b2 % P) + P) % P, seed + i * 31); };
        float v = lerpf(lerpf(H(ix, iy), H(ix + 1, iy), tx), lerpf(H(ix, iy + 1), H(ix + 1, iy + 1), tx), ty);
        s += v * a; n += a; a *= 0.5f; f *= 2;
    }
    return s / n;
}

static void setpx(uint8_t* p, float r, float g, float b, float a = 1) {
    p[0] = (uint8_t)clampf(r * 255, 0, 255); p[1] = (uint8_t)clampf(g * 255, 0, 255);
    p[2] = (uint8_t)clampf(b * 255, 0, 255); p[3] = (uint8_t)clampf(a * 255, 0, 255);
}

static void gStone(int x, int y, int w, uint8_t* p) {
    float u = x / (float)w * 4, v = y / (float)w * 4;
    int row = (int)floorf(v * 2);
    float uu = u * 1.5f + (row & 1) * 0.75f;
    float bx = uu - floorf(uu), by = v * 2 - floorf(v * 2);
    float mortar = (bx < 0.05f || by < 0.08f) ? 0.55f : 1.0f;
    float n = tnoise(x / 16.0f, y / 16.0f, w / 16.0f, 4, 7);
    float c = (0.45f + n * 0.35f) * mortar + hash2((int)uu, row, 3) * 0.08f;
    setpx(p, c * 0.95f, c * 0.92f, c * 0.85f);
}
static void gGrass(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 8.0f, y / 8.0f, w / 8.0f, 5, 11);
    float b = hash2(x, y, 5) * 0.15f;
    setpx(p, 0.18f + n * 0.15f + b * 0.3f, 0.32f + n * 0.25f + b, 0.10f + n * 0.08f);
}
static void gDirt(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 8.0f, y / 8.0f, w / 8.0f, 5, 13);
    float s = hash2(x, y, 9) * 0.1f;
    setpx(p, 0.36f + n * 0.2f + s, 0.27f + n * 0.15f + s, 0.18f + n * 0.1f + s);
}
static void gRock(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 32.0f, y / 32.0f, w / 32.0f, 6, 17);
    float c = 0.35f + n * 0.4f;
    setpx(p, c, c * 0.97f, c * 0.92f);
}
static void gBark(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 4.0f, y / 32.0f, w / 4.0f, 4, 19);
    float g = fabsf(sinf(x * 0.3f + n * 6));
    float c = 0.18f + n * 0.2f + g * 0.08f;
    setpx(p, c * 1.1f, c * 0.85f, c * 0.6f);
}
static void gWood(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 64.0f, y / 4.0f, w / 64.0f, 4, 23);
    float ring = sinf(y * 0.25f + n * 10) * 0.5f + 0.5f;
    float c = 0.35f + ring * 0.12f + n * 0.1f;
    setpx(p, c * 1.15f, c * 0.82f, c * 0.5f);
}
static void gPlanks(int x, int y, int w, uint8_t* p) {
    int plank = y / (w / 8);
    float n = tnoise(x / 64.0f, y / 2.0f, w / 64.0f, 4, 29 + plank);
    float edge = ((y % (w / 8)) < 2) ? 0.5f : 1.0f;
    float c = (0.38f + n * 0.18f + hash2(plank, 0, 2) * 0.1f) * edge;
    setpx(p, c * 1.1f, c * 0.8f, c * 0.52f);
}
static void gMetal(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 16.0f, y / 16.0f, w / 16.0f, 5, 31);
    float s = hash2(x, y / 3, 4) * 0.08f;
    float c = 0.45f + n * 0.2f + s;
    setpx(p, c, c * 1.02f, c * 1.05f);
}
static void gCloth(int x, int y, int w, uint8_t* p) {
    float weave = ((x + y) & 1) ? 0.92f : 1.0f;
    float n = tnoise(x / 16.0f, y / 16.0f, w / 16.0f, 3, 37);
    float c = (0.7f + n * 0.25f) * weave;
    setpx(p, c, c, c);
}
static void gLeather(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 4.0f, y / 4.0f, w / 4.0f, 5, 41);
    float c = 0.3f + n * 0.25f;
    setpx(p, c * 1.2f, c * 0.85f, c * 0.55f);
}
static void gSkin(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 8.0f, y / 8.0f, w / 8.0f, 3, 43);
    float c = 0.85f + n * 0.12f;
    setpx(p, c * 0.92f, c * 0.72f, c * 0.6f);
}
static void gStraw(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 2.0f, y / 32.0f, w / 2.0f, 4, 47);
    float c = 0.6f + n * 0.35f;
    setpx(p, c * 0.95f, c * 0.8f, c * 0.4f);
}

// --------- procédurales pures (alpha)
static void gLeaves(int x, int y, int w, uint8_t* p) {
    // amas de feuilles : petites ellipses aléatoires
    float a = 0, shade = 0;
    float cx = x / (float)w - 0.5f, cy = y / (float)w - 0.5f;
    float rad = sqrtf(cx * cx + cy * cy);
    for (int i = 0; i < 70; i++) {
        float lx = hash2(i, 1, 51), ly = hash2(i, 2, 51);
        float dx = lx - 0.5f, dy = ly - 0.5f;
        if (dx * dx + dy * dy > 0.2f) continue;
        float ang = hash2(i, 3, 51) * 6.28f;
        float px = x / (float)w - lx, py = y / (float)w - ly;
        float rx = px * cosf(ang) + py * sinf(ang), ry = -px * sinf(ang) + py * cosf(ang);
        float e = (rx * rx) / 0.0036f + (ry * ry) / 0.0008f;
        if (e < 1) { a = 1; shade = 0.6f + hash2(i, 4, 51) * 0.4f + (1 - e) * 0.15f; }
    }
    if (rad > 0.48f) a = 0;
    setpx(p, 0.20f * shade, 0.36f * shade, 0.12f * shade, a);
}
static void gGrassBlade(int x, int y, int w, uint8_t* p) {
    float a = 0, sh = 0;
    float fy = 1 - y / (float)w;
    for (int i = 0; i < 18; i++) {
        float bx = 0.1f + hash2(i, 0, 61) * 0.8f;
        float h = 0.5f + hash2(i, 1, 61) * 0.5f;
        float lean = (hash2(i, 2, 61) - 0.5f) * 0.4f;
        float px = bx + lean * fy * fy;
        float wdt = 0.025f * (1 - fy / h);
        if (fy < h && fabsf(x / (float)w - px) < wdt) { a = 1; sh = 0.5f + fy * 0.6f; }
    }
    setpx(p, 0.25f * sh, 0.45f * sh, 0.12f * sh, a);
}
static void gWater(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 16.0f, y / 16.0f, w / 16.0f, 4, 71);
    float c = 0.5f + n * 0.5f;
    setpx(p, 0.25f * c, 0.38f * c, 0.42f * c, 1);
}
static void gClouds(int x, int y, int w, uint8_t* p) {
    float n = tnoise(x / 32.0f, y / 32.0f, w / 32.0f, 6, 81);
    float a = smoothstepf(0.42f, 0.75f, n);
    setpx(p, 1, 1, 1, a);
}
static void gSoft(int x, int y, int w, uint8_t* p) {
    float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / w - 0.5f;
    float d = sqrtf(dx * dx + dy * dy) * 2;
    float a = clampf(1 - d, 0, 1);
    setpx(p, 1, 1, 1, a * a);
}
static void gSmoke(int x, int y, int w, uint8_t* p) {
    float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / w - 0.5f;
    float d = sqrtf(dx * dx + dy * dy) * 2;
    float n = tnoise(x / 8.0f, y / 8.0f, w / 8.0f, 4, 91);
    float a = clampf(1 - d, 0, 1) * (0.4f + n * 0.8f);
    setpx(p, 1, 1, 1, clampf(a, 0, 1));
}
static void gFire(int x, int y, int w, uint8_t* p) {
    float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / w - 0.5f;
    float d = sqrtf(dx * dx * 1.6f + dy * dy) * 2;
    float n = tnoise(x / 8.0f, y / 8.0f, w / 8.0f, 4, 97);
    float a = clampf((1 - d) * (0.6f + n * 0.7f), 0, 1);
    setpx(p, 1, 0.55f + a * 0.4f, 0.2f + a * 0.3f, a);
}
static void gBlood(int x, int y, int w, uint8_t* p) {
    float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / w - 0.5f;
    float ang = atan2f(dy, dx);
    float d = sqrtf(dx * dx + dy * dy) * 2;
    float edge = 0.55f + 0.25f * sinf(ang * 5 + 1) * hash2((int)(ang * 3 + 10), 0, 5) + tnoise(x / 8.0f, y / 8.0f, w / 8.0f, 3, 101) * 0.3f;
    float a = d < edge ? 1 : 0;
    // gouttes satellites
    for (int i = 0; i < 9; i++) {
        float sx = hash2(i, 7, 103) - 0.5f, sy = hash2(i, 8, 103) - 0.5f;
        float r = 0.02f + hash2(i, 9, 103) * 0.04f;
        if ((dx - sx * 0.9f) * (dx - sx * 0.9f) + (dy - sy * 0.9f) * (dy - sy * 0.9f) < r * r) a = 1;
    }
    float shade = 0.7f + tnoise(x / 16.0f, y / 16.0f, w / 16.0f, 3, 107) * 0.3f;
    setpx(p, 0.45f * shade, 0.02f, 0.02f, a * 0.92f);
}
static void gPool(int x, int y, int w, uint8_t* p) {
    float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / w - 0.5f;
    float d = sqrtf(dx * dx + dy * dy) * 2;
    float n = tnoise(x / 16.0f, y / 16.0f, w / 16.0f, 3, 109);
    float a = smoothstepf(0.95f, 0.75f, d + n * 0.25f);
    setpx(p, 0.32f, 0.01f, 0.01f, a * 0.95f);
}
static void gScorch(int x, int y, int w, uint8_t* p) {
    float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / w - 0.5f;
    float d = sqrtf(dx * dx + dy * dy) * 2;
    float n = tnoise(x / 16.0f, y / 16.0f, w / 16.0f, 4, 113);
    float a = clampf((1 - d) * 1.6f * (0.6f + n * 0.6f), 0, 1);
    setpx(p, 0.05f, 0.04f, 0.03f, a * 0.9f);
}
static void gWhite(int, int, int, uint8_t* p) { setpx(p, 1, 1, 1, 1); }
static void gSpark(int x, int y, int w, uint8_t* p) {
    float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / w - 0.5f;
    float d = sqrtf(dx * dx + dy * dy) * 2;
    float a = clampf(1 - d, 0, 1);
    a = a * a * a;
    setpx(p, 1, 0.9f, 0.6f, a);
}

// ---------------------------------------------------------------- Police 5x7 (dessin maison)
struct Glyph { uint32_t cp; const char* rows[7]; };
static const Glyph s_glyphs[] = {
    {'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
    {'C', {" ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####"}},
    {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
    {'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}},
    {'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
    {'G', {" ####", "#    ", "#    ", "#  ##", "#   #", "#   #", " ### "}},
    {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'I', {" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'J', {"  ###", "    #", "    #", "    #", "    #", "#   #", " ### "}},
    {'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}},
    {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
    {'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}},
    {'N', {"#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"}},
    {'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
    {'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}},
    {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
    {'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
    {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'V', {"#   #", "#   #", "#   #", "#   #", " # # ", " # # ", "  #  "}},
    {'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"}},
    {'X', {"#   #", " # # ", "  #  ", "  #  ", "  #  ", " # # ", "#   #"}},
    {'Y', {"#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
    {'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
    {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
    {'3', {"#### ", "    #", "    #", " ### ", "    #", "    #", "#### "}},
    {'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}},
    {'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
    {'6', {" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "}},
    {'7', {"#####", "    #", "   # ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
    {'9', {" ### ", "#   #", "#   #", " ####", "    #", "    #", " ### "}},
    {'.', {"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "}},
    {',', {"     ", "     ", "     ", "     ", " ##  ", "  #  ", " #   "}},
    {':', {"     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "}},
    {';', {"     ", " ##  ", " ##  ", "     ", " ##  ", "  #  ", " #   "}},
    {'!', {"  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "}},
    {'?', {" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "}},
    {'-', {"     ", "     ", "     ", "#####", "     ", "     ", "     "}},
    {'+', {"     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "}},
    {'/', {"    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "}},
    {'%', {"##  #", "##  #", "   # ", "  #  ", " #   ", "#  ##", "#  ##"}},
    {'\'', {"  #  ", "  #  ", " #   ", "     ", "     ", "     ", "     "}},
    {'"', {" # # ", " # # ", "     ", "     ", "     ", "     ", "     "}},
    {'(', {"   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "}},
    {')', {" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "}},
    {'<', {"   # ", "  #  ", " #   ", "#    ", " #   ", "  #  ", "   # "}},
    {'>', {" #   ", "  #  ", "   # ", "    #", "   # ", "  #  ", " #   "}},
    {'=', {"     ", "     ", "#####", "     ", "#####", "     ", "     "}},
    {'*', {"     ", "# # #", " ### ", "#####", " ### ", "# # #", "     "}},
    {'[', {" ### ", " #   ", " #   ", " #   ", " #   ", " #   ", " ### "}},
    {']', {" ### ", "   # ", "   # ", "   # ", "   # ", "   # ", " ### "}},
    {'_', {"     ", "     ", "     ", "     ", "     ", "     ", "#####"}},
    {0x25CB, {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}}, // ○
    {0x25A1, {"#####", "#   #", "#   #", "#   #", "#   #", "#   #", "#####"}}, // □
    {0x25B3, {"  #  ", "  #  ", " # # ", " # # ", "#   #", "#   #", "#####"}}, // △
    {0x2715, {"#   #", " # # ", "  #  ", " # # ", "#   #", "     ", "     "}}, // ✕
};
static const int NGLYPH = sizeof(s_glyphs) / sizeof(s_glyphs[0]);

// accents : base + marque
struct Accent { uint32_t cp; uint32_t base; int mark; }; // 1 aigu 2 grave 3 circonflexe 4 cédille 5 tréma
static const Accent s_accents[] = {
    {0xC9, 'E', 1}, {0xE9, 'E', 1}, {0xC8, 'E', 2}, {0xE8, 'E', 2}, {0xCA, 'E', 3}, {0xEA, 'E', 3},
    {0xCB, 'E', 5}, {0xEB, 'E', 5}, {0xC0, 'A', 2}, {0xE0, 'A', 2}, {0xC2, 'A', 3}, {0xE2, 'A', 3},
    {0xC7, 'C', 4}, {0xE7, 'C', 4}, {0xD4, 'O', 3}, {0xF4, 'O', 3}, {0xDB, 'U', 3}, {0xFB, 'U', 3},
    {0xD9, 'U', 2}, {0xF9, 'U', 2}, {0xCE, 'I', 3}, {0xEE, 'I', 3}, {0xCF, 'I', 5}, {0xEF, 'I', 5},
    {0x152, 'O', 0}, {0x153, 'O', 0},
};
static const int NACC = sizeof(s_accents) / sizeof(s_accents[0]);
// index de glyphe : 1..NGLYPH = glyphes de base, NGLYPH+1.. = accentués
int fontGlyph(uint32_t cp) {
    if (cp == ' ') return 0;
    if (cp >= 'a' && cp <= 'z') cp -= 32;
    for (int i = 0; i < NGLYPH; i++) if (s_glyphs[i].cp == cp) return i + 1;
    for (int i = 0; i < NACC; i++) if (s_accents[i].cp == cp) return NGLYPH + 1 + i;
    return fontGlyph('?');
}

static std::vector<uint8_t> buildFont() {
    std::vector<uint8_t> d(128 * 128 * 4, 0);
    auto put = [&](int g, const Glyph& gl, int mark) {
        int ox = (g % 16) * 8 + 1, oy = (g / 16) * 12 + 3;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (gl.rows[r][c] == '#') {
                    uint8_t* p = &d[((oy + r) * 128 + ox + c) * 4];
                    p[0] = p[1] = p[2] = p[3] = 255;
                }
        auto dot = [&](int x, int y) {
            if (x < 0 || y < 0 || x > 7 || y > 11) return;
            uint8_t* p = &d[(((g / 16) * 12 + y) * 128 + (g % 16) * 8 + x) * 4];
            p[0] = p[1] = p[2] = p[3] = 255;
        };
        int bx = (g % 16) * 8, by = (g / 16) * 12; (void)bx; (void)by;
        switch (mark) {
        case 1: dot(3, 1); dot(4, 0); break;
        case 2: dot(2, 0); dot(3, 1); break;
        case 3: dot(2, 1); dot(3, 0); dot(4, 1); break;
        case 4: dot(3, 10); dot(2, 11); break;
        case 5: dot(2, 1); dot(4, 1); break;
        }
    };
    for (int i = 0; i < NGLYPH; i++) put(i + 1, s_glyphs[i], 0);
    for (int i = 0; i < NACC; i++) {
        int bi = fontGlyph(s_accents[i].base) - 1;
        put(NGLYPH + 1 + i, s_glyphs[bi], s_accents[i].mark);
    }
    return d;
}

// ---------------------------------------------------------------- Init
static void loadOrGen(int id, const char* name, GenFn fn, int size, Color tint) {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!loadTex(name, px, w, h)) {
        px = genTex(size, size, fn);
        w = h = size;
    } else if (tint.r != 255 || tint.g != 255 || tint.b != 255) {
        for (int i = 0; i < w * h; i++) {
            px[i * 4] = px[i * 4] * tint.r / 255;
            px[i * 4 + 1] = px[i * 4 + 1] * tint.g / 255;
            px[i * 4 + 2] = px[i * 4 + 2] * tint.b / 255;
        }
    }
    g_tex[id] = texUpload(w, h, px.data(), true, true);
}

void texturesInit() {
    Color none = rgba(255, 255, 255);
    loadOrGen(TEX_STONE, "stone", gStone, 256, none);
    loadOrGen(TEX_GRASS, "grass", gGrass, 256, none);
    loadOrGen(TEX_DIRT, "dirt", gDirt, 256, none);
    loadOrGen(TEX_ROCK, "rock", gRock, 256, none);
    loadOrGen(TEX_BARK, "bark", gBark, 256, none);
    loadOrGen(TEX_WOOD, "wood", gWood, 256, none);
    loadOrGen(TEX_PLANKS, "planks", gPlanks, 256, none);
    loadOrGen(TEX_METAL, "metal", gMetal, 256, none);
    loadOrGen(TEX_CLOTH, "cloth", gCloth, 128, none);
    loadOrGen(TEX_LEATHER, "leather", gLeather, 128, none);
    loadOrGen(TEX_SKIN, "skin", gSkin, 64, none);
    loadOrGen(TEX_STRAW, "straw", gStraw, 128, none);
    struct P { int id; GenFn fn; int size; bool mip; bool rep; };
    P procs[] = {
        {TEX_LEAVES, gLeaves, 128, true, false}, {TEX_GRASSBLADE, gGrassBlade, 64, true, false},
        {TEX_WATER, gWater, 128, true, true}, {TEX_CLOUDS, gClouds, 256, true, true},
        {TEX_SOFT, gSoft, 32, false, false}, {TEX_SMOKE, gSmoke, 64, false, false},
        {TEX_FIRE, gFire, 64, false, false}, {TEX_BLOOD, gBlood, 64, false, false},
        {TEX_SCORCH, gScorch, 64, false, false}, {TEX_WHITE, gWhite, 4, false, true},
        {TEX_SPARK, gSpark, 16, false, false}, {TEX_POOL, gPool, 64, false, false},
    };
    for (auto& p : procs) {
        std::vector<uint8_t> d = genTex(p.size, p.size, p.fn);
        g_tex[p.id] = texUpload(p.size, p.size, d.data(), p.mip, p.rep);
    }
    std::vector<uint8_t> f = buildFont();
    g_tex[TEX_FONT] = texUpload(128, 128, f.data(), false, false);
    logf("textures ok");
}

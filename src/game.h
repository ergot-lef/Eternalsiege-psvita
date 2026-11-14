// Eternal Siege Vita - déclarations communes
#pragma once
#include "mathx.h"
#include <vector>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------- Plateforme
void logf(const char* fmt, ...);
double timeNow();

enum Btn {
    B_CROSS = 1, B_CIRCLE = 2, B_SQUARE = 4, B_TRIANGLE = 8,
    B_L = 16, B_R = 32, B_START = 64, B_SELECT = 128,
    B_UP = 256, B_DOWN = 512, B_LEFT = 1024, B_RIGHT = 2048
};
struct Input {
    float lx, ly, rx, ry;
    uint32_t held, pressed, released;
};
extern Input g_in;
void platformInit();
void inputUpdate();
const char* dataDir();

// ---------------------------------------------------------------- Rendu
enum TexId {
    TEX_STONE, TEX_GRASS, TEX_DIRT, TEX_ROCK, TEX_BARK, TEX_WOOD, TEX_PLANKS,
    TEX_METAL, TEX_CLOTH, TEX_LEATHER, TEX_SKIN, TEX_STRAW,
    // procédurales
    TEX_LEAVES, TEX_GRASSBLADE, TEX_WATER, TEX_CLOUDS, TEX_SOFT, TEX_SMOKE,
    TEX_FIRE, TEX_BLOOD, TEX_SCORCH, TEX_WHITE, TEX_FONT, TEX_SPARK, TEX_POOL,
    TEX_COUNT
};
extern unsigned g_tex[TEX_COUNT];

struct Vtx {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
    uint8_t r, g, b, a;
};

enum BlendMode { BM_OPAQUE, BM_ALPHATEST, BM_ALPHA, BM_ADD };

struct Batch {
    std::vector<Vtx> v;
    int tex;
    int blend;
    bool lit;
    void init(int t, int b, bool l) { tex = t; blend = b; lit = l; v.reserve(4096); }
    void clear() { v.clear(); }
};

// Lots dynamiques de géométrie (reconstruits chaque image)
enum BatchId {
    BA_STONE, BA_ROCK, BA_BARK, BA_WOOD, BA_PLANKS, BA_METAL, BA_CLOTH, BA_LEATHER,
    BA_SKIN, BA_STRAW, BA_WHITE, BA_LEAVES, BA_GRASS,
    BA_COUNT
};
extern Batch g_batches[BA_COUNT];

void gfxInit();
void gfxBeginFrame();
void gfxEndFrame();
void texturesInit();
unsigned texUpload(int w, int h, const uint8_t* rgba, bool mip, bool repeat);
void batchFlush(Batch& b);
void batchesFlushOpaque();
void batchesFlushAlpha();

void emitTri(Batch& b, const Vtx& a, const Vtx& c, const Vtx& d);
void emitQuad(Batch& b, const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3,
              const vec3& n, Color col, float u0, float v0, float u1, float v1);
void emitBox(Batch& b, const Mat4& m, const vec3& half, Color col, float uvScale = 1.0f);
void emitCylinder(Batch& b, const Mat4& m, float radius, float h, int seg, Color col, bool caps, float uScale = 1.0f, float vScale = 1.0f);
void emitCone(Batch& b, const Mat4& m, float radius, float h, int seg, Color col);
void emitBillboard(Batch& b, const vec3& p, float size, float rot, Color col, const vec3& right, const vec3& up);

// Mesh statique sur GPU
struct StaticMesh {
    unsigned vbo;
    int count;
    std::vector<Vtx> cpu;
    StaticMesh() : vbo(0), count(0) {}
    void upload();
    void draw(int tex, int blend, bool lit);
    void release();
};

struct Camera {
    vec3 pos;
    float yaw, pitch, roll, fov;
    Mat4 view, proj;
    vec3 fwd, right, up;
};
extern Camera g_cam;
void cameraApply();
bool sphereVisible(const vec3& c, float r);

// Lumières dynamiques (feux, explosions)
void lightsReset();
void lightAdd(const vec3& p, float r, float g, float b, float radius, float prio);
void lightsApply();

// Texte et 2D
void begin2D();
void end2D();
void draw2DRect(float x, float y, float w, float h, Color c);
void drawText(float x, float y, const char* s, float scale, Color c, int align = 0);
float textWidth(const char* s, float scale);

// ---------------------------------------------------------------- Audio
enum SoundId {
    S_BOW, S_WHOOSH, S_HIT_FLESH, S_HIT_WOOD, S_HIT_STONE, S_SWORD_SWING, S_SWORD_HIT,
    S_CLANG, S_EXPLO_BIG, S_EXPLO_SMALL, S_CANNON, S_MUSKET, S_RIFLE, S_MG, S_WOOD_BREAK,
    S_STONE_CRACK, S_COLLAPSE, S_SPLASH, S_FIRE, S_RAIN, S_WIND, S_THUNDER,
    S_SCREAM1, S_SCREAM2, S_SCREAM3, S_SCREAM4, S_GRUNT, S_HORN, S_BELL, S_CROW,
    S_STEP, S_LAND, S_ENGINE, S_PLANE, S_DRONE, S_WHISTLE, S_CROSSBOW, S_RELOAD,
    S_THROW, S_IGNITE, S_TREE_FALL, S_CHEER, S_BIRDS, S_RAM, S_ROCKET,
    S_COUNT
};
void audioInit();
void sndPlay(int id, const vec3& pos, float vol = 1.0f, float pitch = 1.0f);
void sndPlay2D(int id, float vol = 1.0f, float pitch = 1.0f);
void sndLoop(int slot, int id, float vol, float pitch); // boucles d'ambiance
void audioListener(const vec3& pos, float yaw);
void musicSet(float intensity, int era, bool paused);
enum LoopSlot { LS_RAIN, LS_WIND, LS_FIRE, LS_ENGINE, LS_PLANE, LS_DRONE, LS_COUNT };

// ---------------------------------------------------------------- Terrain
const int TER_N = 129;
const float TER_SIZE = 256.0f;
const float TER_CELL = 2.0f;
const float TER_HALF = 128.0f;
const float WATER_Y = 1.2f;
const float RIVER_Z0 = 60.0f, RIVER_Z1 = 74.0f;
extern float g_height[TER_N * TER_N];
extern float g_heightBase[TER_N * TER_N];
void terrainGen();
void terrainBuildMesh();
void terrainUpdate(float dt);
void terrainDraw();
void waterDraw(float t);
float terrainHeight(float x, float z);
vec3 terrainNormal(float x, float z);
void terrainDeform(const vec3& p, float radius, float depth);
bool inRiver(const vec3& p);
void grassDraw(float t, float wind);

// ---------------------------------------------------------------- Château (voxels)
const int VX = 80, VY = 28, VZ = 80;
const float VOX0X = -40.0f, VOX0Z = -40.0f, VOX0Y = 9.0f;
enum Mat { M_AIR = 0, M_STONE, M_WOOD, M_RUBBLE, M_KEEP };
extern uint8_t g_vox[VX * VY * VZ];
extern uint8_t g_voxHp[VX * VY * VZ];
inline int vidx(int i, int j, int k) { return (j * VZ + k) * VX + i; }
inline bool vinside(int i, int j, int k) { return i >= 0 && j >= 0 && k >= 0 && i < VX && j < VY && k < VZ; }
uint8_t voxAt(int i, int j, int k);
uint8_t voxAtWorld(const vec3& p);
bool worldToVox(const vec3& p, int& i, int& j, int& k);
vec3 voxCenter(int i, int j, int k);
void castleGen();
void castleUpdate(float dt);
void castleDraw();
void castleDamageSphere(const vec3& p, float radius, float dmg, bool explosive);
bool castleDamageVoxel(int i, int j, int k, float dmg, const vec3& from);
bool castleRaycast(const vec3& o, const vec3& d, float maxDist, float& tHit, int& hi, int& hj, int& hk, vec3& normal);
bool castleRepairTarget(const vec3& o, const vec3& d, int& ri, int& rj, int& rk);
void castlePlace(int i, int j, int k, uint8_t mat);
void castleRepairCell(int i, int j, int k);
float castleIntegrity();
float keepIntegrity();
float surfaceHeight(float x, float z, float yRef); // sol + château
bool solidAt(const vec3& p);
void castleMarkDirty(int i, int k);
void castleIgnite(int i, int j, int k);
void castleSaveExtra(FILE* f);
void castleLoadExtra(FILE* f);

// ---------------------------------------------------------------- Arbres, herbe, oiseaux
struct Tree {
    vec3 pos;
    float height, radius, hp, burn, charred;
    float fallAng, fallVel, fallYaw;
    uint8_t state; // 0 debout, 1 tombe, 2 couché, 3 disparu
    uint8_t kind;  // 0 chêne, 1 sapin
    float phase;
};
extern std::vector<Tree> g_trees;
void treesGen();
void treesUpdate(float dt, float wind);
void treesDraw(float t, float wind);
void treeDamage(int idx, float dmg, const vec3& from);
int treeAtRay(const vec3& o, const vec3& d, float maxDist, float& t);

struct Bird { vec3 pos, vel; float flap, scared; };
void birdsInit();
void birdsUpdate(float dt);
void birdsDraw();
void birdsScare(const vec3& p);

// ---------------------------------------------------------------- Ciel, météo
struct Weather {
    float timeOfDay;  // 0..1 (0.5 = midi)
    float rain, rainTarget;
    float fog, fogTarget;
    float cloud, cloudTarget;
    float wind, windTarget;
    float windDir;
    float lightning, nextChange, nextThunder;
    float sunR, sunG, sunB, ambR, ambG, ambB;
    float skyR, skyG, skyB, fogR, fogG, fogB;
    float daylight;
};
extern Weather g_weather;
void weatherInit();
void weatherUpdate(float dt, float timeScale);
void skyDraw(float t);
void rainUpdateDraw(float dt);
void applySceneLighting();

// ---------------------------------------------------------------- Effets
enum PType { PT_SMOKE, PT_FIRE, PT_SPARK, PT_BLOOD, PT_DUST, PT_SPLASH, PT_DEBRIS, PT_FLASH, PT_EMBER, PT_TRACER };
struct Particle {
    vec3 pos, vel;
    float life, maxLife, size, grow, rot, rotV, gravity, drag;
    Color col;
    uint8_t type;
};
void fxInit();
void fxUpdate(float dt);
void fxDraw();
void spawnParticle(int type, const vec3& p, const vec3& v, float size, float life, Color c);
void fxBlood(const vec3& p, const vec3& dir, int amount);
void fxDust(const vec3& p, float radius, int amount, Color c);
void fxSparks(const vec3& p, const vec3& n, int amount);
void fxSplash(const vec3& p, float strength);
void fxFire(const vec3& p, float radius, float intensity);
void fxSmoke(const vec3& p, float radius, float amount, float dark);
void fxTracer(const vec3& a, const vec3& b);
void explosion(const vec3& p, float power, int ownerTeam);

enum DecalType { DC_BLOOD, DC_POOL, DC_SCORCH, DC_CRATER, DC_MUD };
struct Decal { float x, z, size, rot, alpha, growTo; uint8_t type; };
extern std::vector<Decal> g_decals;
void decalAdd(int type, float x, float z, float size, float growTo = 0);
void decalsDraw();
void decalsUpdate(float dt);

void shadowBlob(const vec3& p, float radius, float strength);
void shadowsDraw();

// ---------------------------------------------------------------- Objets physiques
enum BodyKind {
    BK_DEBRIS, BK_BLOCK, BK_PLANK, BK_BARREL, BK_POWDER, BK_CRATE, BK_HAY,
    BK_BOULDER, BK_LOG, BK_LADDER, BK_LIMB, BK_CART, BK_WRECK, BK_HELMET, BK_SANDBAG
};
struct Body {
    bool alive, sleeping, held;
    uint8_t kind;
    vec3 pos, vel, angVel;
    Mat4 rot;
    vec3 half;
    float mass, hp, burn, life, sleepT, wet;
    Color col;
    int batch;
    int ladderClimbers;
    float charred;
    float bleed;
};
extern std::vector<Body> g_bodies;
int bodySpawn(int kind, const vec3& p, const vec3& half, Color col, int batch, float mass);
void bodiesInit();
void bodiesUpdate(float dt);
void bodiesDraw();
void bodyDamage(int idx, float dmg, const vec3& from, bool fire);
void bodiesImpulseSphere(const vec3& p, float radius, float force);
int bodyAtRay(const vec3& o, const vec3& d, float maxDist, float& t);
void propsGen();
void bodyWake(Body& b);

// ---------------------------------------------------------------- Personnages
enum Team { TEAM_ENEMY = 0, TEAM_ALLY = 1, TEAM_PLAYER = 2 };
enum CharType {
    CT_SWORD, CT_SPEAR, CT_AXE, CT_ARCHER, CT_CROSSBOW, CT_KNIGHT, CT_PIKE,
    CT_ARQUEBUS, CT_MUSKET, CT_GRENADIER, CT_RIFLE, CT_MG, CT_FLAMER, CT_ASSAULT, CT_RPG,
    CT_COUNT
};
enum CharState { CS_WALK, CS_ATTACK, CS_SHOOT, CS_CLIMB, CS_FLEE, CS_DEAD, CS_IDLE, CS_PUSH };
struct Character {
    bool alive, used;
    uint8_t team, type, state, era;
    vec3 pos, vel;
    float yaw, hp, maxHp, speed;
    float animT, actT, reload, stateT, burn, deadT, screamCd, hitFlash;
    vec3 fallAxis;
    float fallAng, fallVel;
    uint8_t limbs; // bits de membres perdus
    Color tunic, pants;
    int target;     // -1 joueur, sinon index
    int ladder;     // corps échelle portée/posée
    int vehicle;    // véhicule poussé
    vec3 goal;      // destination
    float climbTop;
    float stuckT;
    float bloodiness;
    float spawnT;
    bool onWall;
    int burst;
    float vy;
    int post;
    bool grounded;
};
extern std::vector<Character> g_chars;
void charsInit();
int charSpawn(int team, int type, const vec3& p, int era);
void charsUpdate(float dt);
void charsDraw();
void charDamage(int idx, float dmg, const vec3& from, const vec3& dir, int cause);
int charAtRay(const vec3& o, const vec3& d, float maxDist, float& t, bool& head, int ignoreTeam);
void flowFieldDirty();
// déplacement avec collisions (sol, murs, marches) ; renvoie true si bloqué par un bloc
bool actorMove(vec3& pos, float& vy, const vec3& delta, float radius, float dt, bool& onGround, int* vi, int* vj, int* vk);
bool lineOfSight(const vec3& a, const vec3& b);
vec3 ballisticAim(const vec3& from, const vec3& to, float speed, float g);
int countAlive(int team);
void charsKillAll();
void alliesSpawn();
enum DmgCause { DC_ARROW, DC_BULLET, DC_BLADE, DC_EXPLOSION, DC_FIRE, DC_CRUSH, DC_FALL };

// ---------------------------------------------------------------- Projectiles / véhicules
enum ProjKind { PK_ARROW, PK_BOLT, PK_BOULDER, PK_BALL, PK_SHELL, PK_BOMB, PK_GRENADE, PK_ROCKET, PK_MISSILE, PK_OIL };
struct Proj {
    bool alive;
    uint8_t kind, team;
    bool flaming;
    vec3 pos, vel;
    float dmg, life, fuse;
};
extern std::vector<Proj> g_projs;
void projSpawn(int kind, const vec3& p, const vec3& v, int team, float dmg, bool flaming = false);
void projsUpdate(float dt);
void projsDraw();
void hitscan(const vec3& o, const vec3& d, float range, float dmg, int team, bool tracer);
struct Stuck { vec3 pos, dir; float life; bool flaming; };
extern std::vector<Stuck> g_stuck;

enum VehKind { VK_RAM, VK_CATAPULT, VK_TREBUCHET, VK_CANNON, VK_TANK, VK_PLANE, VK_HELI, VK_DRONE, VK_ARTILLERY };
struct Vehicle {
    bool alive, wreck;
    uint8_t kind;
    vec3 pos, vel;
    float yaw, hp, maxHp, reload, stateT, burn, turret, anim;
    int pushers;
};
extern std::vector<Vehicle> g_vehicles;
int vehicleSpawn(int kind, const vec3& p);
void vehiclesUpdate(float dt);
void vehiclesDraw();
void vehicleDamage(int idx, float dmg, bool explosive);
int vehicleAtRay(const vec3& o, const vec3& d, float maxDist, float& t);

// Postes fixes : braseros, chaudron d'huile, canons du château
enum StationKind { ST_BRAZIER, ST_CAULDRON, ST_CANNON, ST_MG_NEST };
struct Station {
    uint8_t kind;
    vec3 pos;
    float yaw, pitch, cooldown;
    bool active;
};
extern std::vector<Station> g_stations;
void stationsGen(int era);
void stationsUpdate(float dt);
void stationsDraw();

// ---------------------------------------------------------------- Joueur
enum WeaponId { W_SWORD, W_BOW, W_CROSSBOW, W_ARQUEBUS, W_MUSKET, W_RIFLE, W_MG, W_ASSAULT, W_ROCKET, W_COUNT };
struct WeaponDef {
    const char* name;
    int mag;
    float reload, fireDelay, dmg, spread, range;
    bool melee, projectile, automatic;
};
extern const WeaponDef g_weaponDefs[W_COUNT];
struct Player {
    vec3 pos, vel;
    float yaw, pitch;
    float hp, hurtT, deadT, bob, bobAmp, recoil, shake, landT;
    bool onGround, dead, inWater;
    int weapons[4];
    int nWeapons, cur;
    int mag[W_COUNT];
    float reloadT, cooldown, drawT, swingT, switchT;
    int held;          // corps tenu
    int station;       // poste utilisé
    float repairT;
    int stone;         // pierres de réparation
    int grenades;
    float zoom;
    bool arrowFlaming;
    float stepT;
    float bloodScreen;
    int kills;
};
extern Player g_player;
void playerInit();
void playerSpawn();
void playerUpdate(float dt);
void playerDrawViewmodel();
void playerDamage(float dmg, const vec3& from);
void playerSetupLoadout(int era);
vec3 playerEye();

// ---------------------------------------------------------------- Ères, vagues, partie
struct EraDef {
    const char* name;
    int year;
};
extern const EraDef g_eras[9];
enum WaveState { WS_INTER, WS_ASSAULT, WS_RETREAT, WS_FALLEN };
struct GameState {
    double year;          // année courante
    int era;
    int wave;
    int waveState;
    float waveTimer;
    int toSpawn, spawned, killed;
    float spawnT;
    int resets;
    int timeSpeedIdx;
    float playTime;
    float msgT;
    char msg[128];
    char sub[128];
    float autosaveT;
    float fallenT;
    int bestWave;
    int historyCount;
    int histWave[16];
    int histYear[16];
    float siegeT, airT;
    bool paused;
    int menuSel;
    float castleStart, keepStart;
    float keepCapture;
};
extern GameState g_game;
extern const float g_timeSpeeds[6];
float timeScale();
void gameNew(bool keepHistory);
void gameUpdate(float dt);
void gameMessage(const char* a, const char* b, float dur);
int eraForYear(double y);
bool gameSave();
bool gameLoad();
void hudDraw();
void menuUpdateDraw();

extern float g_time;      // temps réel écoulé (s)
extern float g_dtReal;

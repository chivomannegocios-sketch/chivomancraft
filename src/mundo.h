// ============================================================================
//  MUNDO VOXEL  -  generacion de terreno + mallas optimizadas (sin OpenGL)
//  Autor: CHlVOMAN
//
//  IDEA: el mundo esta hecho de "cubitos" (voxels). Un bloque de Minecraft = SUB x SUB x SUB cubitos.
//        Cambia SUB (1, 2, 4 u 8) para probar cuanto aguanta tu PC. Con SUB = 2 un bloque son 8 cubitos.
// ============================================================================
#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>
#include <climits>
#include <algorithm>

// ---------------------------------------------------------------- ajustes
constexpr int    SUB = 2;                 // cubitos por lado de un bloque (1, 2, 4 u 8)
constexpr int    CS = 32;                 // cubitos por lado de un chunk
constexpr double VS = 1.0 / SUB;          // tamano de un cubito en bloques
constexpr int    OFFY = 105;              // bloques de profundidad bajo el relieve original (el terreno se sube OFFY bloques)
constexpr int    WORLD_H_BLOCKS = 96 + OFFY;     // altura del mundo en bloques
constexpr int    HYC = (WORLD_H_BLOCKS * SUB + CS - 1) / CS;   // chunks en vertical
constexpr int    WATER_LEVEL = 22 + OFFY;        // nivel del agua (en bloques)
constexpr double LAGO_UMBRAL = 0.40;      // mas alto = mas lagunas
constexpr double RIO_ANCHO = 0.060;       // mas alto = rios mas anchos
#ifdef MOVIL
constexpr int    VIEW_BLOCKS = 64;        // celular: distancia de vision menor (menos chunks, menos memoria y menos calor)
#else
constexpr int    VIEW_BLOCKS = 128;       // distancia de vision en bloques
#endif
constexpr int    RC = (VIEW_BLOCKS * SUB + CS - 1) / CS;       // radio de vision en chunks

static_assert(CS % SUB == 0, "SUB debe dividir a CS");

enum Tipo : uint8_t { AIRE = 0, PASTO, TIERRA, PIEDRA, ARENA, AGUA, MADERA, HOJAS,
                      CARBON, HIERRO, COBRE, ORO, DIAMANTE, CUARZO, ARCILLA,
                      ESTANO, ALUMINIO, PLATA, TITANIO, LITIO, URANIO, ANTORCHA,
                      AGUA_F1, AGUA_F2, AGUA_F3, AGUA_F4, AGUA_F5, AGUA_F6, AGUA_F7, AGUA_F8,   // agua que fluye (nivel 1..8); AGUA = fuente
                      AZUFRE, SALITRE,           // al final para no cambiar los numeros de los mundos ya guardados
                      LED_ROJO, LED_NARANJA, LED_AMARILLO, LED_LIMA, LED_VERDE, LED_TURQUESA,   // bloques de luz LED de colores
                      LED_CELESTE, LED_AZUL, LED_VIOLETA, LED_MAGENTA, LED_ROSA, LED_BLANCO,
                      VIDRIO, CARROCERIA, ASIENTO, TIMON, ALA, RUEDA, HELICE, CONTROL,        // vidrio y piezas de vehiculo (v18)
                      CARR_0, CARR_1, CARR_2, CARR_3, CARR_4, CARR_5, CARR_6, CARR_7, CARR_8, CARR_9, CARR_10, CARR_11,   // carroceria pintada
                      NIEVE, HIELO, CACTUS, HOJA_ROJA, HOJA_NARANJA, HOJA_AMARILLA, HOJA_ROSA, HOJA_PINO, PASTO_SECO,   // biomas v27
                      NTIPOS };
constexpr int ID_SPRAY = 250;                        // herramienta (no es un bloque)
static inline bool esPiezaVehiculo(uint8_t t) { return t >= CARROCERIA && t <= CARR_11; }
static const int NTIPOS_MENU = ANTORCHA + 1 + 2 + 12 + 1 + 7 + 1 + 9;       // los que salen en el menu de materiales
static uint32_t g_semilla = 0;                      // semilla del mundo (0 = mundo clasico)
static inline bool esMineral(uint8_t t) { return (t >= CARBON && t <= CUARZO) || (t >= ESTANO && t <= URANIO) || t == AZUFRE || t == SALITRE; }
static inline bool esLed(uint8_t t) { return t >= LED_ROJO && t <= LED_BLANCO; }
// color de cada LED (el canal mas alto vale 1: todos alumbran igual de lejos)
static const float LED_RGB[12][3] = {{1.0f, 0.10f, 0.10f}, {1.0f, 0.48f, 0.08f}, {1.0f, 0.90f, 0.15f}, {0.60f, 1.0f, 0.12f},
                                     {0.10f, 1.0f, 0.20f}, {0.08f, 1.0f, 0.80f}, {0.15f, 0.70f, 1.0f}, {0.15f, 0.20f, 1.0f},
                                     {0.55f, 0.18f, 1.0f}, {1.0f, 0.15f, 0.80f}, {1.0f, 0.50f, 0.72f}, {1.0f, 1.0f, 1.0f}};
static inline int menuTipo(int i) {   // casilla del menu -> tipo
    if (i <= ANTORCHA) return i;
    if (i == ANTORCHA + 1) return AZUFRE;
    if (i == ANTORCHA + 2) return SALITRE;
    if (i < ANTORCHA + 15) return LED_ROJO + (i - (ANTORCHA + 3));
    if (i == ANTORCHA + 15) return VIDRIO;
    if (i < ANTORCHA + 23) return CARROCERIA + (i - (ANTORCHA + 16));
    if (i == ANTORCHA + 23) return ID_SPRAY;
    return NIEVE + (i - (ANTORCHA + 24));
}
static inline bool esHoja(uint8_t t) { return t == HOJAS || (t >= HOJA_ROJA && t <= HOJA_PINO); }
static inline bool esVeg(uint8_t t) { return t == MADERA || t == CACTUS || esHoja(t); }
static inline bool esAgua(uint8_t t) { return t == AGUA || (t >= AGUA_F1 && t <= AGUA_F8); }
static inline int nivelAgua(uint8_t t) { return t == AGUA ? 9 : (t >= AGUA_F1 && t <= AGUA_F8) ? t - AGUA_F1 + 1 : 0; }
static inline bool esSolido(uint8_t t) { return t != AIRE && !esAgua(t) && t != ANTORCHA; }   // colisiona
static inline bool esRompible(uint8_t t) { return t != AIRE && !esAgua(t); }                  // se puede apuntar/romper

#define IDX(x, y, z) ((((y) * CS) + (z)) * CS + (x))

// ---------------------------------------------------------------- ruido
static inline uint32_t hash2(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 2246822519u + g_semilla * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static inline float rnd01(uint32_t h) { return (h & 0xFFFFFF) / 16777216.0f; }

static float vnoise(float x, float z, uint32_t seed) {
    int xi = (int)std::floor(x), zi = (int)std::floor(z);
    float fx = x - xi, fz = z - zi;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    float a = rnd01(hash2(xi, zi, seed)), b = rnd01(hash2(xi + 1, zi, seed));
    float c = rnd01(hash2(xi, zi + 1, seed)), d = rnd01(hash2(xi + 1, zi + 1, seed));
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
}
static float vnoise3(float x, float y, float z, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    float fx = x - xi, fy = y - yi, fz = z - zi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
    auto H = [&](int a, int b, int c) { return rnd01(hash2(a, c, seed ^ ((uint32_t)b * 0x9E3779B1u))); };
    float c00 = H(xi, yi, zi) + (H(xi + 1, yi, zi) - H(xi, yi, zi)) * fx;
    float c10 = H(xi, yi + 1, zi) + (H(xi + 1, yi + 1, zi) - H(xi, yi + 1, zi)) * fx;
    float c01 = H(xi, yi, zi + 1) + (H(xi + 1, yi, zi + 1) - H(xi, yi, zi + 1)) * fx;
    float c11 = H(xi, yi + 1, zi + 1) + (H(xi + 1, yi + 1, zi + 1) - H(xi, yi + 1, zi + 1)) * fx;
    float a = c00 + (c10 - c00) * fy, b = c01 + (c11 - c01) * fy;
    return a + (b - a) * fz;
}
static float fbm(float x, float z, int oct, uint32_t seed) {
    float s = 0, amp = 0.5f, tot = 0;
    for (int i = 0; i < oct; i++) {
        s += vnoise(x, z, seed + i * 31) * amp;
        tot += amp;
        x *= 2.0f; z *= 2.0f; amp *= 0.5f;
    }
    return s / tot;
}

static inline double suave(double a, double b, double x) {      // 0 si x<=a, 1 si x>=b (curva suave)
    double t = (x - a) / (b - a);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3 - 2 * t);
}

// ---------------------------------------------------------------- biomas
// Dos campos de ruido muy suaves (temperatura y humedad, ~500 bloques) colocan cada punto en un plano; cada bioma tiene su "centro"
// en ese plano y su peso baja con la distancia. Con los pesos se mezclan las formas del terreno (sin cortes bruscos).
enum Bio { BIO_PRADERA = 0, BIO_BOSQUE, BIO_DENSO, BIO_DESIERTO, BIO_MESETA, BIO_PANTANO, BIO_TUNDRA, BIO_SABANA, BIO_OTONO, BIO_N };
static inline float acota01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static int biomaPesos(double bx, double bz, float* w) {      // devuelve el bioma dominante
    float T = fbm((float)bx * 0.0022f, (float)bz * 0.0022f, 2, 61);
    float Hm = fbm((float)bx * 0.0026f + 500.0f, (float)bz * 0.0026f, 2, 71);
    float t = acota01((T - 0.24f) / 0.52f), h = acota01((Hm - 0.24f) / 0.52f);
    static const float C[BIO_N][2] = {{0.44f, 0.30f}, {0.36f, 0.68f}, {0.84f, 0.90f}, {0.86f, 0.10f}, {0.76f, 0.40f}, {0.56f, 0.97f}, {0.05f, 0.28f}, {0.62f, 0.17f}, {0.14f, 0.66f}};
    float tot = 0; int dom = 0;
    for (int b = 0; b < BIO_N; b++) {
        float dt = t - C[b][0], dh = h - C[b][1];
        static const float SESGO[BIO_N] = {1.1f, 1.0f, 0.8f, 3.4f, 0.9f, 0.9f, 1.7f, 2.2f, 1.6f};   // para repartir las superficies
        w[b] = SESGO[b] * std::exp(-(dt * dt + dh * dh) / 0.035f);
        tot += w[b];
    }
    for (int b = 0; b < BIO_N; b++) { w[b] /= tot; if (w[b] > w[dom]) dom = b; }
    return dom;
}
// cuanto desierto hay en ese punto (0 = nada, 1 = desierto completo)
static double desierto(double bx, double bz) { float w[BIO_N]; biomaPesos(bx, bz, w); return w[BIO_DESIERTO]; }
static int biomaDominante(double bx, double bz) { float w[BIO_N]; return biomaPesos(bx, bz, w); }

// altura del terreno en bloques (bx, bz en bloques): colinas, montanas, lagunas, rios y desiertos
static double terrenoBase(double bx, double bz, int* domOut);
static double alturaTerreno(double bx, double bz) { return terrenoBase(bx, bz, nullptr); }
static double terrenoBase(double bx, double bz, int* domOut) {
    const double WL0 = WATER_LEVEL - OFFY;
    float w[BIO_N]; int dom = biomaPesos(bx, bz, w);
    if (domOut) *domOut = dom;
    float base = fbm((float)bx * 0.004f, (float)bz * 0.004f, 3, 1);
    float colinas = fbm((float)bx * 0.02f, (float)bz * 0.02f, 4, 2);
    float monte = fbm((float)bx * 0.008f, (float)bz * 0.008f, 4, 3);
    monte = std::max(0.0f, (monte - 0.5f) * 2.0f);
    const float wD = w[BIO_DESIERTO], wM = w[BIO_MESETA], wP = w[BIO_PANTANO];
    float hillMul = w[BIO_PRADERA] * 0.55f + w[BIO_BOSQUE] * 1.0f + w[BIO_DENSO] * 0.85f + wD * 0.45f + wM * 0.7f + wP * 0.2f +
                    w[BIO_TUNDRA] * 0.85f + w[BIO_SABANA] * 0.40f + w[BIO_OTONO] * 0.95f;
    float monteMul = 1.0f - 0.95f * wD - 0.75f * wP - 0.3f * wM - 0.5f * w[BIO_SABANA];
    double h = 16.0 + base * 14.0 * (1.0 - 0.5 * wP) + colinas * 18.0 * hillMul + monte * monte * 36.0 * monteMul;
    {   // cordilleras: se suman donde el ruido de altitud es alto (no en desierto ni pantano); las cumbres son de roca
        float alt = fbm((float)bx * 0.0035f, (float)bz * 0.0035f, 3, 5);
        double wMont = suave(0.57, 0.70, alt) * std::max(0.0, 1.0 - 0.9 * wD - 0.8 * wP);
        if (wMont > 0.002) {
            float ridge = 1.0f - std::fabs(2.0f * fbm((float)bx * 0.012f, (float)bz * 0.012f, 3, 6) - 1.0f);
            h += wMont * (12.0 + 40.0 * ridge * ridge);
            monte = std::max(monte, (float)wMont);
        }
    }
    if (wM > 0.01f) {   // meseta: escalones de unos 7 bloques con paredes casi verticales
        double x = (h - 14.0) / 7.5, fl = std::floor(x), fr = x - fl;
        double st = 14.0 + 7.5 * (fl + suave(0.80, 1.0, fr)) + 9.0;
        h = h * (1.0 - wM) + st * wM;
    }
    if (wP > 0.01f) {   // pantano: casi plano al nivel del agua, con charcos y montoncitos
        float fino = fbm((float)bx * 0.03f, (float)bz * 0.03f, 3, 83);
        double hp = WL0 + 0.2 + (fino - 0.5) * 5.0;
        h = h * (1.0 - wP) + hp * wP;
    }

    // lagunas: hondonadas redondas que se llenan de agua
    float lago = fbm((float)bx * 0.011f, (float)bz * 0.011f, 3, 21);
    double tl = suave(0.0, 1.0, (LAGO_UMBRAL - lago) / 0.16);
    if (tl > 0) h = h * (1.0 - tl) + std::min(h, (double)WL0 - 4.5) * tl;

    // rios: una franja estrecha donde el ruido cruza 0,5 (mas finos en las montanas altas)
    float r = std::fabs(fbm((float)bx * 0.0045f, (float)bz * 0.0045f, 3, 11) - 0.5f);
    double fuerza = 1.0 - suave(0.0, 0.5, monte * 2.0);
    double tr = (1.0 - suave(0.0, RIO_ANCHO, r)) * fuerza;
    if (tr > 0) h = h * (1.0 - tr) + std::min(h, (double)WL0 - 3.5) * tr;

    return std::min(h, 86.0) + OFFY;
}

// capa superior del terreno segun el bioma (la usan los chunks reales y el mundo lejano)
static inline uint8_t tipoSuperficie(int dom, double h, double bx, double bz, bool arc) {
    h -= OFFY;                                   // las reglas estan escritas con el relieve original
    const double WL0 = WATER_LEVEL - OFFY;
    if (h > 52.0 + 6.0 * vnoise((float)(bx * 0.05), (float)(bz * 0.05), 91u))      // alta montana: roca (nieve en la tundra y en las cumbres)
        return (dom == BIO_TUNDRA || h > 66.0 + 5.0 * vnoise((float)(bx * 0.07), (float)(bz * 0.07), 92u)) ? NIEVE : PIEDRA;
    if (dom == BIO_TUNDRA) return h < WL0 + 0.2 ? (arc ? ARCILLA : ARENA) : NIEVE;
    if (dom == BIO_DESIERTO || h < WL0 + 2.4) return arc ? ARCILLA : ARENA;
    if (dom == BIO_PANTANO) return h < WL0 + 0.4 ? ARCILLA : (vnoise((float)(bx * 0.06), (float)(bz * 0.06), 93u) > 0.55f ? PASTO : TIERRA);
    if (dom == BIO_MESETA) return TIERRA;
    if (dom == BIO_SABANA) return PASTO_SECO;
    return PASTO;
}

// vegetacion: tipo de arbol (0 roble, 1-3 otono rojo/naranja/amarillo, 4 cerezo, 5 pino, 6 acacia, 7 cactus)
static inline bool vegetacion(int tx, int tz, double* altura, int* clase) {
    if (hash2(tx, tz, 77) % 18 != 0) return false;                 // candidatos (1 de cada 18 bloques como maximo)
    float w[BIO_N]; int dom = biomaPesos(tx + 0.5, tz + 0.5, w);
    //                         prad.      bosque     denso      desierto    meseta  pantano    tundra     sabana     otono
    static const float D[BIO_N] = {1.0f / 200, 1.0f / 40, 1.0f / 18, 1.0f / 230, 0.0f, 1.0f / 55, 1.0f / 45, 1.0f / 85, 1.0f / 22};   // arboles por bloque
    float dens = 0;
    for (int b = 0; b < BIO_N; b++) dens += w[b] * D[b];
    if (hash2(tx, tz, 78) % 1000 >= (uint32_t)(dens * 18.0f * 1000.0f)) return false;
    double h = alturaTerreno(tx + 0.5, tz + 0.5);
    if (h < WATER_LEVEL + 1.5 || h > 60.0 + OFFY) return false;
    uint32_t q = hash2(tx, tz, 79) % 100;
    int k = 0;
    switch (dom) {
        case BIO_OTONO: k = q < 34 ? 1 : q < 62 ? 2 : q < 90 ? 3 : 4; break;
        case BIO_BOSQUE: k = q < 8 ? 3 : q < 14 ? 2 : 0; break;
        case BIO_TUNDRA: k = 5; break;
        case BIO_SABANA: k = 6; break;
        case BIO_DESIERTO: k = 7; break;
        case BIO_PRADERA: k = q < 6 ? 4 : 0; break;
        default: k = 0;
    }
    if (k == 7 && tipoSuperficie(dom, h, tx + 0.5, tz + 0.5, false) != ARENA) return false;   // el cactus solo sobre arena
    if (k != 7 && dom == BIO_DESIERTO) return false;
    *altura = h;
    if (clase) *clase = k;
    return true;
}
static inline bool hayArbol(int tx, int tz, double* altura) { return vegetacion(tx, tz, altura, nullptr); }

static inline int divPiso(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

// ---------------------------------------------------------------- minerales (vetas)
// El subsuelo se divide en celdas de VETA_CELDA bloques; cada celda puede tener una veta (una mancha irregular
// de mineral) cuyo tipo depende de la profundidad. La veta siempre queda dentro de su propia celda.
constexpr int VETA_CELDA = 12;
struct Veta { uint8_t tipo; double cx, cy, cz, r; };      // centro y radio en bloques
static bool vetaEnCelda(int ix, int iy, int iz, Veta& v) {
    uint32_t h = hash2((int)hash2(ix, iy, 77u), iz, 91u);
    double yc = iy * VETA_CELDA + VETA_CELDA * 0.5 - OFFY;   // profundidad respecto al relieve original (negativa = mas hondo)
    struct Cand { uint8_t t; double y0, y1, w, r0, r1; };
    static const Cand cand[] = {
        {CARBON, 4, 75, 0.18, 2.4, 3.6},    // comun, desde cerca de la superficie
        {HIERRO, 3, 60, 0.16, 2.0, 3.2},
        {ALUMINIO, 5, 65, 0.08, 2.0, 3.2},  // bauxita
        {COBRE, 6, 55, 0.12, 1.8, 2.8},
        {ESTANO, 4, 45, 0.08, 1.6, 2.6},
        {CUARZO, 3, 50, 0.08, 1.6, 2.5},
        {LITIO, 2, 40, 0.04, 1.4, 2.2},
        {PLATA, 2, 30, 0.05, 1.3, 2.1},
        {ORO, -OFFY, 26, 0.07, 1.3, 2.0},       // raro y profundo
        {DIAMANTE, -OFFY, 15, 0.05, 1.0, 1.6},  // muy raro, casi en el fondo
        {TITANIO, -OFFY, 14, 0.03, 1.0, 1.7},
        {URANIO, -OFFY, 10, 0.015, 1.0, 1.5},
        {SALITRE, 4, 55, 0.07, 1.6, 2.6},   // nitrato de potasio
        {AZUFRE, 2, 40, 0.06, 1.4, 2.4}
    };
    double u = rnd01(h), acum = 0;
    for (const Cand& c : cand) {
        if (yc < c.y0 || yc > c.y1) continue;
        acum += c.w;
        if (u < acum) {
            double r = c.r0 + (c.r1 - c.r0) * rnd01(hash2(iy, ix + 5, h));
            double span = VETA_CELDA - 2 * r;
            v.tipo = c.t; v.r = r;
            v.cx = ix * (double)VETA_CELDA + r + span * rnd01(hash2(ix, iz, h ^ 0x9e37u));
            v.cy = iy * (double)VETA_CELDA + r + span * rnd01(hash2(iy, iz, h ^ 0x85ebu));
            v.cz = iz * (double)VETA_CELDA + r + span * rnd01(hash2(ix, iy, h ^ 0xc2b2u));
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- chunk (datos)
struct DatosChunk {
    std::shared_ptr<std::vector<uint8_t>> d;   // nullptr = chunk uniforme (todo del mismo tipo)
    uint8_t uni = AIRE;
};

// Genera un chunk (cx, cy, cz son indices de chunk)
static DatosChunk generarChunk(int cx, int cy, int cz) {
    DatosChunk r;
    const int ox = cx * CS, oy = cy * CS, oz = cz * CS;
    static thread_local int top[CS * CS];
    static thread_local float hb[CS * CS];
    static thread_local uint8_t bioCol[CS * CS];
    static thread_local bool arcillaCol[CS * CS];
    static thread_local bool entradaCol[CS * CS];
    int minTop = INT_MAX, maxTop = INT_MIN;
    for (int lz = 0; lz < CS; lz++)
        for (int lx = 0; lx < CS; lx++) {
            int dom = 0;
            double h = terrenoBase((ox + lx + 0.5) / SUB, (oz + lz + 0.5) / SUB, &dom);
            // (la misma correccion de lagunas y rios que alturaTerreno se hace dentro de terrenoBase)
            hb[lz * CS + lx] = (float)h;
            bioCol[lz * CS + lx] = (uint8_t)dom;
            arcillaCol[lz * CS + lx] = h < WATER_LEVEL - 0.6 &&
                                       vnoise((float)((ox + lx + 0.5) / SUB * 0.07), (float)((oz + lz + 0.5) / SUB * 0.07), 53u) > 0.52f;
            entradaCol[lz * CS + lx] = h > WATER_LEVEL + 3.0 &&
                                       vnoise((float)((ox + lx + 0.5) / SUB * 0.035), (float)((oz + lz + 0.5) / SUB * 0.035), 311u) > 0.74f;
            int t = (int)std::floor(h * SUB);
            top[lz * CS + lx] = t;
            minTop = std::min(minTop, t);
            maxTop = std::max(maxTop, t);
        }
    // caminos rapidos: chunks de puro aire o pura piedra
    if (oy >= std::max(maxTop + 10 * SUB, WATER_LEVEL * SUB)) { r.uni = AIRE; return r; }
    // vetas de mineral que tocan este chunk
    std::vector<Veta> vetas;
    {
        int bx0 = divPiso(ox, SUB), bx1 = divPiso(ox + CS - 1, SUB);
        int by0 = divPiso(oy, SUB), by1 = divPiso(oy + CS - 1, SUB);
        int bz0 = divPiso(oz, SUB), bz1 = divPiso(oz + CS - 1, SUB);
        for (int iz = divPiso(bz0, VETA_CELDA); iz <= divPiso(bz1, VETA_CELDA); iz++)
            for (int iy = divPiso(by0, VETA_CELDA); iy <= divPiso(by1, VETA_CELDA); iy++)
                for (int ix = divPiso(bx0, VETA_CELDA); ix <= divPiso(bx1, VETA_CELDA); ix++) {
                    Veta v;
                    if (!vetaEnCelda(ix, iy, iz, v)) continue;
                    if (v.cx + v.r < bx0 || v.cx - v.r > bx1 + 1 || v.cy + v.r < by0 || v.cy - v.r > by1 + 1 ||
                        v.cz + v.r < bz0 || v.cz - v.r > bz1 + 1) continue;
                    vetas.push_back(v);
                }
    }

    auto v = std::make_shared<std::vector<uint8_t>>((size_t)CS * CS * CS, (uint8_t)AIRE);
    uint8_t* dt = v->data();
    for (int lz = 0; lz < CS; lz++)
        for (int lx = 0; lx < CS; lx++) {
            int t = top[lz * CS + lx];
            const int dom = bioCol[lz * CS + lx];
            const bool arcC = arcillaCol[lz * CS + lx];
            const uint8_t sup = tipoSuperficie(dom, hb[lz * CS + lx], (ox + lx + 0.5) / SUB, (oz + lz + 0.5) / SUB, arcC);
            for (int ly = 0; ly < CS; ly++) {
                int vy = oy + ly;
                uint8_t tp;
                if (vy <= t) {
                    int prof = (t - vy) / SUB;
                    if (vy > t - SUB) tp = sup;
                    else if (dom == BIO_MESETA && sup != PIEDRA && prof < 20) {          // estratos de colores en las paredes de la meseta
                        static const uint8_t B[6] = {TIERRA, TIERRA, ARENA, TIERRA, ARENA, ARENA};
                        tp = B[(((vy / SUB) % 6) + 6) % 6];
                    }
                    else if (prof < 4) tp = (sup == PASTO || sup == PASTO_SECO || (sup == NIEVE && dom == BIO_TUNDRA && hb[lz * CS + lx] < 52.0f)) ? TIERRA : (sup == NIEVE ? PIEDRA : (sup == ARCILLA && prof >= 2 ? ARENA : sup));
                    else tp = PIEDRA;
                } else if (vy < WATER_LEVEL * SUB) tp = (dom == BIO_TUNDRA && vy >= (WATER_LEVEL - 1) * SUB) ? HIELO : AGUA;     // lagos helados en la tundra
                else tp = AIRE;
                dt[IDX(lx, ly, lz)] = tp;
            }
            // vegetacion cercana (tronco 1 bloque de ancho; copas segun el tipo de arbol)
            int bx = divPiso(ox + lx, SUB), bz = divPiso(oz + lz, SUB);
            for (int tz = bz - 3; tz <= bz + 3; tz++)
                for (int tx = bx - 3; tx <= bx + 3; tx++) {
                    double ha; int kv;
                    if (!vegetacion(tx, tz, &ha, &kv)) continue;
                    const int vb = (int)std::floor(ha * SUB) + 1 - 2;      // base del tronco (cubitos)
                    const double dxw = (ox + lx + 0.5) * VS - (tx + 0.5), dzw = (oz + lz + 0.5) * VS - (tz + 0.5);
                    const double dxz = dxw * dxw + dzw * dzw;
                    if (kv == 7) {   // cactus: columna de 3-4 bloques con un brazo
                        int alto = 3 + (int)(hash2(tx, tz, 81) % 2);
                        int dir = (hash2(tx, tz, 82) & 1) ? 1 : -1;
                        for (int ly = 0; ly < CS; ly++) {
                            int vy = oy + ly;
                            if (tx == bx && tz == bz && vy >= vb && vy < vb + alto * SUB) dt[IDX(lx, ly, lz)] = CACTUS;
                            if (tx + dir == bx && tz == bz && vy >= vb + SUB && vy < vb + 3 * SUB) dt[IDX(lx, ly, lz)] = CACTUS;
                        }
                        continue;
                    }
                    static const uint8_t HT[8] = {HOJAS, HOJA_ROJA, HOJA_NARANJA, HOJA_AMARILLA, HOJA_ROSA, HOJA_PINO, HOJAS, HOJAS};
                    const uint8_t hoja = HT[kv];
                    const int bTronco = kv == 5 ? 7 : kv == 6 ? 5 : 6;
                    if (tx == bx && tz == bz) {
                        for (int ly = 0; ly < CS; ly++) {
                            int vy = oy + ly;
                            if (vy >= vb && vy < vb + bTronco * SUB) dt[IDX(lx, ly, lz)] = MADERA;
                        }
                    }
                    if (dxz > 7.5) continue;
                    for (int ly = 0; ly < CS; ly++) {
                        int vy = oy + ly;
                        double yb = (vy + 0.5) * VS - vb * VS;      // altura sobre la base del tronco, en bloques
                        bool leaf;
                        if (kv == 5) {            // pino: cono
                            double r = 2.4 * (9.6 - yb) / 7.6 + 0.2;
                            leaf = yb >= 2.0 && yb <= 9.6 && dxz <= r * r;
                        } else if (kv == 6) {     // acacia: sombrilla plana y ancha
                            leaf = (yb >= 4.4 && yb < 5.3 && dxz <= 7.2) || (yb >= 5.3 && yb < 6.2 && dxz <= 3.4);
                        } else {                  // redonda (roble, otono, cerezo)
                            double dy = yb - (bTronco - 1.0);
                            leaf = dxz + dy * dy * 1.6 <= 7.2;
                        }
                        if (leaf && dt[IDX(lx, ly, lz)] == AIRE) dt[IDX(lx, ly, lz)] = hoja;
                    }
                }
        }
    // cuevas: tuneles (interseccion de dos campos de ruido 3D) y salas grandes. El ruido se muestrea cada 4 cubitos
    // en una rejilla alineada con el mundo y se interpola, asi es suave y continuo entre chunks.
    if (oy + CS - 1 >= 3 * SUB) {
        constexpr int G = 4, NG = CS / G + 1;
        static thread_local float f1[NG * NG * NG], f2[NG * NG * NG], f3[NG * NG * NG];
        for (int gz = 0; gz < NG; gz++) for (int gy = 0; gy < NG; gy++) for (int gx = 0; gx < NG; gx++) {
            float x = (float)((ox + gx * G) * VS), y = (float)((oy + gy * G) * VS), z = (float)((oz + gz * G) * VS);
            int k = (gy * NG + gz) * NG + gx;
            f1[k] = vnoise3(x * 0.055f, y * 0.080f, z * 0.055f, 301u);
            f2[k] = vnoise3(x * 0.055f + 57.0f, y * 0.080f, z * 0.055f + 31.0f, 302u);
            f3[k] = vnoise3(x * 0.026f, y * 0.050f, z * 0.026f, 303u) + 0.35f * vnoise3(x * 0.09f, y * 0.09f, z * 0.09f, 304u);
        }
        auto mezcla = [&](const float* f, int cx_, int cy_, int cz_, float fx, float fy, float fz) {
            auto at = [&](int a, int b, int c) { return f[((cy_ + b) * NG + (cz_ + c)) * NG + (cx_ + a)]; };
            float c00 = at(0, 0, 0) + (at(1, 0, 0) - at(0, 0, 0)) * fx, c10 = at(0, 1, 0) + (at(1, 1, 0) - at(0, 1, 0)) * fx;
            float c01 = at(0, 0, 1) + (at(1, 0, 1) - at(0, 0, 1)) * fx, c11 = at(0, 1, 1) + (at(1, 1, 1) - at(0, 1, 1)) * fx;
            float a = c00 + (c10 - c00) * fy, b = c01 + (c11 - c01) * fy;
            return a + (b - a) * fz;
        };
        for (int lz = 0; lz < CS; lz++) for (int ly = 0; ly < CS; ly++) {
            int vy = oy + ly;
            if (vy < 3 * SUB) continue;
            double by = vy * VS;
            for (int lx = 0; lx < CS; lx++) {
                int tcol = top[lz * CS + lx];
                int limite = entradaCol[lz * CS + lx] ? tcol : tcol - 4 * SUB;
                if (vy > limite) continue;
                uint8_t& c = dt[IDX(lx, ly, lz)];
                if (c == AIRE || c == AGUA || esVeg(c)) continue;
                int cx_ = lx / G, cy_ = ly / G, cz_ = lz / G;
                float fx = (lx + 0.5f) / G - cx_, fy = (ly + 0.5f) / G - cy_, fz = (lz + 0.5f) / G - cz_;
                float n1 = mezcla(f1, cx_, cy_, cz_, fx, fy, fz) - 0.5f, n2 = mezcla(f2, cx_, cy_, cz_, fx, fy, fz) - 0.5f;
                bool abre = n1 * n1 + n2 * n2 < 0.0070f;                               // tunel
                if (!abre && by < 46.0 + OFFY) abre = mezcla(f3, cx_, cy_, cz_, fx, fy, fz) > 1.02f;   // sala
                if (abre) c = AIRE;
            }
        }
    }
    // minerales: manchas irregulares que reemplazan solo piedra
    for (const Veta& v : vetas) {
        int x0 = std::max(ox, (int)std::floor((v.cx - v.r) * SUB)), x1 = std::min(ox + CS - 1, (int)std::ceil((v.cx + v.r) * SUB));
        int y0 = std::max(oy, (int)std::floor((v.cy - v.r) * SUB)), y1 = std::min(oy + CS - 1, (int)std::ceil((v.cy + v.r) * SUB));
        int z0 = std::max(oz, (int)std::floor((v.cz - v.r) * SUB)), z1 = std::min(oz + CS - 1, (int)std::ceil((v.cz + v.r) * SUB));
        for (int vz = z0; vz <= z1; vz++) for (int vy = y0; vy <= y1; vy++) for (int vx = x0; vx <= x1; vx++) {
            double dx = (vx + 0.5) * VS - v.cx, dy = (vy + 0.5) * VS - v.cy, dz = (vz + 0.5) * VS - v.cz;
            double lim = v.r * v.r * (0.70 + 0.55 * rnd01(hash2(vx * 7 + vy, vz, 123u)));
            if (dx * dx + dy * dy + dz * dz > lim) continue;
            uint8_t& c = dt[IDX(vx - ox, vy - oy, vz - oz)];
            if (c == PIEDRA) c = v.tipo;
        }
    }
#ifdef PRUEBA_FOSO
    for (int lz = 0; lz < CS; lz++) for (int ly = 0; ly < CS; ly++) for (int lx = 0; lx < CS; lx++) {
        double bx = (ox + lx) * VS, by = (oy + ly) * VS, bz = (oz + lz) * VS;
        if (bx > -24 && bx < 24 && bz > -24 && bz < 24 && by >= 8) dt[IDX(lx, ly, lz)] = AIRE;
    }
#endif
    // si quedo uniforme, no guardamos los datos
    uint8_t p = dt[0];
    bool igual = true;
    for (size_t i = 1; i < v->size(); i++) if (dt[i] != p) { igual = false; break; }
    if (igual) { r.uni = p; return r; }
    r.d = v;
    return r;
}

// ---------------------------------------------------------------- malla
struct Vertice { float x, y, z; uint8_t t, f, a, b; };     // 16 bytes
struct DatosMalla {
    std::vector<Vertice> v;
    std::vector<uint32_t> i;
};

// s[0] = el chunk, s[1..6] = vecinos (-x, +x, -y, +y, -z, +z)
static void construirMalla(const DatosChunk s[7], DatosMalla& op, DatosMalla& ag) {
    auto en = [&](int x, int y, int z) -> uint8_t {
        int k = 0, lx = x, ly = y, lz = z;
        if (x < 0) { k = 1; lx += CS; } else if (x >= CS) { k = 2; lx -= CS; }
        else if (y < 0) { k = 3; ly += CS; } else if (y >= CS) { k = 4; ly -= CS; }
        else if (z < 0) { k = 5; lz += CS; } else if (z >= CS) { k = 6; lz -= CS; }
        const DatosChunk& c = s[k];
        uint8_t t = c.d ? (*c.d)[IDX(lx, ly, lz)] : c.uni;
        return (t >= AGUA_F1 && t <= AGUA_F8) ? (uint8_t)AGUA : t;       // el agua que fluye se dibuja como agua
    };
    auto visible = [](uint8_t dueno, uint8_t otro) -> bool {
        if (dueno == AGUA) return otro == AIRE;
        if (dueno == VIDRIO) return otro == AIRE || otro == AGUA;
        return otro == AIRE || otro == AGUA || otro == VIDRIO;
    };
    static thread_local int mask[CS * CS];
    for (int d = 0; d < 3; d++) {
        const int u = (d + 1) % 3, w = (d + 2) % 3;
        for (int slice = 0; slice <= CS; slice++) {
            bool alguno = false;
            for (int j = 0; j < CS; j++)
                for (int i = 0; i < CS; i++) {
                    int pa[3], pb[3];
                    pa[d] = slice - 1; pb[d] = slice;
                    pa[u] = pb[u] = i; pa[w] = pb[w] = j;
                    uint8_t a = en(pa[0], pa[1], pa[2]), b = en(pb[0], pb[1], pb[2]);
                    int m = 0;
                    if (a != b) {
                        if (slice - 1 >= 0 && a != AIRE && visible(a, b)) m = (a << 1);
                        else if (slice < CS && b != AIRE && visible(b, a)) m = (b << 1) | 1;
                    }
                    mask[j * CS + i] = m;
                    if (m) alguno = true;
                }
            if (!alguno) continue;
            for (int j = 0; j < CS; j++)
                for (int i = 0; i < CS;) {
                    int m = mask[j * CS + i];
                    if (!m) { i++; continue; }
                    int ancho = 1;
                    while (i + ancho < CS && mask[j * CS + i + ancho] == m) ancho++;
                    int alto = 1;
                    for (; j + alto < CS; alto++) {
                        bool ok = true;
                        for (int k = 0; k < ancho; k++) if (mask[(j + alto) * CS + i + k] != m) { ok = false; break; }
                        if (!ok) break;
                    }
                    for (int jj = 0; jj < alto; jj++) for (int ii = 0; ii < ancho; ii++) mask[(j + jj) * CS + i + ii] = 0;

                    uint8_t tipo = (uint8_t)(m >> 1);
                    int atras = m & 1;
                    DatosMalla& dm = (tipo == AGUA || tipo == VIDRIO) ? ag : op;
                    float c[4][3];
                    int du[4] = {0, ancho, ancho, 0}, dv[4] = {0, 0, alto, alto};
                    const float ex = 0.004f;      // las caras se agrandan un pelo para que no queden rendijas entre vecinas
                    for (int k = 0; k < 4; k++) {
                        c[k][d] = (float)slice;
                        c[k][u] = du[k] == 0 ? (float)i - ex : (float)(i + du[k]) + ex;
                        c[k][w] = dv[k] == 0 ? (float)j - ex : (float)(j + dv[k]) + ex;
                    }
                    uint32_t base = (uint32_t)dm.v.size();
                    for (int k = 0; k < 4; k++)
                        dm.v.push_back({c[k][0], c[k][1], c[k][2], tipo, (uint8_t)(d * 2 + atras), 0, 0});
                    if (!atras) { uint32_t q[6] = {0, 1, 2, 0, 2, 3}; for (uint32_t x : q) dm.i.push_back(base + x); }
                    else        { uint32_t q[6] = {0, 2, 1, 0, 3, 2}; for (uint32_t x : q) dm.i.push_back(base + x); }
                    i += ancho;
                }
        }
    }
}

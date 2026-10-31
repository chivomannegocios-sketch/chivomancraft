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
#ifndef SUB_VAL
#define SUB_VAL 2
#endif
constexpr int    SUB = SUB_VAL;                 // cubitos por lado de un bloque (1, 2, 4 u 8)
constexpr int    CS = 32;                 // cubitos por lado de un chunk
constexpr double VS = 1.0 / SUB;
constexpr double KS = (SUB == 1) ? 0.5 : 1.0;      // v86: con SUB 1 el relieve y los biomas son el doble de anchos
constexpr double TS = (SUB == 1) ? 1.6 : 1.0;      // v86: con SUB 1 los arboles son mas grandes (mas altos y de copa ancha)          // tamano de un cubito en bloques
constexpr int    OFFY = 105;              // bloques de profundidad bajo el relieve original (el terreno se sube OFFY bloques)
constexpr int    WORLD_H_BLOCKS = (SUB == 1) ? 400 : (96 + OFFY);     // altura del mundo en bloques (v86: SUB 1 llega a 400)
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
                      MADERA_SELVA, HOJA_SELVA, LIANA, BAMBU, HOJA_NIEVE, ARBUSTO_BAYAS, FLOR_ROJA, FLOR_AMARILLA, FLOR_AZUL, FLOR_ROSA, HIELO_COMPACTO,   // v34
                      LAVA,                        // v46: lava (no solida, brilla, quema)
                      CORAL_ROJO, CORAL_ROSA, CORAL_NARANJA, CORAL_MORADO, CORAL_AMARILLO, CORAL_AZUL,   // v48: oceano con vida
                      ALGA, GRAVA,
                      TNT, TNT_ENC,                // v51: TNT y TNT encendido (parpadea antes de explotar)
                      BASE_CONCRETO, BASE_METAL, BASE_REJILLA, BASE_TUBERIA, BASE_PUERTA, BASE_TECLADO, BASE_PANTALLA, BASE_FRANJA, BASE_PISO, BASE_PLACA, PLASMA_FRIO, PLASMA_VERDE, PLASMA_CELESTE, PLASMA_ROJO, PLASMA_AMBAR, BASE_ACERO, BASE_MURO, BASE_CABLE, BASE_AVISO, BASE_ARMARIO, BASE_REJ_R, BASE_IA_OFF, BASE_IA_ON, BASE_CONSOLA, BASE_BOTON,   // v63: bloques de la base lunar
                      NTIPOS };
constexpr int ID_SPRAY = 250;                        // herramienta (no es un bloque)
constexpr int ID_RADAR = 252;                        // objeto (no es un bloque): radar de senal (v63)
constexpr int ID_PISTOLA = 253;                      // objeto: pistola de plasma (v67)
constexpr int ID_POLVORA = 251;                      // objeto (no es un bloque): se fabrica con salitre, carbon y azufre
static inline bool esPiezaVehiculo(uint8_t t) { return t >= CARROCERIA && t <= CARR_11; }
static const int NTIPOS_MENU = ANTORCHA + 1 + 2 + 12 + 1 + 7 + 1 + 1 + 1 + (NTIPOS - NIEVE);       // los que salen en el menu de materiales
static uint32_t g_semilla = 0;                      // semilla del mundo (0 = mundo clasico)
static inline bool marProfundo() { return (g_semilla >> 24) & 1u; }   // el bit 24 de la semilla activa los mares profundos (se decide al crear el mundo; viaja con la semilla en la red)
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
    if (i == ANTORCHA + 24) return ID_RADAR;
    if (i == ANTORCHA + 25) return ID_PISTOLA;
    return NIEVE + (i - (ANTORCHA + 26));
}
static inline bool esHoja(uint8_t t) { return t == HOJAS || (t >= HOJA_ROJA && t <= HOJA_PINO) || t == HOJA_SELVA || t == HOJA_NIEVE; }
static inline bool esPlanta(uint8_t t) { return t == LIANA || (t >= FLOR_ROJA && t <= FLOR_ROSA) || t == ALGA; }      // objetos delgados: se atraviesan y dejan pasar la luz
static inline bool esVeg(uint8_t t) { return t == MADERA || t == CACTUS || esHoja(t) || t == MADERA_SELVA || t == BAMBU || t == ARBUSTO_BAYAS || esPlanta(t); }
static inline bool esLava(uint8_t t) { return t == LAVA; }
static inline bool esAgua(uint8_t t) { return t == AGUA || (t >= AGUA_F1 && t <= AGUA_F8); }
static inline int nivelAgua(uint8_t t) { return t == AGUA ? 9 : (t >= AGUA_F1 && t <= AGUA_F8) ? t - AGUA_F1 + 1 : 0; }
static inline bool esSolido(uint8_t t) { return t != AIRE && !esAgua(t) && t != LAVA && t != ANTORCHA && !esPlanta(t); }   // colisiona
static inline bool esRompible(uint8_t t) { return t != AIRE && !esAgua(t) && t != LAVA; }                  // se puede apuntar/romper

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
enum Bio { BIO_PRADERA = 0, BIO_BOSQUE, BIO_DENSO, BIO_DESIERTO, BIO_MESETA, BIO_PANTANO, BIO_TUNDRA, BIO_SABANA, BIO_OTONO, BIO_PICOS, BIO_CEREZO, BIO_CANON, BIO_SELVA, BIO_TAIGA, BIO_FLORES, BIO_GLACIAR, BIO_PLAYA, BIO_OCEANO, BIO_N };
constexpr int BIO_PLANO = BIO_PLAYA;       // los primeros BIO_PLANO biomas salen del plano temperatura x humedad; playa y oceano, de la continentalidad
static inline float acota01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
// continentalidad: ruido muy grande; por debajo de CO_UMBRAL es oceano, justo encima es playa
constexpr float CO_UMBRAL = 0.345f;
static inline float continental(double bx, double bz) { bx *= KS; bz *= KS; return fbm((float)bx * 0.0011f, (float)bz * 0.0011f, 3, 97) - CO_UMBRAL; }
static inline float temp01(double bx, double bz) { bx *= KS; bz *= KS; return acota01((fbm((float)bx * 0.0022f, (float)bz * 0.0022f, 2, 61) - 0.24f) / 0.52f); }   // 0 = polar, 1 = tropical
static int biomaPesos(double bx0, double bz0, float* w) {      // devuelve el bioma dominante
    const double bx = bx0 * KS, bz = bz0 * KS;
    float T = fbm((float)bx * 0.0022f, (float)bz * 0.0022f, 2, 61);
    float Hm = fbm((float)bx * 0.0026f + 500.0f, (float)bz * 0.0026f, 2, 71);
    float t = acota01((T - 0.24f) / 0.52f), h = acota01((Hm - 0.24f) / 0.52f);
    //                                   prad.         bosque        denso         desierto      meseta        pantano       tundra        sabana        otono         picos         cerezo        canon         selva         taiga         flores        glaciar
    static const float C[BIO_PLANO][2] = {{0.44f, 0.30f}, {0.36f, 0.68f}, {0.70f, 0.84f}, {0.86f, 0.10f}, {0.76f, 0.40f}, {0.56f, 0.97f}, {0.05f, 0.28f}, {0.62f, 0.17f}, {0.14f, 0.66f}, {0.17f, 0.46f}, {0.29f, 0.50f}, {0.92f, 0.28f}, {0.96f, 0.74f}, {0.07f, 0.88f}, {0.54f, 0.56f}, {0.04f, 0.12f}};
    static const float SESGO[BIO_PLANO] = {1.1f, 1.0f, 0.8f, 3.0f, 0.9f, 0.9f, 1.5f, 2.0f, 1.4f, 1.3f, 1.2f, 1.6f, 1.6f, 1.7f, 1.2f, 3.2f};   // para repartir las superficies
    float tot = 0; int dom = 0;
    for (int b = 0; b < BIO_PLANO; b++) {
        float dt = t - C[b][0], dh = h - C[b][1];
        w[b] = SESGO[b] * std::exp(-(dt * dt + dh * dh) / 0.035f);
        tot += w[b];
    }
    for (int b = 0; b < BIO_PLANO; b++) w[b] /= tot;
    // costa: oceano (cc < 0) y playa (0 <= cc < 0.05) tapan a los demas biomas
    float cc = continental(bx0, bz0);
    w[BIO_PLAYA] = 0; w[BIO_OCEANO] = 0;
    if (cc < 0.03f) {
        float wc = cc < 0 ? 1.0f : 1.0f - (float)suave(0.018, 0.03, cc);
        for (int b = 0; b < BIO_PLANO; b++) w[b] *= 1.0f - wc;
        w[cc < 0 ? BIO_OCEANO : BIO_PLAYA] = wc;
    }
    for (int b = 0; b < BIO_N; b++) if (w[b] > w[dom]) dom = b;
    return dom;
}
// cuanto desierto hay en ese punto (0 = nada, 1 = desierto completo)
static double desierto(double bx, double bz) { float w[BIO_N]; biomaPesos(bx, bz, w); return w[BIO_DESIERTO]; }
static int biomaDominante(double bx, double bz) { float w[BIO_N]; return biomaPesos(bx, bz, w); }


// ---------------------------------------------------------------- v59: mundo de la Luna (sin atmosfera, llanura de aterrizaje en (0,0), crateres alrededor)
static bool g_modoLuna = false;
#include "base.h"
static double alturaLuna(double bx, double bz) {
    const double d0 = std::sqrt(bx * bx + bz * bz);
    const double plano = suave(70.0, 230.0, d0);               // 0 = llanura de aterrizaje
    double h = (fbm((float)bx * 0.005f, (float)bz * 0.005f, 3, 501) - 0.5) * 24.0 + (fbm((float)bx * 0.03f, (float)bz * 0.03f, 2, 502) - 0.5) * 3.0;
    h *= plano;
    struct Esc { double S, rmin, rmax; uint32_t seed; double prob; };
    static const Esc E[3] = {{190.0, 18.0, 80.0, 510u, 0.65}, {58.0, 4.5, 20.0, 520u, 0.60}, {17.0, 1.4, 5.5, 530u, 0.55}};
    double cr = 0;
    for (const Esc& e : E) {
        const int ci = (int)std::floor(bx / e.S), cj = (int)std::floor(bz / e.S);
        for (int j = cj - 1; j <= cj + 1; j++)
            for (int i = ci - 1; i <= ci + 1; i++) {
                if (rnd01(hash2(i, j, e.seed)) > e.prob) continue;
                const double cx = (i + 0.15 + 0.7 * rnd01(hash2(i, j, e.seed + 1))) * e.S, cz = (j + 0.15 + 0.7 * rnd01(hash2(i, j, e.seed + 2))) * e.S;
                const double q = rnd01(hash2(i, j, e.seed + 3));
                const double r = e.rmin + (e.rmax - e.rmin) * q * q;
                const double dx = bx - cx, dz = bz - cz, u = std::sqrt(dx * dx + dz * dz) / r;
                if (u > 1.9) continue;
                if (u < 1.0) cr -= 0.30 * r * (1.0 - u * u) * (1.0 - 0.25 * u);                    // cuenco
                const double g = (u - 1.0) / 0.22;
                cr += 0.075 * r * std::exp(-g * g);                                                  // borde elevado
                if (u > 1.0) cr += 0.03 * r * std::exp(-(u - 1.0) * (u - 1.0) / 0.25);              // eyecta
            }
    }
    double hh = LUNA_Y0 + h + cr * plano;
    {   // la base: explanada plana delante de la puerta y una colina grande encima del bunker
        const double db = std::sqrt((bx - BASE_X) * (bx - BASE_X) + (bz - (BASE_Z + 6.0)) * (bz - (BASE_Z + 6.0)));
        const double wb = suave(30.0, 90.0, db);
        hh = LUNA_Y0 + (hh - LUNA_Y0) * wb;
        const double dm = std::sqrt((bx - BASE_X) * (bx - BASE_X) + (bz - (BASE_Z - 36.0)) * (bz - (BASE_Z - 36.0)));
        hh += 18.0 * (1.0 - suave(14.0, 48.0, dm));
        const double ds = std::sqrt((bx - BASE_X) * (bx - BASE_X) + (bz - (BASE_Z - 170.0)) * (bz - (BASE_Z - 170.0)));   // silo vertical (v64)
        hh = LUNA_Y0 + (hh - LUNA_Y0) * suave(36.0, 80.0, ds);
        hh += 10.0 * (1.0 - suave(14.0, 42.0, ds));
        const double dr = std::sqrt((bx - (BASE_X - 62.0)) * (bx - (BASE_X - 62.0)) + (bz - (BASE_Z - 170.0)) * (bz - (BASE_Z - 170.0)));   // pozo del cohete (v70)
        hh = LUNA_Y0 + (hh - LUNA_Y0) * suave(40.0, 74.0, dr);
        const double da = std::sqrt((bx - BASE_X) * (bx - BASE_X) + (bz - (BASE_Z - 222.0)) * (bz - (BASE_Z - 222.0)));   // sala de la IA (v67)
        hh = LUNA_Y0 + (hh - LUNA_Y0) * suave(52.0, 100.0, da);
        hh += 6.0 * (1.0 - suave(16.0, 56.0, da));
    }
    return hh;
}

// altura del terreno en bloques (bx, bz en bloques): colinas, montanas, lagunas, rios y desiertos
static double terrenoBase(double bx, double bz, int* domOut);
static double alturaTerreno(double bx, double bz) { return terrenoBase(bx, bz, nullptr); }
static double terrenoBase(double bx, double bz, int* domOut) {
    if (g_modoLuna) { if (domOut) *domOut = 0; return alturaLuna(bx, bz); }
    const double WL0 = WATER_LEVEL - OFFY;
    const double bxr = bx, bzr = bz;
    float w[BIO_N]; int dom = biomaPesos(bxr, bzr, w);
    bx *= KS; bz *= KS;
    if (domOut) *domOut = dom;
    float base = fbm((float)bx * 0.004f, (float)bz * 0.004f, 3, 1);
    float colinas = fbm((float)bx * 0.02f, (float)bz * 0.02f, 4, 2);
    float monte = fbm((float)bx * 0.008f, (float)bz * 0.008f, 4, 3);
    monte = std::max(0.0f, (monte - 0.5f) * 2.0f);
    const float wD = w[BIO_DESIERTO], wM = w[BIO_MESETA], wP = w[BIO_PANTANO];
    float hillMul = w[BIO_PRADERA] * 0.55f + w[BIO_BOSQUE] * 1.0f + w[BIO_DENSO] * 0.85f + wD * 0.45f + wM * 0.7f + wP * 0.2f +
                    w[BIO_TUNDRA] * 0.85f + w[BIO_SABANA] * 0.40f + w[BIO_OTONO] * 0.95f + w[BIO_PICOS] * 1.2f + w[BIO_CEREZO] * 0.8f + w[BIO_CANON] * 0.6f +
                    w[BIO_SELVA] * 1.1f + w[BIO_TAIGA] * 0.9f + w[BIO_FLORES] * 0.45f + w[BIO_GLACIAR] * 0.35f;
    float monteMul = 1.0f - 0.95f * wD - 0.75f * wP - 0.3f * wM - 0.5f * w[BIO_SABANA] - 0.4f * w[BIO_CANON] + 0.6f * w[BIO_PICOS] - 0.6f * w[BIO_GLACIAR] - 0.3f * w[BIO_FLORES];
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
    if (w[BIO_PICOS] > 0.02f) {   // montanas nevadas: picos mas altos y escarpados
        float rg = 1.0f - std::fabs(2.0f * fbm((float)bx * 0.010f, (float)bz * 0.010f, 4, 41) - 1.0f);
        h += w[BIO_PICOS] * (10.0 + 34.0 * rg * rg);
    }
    const double wC = w[BIO_CANON];
    if (wC > 0.02) {   // canon: meseta de escalones cortada por canones profundos
        float rc = 1.0f - std::fabs(2.0f * fbm((float)bx * 0.009f, (float)bz * 0.009f, 3, 43) - 1.0f);
        double x = (h - 14.0) / 6.0, fl = std::floor(x), fr = x - fl;
        double st = 14.0 + 6.0 * (fl + suave(0.85, 1.0, fr)) + 10.0;
        st -= 24.0 * suave(0.80, 0.93, rc);
        h = h * (1.0 - wC) + st * wC;
    }
    if (w[BIO_GLACIAR] > 0.02f) {   // mundo helado: llanura de hielo casi al nivel del mar con icebergs altos
        const double wG = w[BIO_GLACIAR];
        float fino = fbm((float)bx * 0.03f, (float)bz * 0.03f, 3, 84);
        double hg = WL0 + 2.4 + (fino - 0.5) * 3.0;
        h = h * (1.0 - 0.85 * wG) + hg * 0.85 * wG;
        double ib = suave(0.60, 0.72, fbm((float)bx * 0.018f, (float)bz * 0.018f, 3, 47));
        h += wG * ib * (7.0 + 18.0 * fbm((float)bx * 0.06f, (float)bz * 0.06f, 3, 48));
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

    {   // costa: playa suave y oceano (poco profundo cerca de la orilla, algo mas hondo mar adentro) con islas
        double cc = continental(bxr, bzr);
        if (cc < 0.05) {
            float fino = fbm((float)bx * 0.03f, (float)bz * 0.03f, 3, 85);
            double hcosta;
            if (cc >= 0) hcosta = WL0 + 1.4 + (fino - 0.5) * 1.2;
            else {
                double u = -cc;
                double prof = 4.4 * suave(0.0, 0.02, u) + 9.0 * suave(0.015, 0.08, u) + 8.0 * suave(0.08, 0.15, u)
                             + (marProfundo() ? 34.0 * suave(0.07, 0.17, u) * (0.75 + 0.25 * fino) : 0.0);   // mar profundo: hasta ~50 bloques mar adentro
                hcosta = WL0 + 1.4 - prof + (fino - 0.5) * 2.0 * suave(0.02, 0.07, u);
                double isl = suave(0.60, 0.68, fbm((float)bx * 0.010f, (float)bz * 0.010f, 3, 99)) * suave(0.03, 0.07, u);
                hcosta = hcosta * (1.0 - isl) + (WL0 + 2.6 + 5.0 * fino) * isl;
                double fria = 1.0 - suave(0.10, 0.20, temp01(bxr, bzr));      // mar polar: icebergs
                if (fria > 0.0) {
                    double ib = suave(0.62, 0.72, fbm((float)bx * 0.020f, (float)bz * 0.020f, 3, 49)) * suave(0.02, 0.06, u) * fria;
                    if (ib > 0.0) hcosta = std::max(hcosta, hcosta * (1.0 - ib) + (WL0 + 1.0 + 15.0 * fbm((float)bx * 0.06f, (float)bz * 0.06f, 3, 50)) * ib);
                }
            }
            double L = suave(0.02, 0.05, cc);
            h = hcosta * (1.0 - L) + h * L;
        }
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
    if (dom == BIO_OCEANO && h > WL0 + 0.4 && temp01(bx, bz) < 0.2f) return h > WL0 + 2.5 ? HIELO_COMPACTO : NIEVE;     // islas y icebergs del mar polar
    if (dom == BIO_GLACIAR) return (h < WL0 + 0.2 || h > WL0 + 6.5) ? HIELO_COMPACTO : NIEVE;
    if (dom == BIO_OCEANO) {   // fondo marino con variedad: arena clara en lo somero, grava y arcilla en manchas, roca y grava oscura en lo hondo
        if (h >= WL0 + 2.4) return PASTO;
        const double d = WL0 - h;                                                // profundidad (bloques)
        const float n1 = vnoise((float)(bx * 0.045), (float)(bz * 0.045), 121u), n2 = vnoise((float)(bx * 0.09), (float)(bz * 0.09), 122u);
        if (d < 2.4) return arc ? ARCILLA : ARENA;
        if (d < 13.0) return n1 > 0.62f ? GRAVA : (arc ? ARCILLA : (n2 > 0.80f ? PIEDRA : ARENA));
        if (d < 24.0) return n1 > 0.50f ? GRAVA : (n2 > 0.55f ? ARCILLA : ARENA);
        return n2 > 0.72f ? ARCILLA : (n1 > 0.42f ? GRAVA : PIEDRA);                       // mar profundo: oscuro y rocoso
    }
    if (dom == BIO_PLAYA) return ARENA;
    if (dom == BIO_PICOS) return h > 38.0 + 6.0 * vnoise((float)(bx * 0.06), (float)(bz * 0.06), 94u) ? (vnoise((float)(bx * 0.09), (float)(bz * 0.09), 95u) > 0.72f ? PIEDRA : NIEVE)
                                                                                                    : (vnoise((float)(bx * 0.045), (float)(bz * 0.045), 96u) > 0.40f ? NIEVE : PASTO);
    if (dom == BIO_CANON) return h < WL0 + 2.4 ? ARENA : (vnoise((float)(bx * 0.05), (float)(bz * 0.05), 97u) > 0.5f ? ARCILLA : TIERRA);
    if (dom == BIO_TUNDRA) return h < WL0 + 0.2 ? (arc ? ARCILLA : ARENA) : NIEVE;
    if (dom == BIO_DESIERTO || h < WL0 + 2.4) return arc ? ARCILLA : ARENA;
    if (dom == BIO_PANTANO) return h < WL0 + 0.4 ? ARCILLA : (vnoise((float)(bx * 0.06), (float)(bz * 0.06), 93u) > 0.55f ? PASTO : TIERRA);
    if (dom == BIO_TAIGA) return vnoise((float)(bx * 0.05), (float)(bz * 0.05), 98u) > 0.30f ? NIEVE : PASTO;
    if (dom == BIO_MESETA) return TIERRA;
    if (dom == BIO_SABANA) return PASTO_SECO;
    return PASTO;
}

// vegetacion: tipo de arbol (0 roble, 1-3 otono rojo/naranja/amarillo, 4 cerezo, 5 pino, 6 acacia, 7 cactus, 8 selva enorme, 9 pino nevado, 10 bambu, 11 arbusto de bayas)
static inline bool vegetacion(int tx, int tz, double* altura, int* clase) {
    if (hash2(tx, tz, 77) % 18 != 0) return false;                 // candidatos (1 de cada 18 bloques como maximo)
    float w[BIO_N]; int dom = biomaPesos(tx + 0.5, tz + 0.5, w);
    //                         prad.      bosque     denso      desierto    meseta  pantano    tundra     sabana     otono
    static const float D[BIO_N] = {1.0f / 200, 1.0f / 40, 1.0f / 18, 1.0f / 230, 0.0f, 1.0f / 55, 1.0f / 45, 1.0f / 85, 1.0f / 22, 1.0f / 30, 1.0f / 20, 1.0f / 400, 1.0f / 14, 1.0f / 20, 1.0f / 150, 0.0f, 0.0f, 0.0f};   // arboles por bloque
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
        case BIO_PICOS: k = 5; break;
        case BIO_CEREZO: k = q < 85 ? 4 : 0; break;
        case BIO_SELVA: k = q < 45 ? 8 : 10; break;
        case BIO_TAIGA: k = q < 75 ? 9 : 11; break;
        case BIO_FLORES: k = q < 6 ? 4 : 0; break;
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
static inline int altSelva(int tx, int tz) { return 14 + (int)(hash2(tx, tz, 83) % 5); }      // altura del tronco (bloques) del arbol de selva
static inline bool hayArbol(int tx, int tz, double* altura) { return vegetacion(tx, tz, altura, nullptr); }

static inline int divPiso(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

// ---------------------------------------------------------------- minerales (vetas)
// El subsuelo se divide en celdas de VETA_CELDA bloques; cada celda puede tener una veta (una mancha irregular
// de mineral) cuyo tipo depende de la profundidad. La veta siempre queda dentro de su propia celda.
constexpr int VETA_CELDA = 12;
constexpr double LAVA_Y = OFFY - 25.0;       // por debajo de esta altura (bloques) el hueco de las cuevas se llena de lava
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


static DatosChunk generarChunkLuna(int cx, int cy, int cz) {
    DatosChunk r;
    const int ox = cx * CS, oy = cy * CS, oz = cz * CS;
    int top[CS * CS];
    int minTop = INT_MAX, maxTop = INT_MIN;
    for (int lz = 0; lz < CS; lz++)
        for (int lx = 0; lx < CS; lx++) {
            int t = (int)std::floor(alturaLuna((ox + lx + 0.5) / SUB, (oz + lz + 0.5) / SUB) * SUB);
            top[lz * CS + lx] = t; minTop = std::min(minTop, t); maxTop = std::max(maxTop, t);
        }
    const bool conBase = baseToca(ox, oy, oz);
    if (!conBase && oy > maxTop) { r.uni = AIRE; return r; }
    if (!conBase && oy + CS - 1 <= minTop - 4 * SUB) { r.uni = PIEDRA; return r; }
    auto v = std::make_shared<std::vector<uint8_t>>((size_t)CS * CS * CS, (uint8_t)AIRE);
    uint8_t* dt = v->data();
    for (int lz = 0; lz < CS; lz++)
        for (int lx = 0; lx < CS; lx++) {
            const int t = top[lz * CS + lx];
            const double bx = (ox + lx + 0.5) / SUB, bz = (oz + lz + 0.5) / SUB;
            const bool oscuro = vnoise((float)(bx * 0.045), (float)(bz * 0.045), 540u) > 0.60f;     // manchas de roca mas oscura
            for (int ly = 0; ly < CS; ly++) {
                const int y = oy + ly;
                if (y > t) break;
                uint8_t b = PIEDRA;
                if (y > t - 3 * SUB) {
                    const uint32_t hq = hash2(ox + lx, oz + lz + y * 7919, 541u) % 100;
                    b = (oscuro ? hq < 94 : hq < 78) ? PIEDRA : GRAVA;
                }
                dt[IDX(lx, ly, lz)] = b;
            }
        }
    if (conBase) baseAplicar(dt, ox, oy, oz);
    uint8_t p = dt[0];
    bool igual = true;
    for (size_t i = 1; i < v->size(); i++) if (dt[i] != p) { igual = false; break; }
    if (igual) { r.uni = p; return r; }
    r.d = v;
    return r;
}

// Genera un chunk (cx, cy, cz son indices de chunk)
static DatosChunk generarChunk(int cx, int cy, int cz) {
    if (g_modoLuna) return generarChunkLuna(cx, cy, cz);
    DatosChunk r;
    const int ox = cx * CS, oy = cy * CS, oz = cz * CS;
    static thread_local int top[CS * CS];
    static thread_local float hb[CS * CS];
    static thread_local uint8_t bioCol[CS * CS];
    static thread_local bool arcillaCol[CS * CS];
    static thread_local bool entradaCol[CS * CS];
    static thread_local bool heladoCol[CS * CS];
    struct Arb { int tx, tz, kv; double ha; };
    static thread_local std::vector<Arb> arbs;
    arbs.clear();
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
            heladoCol[lz * CS + lx] = dom == BIO_TUNDRA || dom == BIO_GLACIAR || (dom == BIO_OCEANO && temp01((ox + lx + 0.5) / SUB, (oz + lz + 0.5) / SUB) < 0.2f);
            int t = (int)std::floor(h * SUB);
            top[lz * CS + lx] = t;
            minTop = std::min(minTop, t);
            maxTop = std::max(maxTop, t);
        }
    // caminos rapidos: chunks de puro aire o pura piedra
    auto cargarArboles = [&]() {
        const int a0 = divPiso(ox, SUB) - 5, a1 = divPiso(ox + CS - 1, SUB) + 5, c0 = divPiso(oz, SUB) - 5, c1 = divPiso(oz + CS - 1, SUB) + 5;
        for (int tz = c0; tz <= c1; tz++)
            for (int tx = a0; tx <= a1; tx++) {
                double ha; int kv;
                if (vegetacion(tx, tz, &ha, &kv)) arbs.push_back({tx, tz, kv, ha});
            }
    };
    if (oy >= std::max(maxTop + 10 * SUB, WATER_LEVEL * SUB)) {
        bool alto = false;                      // los arboles de selva (hasta ~21 bloques) pueden asomar por encima
        if (oy < maxTop + 44 * SUB) {
            cargarArboles();
            for (const Arb& a : arbs) if (a.kv == 8 && (int)std::floor(a.ha * SUB) - 1 + (altSelva(a.tx, a.tz) + 4) * SUB > oy) { alto = true; break; }
        }
        if (!alto) { r.uni = AIRE; return r; }
    }
    if (arbs.empty() && oy < maxTop + 44 * SUB) cargarArboles();
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
            const bool helado = heladoCol[lz * CS + lx];
            int florUmbral = 0;                  // % de cubitos de pasto con una flor encima
            if (sup == PASTO && t + 1 >= WATER_LEVEL * SUB) florUmbral = dom == BIO_FLORES ? 34 : dom == BIO_PRADERA ? 3 : dom == BIO_CEREZO ? 4 : dom == BIO_BOSQUE ? 1 : 0;
            // ---- vida marina: arrecifes de coral (mares calidos), bosques de algas y pasto marino
            uint8_t decoT = AIRE; int decoH = 0, decoModo = 0;
            if (!helado && sup != PASTO && sup != NIEVE && t + 2 * SUB < WATER_LEVEL * SUB) {
                const double dbl = (double)(WATER_LEVEL * SUB - 1 - t) / SUB;             // bloques de agua sobre el fondo
                const int bxx = divPiso(ox + lx, SUB), bzz = divPiso(oz + lz, SUB);
                const uint32_t hq = hash2(bxx, bzz, 150);
                const bool arenoso = sup == ARENA || sup == GRAVA || sup == ARCILLA;
                if (dom == BIO_OCEANO && dbl >= 2.0 && dbl <= 20.0 && arenoso && temp01((ox + lx + 0.5) / SUB, (oz + lz + 0.5) / SUB) >= 0.42f &&
                    vnoise((float)(bxx * 0.045), (float)(bzz * 0.045), 141u) > 0.58f) {
                    if (hq % 100 < 38) {   // colonia de coral: columnas de 1 a 3 bloques; el color cambia por manchas
                        int ci = (int)(vnoise((float)(bxx * 0.11), (float)(bzz * 0.11), 142u) * 5.999f);
                        if (((hq >> 12) % 6) == 0) ci = (int)((hq >> 16) % 6);
                        decoT = (uint8_t)(CORAL_ROJO + ci); decoH = (1 + (int)((hq >> 8) % 3)) * SUB; decoModo = 1;
                    }
                } else if (arenoso && dbl >= 3.0 && dbl <= 32.0 && vnoise((float)(bxx * 0.06), (float)(bzz * 0.06), 143u) > 0.54f && hq % 100 < 34) {
                    int alto = 2 + (int)((hq >> 8) % 7);                                      // alga alta (kelp): 2 a 8 bloques
                    alto = std::min(alto, (int)dbl - 1);
                    if (alto >= 1 && lx % SUB == 0 && lz % SUB == 0) { decoT = ALGA; decoH = alto * SUB; decoModo = 2; }
                } else if (arenoso && dbl >= 1.2 && dbl <= 10.0 && hq % 100 < 14 && lx % SUB == 0 && lz % SUB == 0) {
                    decoT = ALGA; decoH = SUB; decoModo = 2;                                  // pasto marino corto
                }
            }
            uint8_t flor = AIRE;
            if (florUmbral) {
                uint32_t hf = hash2(ox + lx, oz + lz, 130);
                if ((int)(hf % 100) < florUmbral) {
                    int c = (hf >> 12) % 100 < 70 ? (int)(vnoise((float)((ox + lx) * 0.07), (float)((oz + lz) * 0.07), 131u) * 3.999f) : (int)((hf >> 20) & 3);
                    flor = (uint8_t)(FLOR_ROJA + (c & 3));
                }
            }
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
                    else if (dom == BIO_CANON && sup != PIEDRA && prof < 40) {           // canon: franjas anchas de arcilla, tierra y arena
                        static const uint8_t B[8] = {ARCILLA, ARCILLA, TIERRA, ARENA, ARENA, TIERRA, ARCILLA, ARENA};
                        tp = B[(((vy / SUB) % 8) + 8) % 8];
                    }
                    else if (sup == HIELO_COMPACTO && prof < 60) tp = HIELO_COMPACTO;     // icebergs macizos
                    else if (dom == BIO_GLACIAR && sup == NIEVE && prof < 30) tp = prof < 2 ? NIEVE : HIELO_COMPACTO;
                    else if (prof < 4) tp = (sup == PASTO || sup == PASTO_SECO || (sup == NIEVE && (dom == BIO_TAIGA || (dom == BIO_TUNDRA && hb[lz * CS + lx] < 52.0f)))) ? TIERRA : (sup == NIEVE ? PIEDRA : (sup == ARCILLA && prof >= 2 ? ARENA : sup));
                    else tp = PIEDRA;
                } else if (vy < WATER_LEVEL * SUB) tp = (helado && vy >= (WATER_LEVEL - (dom == BIO_GLACIAR ? 2 : 1)) * SUB) ? HIELO : ((decoT != AIRE && vy - t <= decoH) ? decoT : (uint8_t)AGUA);     // agua helada en la tundra, el mundo helado y el mar polar; vida marina sobre el fondo
                else tp = (vy == t + 1) ? flor : (uint8_t)AIRE;
                dt[IDX(lx, ly, lz)] = tp;
            }
            // vegetacion cercana (tronco 1 bloque de ancho; copas segun el tipo de arbol)
            const int bx = divPiso(ox + lx, SUB), bz = divPiso(oz + lz, SUB);
            for (const Arb& a : arbs) {
                const int tx = a.tx, tz = a.tz, kv = a.kv;
                const int R = kv == 8 ? 5 : kv == 10 ? 2 : (int)std::ceil(3 * TS);
                if (bx < tx - R || bx > tx + R || bz < tz - R || bz > tz + R) continue;
                const double ha = a.ha;
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
                if (kv == 8) {   // selva: tronco de 2x2 bloques, copa enorme y lianas colgando
                    const int H = altSelva(tx, tz);
                    const double cx2 = (ox + lx + 0.5) * VS - (tx + 1.0), cz2 = (oz + lz + 0.5) * VS - (tz + 1.0);
                    const double d2 = cx2 * cx2 + cz2 * cz2;
                    const bool tronco = bx >= tx && bx <= tx + 1 && bz >= tz && bz <= tz + 1;
                    const int gx = ox + lx, gz = oz + lz;
                    const bool liana = !tronco && d2 > 6.0 && d2 < 16.0 && hash2(gx, gz, 85) % 3 == 0;
                    const int topeL = vb + (H - 1) * SUB, largoL = 8 + (int)(hash2(gx, gz, 86) % 13);
                    for (int ly = 0; ly < CS; ly++) {
                        int vy = oy + ly;
                        uint8_t& c = dt[IDX(lx, ly, lz)];
                        if (tronco && vy >= vb && vy < vb + H * SUB) { c = MADERA_SELVA; continue; }
                        double yb = (vy + 0.5) * VS - vb * VS, dy = yb - (H - 1.0);
                        if (c == AIRE && d2 + dy * dy * 3.5 <= 19.0) { c = HOJA_SELVA; continue; }
                        if (liana && c == AIRE && vy <= topeL && vy > topeL - largoL) c = LIANA;
                    }
                    continue;
                }
                if (kv == 10) {  // bambu: 2-3 tallos de un cubito de ancho
                    const int n = 2 + (int)(hash2(tx, tz, 84) % 2);
                    for (int i = 0; i < n; i++) {
                        const int cx = tx * SUB + (int)(hash2(tx, tz, 90 + i) % (3 * SUB)) - SUB / 2 - 1, cz = tz * SUB + (int)(hash2(tx, tz, 95 + i) % (3 * SUB)) - SUB / 2 - 1;
                        if (ox + lx != cx || oz + lz != cz) continue;
                        const int alto = (5 + (int)(hash2(tx, tz, 100 + i) % 5)) * SUB;
                        for (int ly = 0; ly < CS; ly++) { int vy = oy + ly; if (vy >= vb && vy < vb + alto) dt[IDX(lx, ly, lz)] = BAMBU; }
                    }
                    continue;
                }
                if (kv == 11) {  // arbusto de bayas: bloque de 2x2 cubitos y 3 de alto
                    if (tx == bx && tz == bz) for (int ly = 0; ly < CS; ly++) { int vy = oy + ly; if (vy >= vb + 2 && vy < vb + 5 && dt[IDX(lx, ly, lz)] == AIRE) dt[IDX(lx, ly, lz)] = ARBUSTO_BAYAS; }
                    continue;
                }
                static const uint8_t HT[10] = {HOJAS, HOJA_ROJA, HOJA_NARANJA, HOJA_AMARILLA, HOJA_ROSA, HOJA_PINO, HOJAS, HOJAS, HOJAS, HOJA_PINO};
                const uint8_t hoja = HT[kv];
                const int bTronco0 = (kv == 5 || kv == 9) ? 7 : kv == 6 ? 5 : 6;
                const int bTronco = (int)std::lround(bTronco0 * TS);
                if (tx == bx && tz == bz) {
                    for (int ly = 0; ly < CS; ly++) {
                        int vy = oy + ly;
                        if (vy >= vb && vy < vb + bTronco * SUB) dt[IDX(lx, ly, lz)] = MADERA;
                    }
                }
                if (dxz > 7.5 * TS * TS) continue;
                for (int ly = 0; ly < CS; ly++) {
                    int vy = oy + ly;
                    double yb = ((vy + 0.5) * VS - vb * VS) / TS;      // altura sobre la base del tronco, en bloques (sin escala)
                    const double dxs = dxz / (TS * TS);
                    bool leaf; uint8_t hj = hoja;
                    if (kv == 5 || kv == 9) {            // pino: cono (el de la taiga lleva nieve encima en franjas)
                        double rr = 2.4 * (9.6 - yb) / 7.6 + 0.2;
                        leaf = yb >= 2.0 && yb <= 9.6 && dxs <= rr * rr;
                        if (leaf && kv == 9 && (yb > 8.4 || (((vy % 4) + 4) % 4 < 2 && dxs > (rr - 1.15) * (rr - 1.15)))) hj = HOJA_NIEVE;
                    } else if (kv == 6) {     // acacia: sombrilla plana y ancha
                        leaf = (yb >= 4.4 && yb < 5.3 && dxs <= 7.2) || (yb >= 5.3 && yb < 6.2 && dxs <= 3.4);
                    } else {                  // redonda (roble, otono, cerezo)
                        double dy = yb - (bTronco0 - 1.0);
                        leaf = dxs + dy * dy * 1.6 <= 7.2;
                    }
                    if (leaf && dt[IDX(lx, ly, lz)] == AIRE) dt[IDX(lx, ly, lz)] = hj;
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
        auto cuevaEn = [&](int lx, int ly, int lz) -> bool {       // ¿el hueco de cueva (tunel/sala) abre esta celda?
            int cx_ = lx / G, cy_ = ly / G, cz_ = lz / G;
            float fx = (lx + 0.5f) / G - cx_, fy = (ly + 0.5f) / G - cy_, fz = (lz + 0.5f) / G - cz_;
            float n1 = mezcla(f1, cx_, cy_, cz_, fx, fy, fz) - 0.5f, n2 = mezcla(f2, cx_, cy_, cz_, fx, fy, fz) - 0.5f;
            bool ab = n1 * n1 + n2 * n2 < 0.0070f;
            if (!ab && (oy + ly) * VS < 46.0 + OFFY) ab = mezcla(f3, cx_, cy_, cz_, fx, fy, fz) > 1.02f;
            return ab;
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
                bool abre = cuevaEn(lx, ly, lz);
                if (abre) { c = AIRE; continue; }
                // ---- LAVA (como Minecraft): lagunas planas de 1 bloque de grosor, rebajadas en el suelo, con borde solido ----
                double prof = (double)(tcol - vy) * VS;                  // profundidad bajo la superficie (bloques)
                if (prof < 15.0 || by >= LAVA_Y) continue;               // 0% en los primeros 15 bloques
                const float xx = (float)((ox + lx) * VS), zz = (float)((oz + lz) * VS);
                bool lava = false;
                // (A) laguna en el suelo de una cueva: sustituye la capa superior del piso donde este es medianamente plano
                {
                    auto pisoD = [&](int ax, int ay, int az) -> int {    // 1..SUB si (ax,ay,az) es solido y tiene aire de cueva justo encima; 0 si no; -1 si no se sabe
                        if (ax < 0 || ax >= CS || az < 0 || az >= CS || ay < 0 || ay + SUB >= CS) return -1;
                        if (cuevaEn(ax, ay, az)) return 0;
                        for (int j = 1; j <= SUB; j++) if (cuevaEn(ax, ay + j, az)) return j;
                        return 0;
                    };
                    int d = pisoD(lx, ly, lz);
                    if (d >= 1) {
                        float nl = vnoise3(xx * 0.031f, 3.0f, zz * 0.031f, 310u);       // nivel de profundidad de la zona
                        double L = 80.0 * std::pow((double)nl, 1.4);                  // 0..80 (sesgado a lo hondo)
                        bool zona = false;
                        if (by < L) {
                            float nz = vnoise3(xx * 0.083f, 9.0f, zz * 0.083f, 311u);   // manchas de ~10 bloques
                            zona = nz > 0.84 - 0.35 * (1.0 - L / 80.0);               // mas probable cuanto mas hondo
                        }
                        if (!zona && by < 78.0) zona = vnoise3(xx * 0.083f, 9.0f, zz * 0.083f, 312u) > 0.978f;   // base ~0.1% hasta y=78
                        if (zona) {
                            static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
                            int iguales = 0;
                            for (int q = 0; q < 4; q++) { int dn = pisoD(lx + DX[q], ly, lz + DZ[q]); if (dn == -1 || dn == d) iguales++; }
                            int anchos = 0;                                               // ancho minimo ~3 bloques: a 1 bloque de distancia tambien hay piso (no pared)
                            for (int q = 0; q < 4; q++) { int dn = pisoD(lx + DX[q] * SUB, ly, lz + DZ[q] * SUB); if (dn == -1 || dn >= 1) anchos++; }
                            lava = iguales >= (SUB > 1 ? 2 : 3) && anchos >= 4;                                          // suelo plano (3 de 4 vecinos a la misma altura)
                        }
                    }
                }
                // (B) bolsas escondidas dentro de la roca: huecos planos de 1 bloque que se encuentran al excavar
                if (!lava && by < 78.0) {
                    int zx = (int)std::floor(xx / 16.0f), zz2 = (int)std::floor(zz / 16.0f);
                    for (int k = 0; k < 2 && !lava; k++) {
                        uint32_t hk = (uint32_t)zx * 73856093u ^ (uint32_t)zz2 * 19349663u ^ (0x9E3779B9u + (uint32_t)k * 0x85EBCA6Bu);
                        hk ^= hk >> 13; hk *= 0x5bd1e995u; hk ^= hk >> 15; hk *= 0xc2b2ae35u; hk ^= hk >> 16;
                        int lvl = 3 + (int)(hk % 75u);                                   // nivel (bloque) de la bolsa en esta zona de 16x16
                        if ((int)std::floor(by) != lvl) continue;
                        double q = 0.03 + 0.37 * std::pow(1.0 - lvl / 78.0, 1.3);        // mas probable cuanto mas hondo
                        if ((double)((hk >> 8) % 1000u) / 1000.0 >= q) continue;
                        lava = vnoise3(xx * 0.16f, 21.0f + k, zz * 0.16f, 313u) > 0.86f;
                        if (lava) {   // totalmente encerrada en roca: ningun costado ni arriba/abajo abierto a una cueva
                            static const int DX[6] = {1, -1, 0, 0, 0, 0}, DY[6] = {0, 0, 0, 0, 1, -1}, DZ[6] = {0, 0, 1, -1, 0, 0};
                            for (int q2 = 0; q2 < 6 && lava; q2++) {
                                int ax = lx + DX[q2], ay = ly + DY[q2], az = lz + DZ[q2];
                                if (ax < 0 || ax >= CS || ay < 0 || ay >= CS || az < 0 || az >= CS) continue;
                                if (cuevaEn(ax, ay, az)) lava = false;
                            }
                        }
                    }
                }
                if (lava && ly > 0) {   // el hoyo debe tener fondo: nada de lava sobre aire (piso muy delgado)
                    uint8_t bj = dt[IDX(lx, ly - 1, lz)];
                    if (bj == AIRE || bj == AGUA) lava = false;
                }
                if (lava && ly > 0) {   // nunca mas de 1 bloque de grosor apilado
                    int n = 0;
                    while (n < SUB && ly - 1 - n >= 0 && dt[IDX(lx, ly - 1 - n, lz)] == LAVA) n++;
                    if (n >= SUB) lava = false;
                }
                if (lava) c = (uint8_t)LAVA;
            }
        }
        // borde de roca: donde la lava toca aire por los costados, se pone piedra (solo a los lados) para que nunca quede lava suelta ni "paredes"
        for (int lz = 0; lz < CS; lz++) for (int ly = 0; ly < CS; ly++) for (int lx = 0; lx < CS; lx++) {
            if (dt[IDX(lx, ly, lz)] != LAVA) continue;
            static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
            for (int q = 0; q < 4; q++) {
                int ax = lx + DX[q], az = lz + DZ[q];
                if (ax < 0 || ax >= CS || az < 0 || az >= CS) continue;
                uint8_t& nb = dt[IDX(ax, ly, az)];
                if (nb == AIRE || nb == AGUA || esVeg(nb)) nb = (uint8_t)PIEDRA;
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
        if (esPlanta(dueno)) return otro == AIRE || otro == AGUA || otro == VIDRIO || (esPlanta(otro) && otro != dueno);
        return otro == AIRE || otro == AGUA || otro == VIDRIO || esPlanta(otro);
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
                        if (slice - 1 >= 0 && a != AIRE && visible(a, b)) {
                            m = (a << 1);
                            if (a == AGUA && d == 1) {      // superficie del agua: profundidad (en bloques) para color, transparencia y espuma
                                int pr = 1;
                                while (pr < 62 && en(pa[0], pa[1] - pr, pa[2]) == AGUA) pr++;
                                m |= std::min(31, pr / SUB) << 9;
                            }
                        }
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

                    uint8_t tipo = (uint8_t)((m >> 1) & 255);
                    int atras = m & 1;
                    uint8_t prof = (uint8_t)(m >> 9);
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
                        dm.v.push_back({c[k][0], c[k][1], c[k][2], tipo, (uint8_t)(d * 2 + atras), prof, 0});
                    if (!atras) { uint32_t q[6] = {0, 1, 2, 0, 2, 3}; for (uint32_t x : q) dm.i.push_back(base + x); }
                    else        { uint32_t q[6] = {0, 2, 1, 0, 3, 2}; for (uint32_t x : q) dm.i.push_back(base + x); }
                    i += ancho;
                }
        }
    }
}

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
constexpr int    WORLD_H_BLOCKS = 96;     // altura del mundo en bloques
constexpr int    HYC = (WORLD_H_BLOCKS * SUB + CS - 1) / CS;   // chunks en vertical
constexpr int    WATER_LEVEL = 22;        // nivel del agua (en bloques)
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
                      NTIPOS };
constexpr int ID_SPRAY = 250;                        // herramienta (no es un bloque)
static inline bool esPiezaVehiculo(uint8_t t) { return t >= CARROCERIA && t <= CARR_11; }
static const int NTIPOS_MENU = ANTORCHA + 1 + 2 + 12 + 1 + 7 + 1;       // los que salen en el menu de materiales
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
    return ID_SPRAY;
}
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

// cuanto desierto hay en ese punto (0 = nada, 1 = desierto completo)
static double desierto(double bx, double bz) {
    return suave(0.56, 0.64, fbm((float)bx * 0.0028f, (float)bz * 0.0028f, 3, 41));
}

// altura del terreno en bloques (bx, bz en bloques): colinas, montanas, lagunas, rios y desiertos
static double alturaTerreno(double bx, double bz) {
    float base = fbm((float)bx * 0.004f, (float)bz * 0.004f, 3, 1);
    float colinas = fbm((float)bx * 0.02f, (float)bz * 0.02f, 4, 2);
    float monte = fbm((float)bx * 0.008f, (float)bz * 0.008f, 4, 3);
    monte = std::max(0.0f, (monte - 0.5f) * 2.0f);
    double d = desierto(bx, bz);
    double h = 16.0 + base * 14.0 + colinas * 18.0 * (1.0 - 0.55 * d) + monte * monte * 36.0 * (1.0 - d);

    // lagunas: hondonadas redondas que se llenan de agua
    float lago = fbm((float)bx * 0.011f, (float)bz * 0.011f, 3, 21);
    double tl = suave(0.0, 1.0, (LAGO_UMBRAL - lago) / 0.16);
    if (tl > 0) h = h * (1.0 - tl) + std::min(h, (double)WATER_LEVEL - 4.5) * tl;

    // rios: una franja estrecha donde el ruido cruza 0,5 (mas finos en las montanas altas)
    float r = std::fabs(fbm((float)bx * 0.0045f, (float)bz * 0.0045f, 3, 11) - 0.5f);
    double fuerza = 1.0 - suave(0.0, 0.5, monte * 2.0);
    double tr = (1.0 - suave(0.0, RIO_ANCHO, r)) * fuerza;
    if (tr > 0) h = h * (1.0 - tr) + std::min(h, (double)WATER_LEVEL - 3.5) * tr;

    return std::min(h, 82.0);
}

static inline bool hayArbol(int tx, int tz, double* altura) {
    if (hash2(tx, tz, 77) % 70 != 0) return false;
    if (desierto(tx + 0.5, tz + 0.5) > 0.15) return false;        // en el desierto no hay arboles
    double h = alturaTerreno(tx + 0.5, tz + 0.5);
    if (h < WATER_LEVEL + 1.5 || h > 60.0) return false;
    *altura = h;
    return true;
}

static inline int divPiso(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

// ---------------------------------------------------------------- minerales (vetas)
// El subsuelo se divide en celdas de VETA_CELDA bloques; cada celda puede tener una veta (una mancha irregular
// de mineral) cuyo tipo depende de la profundidad. La veta siempre queda dentro de su propia celda.
constexpr int VETA_CELDA = 12;
struct Veta { uint8_t tipo; double cx, cy, cz, r; };      // centro y radio en bloques
static bool vetaEnCelda(int ix, int iy, int iz, Veta& v) {
    uint32_t h = hash2((int)hash2(ix, iy, 77u), iz, 91u);
    double yc = iy * VETA_CELDA + VETA_CELDA * 0.5;
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
        {ORO, 2, 26, 0.07, 1.3, 2.0},       // raro y profundo
        {DIAMANTE, 1, 15, 0.05, 1.0, 1.6},  // muy raro, casi en el fondo
        {TITANIO, 1, 14, 0.03, 1.0, 1.7},
        {URANIO, 1, 10, 0.015, 1.0, 1.5},
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
    static thread_local bool desiertoCol[CS * CS];
    static thread_local bool arcillaCol[CS * CS];
    static thread_local bool entradaCol[CS * CS];
    int minTop = INT_MAX, maxTop = INT_MIN;
    for (int lz = 0; lz < CS; lz++)
        for (int lx = 0; lx < CS; lx++) {
            double h = alturaTerreno((ox + lx + 0.5) / SUB, (oz + lz + 0.5) / SUB);
            hb[lz * CS + lx] = (float)h;
            desiertoCol[lz * CS + lx] = desierto((ox + lx + 0.5) / SUB, (oz + lz + 0.5) / SUB) > 0.5;
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
            bool arenoso = hb[lz * CS + lx] < WATER_LEVEL + 2.4f || desiertoCol[lz * CS + lx];
            for (int ly = 0; ly < CS; ly++) {
                int vy = oy + ly;
                uint8_t tp;
                if (vy <= t) {
                    int prof = (t - vy) / SUB;
                    bool arc = arcillaCol[lz * CS + lx];
                    if (vy > t - SUB) tp = arenoso ? (arc ? ARCILLA : ARENA) : PASTO;
                    else if (prof < 4) tp = arenoso ? (arc && prof < 2 ? ARCILLA : ARENA) : TIERRA;
                    else tp = PIEDRA;
                } else if (vy < WATER_LEVEL * SUB) tp = AGUA;
                else tp = AIRE;
                dt[IDX(lx, ly, lz)] = tp;
            }
            // arboles cercanos (tronco 1 bloque de ancho, copa redonda de hojas)
            int bx = divPiso(ox + lx, SUB), bz = divPiso(oz + lz, SUB);
            for (int tz = bz - 3; tz <= bz + 3; tz++)
                for (int tx = bx - 3; tx <= bx + 3; tx++) {
                    double ha;
                    if (!hayArbol(tx, tz, &ha)) continue;
                    int vb = (int)std::floor(ha * SUB) + 1 - 2;      // base del tronco (cubitos)
                    int altoTronco = 6 * SUB;
                    if (tx == bx && tz == bz) {
                        for (int ly = 0; ly < CS; ly++) {
                            int vy = oy + ly;
                            if (vy >= vb && vy < vb + altoTronco) dt[IDX(lx, ly, lz)] = MADERA;
                        }
                    }
                    double cxw = tx + 0.5, czw = tz + 0.5, cyw = (vb + altoTronco - SUB) * VS;
                    double dx = (ox + lx + 0.5) * VS - cxw, dz = (oz + lz + 0.5) * VS - czw;
                    double dxz = dx * dx + dz * dz;
                    if (dxz > 7.5) continue;
                    for (int ly = 0; ly < CS; ly++) {
                        int vy = oy + ly;
                        double dy = (vy + 0.5) * VS - cyw;
                        if (dxz + dy * dy * 1.6 <= 7.2 && dt[IDX(lx, ly, lz)] == AIRE) dt[IDX(lx, ly, lz)] = HOJAS;
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
                if (c == AIRE || c == AGUA || c == MADERA || c == HOJAS) continue;
                int cx_ = lx / G, cy_ = ly / G, cz_ = lz / G;
                float fx = (lx + 0.5f) / G - cx_, fy = (ly + 0.5f) / G - cy_, fz = (lz + 0.5f) / G - cz_;
                float n1 = mezcla(f1, cx_, cy_, cz_, fx, fy, fz) - 0.5f, n2 = mezcla(f2, cx_, cy_, cz_, fx, fy, fz) - 0.5f;
                bool abre = n1 * n1 + n2 * n2 < 0.0070f;                               // tunel
                if (!abre && by < 46.0) abre = mezcla(f3, cx_, cy_, cz_, fx, fy, fz) > 1.02f;   // sala
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

// jefe.h (v67): la IA de la base lunar, su sala, la puerta grande, el MECHA (jefe final), los misiles y la pistola de plasma.
// Todo el estado vive aqui; main.cpp solo llama a jefeReset / jefeTick / jefeDibujar / pistolaDisparar y dibuja la barra de vida.
#pragma once
#include <array>

// ---- coordenadas de la sala (bloques locales de la base; mismos numeros que base.h)
constexpr int JF_SD = 170;                                  // eje del silo
constexpr int JF_ARENA_D0 = JF_SD + 32, JF_ARENA_D1 = JF_SD + 79;   // sala de la IA (profundidad)
constexpr int JF_PANT_D = JF_SD + 79;                       // pared de la pantalla
constexpr int JF_PGRAN_X = 26;                              // plano de la puerta grande (pared este)
constexpr int JF_PGRAN_D0 = JF_SD + 50, JF_PGRAN_D1 = JF_SD + 62;

static inline double jfX(double lx) { return BASE_X + lx; }              // x local (bloques) -> mundo
static inline double jfZ(double ld) { return BASE_Z - ld; }              // d local (bloques) -> z del mundo
static inline double jfY(double ly) { return LUNA_Y0 + ly; }
static inline double jfLX(double wx) { return wx - BASE_X; }
static inline double jfLD(double wz) { return BASE_Z - wz; }

// ---------------------------------------------------------------- estado
static bool g_iaLeida = false;                              // ya se leyo del mundo si la IA estaba encendida
static bool g_iaOn = false;                                 // la IA esta encendida
static double g_iaT0 = -1.0;                                // instante de la activacion
static bool g_pgOpen = false;                               // la puerta grande esta abierta del todo
static int g_pgPaso = 0;                                    // filas de cubitos ya quitadas de la puerta grande
static double g_pgT0 = -1.0;
static bool g_norteAbierta = false; static double g_norteT0 = -1.0; static int g_nortePaso = 0;   // puerta del frente del silo
static bool g_oesteAbierta = false; static double g_oesteT0 = -1.0; static int g_oestePaso = 0;      // puerta oeste del silo (cohete)
static bool g_cohAbordo = false; static double g_cohT0 = -1.0, g_motorT0 = -1.0; static int g_techoPaso = 0;     // cohete: a bordo, techo, motores
static bool g_almLuz = false;                               // las luces del almacen estan encendidas
static bool g_pozoAbierto = false;                          // el suelo de la sala ya se hundio (almacen abierto)
static bool g_escAbierta = false;                           // la salida de la escalera del almacen esta abierta
static bool g_jefeHecho = false;                            // ya derrotado (se guarda en jefe.txt)
static double g_pistolaT = -10.0;                           // ultimo disparo

struct Mecha {
    bool viva = false, muriendo = false;
    double x = 0, y = 0, z = 0, yaw = 0;                    // pies (mundo)
    float hp = 500.0f, hpMax = 500.0f;
    double fase = 0, tMisil = 0, tGolpe = 0, tMuerte = 0, flash = -10, brazoGolpe = -10, vel = 0;
    int salvo = 0; double tSalvo = 0;
    int est = 0; double vy = 0, tEst = 0;                   // 0 normal, 1 el suelo tiembla, 2 cayendo, 3 pose de aterrizaje
};
static Mecha g_mecha;
struct Proyectil { double x, y, z, vx, vy, vz, t0; int tipo; };   // 0 plasma del jugador, 1 misil del mecha
static std::vector<Proyectil> g_proy;
struct Boom { double x, y, z, t0; };
static std::vector<Boom> g_booms;

struct Lampara { int x, d, y; uint8_t t; };
static std::vector<Lampara> g_lamp; static int g_lampModo = 0; static size_t g_lampIdx = 0; static bool g_lampPend = false, g_lampFalt = false, g_lampCambio = false;
struct Corr { int lazo, idx, dir; double paso, tNext; bool puesto; int px, pd, py; uint8_t orig; };
static std::vector<std::array<int, 3>> g_lazo[3]; static std::vector<Corr> g_corr; static bool g_corrOn = false, g_corrFin = false;
static bool g_jefeAct() { return g_mecha.viva; }                    // pelea en curso (para mostrar corazones y barra)

static void jefeReset() {
    g_iaLeida = false; g_iaOn = false; g_iaT0 = -1; g_pgOpen = false; g_pgPaso = 0; g_pgT0 = -1;
    g_norteAbierta = false; g_norteT0 = -1; g_nortePaso = 0; g_jefeHecho = false; g_pozoAbierto = false; g_escAbierta = false; g_almLuz = false; g_oesteAbierta = false; g_oesteT0 = -1; g_oestePaso = 0; g_cohAbordo = false; g_cohT0 = -1; g_motorT0 = -1; g_techoPaso = 0; g_lampPend = false; g_lampModo = 0; g_corr.clear(); g_corrOn = false; g_corrFin = false;
    g_mecha = Mecha(); g_proy.clear(); g_booms.clear();
}

// ---------------------------------------------------------------- escritura de celdas (coordenadas locales en CUBITOS)
static void jfCeldas(int lx0, int lx1, int ld0, int ld1, int ly0, int ly1, uint8_t t, int solo = -1, int excepto = -1) {
    std::vector<ChunkP> su;
    for (int lx = lx0; lx <= lx1; lx++) for (int ld = ld0; ld <= ld1; ld++) for (int ly = ly0; ly <= ly1; ly++) {
        const int x = baseCX(lx), y = baseCY(ly), z = baseCZ(ld);
        if (solo >= 0 && leerCubito(x, y, z) != (uint8_t)solo) continue;
        if (excepto >= 0 && leerCubito(x, y, z) == (uint8_t)excepto) continue;
        escribirCubito(x, y, z, t, su);
    }
    for (auto& c : su) remallarAsync(c);
}
// puerta del frente del silo (norte): dos hojas de 5 cubitos que se corren 'paso' cubitos
static void norteSet(int paso) {
    const int ld0 = (JF_SD + 22) * SUB, ld1 = ld0 + SUB - 1, y0 = -16 * SUB, y1 = -10 * SUB - 1;
    std::vector<ChunkP> su;
    for (int lx = -4; lx <= 5; lx++) {
        const bool hojaI = lx <= 0 - paso, hojaD = lx >= 1 + paso;
        const uint8_t t = (hojaI || hojaD) ? BASE_PUERTA : AIRE;
        for (int ld = ld0; ld <= ld1; ld++) for (int ly = y0; ly <= y1; ly++) escribirCubito(baseCX(lx), baseCY(ly), baseCZ(ld), t, su);
    }
    for (auto& c : su) remallarAsync(c);
    g_nortePaso = paso;
}
// puerta grande del hangar del mecha: sube quitando filas desde abajo
static void pgSet(int filas) {
    const int lx0 = JF_PGRAN_X * SUB, lx1 = lx0 + SUB - 1, ld0 = JF_PGRAN_D0 * SUB, ld1 = (JF_PGRAN_D1 + 1) * SUB - 1;
    const int y0 = -16 * SUB;
    if (filas > g_pgPaso) jfCeldas(lx0, lx1, ld0, ld1, y0 + g_pgPaso, y0 + filas - 1, AIRE);
    g_pgPaso = filas;
}
static void jefeGuardar() {
    std::string p = g_dir + "/jefe.txt";
    if (FILE* f = std::fopen(p.c_str(), "w")) { std::fprintf(f, "1\n"); std::fclose(f); }
}
static void jefeCargarArchivo() {
    std::string p = g_dir + "/jefe.txt";
    if (FILE* f = std::fopen(p.c_str(), "r")) { int v = 0; if (std::fscanf(f, "%d", &v) == 1 && v) g_jefeHecho = true; std::fclose(f); }
}

// ---------------------------------------------------------------- el suelo se hunde / la escalera se abre
static void jefeColapsar() {
    if (g_pozoAbierto) return;
    g_pozoAbierto = true;
    jfCeldas(-38, 39, 412, 489, -36, -33, AIRE, -1, BASE_ACERO);   // el suelo (las columnas de acero no se tocan) de la sala (bloques x -19..19, d 206..244)
    jfCeldas(-2, 3, 482, 485, -32, -27, AIRE);                      // el pedestal del boton cae con el suelo
}
static void jefeAbrirEscalera() {
    if (g_escAbierta) return;
    g_escAbierta = true;
    for (int k = 1; k <= 15; k++) { const int cs = -32 - k; if (cs <= -33) jfCeldas(-48, -41, 414 + k, 414 + k, std::max(cs, -36), -33, AIRE); }
}

// ---------------------------------------------------------------- luces del almacen (v69): oscuro al caer, se encienden a 1 s; luces rojas que corren por paredes y suelo
static void lamparasSet(int modo) {
    g_lampModo = modo; g_lampPend = true; g_lampIdx = 0; g_lampFalt = false; g_lampCambio = false;
    if (g_lamp.empty()) almacenLamparas([&](int x, int d, int y, uint8_t t) { g_lamp.push_back({x, d, y, t}); });
}
static uint8_t lampTipo(const Lampara& l) { if (g_lampModo == 0) return BASE_IA_OFF; if (g_lampModo == 2 && l.t == PLASMA_ROJO) return PLASMA_FRIO; return l.t; }
static void lamparasTick() {        // reintenta hasta que todas esten como deben (los chunks lejanos pueden tardar en cargar)
    if (!g_lampPend || g_lamp.empty()) return;
    std::vector<ChunkP> su;
    for (int n = 0; n < 60; n++) {
        const Lampara& l = g_lamp[g_lampIdx]; bool c = false;
        const uint8_t cur = leerCubito(baseCX(l.x * SUB), baseCY(l.y * SUB), baseCZ(l.d * SUB), &c), want = lampTipo(l);
        if (!c) g_lampFalt = true;
        else if (cur != want) {
            for (int i = 0; i < SUB; i++) for (int k = 0; k < SUB; k++) for (int q = 0; q < SUB; q++) escribirCubito(baseCX(l.x * SUB + i), baseCY(l.y * SUB + k), baseCZ(l.d * SUB + q), want, su);
            g_lampCambio = true;
        }
        if (++g_lampIdx >= g_lamp.size()) { g_lampIdx = 0; if (!g_lampFalt && !g_lampCambio) g_lampPend = false; g_lampFalt = false; g_lampCambio = false; break; }
    }
    for (auto& c : su) remallarAsync(c);
}
static void bloqueSet(int x, int d, int y, uint8_t t, std::vector<ChunkP>& su) {
    for (int i = 0; i < SUB; i++) for (int k = 0; k < SUB; k++) for (int q = 0; q < SUB; q++) escribirCubito(baseCX(x * SUB + i), baseCY(y * SUB + k), baseCZ(d * SUB + q), t, su);
}
static void lazoRect(std::vector<std::array<int, 3>>& L, int x0, int x1, int d0, int d1, int y) {
    L.clear();
    for (int x = x0; x <= x1; x++) L.push_back({x, d0, y});
    for (int d = d0 + 1; d <= d1; d++) L.push_back({x1, d, y});
    for (int x = x1 - 1; x >= x0; x--) L.push_back({x, d1, y});
    for (int d = d1 - 1; d > d0; d--) L.push_back({x0, d, y});
}
static void corrIniciar() {
    if (g_corrOn) return;
    lazoRect(g_lazo[0], -50, 50, 201, 298, -28);          // pared (a media altura)
    lazoRect(g_lazo[1], -16, 40, 215, 285, -48);          // suelo, anillo interior
    lazoRect(g_lazo[2], -44, 44, 206, 294, -48);          // suelo, anillo exterior
    const int cuantos[3] = {4, 3, 3};
    const double pasos[10] = {0.40, 0.45, 0.35, 0.50, 0.42, 0.38, 0.48, 0.44, 0.36, 0.52};
    int n = 0; g_corr.clear();
    for (int l = 0; l < 3; l++) for (int k = 0; k < cuantos[l]; k++, n++) {
        Corr r{}; r.lazo = l; r.idx = (int)(g_lazo[l].size() * k / cuantos[l]); r.dir = (n % 2) ? -1 : 1; r.paso = pasos[n]; r.tNext = glfwGetTime() + 0.2 * n; r.puesto = false;
        g_corr.push_back(r);
    }
    g_corrOn = true; g_corrFin = false;
}
static void corrTick(double ahora) {
    if (!g_corrOn || g_corrFin) return;
    std::vector<ChunkP> su; int mov = 0;
    for (Corr& r : g_corr) {
        if (ahora < r.tNext || mov >= 3) continue;
        const auto& L = g_lazo[r.lazo];
        const int ni = (int)((r.idx + r.dir + (int)L.size()) % (int)L.size());
        const auto& p = L[ni]; bool c = false;
        const uint8_t o = leerCubito(baseCX(p[0] * SUB), baseCY(p[2] * SUB), baseCZ(p[1] * SUB), &c);
        if (!c || o == PLASMA_ROJO) { r.tNext = ahora + 0.4; continue; }
        if (r.puesto) bloqueSet(r.px, r.pd, r.py, r.orig, su);
        r.idx = ni; r.px = p[0]; r.pd = p[1]; r.py = p[2]; r.orig = o; r.puesto = true;
        bloqueSet(p[0], p[1], p[2], PLASMA_ROJO, su);
        r.tNext = ahora + r.paso; mov++;
    }
    for (auto& c : su) remallarAsync(c);
}
static void corrVictoria() {          // al ganar: se quedan quietas y pasan a blanco azulado
    std::vector<ChunkP> su;
    for (Corr& r : g_corr) if (r.puesto) bloqueSet(r.px, r.pd, r.py, PLASMA_FRIO, su);
    for (auto& c : su) remallarAsync(c);
    g_corrFin = true;
}
static void jefeRestaurarSuelo() {
    jfCeldas(-38, 39, 412, 489, -36, -35, BASE_MURO, AIRE);
    jfCeldas(-38, 39, 412, 489, -34, -33, BASE_PISO, AIRE);
    jfCeldas(-2, 3, 412, 485, -34, -33, BASE_FRANJA);
    g_pozoAbierto = false;
}

// ---------------------------------------------------------------- v70: puerta oeste del silo y cohete
constexpr int RK_CX = -62, RK_CD = JF_SD;                         // eje del pozo del cohete (bloques locales)
static void oesteSet(int paso) {                                  // dos hojas de 5 cubitos que se separan hasta abrirse del todo
    const int x0 = -22 * SUB, ld0 = (JF_SD - 2) * SUB;
    std::vector<ChunkP> su;
    for (int u = 0; u < 10; u++) {
        const bool hl = u <= 4 - paso, hr = u >= 5 + paso;
        const uint8_t t = (hl || hr) ? BASE_PUERTA : AIRE;
        for (int x = x0; x < x0 + SUB; x++) for (int y = -16 * SUB; y <= -10 * SUB - 1; y++) escribirCubito(baseCX(x), baseCY(y), baseCZ(ld0 + u), t, su);
    }
    for (auto& c : su) remallarAsync(c);
    g_oestePaso = paso;
}
static bool jefeCercaCohete() {
    const double dx = jfLX(g_j.p.x) - RK_CX, dd = jfLD(g_j.p.z) - RK_CD;
    return dx * dx + dd * dd < 30.0 * 30.0 && g_j.p.y < jfY(-5.0);
}
static void jefeAbordar(double ahora) {                          // boton junto a la escotilla del cohete
    if (!g_jefeHecho) { aviso("EL COHETE ESTA BLOQUEADO"); return; }
    if (g_cohAbordo) { aviso("YA ESTAS A BORDO"); return; }
    g_cohAbordo = true; g_cohT0 = ahora; g_motorT0 = -1;
    g_j.p = {jfX((double)RK_CX), jfY(-16.0) + 0.05, jfZ((double)RK_CD)}; g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0}; g_j.volar = false;
    aviso("A BORDO: SE ABRE EL TECHO");
}
// v71: el cohete de bloques pasa a ser una nave de vuelo MANUAL (la misma del cohete normal): no hay ascenso automatico
static void cohLanzarManual() {
    Cohete& k = g_ck;
    if (k.activo) return;
    const int OX = RK_CX * SUB, OD = RK_CD * SUB, OY = -51;                  // origen de la nave (cubitos locales de la base): centro del cohete
    std::vector<ChunkP> su;
    std::vector<CohOrig> orig;
    k.e1.celdas.clear(); k.e2.celdas.clear();
    int ex0 = 1 << 30, ex1 = -(1 << 30), ey0 = 1 << 30, ey1 = -(1 << 30), ez0 = 1 << 30, ez1 = -(1 << 30);
    for (int dx = -21; dx <= 21; dx++) for (int dd = -21; dd <= 21; dd++) for (int ly = -94; ly <= -9; ly++) {
        const int X = baseCX(OX + dx), Y = baseCY(ly), Z = baseCZ(OD + dd);
        bool kn = true;
        const uint8_t t = leerCubito(X, Y, Z, &kn);
        if (!kn) { aviso("ESPERA UN MOMENTO: EL COHETE AUN NO CARGA"); return; }
        if (t == AIRE) continue;
        if (t == BASE_REJILLA || t == BASE_TUBERIA) { orig.push_back({X, Y, Z, t}); continue; }   // la pasarela se retira (las aletas la atravesarian); vuelve si bajas en la plataforma
        if (dx < -18 || dx > 18 || dd < -18 || dd > 18) continue;
        orig.push_back({X, Y, Z, t});
        k.e2.celdas.push_back({dx, ly - OY, dd, t});
    }
    if (k.e2.celdas.size() < 2000) { aviso("EL COHETE ESTA ROTO"); k.e2.celdas.clear(); return; }
    const bool prev = g_redEnviar; g_redEnviar = false;
    for (auto& c : orig) {
        escribirCubito(c.x, c.y, c.z, AIRE, su);
        ex0 = std::min(ex0, c.x); ex1 = std::max(ex1, c.x); ey0 = std::min(ey0, c.y); ey1 = std::max(ey1, c.y); ez0 = std::min(ez0, c.z); ez1 = std::max(ez1, c.z);
    }
    g_redEnviar = prev;
    for (auto& c : su) remallarAsync(c);
    marcarLuzSucia(ex0 - 1, ex1 + 1, ey0 - 1, ey1 + 1, ez0 - 1, ez1 + 1, su);
    g_cohOrig = orig;
    g_cohJug = g_j.p;
    Veh& e = k.e2;
    e.libre = false; e.yaw = 0; e.pitch = 0; e.roll = 0; e.v = {0, 0, 0}; e.activo = true; e.pilotando = false;
    e.p = {(baseCX(OX) + 0.5) * VS, (baseCY(OY) + 0.5) * VS, (baseCZ(OD) + 0.5) * VS};
    vehReconstruir(e);
    ckLibre(e);
    k.e1vivo = false; k.e1.activo = false;
    k.fase = 2; k.t = 0; k.tv = 0; k.sp = 0; k.tSep = 10.0; k.V = {0, 0, 0}; k.w = {0, 0, 0}; k.cab = true; k.sobra = 0;
    k.fuel = 100.0; k.quema = false; k.llegada = false; k.desc = false; k.esperaLuna = false; k.enLuna = true; k.tLleg = -1; g_terraFija = false;
    k.humo.clear();
    k.muestras.clear();
    const size_t N = e.celdas.size(), paso = std::max<size_t>(1, N / 2500);
    for (size_t i = 0; i < N; i += paso) k.muestras.push_back({e.celdas[i].x, e.celdas[i].y, e.celdas[i].z});
    k.bloques = true; k.pad = true; k.padY = e.p.y; k.empuje = 8.0; k.camY = 22.5; k.camD = 70.0; k.flamaY = -44.0 - 1.5;
    k.techo = (WORLD_H_BLOCKS - 4) * 1.0 - 45.0 * 0.0;                         // y maxima del centro de la nave (bloques)
    k.techo = (double)(WORLD_H_BLOCKS - 3) - 22.0;                              // y maxima del centro de la nave (bloques)
    k.activo = true; g_cohLanzado = true;
    g_j.volar = true; g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0}; g_sobreVeh = false;
    g_j.yaw = 0.0; g_j.pitch = 0.0;
    aviso("COHETE LISTO: MANTEN 2 = MOTOR | W/S A/D Q/E = GIRAR | TAB = CAMARA | G = BAJAR");
}
// v71c: la abertura del techo es un DISCO de 21 cubitos de radio: se quita todo (tapa, muro y bloques lunares) para que pasen aletas y propulsores
static void cohDisco(int xlo, int xhi, int lado) {                   // columnas |dx| en [xlo, xhi) de un lado (-1 / +1)
    const int cx = RK_CX * SUB, cd = RK_CD * SUB;
    std::vector<ChunkP> su;
    for (int a = xlo; a < xhi; a++) for (int dd = -21; dd <= 21; dd++) {
        if (a * a + dd * dd > 21 * 21) continue;
        const int lx = cx + lado * a + (lado > 0 ? 0 : 0);
        for (int ly = -4; ly <= 8; ly++) escribirCubito(baseCX(lx), baseCY(ly), baseCZ(cd + dd), AIRE, su);
    }
    for (auto& c : su) remallarAsync(c);
}
static void cohTick(double ahora) {
    if (g_cohLanzado && !g_ck.activo) { g_cohLanzado = false; g_cohAbordo = false; }
    if (!g_cohAbordo || g_cohLanzado) return;
    const double t = ahora - g_cohT0;
    const int want = std::max(0, std::min(22, (int)((t - 1.0) / 3.5 * 22.0)));
    if (want > g_techoPaso) {                                    // la tapa se abre desde el centro hacia los lados
        cohDisco(g_techoPaso, want, -1); cohDisco(g_techoPaso, want, 1);
        g_techoPaso = want;
    }
    if (g_techoPaso >= 22 && t > 5.0) cohLanzarManual();
}
// /cohetemoderno: vuelve a poner el cohete (y la tapa) como en el diseno de la base
static bool g_cohRepon = false, g_cohRepTp = false;
static void cohReponerTick() {
    if (!g_cohRepon) return;
    const int OX = RK_CX * SUB, OD = RK_CD * SUB;
    if (!g_cohRepTp) {                                           // primero te lleva al tunel (asi cargan los chunks)
        g_j.p = {jfX(-40.0), jfY(-16.0) + 0.1, jfZ((double)JF_SD)}; g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0}; g_j.volar = false; g_j.yaw = 1.5708; g_j.pitch = 0;
        g_cohRepTp = true; return;
    }
    bool c1 = true, c2 = true, c3 = true;
    leerCubito(baseCX(OX), baseCY(-50), baseCZ(OD), &c1); leerCubito(baseCX(OX + 21), baseCY(0), baseCZ(OD + 21), &c2); leerCubito(baseCX(OX - 21), baseCY(-94), baseCZ(OD - 21), &c3);
    if (!c1 || !c2 || !c3) return;                               // aun no cargan
    g_cohRepon = false;
    if (g_ck.activo) ckLiberar();
    std::unordered_map<uint64_t, std::vector<uint8_t>> bufs;
    auto buf = [&](int ox, int oy, int oz) -> std::vector<uint8_t>& {
        const uint64_t key = ((uint64_t)(uint32_t)(ox + (1 << 20)) << 42) ^ ((uint64_t)(uint32_t)(oy + (1 << 20)) << 21) ^ (uint64_t)(uint32_t)(oz + (1 << 20));
        auto it = bufs.find(key);
        if (it == bufs.end()) { it = bufs.emplace(key, std::vector<uint8_t>((size_t)CS * CS * CS, (uint8_t)AIRE)).first; baseAplicar(it->second.data(), ox, oy, oz); }
        return it->second;
    };
    std::vector<ChunkP> su;
    int n = 0;
    for (int dx = -21; dx <= 21; dx++) for (int dd = -21; dd <= 21; dd++) for (int ly = -94; ly <= 0; ly++) {
        const bool disco = dx * dx + dd * dd <= 21 * 21;
        if (ly > -9 && !(disco && ly >= -4)) continue;           // entre la ojiva y la tapa no hay nada
        const int X = baseCX(OX + dx), Y = baseCY(ly), Z = baseCZ(OD + dd);
        const int ox = divPiso(X, CS) * CS, oy = divPiso(Y, CS) * CS, oz = divPiso(Z, CS) * CS;
        uint8_t t = buf(ox, oy, oz)[IDX(X - ox, Y - oy, Z - oz)];
        if (t == AIRE && ly == 0 && disco) t = PIEDRA;           // el suelo lunar alrededor de la tapa
        if (t == AIRE) continue;
        if (leerCubito(X, Y, Z) != t) { escribirCubito(X, Y, Z, t, su); n++; }
    }
    for (auto& c : su) remallarAsync(c);
    g_techoPaso = 0; g_cohAbordo = false; g_cohLanzado = false; g_cohOrig.clear();
    char m[80]; std::snprintf(m, sizeof m, "COHETE MODERNO REPUESTO (%d CUBITOS)", n); aviso(m);
}

// ---------------------------------------------------------------- activar la IA (boton de la computadora)
static void jefeActivarIA(double ahora) {
    if (g_iaOn) return;
    g_iaOn = true; g_iaT0 = ahora;
    jfCeldas(-14 * SUB, 15 * SUB - 1, JF_PANT_D * SUB, JF_PANT_D * SUB + SUB - 1, -14 * SUB, -5 * SUB - 1, BASE_IA_ON, BASE_IA_OFF);
    aviso("IA ACTIVADA... SE ESCUCHA ALGO ABRIENDOSE");
}
static void jefeLeerEstado() {      // al acercarse: ¿la IA ya estaba encendida en este mundo?
    g_iaLeida = true;
    jefeCargarArchivo();
    bool c = false;
    uint8_t v = leerCubito(baseCX(0), baseCY(-10 * SUB), baseCZ(JF_PANT_D * SUB), &c);
    if (c && v == BASE_IA_ON) g_iaOn = true;
    uint8_t w = leerCubito(baseCX(JF_PGRAN_X * SUB), baseCY(-12 * SUB), baseCZ((JF_PGRAN_D0 + 6) * SUB), &c);
    if (c && w == AIRE) { g_pgOpen = true; g_pgPaso = 99; }
    uint8_t n = leerCubito(baseCX(0), baseCY(-14 * SUB), baseCZ((JF_SD + 22) * SUB), &c);
    if (c && n == AIRE) { g_norteAbierta = true; g_nortePaso = 3; }
    uint8_t pz = leerCubito(baseCX(0), baseCY(-33), baseCZ(450), &c);
    if (c && pz == AIRE) {
        g_pozoAbierto = true;
        bool c2 = false; const uint8_t lz = leerCubito(baseCX(-48 * SUB), baseCY(-19 * SUB), baseCZ(207 * SUB), &c2);
        if (c2 && lz != BASE_IA_OFF && !g_jefeHecho) { g_almLuz = true; lamparasSet(1);  }
    }
    if (g_jefeHecho) jefeAbrirEscalera();
    uint8_t ow = leerCubito(baseCX(-22 * SUB), baseCY(-14 * SUB), baseCZ(JF_SD * SUB), &c);
    if (c && ow == AIRE) { g_oesteAbierta = true; g_oestePaso = 5; }
    uint8_t tc = leerCubito(baseCX(RK_CX * SUB), baseCY(-2), baseCZ(RK_CD * SUB), &c);
    if (c && tc == AIRE) g_techoPaso = 22;
    { bool c3 = false; const uint8_t tb = leerCubito(baseCX(RK_CX * SUB), baseCY(0), baseCZ(RK_CD * SUB), &c3); if (c3 && tb == AIRE) g_techoPaso = 22; }
}

// ---------------------------------------------------------------- mecha
static void jefeSpawn() {
    Mecha m; m.viva = true;
    m.x = jfX(35.0); m.z = jfZ(JF_SD + 56.0); m.y = jfY(-16.0); m.yaw = -1.5708;      // dentro del hangar, mirando a la sala (hacia -x)
    if (g_pozoAbierto) { m.x = jfX(0.0); m.z = jfZ(250.0); m.y = jfY(-47.0); m.yaw = 3.14159; if (!g_almLuz) { g_almLuz = true; lamparasSet(1);  } }      // el suelo ya cayo: aparece abajo
    m.tMisil = glfwGetTime() + 4.0; m.tGolpe = 0;
    g_mecha = m;
    aviso("EL MECHA DE LA IA SE ACTIVA");
}
static bool jfSolido(double x, double y, double z) {
    const uint8_t t = leerCubito((int)std::floor(x * SUB), (int)std::floor(y * SUB), (int)std::floor(z * SUB));
    return t != AIRE && !esAgua(t) && t != LAVA;
}
static void jfBoom(double x, double y, double z, double ahora, double fuerza) {
    g_booms.push_back({x, y, z, ahora});
    const double ddx = g_j.p.x - x, ddy = g_j.p.y - y, ddz = g_j.p.z - z;
    if (ddx * ddx + ddy * ddy + ddz * ddz < 14.0 * 14.0) { g_explDestello = ahora; g_explFuerza = (float)std::min(0.5, fuerza); }
}
static void jefeDanoJugador(float hp, double ahora) {
    if (glfwGetTime() < g_protT) return;
    survDano(hp);
}
static void mechaDano(float hp, double ahora) {
    if (!g_mecha.viva || g_mecha.muriendo) return;
    g_mecha.hp -= hp; g_mecha.flash = ahora;
    if (g_mecha.hp <= 0) { g_mecha.hp = 0; g_mecha.muriendo = true; g_mecha.tMuerte = ahora; aviso("MECHA DESTRUIDO"); }
}

static void pistolaDisparar(V3 cam, V3 dir, double ahora) {
    if (ahora - g_pistolaT < 0.28) return;
    g_pistolaT = ahora;
    Proyectil p; p.x = cam.x + dir.x * 0.8; p.y = cam.y + dir.y * 0.8 - 0.15; p.z = cam.z + dir.z * 0.8; p.t0 = ahora; p.tipo = 0;
    p.vx = dir.x * 70.0; p.vy = dir.y * 70.0; p.vz = dir.z * 70.0;
    g_proy.push_back(p);
}

// v82: flechas del arco (tipo 2): vuelan con gravedad, chocan con el terreno y danan al mecha
static void flechaDisparar(V3 cam, V3 dir, double ahora) {
    Proyectil p; p.x = cam.x + dir.x * 0.8; p.y = cam.y + dir.y * 0.8 - 0.1; p.z = cam.z + dir.z * 0.8; p.t0 = ahora; p.tipo = 2;
    p.vx = dir.x * 58.0; p.vy = dir.y * 58.0; p.vz = dir.z * 58.0;
    g_proy.push_back(p);
}
static void flechasTick(double dt, double ahora) {
    for (size_t i = 0; i < g_proy.size();) {
        Proyectil& p = g_proy[i];
        if (p.tipo != 2) { i++; continue; }
        bool muere = false;
        for (int s = 0; s < 3 && !muere; s++) {
            p.vy -= 14.0 * dt / 3.0;
            p.x += p.vx * dt / 3.0; p.y += p.vy * dt / 3.0; p.z += p.vz * dt / 3.0;
            Mecha& m = g_mecha;
            if (g_modoLuna && m.viva && !m.muriendo) {
                const double ex = p.x - m.x, ez = p.z - m.z;
                if (ex * ex + ez * ez < 2.4 * 2.4 && p.y > m.y && p.y < m.y + 9.0) { mechaDano(12.0f, ahora); muere = true; jfBoom(p.x, p.y, p.z, ahora, 0.04); }
            }
            if (!muere && jfSolido(p.x, p.y, p.z)) muere = true;
        }
        if (ahora - p.t0 > 6.0) muere = true;
        if (muere) { g_proy[i] = g_proy.back(); g_proy.pop_back(); } else i++;
    }
}

static void jefeMover(Mecha& m, double dx, double dz, double dt) {
    double nx = m.x + dx * dt, nz = m.z + dz * dt;
    double lx = jfLX(nx), ld = jfLD(nz);
    if (m.y < jfY(-30.0)) {                                          // almacen subterraneo
        lx = std::max(-47.0, std::min(47.0, lx)); ld = std::max(204.0, std::min(295.0, ld));
        for (int cx : {-36, -18, 18, 36}) for (int cd : {222, 250, 278}) {
            const double ex = lx - cx, ed = ld - cd, r2 = ex * ex + ed * ed;
            if (r2 < 3.6 * 3.6 && r2 > 1e-6) { const double r = std::sqrt(r2); lx = cx + ex / r * 3.6; ld = cd + ed / r * 3.6; }
        }
        for (int cx : {-12, 12}) for (int cd : {JF_SD + 48, JF_SD + 64}) {      // las columnas de la sala atraviesan el almacen
            const double ex = lx - cx, ed = ld - cd, r2 = ex * ex + ed * ed;
            if (r2 < 3.6 * 3.6 && r2 > 1e-6) { const double r = std::sqrt(r2); lx = cx + ex / r * 3.6; ld = cd + ed / r * 3.6; }
        }
        m.x = jfX(lx); m.z = jfZ(ld); return;
    }
    const bool enHangar = lx > 24.0;
    if (enHangar) { ld = std::max((double)JF_PGRAN_D0 + 2.5, std::min((double)JF_PGRAN_D1 - 1.5, ld)); lx = std::min(lx, 41.0); }
    else { lx = std::max(-24.0, std::min(24.0, lx)); ld = std::max((double)JF_ARENA_D0 + 3.0, std::min((double)JF_ARENA_D1 - 5.0, ld)); }
    if (!enHangar) for (int sx : {-12, 12}) for (int sd : {JF_SD + 48, JF_SD + 64}) {      // columnas de la sala
        const double ex = lx - sx, ed = ld - sd, r2 = ex * ex + ed * ed;
        if (r2 < 3.4 * 3.4 && r2 > 1e-6) { const double r = std::sqrt(r2); lx = sx + ex / r * 3.4; ld = sd + ed / r * 3.4; }
    }
    m.x = jfX(lx); m.z = jfZ(ld);
}

static void jefeTick(double dt, double ahora) {
    static std::string ultimoMundo; if (ultimoMundo != g_dir) { jefeReset(); ultimoMundo = g_dir; }
    flechasTick(dt, ahora);
    if (!g_modoLuna) return;
    cohReponerTick();
    const double pLX = jfLX(g_j.p.x), pLD = jfLD(g_j.p.z);
    const bool cerca = pLX > -115 && pLX < 60 && pLD > JF_SD - 40 && pLD < JF_SD + 110;
    if (!g_iaLeida && cerca) jefeLeerEstado();
    // puerta del frente del silo: se abre sola, hasta la mitad
    if (!g_norteAbierta && g_norteT0 < 0 && std::fabs(pLX) < 9 && pLD > JF_SD + 6 && pLD < JF_SD + 30 && std::fabs(g_j.p.y - jfY(-16.0)) < 6) g_norteT0 = ahora;
    if (g_norteT0 >= 0) {
        const int want = std::min(3, (int)((ahora - g_norteT0) / 1.2 * 3.0 + 0.001));
        if (want > g_nortePaso) norteSet(want);
        if (g_nortePaso >= 3) { g_norteAbierta = true; g_norteT0 = -1; }
    }
    // puerta oeste del silo (la del cohete): se abre sola al acercarte, pero solo despues de vencer al mecha
    if (g_jefeHecho && !g_oesteAbierta && g_oesteT0 < 0 && pLX > -34 && pLX < -12 && std::fabs(pLD - JF_SD) < 8 && std::fabs(g_j.p.y - jfY(-16.0)) < 6) { g_oesteT0 = ahora; aviso("LA PUERTA OESTE SE ABRE"); }
    if (g_oesteT0 >= 0) {
        const int want = std::min(5, (int)((ahora - g_oesteT0) / 1.5 * 5.0 + 0.001));
        if (want > g_oestePaso) oesteSet(want);
        if (g_oestePaso >= 5) { g_oesteAbierta = true; g_oesteT0 = -1; }
    }
    cohTick(ahora);
    // secuencia tras activar la IA: la puerta grande se abre y sale el mecha
    if (g_iaOn && g_iaT0 >= 0 && !g_pgOpen && ahora - g_iaT0 > 2.5) {
        if (g_pgT0 < 0) { g_pgT0 = ahora; aviso("SE ABRE LA PUERTA GRANDE: PREPARATE"); }
        const int total = 11 * SUB;
        const int want = std::min(total, (int)((ahora - g_pgT0) / 3.0 * total));
        if (want > g_pgPaso) pgSet(want);
        if (g_pgPaso >= total) g_pgOpen = true;
    }
    if (g_iaOn && g_pgOpen && !g_jefeHecho && !g_mecha.viva && !g_mecha.muriendo && (g_iaT0 >= 0 ? ahora - g_pgT0 > 3.2 : cerca)) jefeSpawn();
    if (g_iaOn && g_iaT0 < 0 && g_pgOpen && !g_jefeHecho && !g_mecha.viva && cerca) jefeSpawn();     // mundo recargado con el jefe pendiente

    if (g_pozoAbierto && std::fabs(pLX) < 40 && pLD > 204 && pLD < 246 && g_j.p.y > jfY(-46.0)) g_protT = std::max(g_protT, ahora + 1.0);
    lamparasTick();
    Mecha& m = g_mecha;
    if (m.viva && !m.muriendo) {
        const double dx = g_j.p.x - m.x, dz = g_j.p.z - m.z, dist = std::sqrt(dx * dx + dz * dz);
        double want = std::atan2(dx, -dz), dy = want - m.yaw;
        while (dy > 3.14159265) dy -= 6.2831853; while (dy < -3.14159265) dy += 6.2831853;
        m.yaw += dy * std::min(1.0, dt * 2.2);
        const double fx = std::sin(m.yaw), fz = -std::cos(m.yaw);
        const bool enHangar = jfLX(m.x) > 24.5;
        double sp = 0;
        const double ahoraT = ahora - m.tEst;
        if (m.est == 1) {                                              // el suelo tiembla
            m.vel = 0;
            if (std::fmod(ahoraT, 0.3) < dt) { g_explDestello = ahora; g_explFuerza = 0.12f; }
            if (ahoraT > 1.6) { jefeColapsar(); m.est = 2; m.vy = 0; m.tEst = ahora; aviso("EL SUELO SE ABRE!"); }
        } else if (m.est == 2) {                                       // cae junto con el jugador
            m.vel = 0; m.vy -= 24.0 * dt; m.y += m.vy * dt;
            if (m.y <= jfY(-47.0)) {
                m.y = jfY(-47.0); m.est = 3; m.tEst = ahora; m.vy = 0;
                for (int i = 0; i < 6; i++) { const double a2 = i * 1.0472; jfBoom(m.x + std::cos(a2) * 4.5, m.y + 0.6, m.z + std::sin(a2) * 4.5, ahora, 0.3); }
                aviso("EL MECHA TE MIRA FIJAMENTE...");
            }
        } else if (m.est == 3) {                                       // pose de aterrizaje, sin atacar
            m.vel = 0;
            if (!g_almLuz && ahoraT > 1.0) { g_almLuz = true; lamparasSet(1);  aviso("SE ENCIENDEN LAS LUCES"); }
            if (ahoraT > 3.0) { m.est = 0; m.tMisil = ahora + 2.0; }
        } else {
        if (!g_pozoAbierto && !enHangar && m.y > jfY(-20.0) && dist < 17.0 && std::fabs(jfLX(m.x)) < 15.0 && jfLD(m.z) > 210.0 && jfLD(m.z) < 240.0) {
            m.est = 1; m.tEst = ahora; g_j.volar = false; g_protT = ahora + 12.0; aviso("EL SUELO EMPIEZA A TEMBLAR...");
        }
        if (enHangar) sp = 3.4;                                   // sale del hangar
        else if (dist > 16.0) sp = 3.0;
        else if (dist < 8.0) sp = -2.2;
        if (sp != 0) jefeMover(m, fx * sp, fz * sp, dt);
        if (sp != 0 && !enHangar && std::fabs(sp) > 0) {            // a veces se desplaza de lado para dificultar la puntería
            const double lado = std::sin(ahora * 0.6) * 1.6;
            jefeMover(m, -fz * lado, fx * lado, dt);
        }
        m.vel = std::fabs(sp); m.fase += dt * (1.0 + m.vel * 0.9);
        // golpe cuerpo a cuerpo
        if (dist < 7.0 && ahora - m.tGolpe > 2.4) { m.tGolpe = ahora; m.brazoGolpe = ahora; }
        if (m.brazoGolpe > 0 && ahora - m.brazoGolpe > 0.55 && ahora - m.brazoGolpe < 0.7 && dist < 7.5 && m.tGolpe == m.brazoGolpe) { jefeDanoJugador(6.0f, ahora); m.tGolpe = m.brazoGolpe + 0.01; }
        // misiles en salvas de 4
        const double cad = m.hp < m.hpMax * 0.5f ? 3.3 : 4.6;
        if (!enHangar && m.salvo == 0 && ahora >= m.tMisil && dist < 60) { m.salvo = 4; m.tSalvo = ahora; m.tMisil = ahora + cad; }
        if (m.salvo > 0 && ahora >= m.tSalvo) {
            const int i = 4 - m.salvo; m.salvo--; m.tSalvo = ahora + 0.22;
            const double lado = (i % 2 == 0) ? -2.9 : 2.9, rx = std::cos(m.yaw), rz = std::sin(m.yaw);
            Proyectil p; p.tipo = 1; p.t0 = ahora;
            p.x = m.x + rx * lado + fx * 1.0; p.y = m.y + 6.6; p.z = m.z + rz * lado + fz * 1.0;
            p.vx = fx * 4.0 + rx * lado * 1.2; p.vy = 7.0; p.vz = fz * 4.0 + rz * lado * 1.2;
            g_proy.push_back(p);
        }
        }
    } else if (m.muriendo) {
        if (ahora - m.tMuerte > 3.0) {
            for (int i = 0; i < 5; i++) jfBoom(m.x + (i - 2) * 1.2, m.y + 3.0 + (i % 3), m.z + ((i * 7) % 3 - 1) * 1.0, ahora, 0.45);
            m.viva = false; m.muriendo = false; g_jefeHecho = true; jefeGuardar(); jefeAbrirEscalera(); jefeRestaurarSuelo(); lamparasSet(2);
            if (g_inv[0] == 0) { g_inv[0] = 1; guardarSurv(); aviso("ARMAS DESBLOQUEADAS: ESPADA DE PLASMA Y MARTILLO SISMICO"); }
            else aviso("DERROTASTE AL MECHA: EL SUELO SE RECONSTRUYE Y SE ABRE LA ESCALERA");
        } else if (std::fmod(ahora - m.tMuerte, 0.35) < dt) jfBoom(m.x + std::sin(ahora * 7) * 2.0, m.y + 1.5 + std::fmod(ahora * 3.0, 5.0), m.z + std::cos(ahora * 5) * 1.5, ahora, 0.25);
    }

    // proyectiles
    for (size_t i = 0; i < g_proy.size();) {
        Proyectil& p = g_proy[i];
        if (p.tipo == 2) { i++; continue; }          // las flechas las mueve flechasTick
        bool muere = false;
        const double edad = ahora - p.t0;
        if (p.tipo == 0) {                                            // plasma del jugador
            const int pasos = 3;
            for (int s = 0; s < pasos && !muere; s++) {
                p.x += p.vx * dt / pasos; p.y += p.vy * dt / pasos; p.z += p.vz * dt / pasos;
                if (m.viva && !m.muriendo) {
                    const double ex = p.x - m.x, ez = p.z - m.z;
                    if (ex * ex + ez * ez < 2.4 * 2.4 && p.y > m.y && p.y < m.y + 9.0) { mechaDano(8.0f, ahora); muere = true; jfBoom(p.x, p.y, p.z, ahora, 0.05); }
                }
                if (!muere && jfSolido(p.x, p.y, p.z)) muere = true;
            }
            if (edad > 1.6) muere = true;
        } else {                                                      // misil del mecha: persigue despacio
            const double ex = g_j.p.x - p.x, ey = g_j.p.y + 1.0 - p.y, ez = g_j.p.z - p.z, d = std::sqrt(ex * ex + ey * ey + ez * ez) + 1e-6;
            double sp = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz); sp = std::min(12.0, sp + 9.0 * dt);
            const double k = std::min(1.0, 1.15 * dt * (edad > 0.5 ? 1.0 : 0.0));
            double nx = p.vx / (sp + 1e-6) * (1 - k) + ex / d * k, ny = p.vy / (sp + 1e-6) * (1 - k) + ey / d * k, nz = p.vz / (sp + 1e-6) * (1 - k) + ez / d * k;
            const double nl = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-6;
            p.vx = nx / nl * sp; p.vy = ny / nl * sp; p.vz = nz / nl * sp;
            p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
            if (d < 1.7) { jefeDanoJugador(5.0f, ahora); muere = true; }
            else if (jfSolido(p.x, p.y, p.z) || edad > 6.0) muere = true;
            if (muere) jfBoom(p.x, p.y, p.z, ahora, d < 8 ? 0.25 : 0.1);
        }
        if (muere) { g_proy[i] = g_proy.back(); g_proy.pop_back(); } else i++;
    }
    for (size_t i = 0; i < g_booms.size();) { if (ahora - g_booms[i].t0 > 0.5) { g_booms[i] = g_booms.back(); g_booms.pop_back(); } else i++; }
}

// ---------------------------------------------------------------- dibujo (triangulos con color, relativos a la camara)
struct JfXf { double ox, oy, oz, cy, sy; double cam[3]; };
static void jfCaja(std::vector<float>& v, const JfXf& T, float x0, float x1, float y0, float y1, float z0, float z1, const float* c,
                   float ang = 0, float px = 0, float py = 0, float pz = 0) {
    const float ca = std::cos(ang), sa = std::sin(ang);
    auto P = [&](float x, float y, float z, float* o, bool dirOnly) {
        float yy = y - (dirOnly ? 0 : py), zz = z - (dirOnly ? 0 : pz);
        float y1_ = (dirOnly ? 0 : py) + yy * ca - zz * sa, z1_ = (dirOnly ? 0 : pz) + yy * sa + zz * ca;
        const double wx = (dirOnly ? 0 : T.ox) + x * T.cy + z1_ * T.sy, wz = (dirOnly ? 0 : T.oz) + x * T.sy - z1_ * T.cy;
        o[0] = (float)(wx - (dirOnly ? 0 : T.cam[0])); o[1] = (float)((dirOnly ? 0 : T.oy) + y1_ - (dirOnly ? 0 : T.cam[1])); o[2] = (float)(wz - (dirOnly ? 0 : T.cam[2]));
    };
    struct Cara { float q[4][3]; float n[3]; float sh; };
    const Cara cs[6] = {
        {{{x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}}, {-1, 0, 0}, 0.80f},
        {{{x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}}, {1, 0, 0}, 0.80f},
        {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, {0, -1, 0}, 0.55f},
        {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, {0, 1, 0}, 1.00f},
        {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}, {0, 0, -1}, 0.70f},
        {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, {0, 0, 1}, 0.90f}};
    static const int I[6] = {0, 1, 2, 0, 2, 3};
    for (auto& f : cs) {
        float Q[4][3], N[3];
        for (int i = 0; i < 4; i++) P(f.q[i][0], f.q[i][1], f.q[i][2], Q[i], false);
        P(f.n[0], f.n[1], f.n[2], N, true);
        for (int q : I) { v.push_back(Q[q][0]); v.push_back(Q[q][1]); v.push_back(Q[q][2]); v.push_back(c[0] * f.sh); v.push_back(c[1] * f.sh); v.push_back(c[2] * f.sh); v.push_back(N[0]); v.push_back(N[1]); v.push_back(N[2]); }
    }
}

// ---------------------------------------------------------------- mecha detallado: partes con jerarquia (se puede agachar, girar el torso, mover brazos y piernas)
struct Mx { float r[3][3]; float t[3]; };
static Mx mxI() { Mx a{}; a.r[0][0] = a.r[1][1] = a.r[2][2] = 1; return a; }
static Mx mxMul(const Mx& A, const Mx& B) {
    Mx c{};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) c.r[i][j] = A.r[i][0] * B.r[0][j] + A.r[i][1] * B.r[1][j] + A.r[i][2] * B.r[2][j];
        c.t[i] = A.r[i][0] * B.t[0] + A.r[i][1] * B.t[1] + A.r[i][2] * B.t[2] + A.t[i];
    }
    return c;
}
static Mx mxTr(float x, float y, float z) { Mx a = mxI(); a.t[0] = x; a.t[1] = y; a.t[2] = z; return a; }
static Mx mxRot(int eje, float ang, float p0, float p1) {       // gira alrededor de un eje que pasa por el punto (p0,p1) del plano perpendicular
    const float c = std::cos(ang), s = std::sin(ang);
    Mx R = mxI();
    if (eje == 0) { R.r[1][1] = c; R.r[1][2] = -s; R.r[2][1] = s; R.r[2][2] = c; return mxMul(mxTr(0, p0, p1), mxMul(R, mxTr(0, -p0, -p1))); }          // X: (y,z)
    if (eje == 1) { R.r[0][0] = c; R.r[0][2] = s; R.r[2][0] = -s; R.r[2][2] = c; return mxMul(mxTr(p0, 0, p1), mxMul(R, mxTr(-p0, 0, -p1))); }          // Y: (x,z)
    R.r[0][0] = c; R.r[0][1] = -s; R.r[1][0] = s; R.r[1][1] = c; return mxMul(mxTr(p0, p1, 0), mxMul(R, mxTr(-p0, -p1, 0)));                              // Z: (x,y)
}
static void mxPt(const Mx& M, float x, float y, float z, float* o) {
    for (int i = 0; i < 3; i++) o[i] = M.r[i][0] * x + M.r[i][1] * y + M.r[i][2] * z + M.t[i];
}
struct MxCtx { std::vector<float>* v; double ox, oy, oz, cy, sy, sc; double cam[3]; };
static void mxCaja(const MxCtx& X, const Mx& M, float x0, float x1, float y0, float y1, float z0, float z1, const float* c) {
    struct Cara { float q[4][3]; float n[3]; float sh; };
    const Cara cs[6] = {
        {{{x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}}, {-1, 0, 0}, 0.80f},
        {{{x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}}, {1, 0, 0}, 0.80f},
        {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, {0, -1, 0}, 0.55f},
        {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, {0, 1, 0}, 1.00f},
        {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}, {0, 0, -1}, 0.70f},
        {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, {0, 0, 1}, 0.90f}};
    static const int I[6] = {0, 1, 2, 0, 2, 3};
    auto mundo = [&](const float* p, float* o) {
        const double wx = X.ox + (p[0] * X.cy + p[2] * X.sy) * X.sc, wy = X.oy + p[1] * X.sc, wz = X.oz + (p[0] * X.sy - p[2] * X.cy) * X.sc;
        o[0] = (float)(wx - X.cam[0]); o[1] = (float)(wy - X.cam[1]); o[2] = (float)(wz - X.cam[2]);
    };
    for (const Cara& f : cs) {
        float Q[4][3], N[3], tmp[3];
        for (int i = 0; i < 4; i++) { mxPt(M, f.q[i][0], f.q[i][1], f.q[i][2], tmp); mundo(tmp, Q[i]); }
        float nm[3];
        for (int i = 0; i < 3; i++) nm[i] = M.r[i][0] * f.n[0] + M.r[i][1] * f.n[1] + M.r[i][2] * f.n[2];
        N[0] = (float)(nm[0] * X.cy + nm[2] * X.sy); N[1] = nm[1]; N[2] = (float)(nm[0] * X.sy - nm[2] * X.cy);
        for (int q : I) {
            X.v->push_back(Q[q][0]); X.v->push_back(Q[q][1]); X.v->push_back(Q[q][2]);
            X.v->push_back(c[0] * f.sh); X.v->push_back(c[1] * f.sh); X.v->push_back(c[2] * f.sh);
            X.v->push_back(N[0]); X.v->push_back(N[1]); X.v->push_back(N[2]);
        }
    }
}
static void mechaDibujar(std::vector<float>& v, const Mecha& m, double camx, double camy, double camz, double ahora) {
    MxCtx X; X.v = &v; X.ox = m.x; X.oy = m.y; X.oz = m.z; X.cy = std::cos(m.yaw); X.sy = std::sin(m.yaw); X.sc = 0.84; X.cam[0] = camx; X.cam[1] = camy; X.cam[2] = camz;
    if (m.muriendo) X.ox += 0.12 * std::sin(ahora * 60.0);
    float GUN[3] = {0.20f, 0.21f, 0.25f}, ACE[3] = {0.46f, 0.48f, 0.54f}, OSC[3] = {0.07f, 0.07f, 0.09f}, ROJ[3] = {0.52f, 0.05f, 0.05f}, VID[3] = {0.04f, 0.05f, 0.08f}, LUZ[3] = {4.2f, 0.2f, 0.12f};
    if (ahora - m.flash < 0.12) for (int i = 0; i < 3; i++) { GUN[i] = ACE[i] = OSC[i] = 1.3f; ROJ[i] = 1.8f; }
    // ---- pose
    const double t3 = ahora - m.tEst;
    double ag = 0;                                                      // cuanto esta agachado en la pose de aterrizaje
    if (m.est == 1) ag = 0.25 * std::min(1.0, t3 / 1.0);
    else if (m.est == 2) ag = 0.55;
    else if (m.est == 3) ag = t3 < 2.5 ? 1.0 : std::max(0.0, 1.0 - (t3 - 2.5) / 0.5);
    const double w = (m.est == 0 ? std::sin(m.fase * 2.2) * 0.55 : 0.0);
    const float brazoG = (m.brazoGolpe > 0 && ahora - m.brazoGolpe < 0.7) ? (float)std::sin(std::min(1.0, (ahora - m.brazoGolpe) / 0.7) * 3.14159) * 2.2f : 0.0f;
    float thA[2], caA[2], foA[2];                                        // 0: lado derecho (adelantado al agacharse), 1: izquierdo (atras)
    for (int i = 0; i < 2; i++) {
        const double s = i == 0 ? 1.0 : -1.0;
        thA[i] = (float)(ag * (i == 0 ? -0.95 : 0.55) + w * s);
        caA[i] = (float)(ag * (i == 0 ? 1.25 : 1.15) + 0.7 * std::max(0.0, w * s));
        foA[i] = -(thA[i] + caA[i]) * 0.9f;
    }
    // altura de la cadera: que los pies toquen el suelo en cualquier pose
    float minY = 1e9f;
    for (int i = 0; i < 2; i++) {
        const float sx = i == 0 ? 1.55f : -1.55f;
        Mx Mt = mxRot(0, thA[i], 0, 0), Mk = mxMul(Mt, mxRot(0, caA[i], -2.7f, 0)), Mf = mxMul(Mk, mxRot(0, foA[i], -5.2f, 0));
        float p[3]; mxPt(Mf, sx, -5.9f, -1.4f, p); minY = std::min(minY, p[1]); mxPt(Mf, sx, -5.9f, 3.0f, p); minY = std::min(minY, p[1]);
    }
    const float rootY = -minY;
    const float lean = (float)(ag * 0.38 + (m.est == 0 ? 0.05 * std::sin(m.fase * 2.2 * 2) : 0.0)), twist = (float)(ag * 0.40), headP = (float)(ag * 0.18);
    const Mx P = mxTr(0, rootY, 0);
    // ---- pelvis y faldon
    mxCaja(X, P, -2.0f, 2.0f, -0.9f, 0.5f, -1.3f, 1.3f, OSC);
    mxCaja(X, P, -1.5f, 1.5f, -2.1f, 0.1f, 1.25f, 1.55f, GUN); mxCaja(X, P, -1.4f, 1.4f, -2.1f, -1.9f, 1.25f, 1.6f, ROJ); mxCaja(X, P, -0.25f, 0.25f, -1.3f, -0.8f, 1.55f, 1.65f, LUZ);
    mxCaja(X, P, -1.5f, 1.5f, -1.9f, 0.1f, -1.55f, -1.25f, GUN);
    for (int s : {-1, 1}) { mxCaja(X, P, s * 2.0f - 0.2f + (s > 0 ? 0.2f : 0.0f), s * 2.0f + 0.35f * s + (s > 0 ? 0.0f : -0.0f), -1.8f, 0.3f, -1.0f, 1.0f, ROJ); mxCaja(X, P, s * 2.0f, s * 2.0f + 0.12f * s, -1.4f, -1.0f, -0.5f, 0.5f, LUZ); }
    // ---- piernas
    for (int i = 0; i < 2; i++) {
        const float s = i == 0 ? 1.0f : -1.0f, hx = s * 1.55f;
        const Mx Mt = mxMul(P, mxRot(0, thA[i], 0, 0)), Mk = mxMul(Mt, mxRot(0, caA[i], -2.7f, 0)), Mf = mxMul(Mk, mxRot(0, foA[i], -5.2f, 0));
        // muslo
        mxCaja(X, Mt, hx - 0.8f, hx + 0.8f, -2.8f, 0.2f, -0.85f, 0.85f, ACE);
        mxCaja(X, mxMul(Mt, mxRot(2, s * 0.12f, hx, -1.2f)), hx + (s > 0 ? 0.8f : -1.1f), hx + (s > 0 ? 1.1f : -0.8f), -2.5f, -0.2f, -0.75f, 0.75f, GUN);
        mxCaja(X, Mt, hx - 0.2f, hx + 0.2f, -2.4f, -0.6f, 0.85f, 1.2f, ACE); mxCaja(X, Mt, hx - 0.28f, hx + 0.28f, -1.8f, -1.6f, 0.82f, 1.25f, OSC); mxCaja(X, Mt, hx - 0.28f, hx + 0.28f, -1.0f, -0.8f, 0.82f, 1.25f, OSC);
        mxCaja(X, Mt, hx - 0.5f, hx + 0.5f, -0.9f, -0.2f, -0.9f, -0.85f, ROJ);
        // rodilla
        mxCaja(X, Mk, hx - 0.75f, hx + 0.75f, -3.15f, -2.3f, -0.75f, 1.15f, OSC);
        mxCaja(X, Mk, hx - 0.65f, hx + 0.65f, -3.05f, -2.25f, 0.95f, 1.5f, ROJ); mxCaja(X, Mk, hx - 0.15f, hx + 0.15f, -2.8f, -2.5f, 1.5f, 1.58f, LUZ);
        // pantorrilla
        mxCaja(X, Mk, hx - 0.9f, hx + 0.9f, -5.3f, -3.0f, -1.0f, 0.95f, GUN);
        mxCaja(X, Mk, hx - 0.8f, hx + 0.8f, -5.0f, -3.2f, -1.8f, -1.0f, OSC);
        mxCaja(X, Mk, hx - 0.6f, hx + 0.6f, -5.1f, -3.1f, 0.95f, 1.3f, ACE); mxCaja(X, Mk, hx - 0.14f, hx + 0.14f, -4.6f, -3.6f, 1.3f, 1.37f, LUZ);
        mxCaja(X, mxMul(Mk, mxRot(2, s * 0.22f, hx + s * 0.9f, -4.0f)), hx + (s > 0 ? 0.88f : -1.25f), hx + (s > 0 ? 1.25f : -0.88f), -5.2f, -2.9f, -0.85f, 1.05f, ROJ);
        mxCaja(X, mxMul(Mk, mxRot(2, s * 0.22f, hx + s * 0.9f, -4.0f)), hx + (s > 0 ? 1.25f : -1.35f), hx + (s > 0 ? 1.35f : -1.25f), -4.9f, -3.2f, -0.6f, 0.8f, GUN);
        // pie
        mxCaja(X, Mf, hx - 0.95f, hx + 0.95f, -5.9f, -5.2f, -1.5f, 0.95f, OSC);
        mxCaja(X, Mf, hx - 1.05f, hx + 1.05f, -5.9f, -5.35f, 0.95f, 2.3f, GUN);
        for (int d = -1; d <= 1; d++) mxCaja(X, Mf, hx + d * 0.62f - 0.27f, hx + d * 0.62f + 0.27f, -5.9f, -5.5f, 2.3f, 3.1f, ACE);
        mxCaja(X, Mf, hx - 0.85f, hx + 0.85f, -5.55f, -5.3f, 0.95f, 2.45f, ROJ);
        mxCaja(X, Mf, hx - 0.5f, hx + 0.5f, -5.3f, -5.0f, -1.45f, -0.95f, ACE);
    }
    // ---- torso (se inclina y gira)
    const Mx Mtor = mxMul(P, mxMul(mxRot(0, lean, 0, 0), mxRot(1, twist, 0, 0)));
    for (int k = 0; k < 4; k++) mxCaja(X, Mtor, -1.35f + 0.0f, 1.35f, 0.5f + k * 0.38f, 0.82f + k * 0.38f, -0.95f, 0.95f, (k & 1) ? ACE : OSC);       // abdomen segmentado
    for (int s : {-1, 1}) mxCaja(X, Mtor, s * 1.5f - 0.12f, s * 1.5f + 0.12f, 0.6f, 2.0f, 0.5f, 0.75f, ACE);
    mxCaja(X, Mtor, -2.6f, 2.6f, 2.0f, 5.0f, -1.7f, 1.7f, GUN);                                               // pecho
    mxCaja(X, Mtor, -2.0f, 2.0f, 2.0f, 2.5f, 1.65f, 2.1f, ROJ);
    mxCaja(X, Mtor, -2.7f, 2.7f, 4.6f, 5.0f, 1.45f, 2.15f, ROJ);                                              // cornisa roja
    for (int s : {-1, 1}) mxCaja(X, mxMul(Mtor, mxRot(0, -0.35f, 2.6f, 1.7f)), s > 0 ? 0.1f : -2.5f, s > 0 ? 2.5f : -0.1f, 2.6f, 4.6f, 1.7f, 2.0f, VID);    // cristales inclinados
    mxCaja(X, Mtor, -0.18f, 0.18f, 2.6f, 4.6f, 1.75f, 2.05f, ACE);                                            // marco central
    mxCaja(X, Mtor, -0.5f, 0.5f, 2.7f, 3.5f, 1.95f, 2.15f, LUZ);                                              // nucleo
    for (int s : {-1, 1}) { mxCaja(X, Mtor, s * 2.6f, s * 2.6f + 0.25f * s, 2.3f, 4.5f, -1.1f, 1.1f, OSC); for (int k = 0; k < 3; k++) mxCaja(X, Mtor, s * 2.85f - 0.05f, s * 2.85f + 0.05f, 2.7f + k * 0.55f, 2.9f + k * 0.55f, -0.8f, 0.8f, LUZ); }
    mxCaja(X, Mtor, -2.1f, 2.1f, 2.2f, 4.9f, -2.7f, -1.7f, OSC);                                              // mochila
    for (int s : {-1, 1}) { mxCaja(X, Mtor, s * 1.0f - 0.45f, s * 1.0f + 0.45f, 4.9f, 6.0f, -2.6f, -1.9f, ACE); mxCaja(X, Mtor, s * 1.0f - 0.38f, s * 1.0f + 0.38f, 6.0f, 6.1f, -2.55f, -1.95f, LUZ); mxCaja(X, Mtor, s * 1.0f - 0.9f, s * 1.0f + 0.9f, 2.6f, 4.4f, -2.75f, -2.7f, LUZ); }
    mxCaja(X, Mtor, -0.6f, 0.6f, 4.9f, 5.5f, -0.6f, 0.6f, OSC);                                               // cuello
    // ---- cabeza
    const Mx Mh = mxMul(Mtor, mxRot(0, headP, 5.3f, 0));
    mxCaja(X, Mh, -0.95f, 0.95f, 5.5f, 7.0f, -0.95f, 1.0f, ACE);
    mxCaja(X, Mh, -0.6f, 0.6f, 7.0f, 7.3f, -0.7f, 0.6f, ROJ);
    mxCaja(X, Mh, -0.9f, 0.9f, 6.65f, 6.95f, 0.95f, 1.35f, GUN);                                              // ceja
    mxCaja(X, Mh, -0.72f, 0.72f, 5.6f, 6.65f, 1.0f, 1.25f, OSC);
    mxCaja(X, Mh, -0.72f, 0.72f, 6.2f, 6.5f, 1.25f, 1.33f, LUZ);                                              // visor rojo
    mxCaja(X, Mh, -0.55f, 0.55f, 5.5f, 5.85f, 1.0f, 1.45f, ACE);                                              // mentonera
    for (int s : {-1, 1}) { mxCaja(X, Mh, s * 1.0f - 0.12f + (s > 0 ? 0.12f : 0), s * 1.0f + 0.25f * s, 5.8f, 6.7f, -0.3f, 0.5f, ROJ); mxCaja(X, Mh, s * 0.85f - 0.06f, s * 0.85f + 0.06f, 7.0f, 7.9f, -0.5f, -0.38f, ACE); }
    // ---- brazos
    for (int i = 0; i < 2; i++) {
        const float s = i == 0 ? 1.0f : -1.0f, ax = s * 3.5f;
        float sh, el;
        if (i == 1) { sh = (float)(ag * -1.5 - w * 0.9 * (-1)); el = (float)(ag * -0.7); }                   // izquierdo: puno adelante
        else { sh = (float)(ag * 0.75 + w * 0.9) - brazoG; el = (float)(ag * -1.1 - (brazoG > 0 ? 0.4 : 0.0)); }
        if (m.est == 0 && i == 1) sh = (float)(-w * 0.9);
        const Mx Ma = mxMul(Mtor, mxRot(0, sh, 4.4f, 0)), Me = mxMul(Ma, mxRot(0, el, 2.0f, 0));
        mxCaja(X, Mtor, ax - 1.1f, ax + 1.1f, 4.1f, 5.4f, -1.35f, 1.35f, ROJ);                                // hombrera
        mxCaja(X, Mtor, ax - 1.15f, ax + 1.15f, 5.4f, 5.6f, -1.4f, 1.4f, ACE);
        mxCaja(X, Mtor, ax - 0.85f, ax + 0.85f, 5.6f, 6.2f, -1.1f, 1.0f, OSC);                                // lanzamisiles
        for (int k = 0; k < 6; k++) mxCaja(X, Mtor, ax - 0.7f + (k % 3) * 0.5f, ax - 0.4f + (k % 3) * 0.5f, 5.7f + (k / 3) * 0.28f, 5.95f + (k / 3) * 0.28f, 1.0f, 1.15f, LUZ);
        mxCaja(X, Ma, ax - 0.75f, ax + 0.75f, 2.1f, 4.4f, -0.75f, 0.75f, GUN);                                // brazo
        mxCaja(X, Ma, ax - 0.2f, ax + 0.2f, 2.4f, 4.1f, 0.75f, 1.1f, ACE); mxCaja(X, Ma, ax - 0.28f, ax + 0.28f, 3.2f, 3.4f, 0.72f, 1.15f, OSC);
        mxCaja(X, Ma, ax - 0.85f, ax + 0.85f, 1.65f, 2.3f, -0.85f, 0.85f, OSC); mxCaja(X, Ma, ax + s * 0.85f - 0.05f, ax + s * 0.85f + 0.1f * s, 1.8f, 2.1f, -0.2f, 0.2f, LUZ);
        mxCaja(X, Me, ax - 0.95f, ax + 0.95f, -0.45f, 2.0f, -0.95f, 1.0f, ACE);                               // antebrazo
        mxCaja(X, Me, ax - 0.9f, ax + 0.9f, 0.1f, 1.8f, 1.0f, 1.3f, ROJ); mxCaja(X, Me, ax - 0.12f, ax + 0.12f, 0.4f, 1.5f, 1.3f, 1.37f, LUZ);
        mxCaja(X, Me, ax - 1.05f, ax + 1.05f, 0.3f, 1.9f, -0.9f, 0.0f, GUN);
        // mano cerrada
        mxCaja(X, Me, ax - 0.7f, ax + 0.7f, -1.3f, -0.45f, -0.6f, 0.9f, OSC);
        for (int k = 0; k < 4; k++) { const float fx0 = ax - 0.6f + k * 0.31f; mxCaja(X, Me, fx0, fx0 + 0.27f, -1.35f, -0.9f, 0.9f, 1.35f, ACE); mxCaja(X, Me, fx0, fx0 + 0.27f, -1.6f, -1.3f, 0.2f, 1.35f, OSC); }
        mxCaja(X, Me, ax - 0.2f - s * 0.45f, ax + 0.2f - s * 0.45f, -1.2f, -0.8f, 0.2f, 1.0f, ACE);
    }
}

static float jefeSkyMul(float s) { if (g_mecha.viva && g_mecha.y < jfY(-30.0) && !g_almLuz) return s * 0.1f; return std::max(s, 0.8f); }
static void jefeDibujar(const float* vp, double camx, double camy, double camz, const float* sol, float solF, float skyMul, const float* cielo, float fogA, float fogB, double ahora) {
    if (!g_modoLuna) return;
    std::vector<float> v;
    const Mecha& m = g_mecha;
    if (m.viva) mechaDibujar(v, m, camx, camy, camz, ahora);
    // proyectiles
    JfXf T0; T0.ox = T0.oy = T0.oz = 0; T0.cy = 1; T0.sy = 0; T0.cam[0] = camx; T0.cam[1] = camy; T0.cam[2] = camz;
    for (const Proyectil& p : g_proy) {
        JfXf T1 = T0; T1.ox = p.x; T1.oy = p.y; T1.oz = p.z;
        if (p.tipo == 2) {
            const double sp = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz) + 1e-6;
            JfXf T2 = T1; const double yw = std::atan2(p.vx, -p.vz); T2.cy = std::cos(yw); T2.sy = std::sin(yw);
            const float pit = (float)std::asin(std::max(-1.0, std::min(1.0, -p.vy / sp)));
            const float cv[3] = {0.45f, 0.30f, 0.16f}, cp[3] = {0.75f, 0.77f, 0.80f}, cf[3] = {0.9f, 0.9f, 0.9f};
            jfCaja(v, T2, -0.03f, 0.03f, -0.03f, 0.03f, -0.5f, 0.45f, cv, pit, 0, 0, 0);
            jfCaja(v, T2, -0.05f, 0.05f, -0.05f, 0.05f, 0.45f, 0.65f, cp, pit, 0, 0, 0);
            jfCaja(v, T2, -0.08f, 0.08f, -0.01f, 0.01f, -0.55f, -0.4f, cf, pit, 0, 0, 0);
        } else if (p.tipo == 0) { const float c[3] = {0.5f, 2.2f, 4.5f}; const float s = 0.13f; jfCaja(v, T1, -s, s, -s, s, -s, s, c); const float c2[3] = {0.2f, 0.9f, 3.0f}; jfCaja(v, T1, -s * 2, s * 2, -s * 2, s * 2, -s * 2, s * 2, c2); }
        else {
            const double sp = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz) + 1e-6;
            JfXf T2 = T1; const double yw = std::atan2(p.vx, -p.vz); T2.cy = std::cos(yw); T2.sy = std::sin(yw);
            const float cu[3] = {0.7f, 0.7f, 0.72f}, ce[3] = {4.0f, 1.8f, 0.4f}, cl[3] = {4.0f, 0.2f, 0.1f};
            const float pit = (float)std::asin(std::max(-1.0, std::min(1.0, -p.vy / sp)));
            jfCaja(v, T2, -0.14f, 0.14f, -0.14f, 0.14f, -0.5f, 0.5f, cu, pit, 0, 0, 0);
            jfCaja(v, T2, -0.1f, 0.1f, -0.1f, 0.1f, 0.5f, 0.7f, cl, pit, 0, 0, 0);
            jfCaja(v, T2, -0.2f, 0.2f, -0.2f, 0.2f, -1.0f, -0.5f, ce, pit, 0, 0, 0);
        }
    }
    for (const Boom& b : g_booms) {
        const double t = (ahora - b.t0) / 0.5; const float s = (float)(0.4 + 2.8 * t);
        JfXf T1 = T0; T1.ox = b.x; T1.oy = b.y; T1.oz = b.z;
        const float c[3] = {4.0f * (float)(1.0 - t * 0.6), 1.6f * (float)(1.0 - t), 0.3f};
        jfCaja(v, T1, -s, s, -s, s, -s, s, c);
    }
    if (!v.empty()) {
        glDisable(GL_CULL_FACE);
        itemDibujar(vp, sol, solF, skyMul, cielo, fogA, fogB, v);
    }
}

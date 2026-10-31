// Medidor de rendimiento (v87). F3 muestra el panel; con el panel abierto se guarda rendimiento.csv (una linea cada 0.5 s).
// Mide: milisegundos de CPU por parte del cuadro, tiempo de GPU (consultas GL_TIME_ELAPSED, solo PC), los hilos de generacion/mallado,
// llamadas de dibujo, triangulos, colas pendientes y tirones. Cuesta casi nada (unos relojes por cuadro).
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace perf {
using clk = std::chrono::steady_clock;
enum { S_ACT, S_PREP, S_TERR, S_RESTO, S_HUD, S_SWAP, S_OTRO, S_N };            // partes del cuadro (hilo principal)
enum { P_SUBIR, P_REMALLAR, P_MOBS, P_LOD, P_CIELO, P_CULL, P_ORDEN, P_N };      // trozos: los 4 primeros dentro de S_ACT, los 3 ultimos dentro de S_PREP
static const char* NOM_S[S_N] = {"ACTUALIZAR (ENTRADA, FISICA, RED, MUNDO)", "PREPARAR (CAMARA, CIELO, CULLING)", "DIBUJAR TERRENO (ENVIO CPU)", "DIBUJAR RESTO (AGUA, LEJANO, MOBS, POST)", "HUD", "SWAP / ESPERA DE PANTALLA", "OTROS (TITULO, ETC)"};
static const char* NOM_P[P_N] = {"SUBIR MALLAS A LA GPU", "REMALLAR AL PICAR (HILO PRINCIPAL)", "MOBS Y JEFE", "MUNDO LEJANO (LOD)", "CAMARA, CIELO, CLIMA, MATRICES", "CULLING DE CHUNKS (FRUSTUM Y OCLUSION)", "ORDENAR CHUNKS"};

static bool visible = false;                  // panel abierto (F3)
static bool gpuOk = false;                    // hay consultas de tiempo de GPU
static double fr[S_N], frP[P_N];              // acumulado del cuadro actual (ms)
static double sum[S_N], sumP[P_N], mx[S_N], mxP[P_N];
static double sumFrame = 0, mxFrame = 0, sumGpu[2] = {0, 0}, mxGpu = 0;
static int nFr = 0, tir33 = 0, tir50 = 0;
static clk::time_point marca, t0Ventana;
static bool tieneMarca = false;
static double frameMs = 0;

// estadisticas de los hilos (microsegundos acumulados, escritos por los hilos de trabajo)
static std::atomic<long long> genUs{0}, genN{0}, mallaUs{0}, luzUs{0}, mallaN{0}, lodUs{0}, lodN{0};
struct Cnt { int llamadas = 0, tri = 0, visibles = 0, tapados = 0, chunks = 0, lodTiles = 0, genPend = 0, meshPend = 0, lodPend = 0, cola = 0, hechos = 0, ovejas = 0, neans = 0, sueltos = 0, hilos = 0, w = 0, h = 0; bool vsync = true; };
static Cnt cnt;

// instantanea del ultimo periodo (lo que dibuja el panel)
struct Snap {
    bool ok = false;
    double s[S_N] = {}, smx[S_N] = {}, p[P_N] = {}, pmx[P_N] = {};
    double frame = 0, frameMx = 0, fps = 0, gpuA = -1, gpuB = -1, gpuMx = -1;
    int tir33 = 0, tir50 = 0;
    double genMs = 0, mallaMs = 0, luzMs = 0, lodMs = 0; double genPorS = 0, mallaPorS = 0, lodPorS = 0;
    double ramMB = 0;
    std::string diag, diag2;
    Cnt c;
};
static Snap snap;
static std::string archivoLog = "rendimiento.csv";
static bool logCabecera = false;

static inline double ms(clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
static inline clk::time_point ahora() { return clk::now(); }
static inline void trozo(int id, clk::time_point a) { frP[id] += ms(a, clk::now()); }
static inline void marcar(int s) { auto n = clk::now(); fr[s] += ms(marca, n); marca = n; }
struct Alcance {      // mide un trozo dentro de S_ACT
    int id; clk::time_point a;
    explicit Alcance(int i) : id(i), a(clk::now()) {}
    ~Alcance() { frP[id] += ms(a, clk::now()); }
};
struct Reloj {        // para hilos de trabajo
    std::atomic<long long>* dst; clk::time_point a;
    explicit Reloj(std::atomic<long long>& d) : dst(&d), a(clk::now()) {}
    ~Reloj() { *dst += (long long)(ms(a, clk::now()) * 1000.0); }
};

// ---- GPU: consultas de tiempo (A = cielo + terreno opaco, B = el resto hasta el swap)
#ifndef ANDROID_GAME
static const int QN = 4;
static GLuint q[2][QN];
static bool qEmit[2][QN];
static int qAct = -1, qSlot = 0;
static bool qInit = false;
static void gpuIniciar() {
    glGenQueries(2 * QN, &q[0][0]);
    qInit = true; gpuOk = true;
    for (int i = 0; i < 2; i++) for (int j = 0; j < QN; j++) qEmit[i][j] = false;
}
static void gpuEmpezar(int i) { if (!qInit || qAct >= 0) return; glBeginQuery(0x88BF, q[i][qSlot]); qEmit[i][qSlot] = true; qAct = i; }
static void gpuTerminar() { if (!qInit || qAct < 0) return; glEndQuery(0x88BF); qAct = -1; }
static void gpuLeer(double& a, double& b) {       // resultados de hace unos cuadros (sin parar la GPU)
    a = b = -1;
    if (!qInit) return;
    const int viejo = (qSlot + 1) % QN;
    double r[2] = {-1, -1};
    for (int i = 0; i < 2; i++) if (qEmit[i][viejo]) {
        GLuint disp = 0; glGetQueryObjectuiv(q[i][viejo], 0x8867, &disp);
        if (disp) { GLuint ns = 0; glGetQueryObjectuiv(q[i][viejo], 0x8866, &ns); r[i] = ns / 1.0e6; qEmit[i][viejo] = false; }
    }
    a = r[0]; b = r[1];
}
#else
static void gpuIniciar() { gpuOk = false; }
static void gpuEmpezar(int) {}
static void gpuTerminar() {}
static void gpuLeer(double& a, double& b) { a = b = -1; }
static int qSlot = 0;
#endif

static double ramMB() {
#ifdef __linux__
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long a = 0, b = 0; int n = std::fscanf(f, "%ld %ld", &a, &b); std::fclose(f);
    return n == 2 ? b * 4096.0 / 1048576.0 : 0;
#else
    return 0;
#endif
}

static double gA = -1, gB = -1;       // ultimo tiempo de GPU leido
static void cerrarVentana(double ahoraS);
static void diagnosticar(Snap& s) {
    const double cpu = s.s[S_ACT] + s.s[S_PREP] + s.s[S_TERR] + s.s[S_RESTO] + s.s[S_HUD];
    const double gpu = (s.gpuA >= 0 && s.gpuB >= 0) ? s.gpuA + s.gpuB : -1;
    const double espera = s.s[S_SWAP];
    char b[200];
    s.diag2.clear();
    if (s.frame > 0 && espera > 0.45 * s.frame && (gpu < 0 || gpu < 0.75 * s.frame) && cpu < 0.75 * s.frame) {
        s.diag = s.c.vsync ? "NADA LIMITA: LA PANTALLA (VSYNC) ESPERA. PULSA V PARA APAGARLO Y MEDIR" : "NADA LIMITA FUERTE: SOBRA CPU Y GPU";
    } else if (gpu >= 0 && gpu > cpu * 1.1) {
        std::snprintf(b, sizeof b, "LIMITA LA GPU (%s: %.1f MS)", s.gpuA > s.gpuB ? "CIELO Y TERRENO" : "AGUA, LEJANO, MOBS, POST Y HUD", std::max(s.gpuA, s.gpuB));
        s.diag = b;
        s.diag2 = s.c.tri > 1500000 ? "MUCHOS TRIANGULOS: BAJA LA DISTANCIA O USA MAS FUSION DE CARAS" : "PUEDE SER RESOLUCION O SOMBREADO: PRUEBA UNA VENTANA MAS CHICA";
    } else {
        int mayor = 0; for (int i = 1; i < S_SWAP; i++) if (s.s[i] > s.s[mayor]) mayor = i;
        std::snprintf(b, sizeof b, "LIMITA LA CPU: %s (%.1f MS)", NOM_S[mayor], s.s[mayor]);
        s.diag = b;
        if (mayor == S_ACT) {
            int sub = 0; for (int i = 1; i <= P_LOD; i++) if (s.p[i] > s.p[sub]) sub = i;
            if (s.p[sub] > 0.25 * s.s[S_ACT]) { std::snprintf(b, sizeof b, "LO MAS PESADO: %s (%.1f MS)", NOM_P[sub], s.p[sub]); s.diag2 = b; }
            else s.diag2 = "FISICA, ENTRADA, RED O STREAMING DE CHUNKS";
        } else if (mayor == S_TERR) s.diag2 = s.c.llamadas > 400 ? "MUCHAS LLAMADAS DE DIBUJO: AGRUPAR MALLAS AYUDARIA" : "ENVIO DE DIBUJO LENTO (DRIVER)";
        else if (mayor == S_PREP) {
            int sub = P_CIELO; for (int i = P_CIELO + 1; i < P_N; i++) if (s.p[i] > s.p[sub]) sub = i;
            std::snprintf(b, sizeof b, "LO MAS PESADO: %s (%.1f MS)", NOM_P[sub], s.p[sub]); s.diag2 = b;
        }
    }
}
static void escribirLog(const Snap& s) {
    FILE* f = std::fopen(archivoLog.c_str(), logCabecera ? "a" : "w");
    if (!f) return;
    if (!logCabecera) {
        std::fprintf(f, "fps,frame_ms,peor_ms,gpu_cielo_terreno_ms,gpu_resto_ms,actualizar_ms,preparar_ms,terreno_ms,resto_ms,hud_ms,swap_ms,otros_ms,subir_ms,remallar_ms,mobs_ms,lod_ms,llamadas,triangulos,visibles,tapados,chunks,lod,genPend,meshPend,lodPend,cola,hechos,gen_ms,malla_ms,luz_ms,lod_ms_hilo,ram_mb,tirones33,tirones50,vsync,ancho,alto,diagnostico\n");
        logCabecera = true;
    }
    std::fprintf(f, "%.1f,%.2f,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.0f,%d,%d,%d,%d,%d,%s\n",
                 s.fps, s.frame, s.frameMx, s.gpuA, s.gpuB, s.s[0], s.s[1], s.s[2], s.s[3], s.s[4], s.s[5], s.s[6], s.p[0], s.p[1], s.p[2], s.p[3],
                 s.c.llamadas, s.c.tri, s.c.visibles, s.c.tapados, s.c.chunks, s.c.lodTiles, s.c.genPend, s.c.meshPend, s.c.lodPend, s.c.cola, s.c.hechos,
                 s.genMs, s.mallaMs, s.luzMs, s.lodMs, s.ramMB, s.tir33, s.tir50, s.c.vsync ? 1 : 0, s.c.w, s.c.h, s.diag.c_str());
    std::fclose(f);
}
// cierra el periodo (cada 0.5 s) y deja la instantanea lista
static void cerrarPeriodo() {
    if (nFr <= 0) return;
    const double n = (double)nFr;
    Snap s;
    s.ok = true;
    for (int i = 0; i < S_N; i++) { s.s[i] = sum[i] / n; s.smx[i] = mx[i]; }
    for (int i = 0; i < P_N; i++) { s.p[i] = sumP[i] / n; s.pmx[i] = mxP[i]; }
    s.frame = sumFrame / n; s.frameMx = mxFrame; s.fps = s.frame > 0 ? 1000.0 / s.frame : 0;
    s.gpuA = sumGpu[0] >= 0 && gpuOk ? sumGpu[0] / n : -1; s.gpuB = sumGpu[1] >= 0 && gpuOk ? sumGpu[1] / n : -1; s.gpuMx = mxGpu;
    s.tir33 = tir33; s.tir50 = tir50;
    const long long gn = genN.exchange(0), mn = mallaN.exchange(0), ln = lodN.exchange(0);
    const double gu = (double)genUs.exchange(0), mu = (double)mallaUs.exchange(0), lu = (double)luzUs.exchange(0), lod = (double)lodUs.exchange(0);
    s.genMs = gn ? gu / 1000.0 / gn : 0; s.mallaMs = mn ? mu / 1000.0 / mn : 0; s.luzMs = mn ? lu / 1000.0 / mn : 0; s.lodMs = ln ? lod / 1000.0 / ln : 0;
    const double dur = ms(t0Ventana, clk::now()) / 1000.0;
    s.genPorS = dur > 0 ? gn / dur : 0; s.mallaPorS = dur > 0 ? mn / dur : 0; s.lodPorS = dur > 0 ? ln / dur : 0;
    s.ramMB = ramMB();
    s.c = cnt;
    diagnosticar(s);
    snap = s;
    if (visible) escribirLog(snap);
    for (int i = 0; i < S_N; i++) sum[i] = mx[i] = 0;
    for (int i = 0; i < P_N; i++) sumP[i] = mxP[i] = 0;
    sumFrame = mxFrame = 0; sumGpu[0] = sumGpu[1] = 0; mxGpu = 0; nFr = 0; tir33 = tir50 = 0;
    t0Ventana = clk::now();
}
// llamar al principio de cada cuadro
static void inicioCuadro() {
    auto n = clk::now();
    static bool iniciado = false;
    if (!iniciado) { iniciado = true; gpuIniciar(); t0Ventana = n; }
    if (tieneMarca) {
        fr[S_OTRO] += ms(marca, n);
        double tot = 0; for (int i = 0; i < S_N; i++) tot += fr[i];
        for (int i = 0; i < S_N; i++) { sum[i] += fr[i]; mx[i] = std::max(mx[i], fr[i]); }
        for (int i = 0; i < P_N; i++) { sumP[i] += frP[i]; mxP[i] = std::max(mxP[i], frP[i]); }
        sumFrame += tot; mxFrame = std::max(mxFrame, tot); nFr++;
        if (tot > 33.4) tir33++;
        if (tot > 50.0) tir50++;
        frameMs = tot;
        double a, b; gpuLeer(a, b);
        if (a >= 0) sumGpu[0] += a;
        if (b >= 0) sumGpu[1] += b;
        if (a >= 0 && b >= 0) mxGpu = std::max(mxGpu, a + b);
        if (ms(t0Ventana, n) >= 500.0) cerrarPeriodo();
    }
    for (int i = 0; i < S_N; i++) fr[i] = 0;
    for (int i = 0; i < P_N; i++) frP[i] = 0;
#ifndef ANDROID_GAME
    qSlot = (qSlot + 1) % QN;
#else
    qSlot = 1;
#endif
    marca = n; tieneMarca = true;
}
}  // namespace perf

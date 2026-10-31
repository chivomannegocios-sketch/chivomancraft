// ============================================================================
//  JUEGO VOXEL  -  prototipo 3D tipo Minecraft con cubitos mas pequenos que un bloque
//  Autor: CHlVOMAN
//
//  CONTROLES
//    W A S D ........ moverte        Espacio ... saltar (en vuelo: subir)
//    Shift .......... correr         Ctrl ...... (en vuelo: bajar)
//    F .............. volar / caminar (volando atraviesas todo)
//    Clic izquierdo . romper         Clic derecho ... poner
//    B .............. pincel: CUBITO (pequeno) <-> BLOQUE completo (SUB x SUB x SUB cubitos)
//    1..7 / rueda ... material (pasto, tierra, piedra, arena, agua, madera, hojas)
//    F5 ............. guardar ahora (tambien se guarda solo cada 30 s y al salir)
//    V .............. activar / quitar la sincronizacion vertical (FPS libres)
//    Esc ............ soltar el mouse (clic para volver a capturarlo) | Esc doble: salir
// ============================================================================
#include "gl_loader.h"
#include "mundo.h"
#include "luz.h"
#include "tactil.h"
#include "red.h"
#include <cctype>
#include <deque>
#include <string>
#include <vector>
static bool g_chat = false;      // escribiendo en el chat: el teclado no mueve al jugador
static inline int teclaReal_(GLFWwindow* w, int k) { return g_chat ? 0 : glfwGetKey(w, k); }
#define glfwGetKey(w, k) teclaReal_(w, k)
struct ChatMsg { std::string txt; double t; };
static std::deque<ChatMsg> g_chatLog;
static std::string g_chatBuf;

#include <unordered_map>
#include <unordered_set>
#include <array>
#include <random>
#include <algorithm>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <filesystem>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// ---------------------------------------------------------------- utilidades matematicas
struct V3 { double x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static V3 cruz(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static V3 norm(V3 a) { double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return l > 0 ? a * (1.0 / l) : a; }

static void matMul(float* o, const float* a, const float* b) {   // o = a * b (columna-mayor)
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    std::memcpy(o, t, sizeof t);
}
static void matPerspectiva(float* m, float fovy, float asp, float n, float f) {
    float t = 1.0f / std::tan(fovy * 0.5f);
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1.0f; m[14] = 2.0f * f * n / (n - f);
}
static void matVista(float* m, V3 f, V3 up = {0, 1, 0}) {   // solo rotacion (la posicion se resta en el CPU para no perder precision)
    V3 r = norm(cruz(f, up));
    V3 u = cruz(r, f);
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = (float)r.x; m[4] = (float)r.y; m[8] = (float)r.z;
    m[1] = (float)u.x; m[5] = (float)u.y; m[9] = (float)u.z;
    m[2] = (float)-f.x; m[6] = (float)-f.y; m[10] = (float)-f.z;
    m[15] = 1.0f;
}

// ---------------------------------------------------------------- chunks y mundo
enum { ST_GEN = 0, ST_READY = 1, ST_MESHING = 2, ST_MESHED = 3 };

struct MallaGL {
    GLuint vao = 0, vbo = 0, ibo = 0;
    int n = 0;
    void liberar() {
        if (vao) { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ibo); }
        vao = vbo = ibo = 0; n = 0;
    }
    void subir(const DatosMalla& m) {
        if (m.i.empty()) { liberar(); return; }
        if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ibo); }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m.v.size() * sizeof(Vertice)), m.v.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(m.i.size() * sizeof(uint32_t)), m.i.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertice), (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(Vertice), (const void*)12);
        n = (int)m.i.size();
    }
};

// textura 3D con la luz del chunk (2 bytes por celda: cielo y bloque)
struct LuzGL {
    GLuint tex = 0;
    void liberar() { if (tex) { glDeleteTextures(1, &tex); tex = 0; } }
    void subir(const std::vector<uint8_t>& d) {
        if (d.empty()) { liberar(); return; }
        if (!tex) {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_3D, tex);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        } else glBindTexture(GL_TEXTURE_3D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, LN, LN, LN, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.data());
    }
};

struct Chunk {
    int cx, cy, cz;
    std::atomic<int> state{ST_GEN};
    DatosChunk dat;                       // lo escribe el hilo generador antes de ST_READY
    std::atomic<uint32_t> version{0};
    std::atomic<bool> viva{true};
    std::atomic<bool> sucioDisco{false};  // tiene cambios sin guardar
    std::mutex mtx;                       // protege resOp/resAg
    DatosMalla resOp, resAg;
    std::vector<uint8_t> resLuz;
    uint32_t resVersion = 0;
    MallaGL gOp, gAg;
    LuzGL gLuz;
    Chunk(int x, int y, int z) : cx(x), cy(y), cz(z) {}
};
typedef std::shared_ptr<Chunk> ChunkP;

static inline int64_t clave(int cx, int cy, int cz) {
    return ((int64_t)(cx + 32768) << 40) | ((int64_t)(cy + 32768) << 20) | (int64_t)(cz + 32768);
}
static std::unordered_map<int64_t, ChunkP> g_mapa;

static ChunkP buscar(int cx, int cy, int cz) {
    auto it = g_mapa.find(clave(cx, cy, cz));
    return it == g_mapa.end() ? nullptr : it->second;
}

// ---------------------------------------------------------------- hilos de trabajo
// los hilos que generan y mallan chunks trabajan con prioridad baja, para que el hilo que dibuja no se quede sin CPU al cargar terreno nuevo
#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread(void);
extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void*, int);
static void bajarPrioridadHilo() { SetThreadPriority(GetCurrentThread(), -1); }      // THREAD_PRIORITY_BELOW_NORMAL
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
static void bajarPrioridadHilo() { setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 5); }
#else
static void bajarPrioridadHilo() {}
#endif
struct Pool {
    std::vector<std::thread> hilos;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void()>> cola;
    bool parar = false;
    std::atomic<int> activos{1000};                       // cuantos hilos pueden trabajar (los demas esperan): cambia con el perfil de rendimiento
    void fijarActivos(int n) { activos.store(n); cv.notify_all(); }
    void iniciar(int n) {
        for (int i = 0; i < n; i++)
            hilos.emplace_back([this, i] {
                bajarPrioridadHilo();
                for (;;) {
                    std::function<void()> f;
                    {
                        std::unique_lock<std::mutex> lk(m);
                        cv.wait(lk, [this, i] { return parar || (!cola.empty() && i < activos.load()); });
                        if (parar && cola.empty()) return;
                        f = std::move(cola.front());
                        cola.pop_front();
                    }
                    f();
                }
            });
    }
    void meter(std::function<void()> f) {
        { std::lock_guard<std::mutex> lk(m); cola.push_back(std::move(f)); }
        cv.notify_one();
    }
    void detener() {
        { std::lock_guard<std::mutex> lk(m); parar = true; cola.clear(); }
        cv.notify_all();
        for (auto& t : hilos) t.join();
        hilos.clear();
    }
} g_pool;

static std::atomic<int> g_genPend{0}, g_meshPend{0};
static std::mutex g_hechoM;
static std::vector<ChunkP> g_hecho;           // mallas terminadas por los hilos

static const int DIRS[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

// copia los datos del chunk y sus 6 vecinos; devuelve false si falta alguno
static bool juntar(const Chunk& c, DatosChunk s[7]) {
    bool completo = true;
    s[0] = c.dat;
    for (int k = 0; k < 6; k++) {
        int nx = c.cx + DIRS[k][0], ny = c.cy + DIRS[k][1], nz = c.cz + DIRS[k][2];
        s[k + 1] = DatosChunk();
        if (ny < 0) { s[k + 1].uni = PIEDRA; continue; }
        if (ny >= HYC) { s[k + 1].uni = AIRE; continue; }
        ChunkP n = buscar(nx, ny, nz);
        if (!n || n->state.load(std::memory_order_acquire) < ST_READY) { completo = false; s[k + 1].uni = AIRE; continue; }
        s[k + 1] = n->dat;
    }
    return completo;
}

// copia las 3x3 columnas de chunks alrededor de c (para calcular la luz). Lo que falta queda marcado (uni = 255).
static void juntarColumnas(const Chunk& c, SnapColumnas& sn) {
    sn.completo = true;
    for (int ix = 0; ix < 3; ix++) for (int iz = 0; iz < 3; iz++) for (int cy = 0; cy < HYC; cy++) {
        DatosChunk& d = sn.c[ix][cy][iz];
        if (ix == 1 && iz == 1 && cy == c.cy) { d = c.dat; continue; }
        ChunkP n = buscar(c.cx + ix - 1, cy, c.cz + iz - 1);
        if (!n || n->state.load(std::memory_order_acquire) < ST_READY) { d = DatosChunk(); d.uni = 255; sn.completo = false; }
        else d = n->dat;
    }
}

static bool g_lodOnAntes = false;                    // v59: mundo lejano que tenias antes de ir a la Luna
static bool g_jRespOk = false; static double g_jResp[3] = {0, 0, 0};   // v59: donde volver a aparecer en la Tierra si sales del juego en pleno viaje
static std::string g_dirTierra;
static bool g_irBase = false;
static void irALaBase(double x, double d, double y, double yaw, double pitch);
// ---------------------------------------------------------------- guardado en disco
// Solo se guardan los chunks que cambiaste. Carpeta: mundo_SUB<N>/ (junto al juego). Formato: cabecera + datos comprimidos (RLE).
static std::string g_dir = "mundos/mundo_1";        // carpeta del mundo elegido (se fija en el menu de mundos)
static std::atomic<int> g_cargadosDisco{0}, g_guardados{0};

static std::string rutaChunk(int cx, int cy, int cz) {
    return g_dir + "/c_" + std::to_string(cx) + "_" + std::to_string(cy) + "_" + std::to_string(cz) + ".bin";
}

static bool guardarChunk(const Chunk& c) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(g_dir, ec);
    std::string ruta = rutaChunk(c.cx, c.cy, c.cz), tmp = ruta + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    uint8_t cab[6] = {'V', 'X', 1, (uint8_t)CS, c.dat.uni, (uint8_t)(c.dat.d ? 1 : 0)};
    std::fwrite(cab, 1, 6, f);
    if (c.dat.d) {
        const std::vector<uint8_t>& d = *c.dat.d;
        size_t i = 0;
        while (i < d.size()) {
            uint8_t v = d[i];
            size_t n = 1;
            while (i + n < d.size() && d[i + n] == v && n < 65535) n++;
            uint8_t reg[3] = {(uint8_t)(n & 255), (uint8_t)(n >> 8), v};
            std::fwrite(reg, 1, 3, f);
            i += n;
        }
    }
    std::fclose(f);
    fs::rename(tmp, ruta, ec);
    if (ec) { fs::remove(ruta, ec); fs::rename(tmp, ruta, ec); }
    if (!ec) g_guardados++;
    return !ec;
}

static bool cargarChunkArchivo(int cx, int cy, int cz, DatosChunk& out) {
    FILE* f = std::fopen(rutaChunk(cx, cy, cz).c_str(), "rb");
    if (!f) return false;
    uint8_t cab[6];
    bool ok = std::fread(cab, 1, 6, f) == 6 && cab[0] == 'V' && cab[1] == 'X' && cab[2] == 1 && cab[3] == CS;
    DatosChunk r;
    if (ok) {
        r.uni = cab[4];
        if (cab[5]) {
            auto v = std::make_shared<std::vector<uint8_t>>((size_t)CS * CS * CS);
            size_t i = 0;
            uint8_t reg[3];
            while (i < v->size() && std::fread(reg, 1, 3, f) == 3) {
                size_t n = reg[0] | (reg[1] << 8);
                if (i + n > v->size()) { ok = false; break; }
                std::fill(v->begin() + i, v->begin() + i + n, reg[2]);
                i += n;
            }
            if (i != v->size()) ok = false;
            r.d = v;
        }
    }
    std::fclose(f);
    if (!ok) return false;
    out = r;
    g_cargadosDisco++;
    return true;
}

static int guardarTodosLosSucios() {
    int n = 0;
    for (auto& kv : g_mapa) {
        Chunk& c = *kv.second;
        if (c.sucioDisco.load() && c.state.load() >= ST_READY) {
            if (guardarChunk(c)) { c.sucioDisco = false; n++; }
        }
    }
    return n;
}

static void pedirGenerar(const ChunkP& c) {
    g_genPend++;
    g_pool.meter([c] {
        if (!cargarChunkArchivo(c->cx, c->cy, c->cz, c->dat)) c->dat = generarChunk(c->cx, c->cy, c->cz);
        c->state.store(ST_READY, std::memory_order_release);
        g_genPend--;
    });
}

static void pedirMalla(const ChunkP& c, int d2) {
    DatosChunk s[7];
    if (!juntar(*c, s)) return;
    auto sn = std::make_shared<SnapColumnas>();
    juntarColumnas(*c, *sn);
    if (!sn->completo && d2 <= (RC - 1) * (RC - 1)) return;      // esperar a los vecinos (la luz los necesita)
    uint32_t ver = c->version.load();
    c->state.store(ST_MESHING);
    g_meshPend++;
    DatosChunk copia[7];
    for (int i = 0; i < 7; i++) copia[i] = s[i];
    g_pool.meter([c, ver, copia, sn] {
        DatosMalla op, ag;
        std::vector<uint8_t> luz;
        if (c->viva.load()) {
            construirMalla(copia, op, ag);
            if (!op.i.empty() || !ag.i.empty()) {
                luz.resize((size_t)LN * LN * LN * 4);
                calcularLuz(*sn, c->cy, luz.data());
            }
        }
        {
            std::lock_guard<std::mutex> lk(c->mtx);
            c->resOp = std::move(op);
            c->resAg = std::move(ag);
            c->resLuz = std::move(luz);
            c->resVersion = ver;
        }
        { std::lock_guard<std::mutex> lk(g_hechoM); g_hecho.push_back(c); }
        g_meshPend--;
    });
}

static void recogerMallas(int maxSubidas) {
    std::vector<ChunkP> lista;
    {
        std::lock_guard<std::mutex> lk(g_hechoM);
        int n = std::min<int>(maxSubidas, (int)g_hecho.size());
        lista.assign(g_hecho.begin(), g_hecho.begin() + n);
        g_hecho.erase(g_hecho.begin(), g_hecho.begin() + n);
    }
    for (auto& c : lista) {
        std::lock_guard<std::mutex> lk(c->mtx);
        if (!c->viva.load() || c->resVersion != c->version.load()) {
            if (c->viva.load() && c->state.load() == ST_MESHING) c->state.store(ST_READY);   // quedo vieja: se rehace
            continue;
        }
        c->gOp.subir(c->resOp);
        c->gAg.subir(c->resAg);
        c->gLuz.subir(c->resLuz);
        c->resOp = DatosMalla(); c->resAg = DatosMalla(); c->resLuz.clear();
        c->state.store(ST_MESHED);
    }
}

// remalla en el momento (al editar bloques)
static void remallarYa(const ChunkP& c) {
    DatosChunk s[7];
    juntar(*c, s);
    DatosMalla op, ag;
    auto t0 = std::chrono::steady_clock::now();
    construirMalla(s, op, ag);
    auto t1 = std::chrono::steady_clock::now();
    std::vector<uint8_t> luz;
    if (!op.i.empty() || !ag.i.empty()) {
        auto sn = std::make_unique<SnapColumnas>();
        juntarColumnas(*c, *sn);
        luz.resize((size_t)LN * LN * LN * 4);
        calcularLuz(*sn, c->cy, luz.data());
    }
    auto t2 = std::chrono::steady_clock::now();
    if (std::getenv("DBGROMPER")) std::printf("  remallarYa: malla %.2f ms, luz %.2f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(t2 - t1).count());
    c->version++;
    std::lock_guard<std::mutex> lk(c->mtx);
    c->gOp.subir(op);
    c->gAg.subir(ag);
    c->gLuz.subir(luz);
    c->state.store(ST_MESHED);
}

// Tras editar, la luz de los chunks cercanos puede cambiar (hasta LMAX cubitos a los lados y toda la columna hacia abajo).
// Se marcan para rehacerse en los hilos de trabajo; mientras tanto se sigue viendo la malla anterior.
static uint8_t leerCubito(int vx, int vy, int vz, bool* conocido);
static int g_marcadosN = 0;
static void marcarLuzSucia(int xmin, int xmax, int ymin, int ymax, int zmin, int zmax, const std::vector<ChunkP>& excluir) {
    int cx0 = divPiso(xmin - LMAX, CS), cx1 = divPiso(xmax + LMAX, CS);
    int cz0 = divPiso(zmin - LMAX, CS), cz1 = divPiso(zmax + LMAX, CS);
    int cy1 = std::min(HYC - 1, divPiso(ymax + LMAX, CS));
    // la luz del cielo baja por la columna editada hasta el primer bloque opaco; mas abajo (y mas lejos que LMAX) nada cambia
    int yPiso = ymin;
    for (int k = 0; k < 4; k++) {
        int ex = (k & 1) ? xmax : xmin, ez = (k & 2) ? zmax : zmin, y = ymin;
        while (y > 0) {
            bool conocido = true;
            uint8_t t = leerCubito(ex, y, ez, &conocido);
            if (!conocido || luzCoste(t) == 255) break;
            y--;
        }
        yPiso = std::min(yPiso, y);
    }
    int cy0 = std::max(0, divPiso(yPiso - LMAX - 1, CS));
    for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) for (int cy = cy0; cy <= cy1; cy++) {
        ChunkP c = buscar(cx, cy, cz);
        if (!c || c->state.load() != ST_MESHED) continue;
        if (!c->gOp.n && !c->gAg.n) continue;
        if (std::find(excluir.begin(), excluir.end(), c) != excluir.end()) continue;
        c->version++;
        c->state.store(ST_READY);
        g_marcadosN++;
    }
}

// ---------------------------------------------------------------- acceso a cubitos
static uint8_t leerCubito(int vx, int vy, int vz, bool* conocido = nullptr) {
    if (conocido) *conocido = true;
    int cy = divPiso(vy, CS);
    if (cy < 0) return PIEDRA;
    if (cy >= HYC) return AIRE;
    ChunkP c = buscar(divPiso(vx, CS), cy, divPiso(vz, CS));
    if (!c || c->state.load(std::memory_order_acquire) < ST_READY) {
        if (conocido) *conocido = false;
        return PIEDRA;                     // lo no cargado cuenta como solido (no te caes al vacio)
    }
    if (!c->dat.d) return c->dat.uni;
    return (*c->dat.d)[IDX(vx - c->cx * CS, vy - c->cy * CS, vz - c->cz * CS)];
}

// columnas (x, z de chunk) que tienen algun chunk editado: se siguen dibujando con chunks reales aunque queden lejos
static inline int64_t colKey(int cx, int cz) { return ((int64_t)(cx + 1048576) << 21) | (int64_t)(cz + 1048576); }
static inline void colDec(int64_t k, int& cx, int& cz) { cx = (int)(k >> 21) - 1048576; cz = (int)(k & 0x1FFFFF) - 1048576; }

// ---- superficie REAL (con tus cambios) de una columna de chunks: altura minima (en bloques) del suelo solido mas alto
// Sirve para no dibujar (ni en imagen ni en sombras) lo que queda enterrado bajo ella: cuevas y minas de columnas editadas.
// Devuelve -1e9 si la columna no esta completa (no se oculta nada).
struct SupCol { float minH; uint32_t firma; double t; };
static std::unordered_map<int64_t, SupCol> g_supCache;
static double supAhora() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static float supMinColumna(int cx, int cz) {
    ChunkP cs[HYC];
    uint32_t firma = 17;
    for (int cy = 0; cy < HYC; cy++) {
        cs[cy] = buscar(cx, cy, cz);
        if (!cs[cy] || cs[cy]->state.load(std::memory_order_acquire) < ST_READY) return -1e9f;
        firma = firma * 31u + cs[cy]->version.load(std::memory_order_relaxed) + 1u;
    }
    const int64_t k = colKey(cx, cz);
    const double now = supAhora();
    auto it = g_supCache.find(k);
    if (it != g_supCache.end() && (now - it->second.t < 0.5 || it->second.firma == firma)) { it->second.t = now; return it->second.minH; }
    int mn = 1 << 30;
    for (int lz = 2; lz < CS; lz += 4) for (int lx = 2; lx < CS; lx += 4) {
        int top = -1;
        for (int cy = HYC - 1; cy >= 0 && top < 0; cy--) {
            const DatosChunk& d = cs[cy]->dat;
            if (!d.d) { if (d.uni != AIRE && esSolido(d.uni) && !esHoja(d.uni)) top = cy * CS + CS - 1; continue; }
            for (int ly = CS - 1; ly >= 0; ly--) {
                uint8_t t = (*d.d)[IDX(lx, ly, lz)];
                if (t != AIRE && esSolido(t) && !esHoja(t)) { top = cy * CS + ly; break; }
            }
        }
        mn = std::min(mn, top + 1);
    }
    float h = (float)mn / SUB;
    if (g_supCache.size() > 4000) g_supCache.clear();
    g_supCache[k] = {h, firma, now};
    return h;
}
// minimo en la columna y sus 8 vecinas: un chunk por debajo de eso no se asoma al exterior por ningun lado
static float supMin3(int cx, int cz) {
    float m = 1e9f;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        float h = supMinColumna(cx + dx, cz + dz);
        if (h < -1e8f) return -1e9f;
        m = std::min(m, h);
    }
    return m;
}
static std::unordered_set<int64_t> g_editCols;
static int g_ocluidosN = 0;           // chunks no dibujados por oclusion (para el titulo)
static int g_hilos = 4;
static int g_hilosMax = 4;           // hilos creados; g_hilos = los que estan activos segun el perfil
static int g_perf = 0;               // perfil de rendimiento: 0 normal, 1 alto, 2 maximo
static bool g_conservar = false;     // conservar en RAM los chunks ya cargados (no se descartan al alejarte)
static int g_hilosBase = 4;
static void aplicarPerf() {      // perfil de rendimiento -> cuantos hilos de carga trabajan
    int n = g_perf == 0 ? g_hilosBase : g_perf == 1 ? std::min(g_hilosMax, g_hilosBase * 3 / 2 + 1) : g_hilosMax;
    g_hilos = n; g_pool.fijarActivos(n);
}
static inline int genMax() { static const int M[3] = {1, 2, 4}; return g_hilos * 6 * M[g_perf]; }
static inline int mallaMax() { static const int M[3] = {1, 2, 4}; return g_hilos * 4 * M[g_perf]; }
static inline int subidasMax() {
#ifdef MOVIL
    static const int M[3] = {4, 8, 12};
#else
    static const int M[3] = {12, 24, 48};
#endif
    return M[g_perf];
}

// ---------------------------------------------------------------- agua que fluye
// Cola de celdas "despiertas": cada cambio despierta a la celda y a sus 6 vecinas; cada tick (0.1 s) se revisan en tandas.
static std::vector<int64_t> g_aguaAct, g_aguaSig;
static std::unordered_set<int64_t> g_aguaEnSig;
static inline int64_t packC(int x, int y, int z) { return ((int64_t)(x + 1048576) << 42) | ((int64_t)(y + 1048576) << 21) | (int64_t)(z + 1048576); }
static inline void unpackC(int64_t k, int& x, int& y, int& z) { x = (int)(k >> 42) - 1048576; y = (int)((k >> 21) & 0x1FFFFF) - 1048576; z = (int)(k & 0x1FFFFF) - 1048576; }
static void aguaDespertar(int x, int y, int z) {
    static const int D[7][3] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (auto& d : D) {
        int64_t k = packC(x + d[0], y + d[1], z + d[2]);
        if (g_aguaEnSig.insert(k).second) g_aguaSig.push_back(k);
    }
}

// escribe un cubito y anota los chunks que hay que remallar
static bool g_redEnviar = false;     // true mientras se aplican acciones del jugador local: esas ediciones se mandan por la red
static void escribirCubito(int vx, int vy, int vz, uint8_t t, std::vector<ChunkP>& sucios) {
    int cy = divPiso(vy, CS);
    if (cy < 0 || cy >= HYC) return;
    int cx = divPiso(vx, CS), cz = divPiso(vz, CS);
    ChunkP c = buscar(cx, cy, cz);
    if (!c || c->state.load() < ST_READY) return;
    int lx = vx - cx * CS, ly = vy - cy * CS, lz = vz - cz * CS;
    if (!c->dat.d) {
        if (c->dat.uni == t) return;
        c->dat.d = std::make_shared<std::vector<uint8_t>>((size_t)CS * CS * CS, c->dat.uni);
    }
    if (g_redEnviar && (*c->dat.d)[IDX(lx, ly, lz)] != t) g_red.sal.push_back({vx, vy, vz, t});
    (*c->dat.d)[IDX(lx, ly, lz)] = t;
    c->sucioDisco = true;
    g_editCols.insert(colKey(cx, cz));
    aguaDespertar(vx, vy, vz);
    auto marca = [&](const ChunkP& k) { if (k && std::find(sucios.begin(), sucios.end(), k) == sucios.end()) sucios.push_back(k); };
    marca(c);
    if (lx == 0) marca(buscar(cx - 1, cy, cz));
    if (lx == CS - 1) marca(buscar(cx + 1, cy, cz));
    if (ly == 0) marca(buscar(cx, cy - 1, cz));
    if (ly == CS - 1) marca(buscar(cx, cy + 1, cz));
    if (lz == 0) marca(buscar(cx, cy, cz - 1));
    if (lz == CS - 1) marca(buscar(cx, cy, cz + 1));
}

// remalla sin parar el juego (la malla vieja se sigue viendo hasta que llega la nueva)
static void remallarAsync(const ChunkP& c) {
    int st = c->state.load();
    if (st == ST_MESHED) { c->version++; c->state.store(ST_READY); }
    else if (st == ST_MESHING) c->version++;
}

static bool g_pedirCohete = false;      // comando /cohete del chat: construye un cohete de dos etapas delante de ti
static bool g_pedirCiudad = false;      // F9: prueba de rendimiento (ciudad de luces), ver construirCiudad()

// Un tick de agua. Reglas (como Minecraft, pero por cubitos): la fuente (AGUA) nunca cambia; el agua que fluye tiene nivel 1..8.
//  - si hay agua encima: nivel 8 (cae);  - si no: el mayor nivel vecino - 1 (el vecino solo reparte de lado si no puede caer, o es fuente);
//  - sin nada que la alimente, se seca.
static int tickAgua() {
    if (g_aguaAct.empty()) { g_aguaAct.swap(g_aguaSig); g_aguaEnSig.clear(); }
    std::vector<ChunkP> sucios;
    int hechas = 0;
    while (!g_aguaAct.empty() && hechas < 700) {
        int64_t k = g_aguaAct.back(); g_aguaAct.pop_back(); hechas++;
        int x, y, z; unpackC(k, x, y, z);
        bool conocido = true;
        uint8_t v = leerCubito(x, y, z, &conocido);
        if (!conocido || v == AGUA) continue;
        if (v != AIRE && !(v >= AGUA_F1 && v <= AGUA_F8)) continue;
        int obj = 0;
        if (esAgua(leerCubito(x, y + 1, z))) obj = 8;
        else {
            static const int H[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& h : H) {
                uint8_t n = leerCubito(x + h[0], y, z + h[1]);
                int ln = nivelAgua(n);
                if (ln <= 1) continue;
                uint8_t bajo = leerCubito(x + h[0], y - 1, z + h[1]);
                if (n != AGUA && (bajo == AIRE || bajo == ANTORCHA || (bajo >= AGUA_F1 && bajo <= AGUA_F8))) continue;   // ese vecino esta cayendo: no reparte de lado
                obj = std::max(obj, ln - 1);
            }
        }
        uint8_t nuevo = obj ? (uint8_t)(AGUA_F1 + obj - 1) : (uint8_t)AIRE;
        if (nuevo != v) escribirCubito(x, y, z, nuevo, sucios);
    }
    for (auto& c : sucios) remallarAsync(c);
    return hechas;
}

// ---------------------------------------------------------------- mundo lejano (LOD)
// Terreno de baja resolucion calculado directo del generador (sin chunks 3D). Tres anillos de baldosas:
// celdas de 2, 4 y 8 bloques. Se dibuja antes que el mundo real, con su propia proyeccion (plano cercano grande,
// asi no hay parpadeos de profundidad) y luego se borra la profundidad: el mundo real siempre queda encima.
#ifdef MOVIL
static bool g_lodOn = false;        // celular: sin mundo lejano por defecto (se puede activar en el menu de pausa)
#else
static bool g_lodOn = true;
#endif
#ifdef MOVIL
static bool g_graf = false;         // celular: sin sombras ni efectos de pantalla
#else
static bool g_graf = true;
#endif      // graficos mejorados: sombras de sol, oclusion ambiental, bloom, FXAA, tonemapping
static bool g_pausa = false;
static int g_pausaSub = 0;          // 0 = pausa normal, 1 = ajustes de graficos
static int g_cielo = 1;             // cielo con atmosfera (0 simple, 1 mejorado)
static int g_viento = 1;            // hojas y plantas se mueven con el viento
static int g_nubes = 2;             // 0 apagadas, 1 simples (cajas), 2 volumetricas (necesita graficos ON)
#ifdef MOVIL
static int g_agua = 0;              // 0 simple, 1 mejorada, 2 mejorada con olas
#else
static int g_agua = 2;
#endif
static bool g_anclaOn = true;          // mantener dibujadas las zonas editadas lejanas
static const int LOD_N = 32, LOD_NIV = 4;
static const int LOD_CELDA[LOD_NIV] = {2, 4, 8, 16};
static const double LOD_ROUT[LOD_NIV] = {256.0, 640.0, 1280.0, 3840.0};
static bool g_horReal = false;      // v75: opcion "horizonte real": cuarto anillo lejano (x3 de alcance) y, desde muy alto, el suelo de abajo tambien se dibuja simple
static float g_lodCurv = 0.0f;
static bool g_lodEsp = false;
static bool g_altoLod = false;      // true: estas muy alto y solo se dibuja el mundo lejano (con histeresis)
static double g_lodRin0 = 100.0;
#define LOD_RIN0 g_lodRin0
static inline double lodFogMax() { return g_horReal ? 3840.0 : 1280.0; }
#define LOD_FOG_MAX lodFogMax()

struct LodTile {
    int niv = 0, tx = 0, tz = 0;
    std::atomic<int> estado{0};            // 0 calculando, 1 listo para subir, 2 subido
    std::vector<float> v[2];
    std::vector<uint16_t> ix[2];           // [0] terreno, [1] agua
    GLuint vao[2] = {0, 0}, vbo[2] = {0, 0}, ebo[2] = {0, 0};
    int n[2] = {0, 0};
};
typedef std::shared_ptr<LodTile> LodP;
static std::unordered_map<int64_t, LodP> g_lod;
static std::atomic<int> g_lodPend{0};
static inline int64_t lodClave(int niv, int tx, int tz) {
    return ((int64_t)niv << 58) | ((int64_t)(tx + 1048576) << 29) | (int64_t)(tz + 1048576);
}

typedef std::shared_ptr<const std::unordered_set<int64_t>> MascaraP;      // columnas (16x16 bloques) que ya se dibujan con chunks reales
static MascaraP g_mascara = std::make_shared<const std::unordered_set<int64_t>>();

static void lodGenerar(LodTile& t, MascaraP mask) {
    const int N = LOD_N, M = 5, G = N + 1 + 2 * M, cel = LOD_CELDA[t.niv];
    const int bx0 = t.tx * N * cel, bz0 = t.tz * N * cel;
    std::vector<float> H((size_t)G * G);
    for (int j = 0; j < G; j++)
        for (int i = 0; i < G; i++)
            H[(size_t)j * G + i] = (float)(std::floor(alturaTerreno(bx0 + (i - M) * cel, bz0 + (j - M) * cel) * SUB) / SUB);
    auto h = [&](int i, int j) { return H[(size_t)(j + M) * G + (i + M)]; };      // i, j = -M..N+M
    auto enMascara = [&](double bx, double bz) { return !mask->empty() && mask->count(colKey((int)std::floor(bx / 16.0), (int)std::floor(bz / 16.0))) > 0; };

    // arboles (solo en los dos anillos cercanos): copa de 5 bloques y tronco, como los reales
    struct Arbol { double x, z, base; int k; };
    std::vector<Arbol> arboles;
    if (t.niv <= 1) {
        const int ext = N * cel;
        for (int tz = bz0; tz < bz0 + ext; tz++)
            for (int tx = bx0; tx < bx0 + ext; tx++) {
                if (hash2(tx, tz, 77) % 18 != 0) continue;
                double ha; int kv = 0;
                if (!vegetacion(tx, tz, &ha, &kv)) continue;
                if (enMascara(tx + 0.5, tz + 0.5)) continue;
                arboles.push_back({tx + 0.5, tz + 0.5, (std::floor(ha * SUB) + 1 - 2) / SUB, kv});
            }
    }

    std::vector<float>& V = t.v[0];
    V.reserve((size_t)(N + 1) * (N + 1) * 6 + arboles.size() * 20 * 6);
    static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, DZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int j = 0; j <= N; j++)
        for (int i = 0; i <= N; i++) {
            int bx = bx0 + i * cel, bz = bz0 + j * cel;
            float y = h(i, j);
            float dx = (h(i + 1, j) - h(i - 1, j)) / (2.0f * cel), dz = (h(i, j + 1) - h(i, j - 1)) / (2.0f * cel);
            float ny = 1.0f / std::sqrt(dx * dx + 1.0f + dz * dz);
            float sh = 0.60f + 0.40f * ny * ny;
            // sombra ambiental: cuanto tapan el cielo las lomas cercanas (8 direcciones, hasta 5 celdas)
            float ocl = 0;
            for (int d = 0; d < 8; d++) {
                float mx = 0;
                for (int k = 1; k <= M; k++) {
                    float pend = (h(i + DX[d] * k, j + DZ[d] * k) - y) / (k * cel * (DX[d] && DZ[d] ? 1.414f : 1.0f));
                    mx = std::max(mx, pend);
                }
                ocl += std::min(0.7f, mx * 0.9f);
            }
            sh *= 1.0f - 0.30f * (ocl / 8.0f) / 0.7f * 0.7f;
            // sombra bajo las copas de los arboles
            for (const Arbol& a : arboles) {
                double ddx = a.x - bx, ddz = a.z - bz;
                if (ddx * ddx + ddz * ddz < 7.5) { sh *= 0.80f; break; }
            }
            const int dom = biomaDominante(bx, bz);
            const bool arcL = y < WATER_LEVEL - 0.6f && vnoise((float)(bx * 0.07), (float)(bz * 0.07), 53u) > 0.52f;
            const uint8_t sup = tipoSuperficie(dom, y, bx, bz, arcL);
            float cr, cg, cb;
            if (sup == PIEDRA) { cr = 0.50f; cg = 0.50f; cb = 0.52f; }
            else if (sup == TIERRA) { cr = 0.45f; cg = 0.32f; cb = 0.20f; }
            else if (sup == ARCILLA) { cr = 0.62f; cg = 0.64f; cb = 0.72f; }
            else if (sup == ARENA) { cr = 0.86f; cg = 0.80f; cb = 0.55f; }
            else if (sup == NIEVE) { cr = 0.93f; cg = 0.95f; cb = 0.99f; }
            else if (sup == PASTO_SECO) { cr = 0.66f; cg = 0.60f; cb = 0.30f; }
            else if (sup == HIELO_COMPACTO) { cr = 0.40f; cg = 0.62f; cb = 0.93f; }
            else if (sup == GRAVA) { cr = 0.50f; cg = 0.48f; cb = 0.45f; }
            else {
                cr = 0.34f; cg = 0.64f; cb = 0.21f;
                float m = std::min(0.6f, std::max(0.0f, (1.0f - ny) * 1.4f));       // laderas empinadas: se ve la tierra de los escalones
                cr += (0.45f - cr) * m; cg += (0.32f - cg) * m; cb += (0.20f - cb) * m;
            }
            float var = 1.0f + (rnd01(hash2(bx, bz, 5u)) - 0.5f) * 0.14f;
            V.push_back((float)(i * cel)); V.push_back(y); V.push_back((float)(j * cel));
            V.push_back(cr * sh * var); V.push_back(cg * sh * var); V.push_back(cb * sh * var);
        }
    std::vector<uint16_t>& I = t.ix[0];
    I.reserve((size_t)N * N * 6 + arboles.size() * 30);
    std::vector<float>& W = t.v[1];
    std::vector<uint16_t>& IW = t.ix[1];
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            const bool enMasc = enMascara(bx0 + (i + 0.5) * cel, bz0 + (j + 0.5) * cel);      // ahi dibujan los chunks reales (con tus cambios): sin terreno lejano, pero el AGUA lejana si (tapa el fondo oscuro)
            uint16_t a = (uint16_t)(j * (N + 1) + i), b = (uint16_t)(a + 1), d = (uint16_t)(a + N + 1), c = (uint16_t)(d + 1);
            float ha = h(i, j), hb = h(i + 1, j), hc = h(i + 1, j + 1), hd = h(i, j + 1);
            if (!enMasc) {
                if (std::fabs(ha - hc) < std::fabs(hb - hd)) { uint16_t q[6] = {a, d, c, a, c, b}; I.insert(I.end(), q, q + 6); }
                else { uint16_t q[6] = {a, d, b, d, c, b}; I.insert(I.end(), q, q + 6); }
            }
            if (std::min(std::min(ha, hb), std::min(hc, hd)) < WATER_LEVEL - 0.01f) {
                uint16_t base = (uint16_t)(W.size() / 6);
                float wr = 0.18f, wg = 0.38f, wb = 0.80f;     // agua helada (tundra, mundo helado, mar polar): color de hielo
                {
                    double cxw = bx0 + (i + 0.5) * cel, czw = bz0 + (j + 0.5) * cel;
                    int dm = biomaDominante(cxw, czw);
                    if (dm == BIO_TUNDRA || dm == BIO_GLACIAR || (dm == BIO_OCEANO && temp01(cxw, czw) < 0.2f)) { wr = 0.62f; wg = 0.80f; wb = 0.96f; }
                }
                float xs[4] = {(float)(i * cel), (float)(i * cel), (float)((i + 1) * cel), (float)((i + 1) * cel)};
                float zs[4] = {(float)(j * cel), (float)((j + 1) * cel), (float)((j + 1) * cel), (float)(j * cel)};
                for (int k = 0; k < 4; k++) {
                    W.push_back(xs[k]); W.push_back((float)WATER_LEVEL); W.push_back(zs[k]);
                    W.push_back(wr); W.push_back(wg); W.push_back(wb);
                }
                uint16_t q[6] = {base, (uint16_t)(base + 1), (uint16_t)(base + 2), base, (uint16_t)(base + 2), (uint16_t)(base + 3)};
                IW.insert(IW.end(), q, q + 6);
            }
        }
    // cajas de los arboles (sin la cara de abajo; cada cara con su sombreado como en el mundo real)
    auto caja = [&](double x0, double y0, double z0, double x1, double y1, double z1, float cr, float cg, float cb) {
        float X0 = (float)(x0 - bx0), X1 = (float)(x1 - bx0), Z0 = (float)(z0 - bz0), Z1 = (float)(z1 - bz0), Y0 = (float)y0, Y1 = (float)y1;
        // cara: 4 esquinas (CCW visto desde fuera), sombreado
        struct F { float p[4][3]; float s; };
        F fs[5] = {
            {{{X0, Y1, Z0}, {X0, Y1, Z1}, {X1, Y1, Z1}, {X1, Y1, Z0}}, 1.0f},        // arriba
            {{{X1, Y0, Z0}, {X1, Y1, Z0}, {X1, Y1, Z1}, {X1, Y0, Z1}}, 0.62f},       // +x
            {{{X0, Y0, Z1}, {X0, Y1, Z1}, {X0, Y1, Z0}, {X0, Y0, Z0}}, 0.62f},       // -x
            {{{X1, Y0, Z1}, {X1, Y1, Z1}, {X0, Y1, Z1}, {X0, Y0, Z1}}, 0.80f},       // +z
            {{{X0, Y0, Z0}, {X0, Y1, Z0}, {X1, Y1, Z0}, {X1, Y0, Z0}}, 0.80f}};      // -z
        for (auto& f : fs) {
            uint16_t base = (uint16_t)(V.size() / 6);
            for (int k = 0; k < 4; k++) {
                V.push_back(f.p[k][0]); V.push_back(f.p[k][1]); V.push_back(f.p[k][2]);
                V.push_back(cr * f.s); V.push_back(cg * f.s); V.push_back(cb * f.s);
            }
            uint16_t q[6] = {base, (uint16_t)(base + 1), (uint16_t)(base + 2), base, (uint16_t)(base + 2), (uint16_t)(base + 3)};
            I.insert(I.end(), q, q + 6);
        }
    };
    for (const Arbol& a : arboles) {
        static const float LC[12][3] = {{0.20f, 0.52f, 0.16f}, {0.78f, 0.13f, 0.10f}, {0.93f, 0.47f, 0.08f}, {0.94f, 0.80f, 0.13f},
                                        {0.98f, 0.64f, 0.78f}, {0.09f, 0.33f, 0.19f}, {0.20f, 0.52f, 0.16f}, {0.25f, 0.55f, 0.25f},
                                        {0.07f, 0.40f, 0.12f}, {0.55f, 0.68f, 0.62f}, {0.56f, 0.74f, 0.26f}, {0.16f, 0.42f, 0.14f}};
        const float* lc = LC[a.k];
        if (a.k == 8) {   // selva: tronco ancho y copa enorme
            const double H = altSelva((int)std::floor(a.x), (int)std::floor(a.z));
            caja(a.x - 0.5, a.base, a.z - 0.5, a.x + 1.5, a.base + H, a.z + 1.5, 0.36f, 0.22f, 0.14f);
            caja(a.x - 3.4, a.base + H - 3.0, a.z - 3.4, a.x + 4.4, a.base + H + 1.6, a.z + 4.4, lc[0], lc[1], lc[2]);
            continue;
        }
        if (a.k == 10) { if (t.niv == 0) caja(a.x - 0.25, a.base, a.z - 0.25, a.x + 0.25, a.base + 7.0, a.z + 0.25, lc[0], lc[1], lc[2]); continue; }
        if (a.k == 11) { if (t.niv == 0) caja(a.x - 0.5, a.base + 1.0, a.z - 0.5, a.x + 0.5, a.base + 2.5, a.z + 0.5, lc[0], lc[1], lc[2]); continue; }
        if (a.k == 9) { if (t.niv == 0) caja(a.x - 0.5, a.base, a.z - 0.5, a.x + 0.5, a.base + 6.0, a.z + 0.5, 0.40f, 0.28f, 0.15f); caja(a.x - 1.6, a.base + 2.0, a.z - 1.6, a.x + 1.6, a.base + 9.0, a.z + 1.6, lc[0], lc[1], lc[2]); continue; }
        if (a.k == 7) { caja(a.x - 0.5, a.base, a.z - 0.5, a.x + 0.5, a.base + 3.5, a.z + 0.5, lc[0], lc[1], lc[2]); continue; }
        if (t.niv == 0) caja(a.x - 0.5, a.base, a.z - 0.5, a.x + 0.5, a.base + (a.k == 5 ? 6.0 : 4.0), a.z + 0.5, 0.40f, 0.28f, 0.15f);
        if (a.k == 5) caja(a.x - 1.6, a.base + 2.0, a.z - 1.6, a.x + 1.6, a.base + 9.0, a.z + 1.6, lc[0], lc[1], lc[2]);
        else if (a.k == 6) caja(a.x - 2.6, a.base + 4.4, a.z - 2.6, a.x + 2.6, a.base + 6.2, a.z + 2.6, lc[0], lc[1], lc[2]);
        else caja(a.x - 2.5, a.base + 2.9, a.z - 2.5, a.x + 2.5, a.base + 7.1, a.z + 2.5, lc[0], lc[1], lc[2]);
    }
}
static void lodSubir(LodTile& t) {
    for (int k = 0; k < 2; k++) {
        if (t.ix[k].empty()) continue;
        glGenVertexArrays(1, &t.vao[k]); glGenBuffers(1, &t.vbo[k]); glGenBuffers(1, &t.ebo[k]);
        glBindVertexArray(t.vao[k]);
        glBindBuffer(GL_ARRAY_BUFFER, t.vbo[k]);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(t.v[k].size() * sizeof(float)), t.v[k].data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t.ebo[k]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(t.ix[k].size() * sizeof(uint16_t)), t.ix[k].data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (const void*)12);
        t.n[k] = (int)t.ix[k].size();
        std::vector<float>().swap(t.v[k]);
        std::vector<uint16_t>().swap(t.ix[k]);
    }
    t.estado.store(2);
}
static void lodLiberar(LodTile& t) {
    for (int k = 0; k < 2; k++) if (t.vao[k]) {
        glDeleteVertexArrays(1, &t.vao[k]); glDeleteBuffers(1, &t.vbo[k]); glDeleteBuffers(1, &t.ebo[k]);
        t.vao[k] = 0;
    }
}
static void lodLiberarTodo() {
    for (auto& kv : g_lod) lodLiberar(*kv.second);
    g_lod.clear();
}
static void lodDist(int niv, int tx, int tz, double px, double pz, double& dn, double& df) {
    double ts = (double)LOD_N * LOD_CELDA[niv], x0 = tx * ts, x1 = x0 + ts, z0 = tz * ts, z1 = z0 + ts;
    double nx = std::max(std::max(x0 - px, 0.0), px - x1), nz = std::max(std::max(z0 - pz, 0.0), pz - z1);
    double fx = std::max(std::fabs(px - x0), std::fabs(px - x1)), fz = std::max(std::fabs(pz - z0), std::fabs(pz - z1));
    dn = std::sqrt(nx * nx + nz * nz); df = std::sqrt(fx * fx + fz * fz);
}
// una baldosa se usa si no queda toda dentro del anillo mas fino (o del mundo real) ni toda fuera de su propio radio
static bool lodNecesario(int niv, int tx, int tz, double px, double pz, double margen) {
    double dn, df;
    lodDist(niv, tx, tz, px, pz, dn, df);
    if (niv == 3 && !g_horReal) return false;
    double rin = niv == 0 ? LOD_RIN0 : LOD_ROUT[niv - 1];
    return df > rin - margen && dn < LOD_ROUT[niv] + margen;
}
static void lodActualizar(double px, double pz) {
    for (auto it = g_lod.begin(); it != g_lod.end();) {
        double ts = (double)LOD_N * LOD_CELDA[it->second->niv];
        if (!lodNecesario(it->second->niv, it->second->tx, it->second->tz, px, pz, ts * 1.5)) { lodLiberar(*it->second); it = g_lod.erase(it); }
        else ++it;
    }
    int libres = 4 - g_lodPend.load();
    if (libres <= 0) return;
    struct Cand { double d; int niv, tx, tz; };
    std::vector<Cand> cand;
    for (int niv = 0; niv < LOD_NIV; niv++) {
        double ts = (double)LOD_N * LOD_CELDA[niv], rout = LOD_ROUT[niv] + ts;
        for (int tz = (int)std::floor((pz - rout) / ts); tz <= (int)std::floor((pz + rout) / ts); tz++)
            for (int tx = (int)std::floor((px - rout) / ts); tx <= (int)std::floor((px + rout) / ts); tx++) {
                if (!lodNecesario(niv, tx, tz, px, pz, ts * 0.25) || g_lod.count(lodClave(niv, tx, tz))) continue;
                double dn, df;
                lodDist(niv, tx, tz, px, pz, dn, df);
                cand.push_back({dn + niv * 50.0, niv, tx, tz});
            }
    }
    std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (int k = 0; k < (int)cand.size() && k < libres; k++) {
        auto t = std::make_shared<LodTile>();
        t->niv = cand[k].niv; t->tx = cand[k].tx; t->tz = cand[k].tz;
        g_lod[lodClave(t->niv, t->tx, t->tz)] = t;
        g_lodPend++;
        MascaraP mk = g_mascara;
        g_pool.meter([t, mk] { lodGenerar(*t, mk); t->estado.store(1, std::memory_order_release); g_lodPend--; });
    }
}
static void planosFrustum(const float* m, float pl[6][4]) {
    float r0[4] = {m[0], m[4], m[8], m[12]}, r1[4] = {m[1], m[5], m[9], m[13]};
    float r2[4] = {m[2], m[6], m[10], m[14]}, r3[4] = {m[3], m[7], m[11], m[15]};
    for (int k = 0; k < 4; k++) {
        pl[0][k] = r3[k] + r0[k]; pl[1][k] = r3[k] - r0[k];
        pl[2][k] = r3[k] + r1[k]; pl[3][k] = r3[k] - r1[k];
        pl[4][k] = r3[k] + r2[k]; pl[5][k] = r3[k] - r2[k];
    }
}
// ---- baldosas que hay que rehacer porque cambio la mascara (una columna real aparecio o desaparecio)
static std::unordered_set<int64_t> g_lodRegen;
static std::unordered_map<int64_t, LodP> g_lodRep;
static void lodRegenColumna(int64_t col) {
    int cx, cz;
    colDec(col, cx, cz);
    for (int niv = 0; niv < LOD_NIV; niv++) {
        double ts = (double)LOD_N * LOD_CELDA[niv];
        g_lodRegen.insert(lodClave(niv, (int)std::floor(cx * 16.0 / ts), (int)std::floor(cz * 16.0 / ts)));
    }
}
static void lodProcesarRegen() {
    for (int64_t k : g_lodRegen) {
        auto it = g_lod.find(k);
        if (it == g_lod.end()) continue;
        auto t = std::make_shared<LodTile>();
        t->niv = it->second->niv; t->tx = it->second->tx; t->tz = it->second->tz;
        g_lodRep[k] = t;
        g_lodPend++;
        MascaraP mk = g_mascara;
        g_pool.meter([t, mk] { lodGenerar(*t, mk); t->estado.store(1, std::memory_order_release); g_lodPend--; });
    }
    g_lodRegen.clear();
}
static void lodReemplazar() {
    for (auto it = g_lodRep.begin(); it != g_lodRep.end();) {
        auto viejo = g_lod.find(it->first);
        if (viejo == g_lod.end()) { it = g_lodRep.erase(it); continue; }
        if (it->second->estado.load(std::memory_order_acquire) == 1) {
            lodSubir(*it->second);
            lodLiberar(*viejo->second);
            viejo->second = it->second;
            it = g_lodRep.erase(it);
        } else ++it;
    }
}

// ---- columnas editadas lejanas: se mantienen cargadas y con malla real (junto con sus vecinas, para que las caras queden bien)
static const double ANCLA_DIST = 700.0;
static const size_t ANCLA_MAX = 100;
static std::unordered_set<int64_t> g_ancla, g_requeridos;
static void anclarActualizar(double px, double pz) {
    struct C { double d; int cx, cz; };
    std::vector<C> v;
    if (g_lodOn && g_anclaOn) {
        for (int64_t k : g_editCols) {
            int cx, cz;
            colDec(k, cx, cz);
            double dx = (cx + 0.5) * 16 - px, dz = (cz + 0.5) * 16 - pz, d = std::sqrt(dx * dx + dz * dz);
            if (d <= ANCLA_DIST) v.push_back({d, cx, cz});
        }
        std::sort(v.begin(), v.end(), [](const C& a, const C& b) { return a.d < b.d; });
        if (v.size() > ANCLA_MAX) v.resize(ANCLA_MAX);
    }
    g_ancla.clear(); g_requeridos.clear();
    for (const C& c : v) {
        g_ancla.insert(colKey(c.cx, c.cz));
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) g_requeridos.insert(colKey(c.cx + dx, c.cz + dz));
    }
    int pcx = divPiso((int)std::floor(px), 16), pcz = divPiso((int)std::floor(pz), 16);
    auto cerca = [&](int cx, int cz) { return (cx - pcx) * (cx - pcx) + (cz - pcz) * (cz - pcz) <= RC * RC; };   // de eso se encarga el bucle normal
    for (int64_t k : g_requeridos) {
        int cx, cz;
        colDec(k, cx, cz);
        if (cerca(cx, cz)) continue;
        for (int cy = 0; cy < HYC; cy++) {
            if (buscar(cx, cy, cz)) continue;
            if (g_genPend.load() >= genMax()) break;
            auto c = std::make_shared<Chunk>(cx, cy, cz);
            g_mapa[clave(cx, cy, cz)] = c;
            pedirGenerar(c);
        }
    }
    for (int64_t k : g_ancla) {
        int cx, cz;
        colDec(k, cx, cz);
        if (cerca(cx, cz)) continue;
        for (int cy = 0; cy < HYC; cy++) {
            ChunkP c = buscar(cx, cy, cz);
            if (!c || c->state.load(std::memory_order_acquire) != ST_READY) continue;
            if (!c->dat.d && c->dat.uni == AIRE) { c->state.store(ST_MESHED); continue; }
            if (g_meshPend.load() < mallaMax()) pedirMalla(c, 0);
        }
    }
    // mascara: columnas editadas que ya estan completamente dibujadas con chunks reales
    auto nueva = std::make_shared<std::unordered_set<int64_t>>();
    for (int64_t k : g_ancla) {
        int cx, cz;
        colDec(k, cx, cz);
        bool ok = true;
        for (int cy = 0; cy < HYC && ok; cy++) {
            ChunkP c = buscar(cx, cy, cz);
            if (!c || c->state.load(std::memory_order_acquire) < ST_MESHED) ok = false;
        }
        if (ok) nueva->insert(k);
    }
    bool cambio = false;
    for (int64_t k : *nueva) if (!g_mascara->count(k)) { lodRegenColumna(k); cambio = true; }
    for (int64_t k : *g_mascara) if (!nueva->count(k)) { lodRegenColumna(k); cambio = true; }
    if (cambio) g_mascara = nueva;
}
static void escanearEditados() {
    namespace fs = std::filesystem;
    std::error_code ec;
    g_editCols.clear();
    for (auto& e : fs::directory_iterator(g_dir, ec)) {
        int x, y, z;
        std::string fn = e.path().filename().string();
        if (std::sscanf(fn.c_str(), "c_%d_%d_%d.bin", &x, &y, &z) == 3) g_editCols.insert(colKey(x, z));
    }
}
static bool g_nuevoProf = false;   // al crear un mundo nuevo: con mares profundos (opcional; por defecto NO)
static bool g_espAtard = false;    // ajuste: mostrar la franja naranja del atardecer tambien desde el espacio (por defecto NO)
static void guardarOpciones() {
    FILE* f = std::fopen("opciones.txt", "w");
    if (!f) return;
    std::fprintf(f, "mundo_lejano=%d\ncambios_lejanos=%d\ngraficos=%d\nrendimiento=%d\nconservar=%d\nnubes=%d\nagua=%d\ncielo=%d\nviento=%d\nmares_profundos=%d\natardecer_espacio=%d\nhorizonte_real=%d\n", (g_modoLuna ? g_lodOnAntes : g_lodOn) ? 1 : 0, g_anclaOn ? 1 : 0, g_graf ? 1 : 0, g_perf, g_conservar ? 1 : 0, g_nubes, g_agua, g_cielo, g_viento, g_nuevoProf ? 1 : 0, g_espAtard ? 1 : 0, g_horReal ? 1 : 0);
    std::fclose(f);
}
static void cargarOpciones() {
    FILE* f = std::fopen("opciones.txt", "r");
    if (!f) return;
    int v = 1, w = 1, g = 1, pf = 0, cs = 0, nb = g_nubes, ag = g_agua, ci = g_cielo, vi = g_viento, mp = 0, ae = 0, hr = 0;
    int nl = std::fscanf(f, "mundo_lejano=%d cambios_lejanos=%d graficos=%d rendimiento=%d conservar=%d nubes=%d agua=%d cielo=%d viento=%d mares_profundos=%d atardecer_espacio=%d horizonte_real=%d", &v, &w, &g, &pf, &cs, &nb, &ag, &ci, &vi, &mp, &ae, &hr);
    if (nl >= 1) { g_lodOn = v != 0; g_anclaOn = w != 0; g_graf = g != 0; }
    if (nl >= 5) { g_perf = std::max(0, std::min(2, pf)); g_conservar = cs != 0; }
    if (nl >= 7) { g_nubes = std::max(0, std::min(2, nb)); g_agua = std::max(0, std::min(2, ag)); }
    if (nl >= 9) { g_cielo = ci != 0; g_viento = vi != 0; }
    if (nl >= 10) g_nuevoProf = mp != 0;
    if (nl >= 11) g_espAtard = ae != 0;
    if (nl >= 12) g_horReal = hr != 0;
    std::fclose(f);
}

// ---------------------------------------------------------------- jugador
struct Jugador {
    V3 p{0.5, 60, 0.5};
    V3 v{0, 0, 0};
    V3 inercia{0, 0, 0};   // velocidad horizontal heredada del vehiculo al saltar o caer de el
    bool suelo = false, volar = false;
    double yaw = 0, pitch = 0;
    const double mitad = 0.3, alto = 1.8, ojo = 1.62;
};
static Jugador g_j;
static bool g_pisaba = false;   // estaba en el suelo al empezar este paso (para subir escalones)

static bool chocaVeh(double px, double py, double pz);     // (vehiculo.h) colision con las piezas del vehiculo
static bool chocaMundo(double px, double py, double pz) {
    int x0 = (int)std::floor((px - g_j.mitad) * SUB), x1 = (int)std::floor((px + g_j.mitad - 1e-6) * SUB);
    int y0 = (int)std::floor(py * SUB), y1 = (int)std::floor((py + g_j.alto - 1e-6) * SUB);
    int z0 = (int)std::floor((pz - g_j.mitad) * SUB), z1 = (int)std::floor((pz + g_j.mitad - 1e-6) * SUB);
    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
                if (esSolido(leerCubito(x, y, z))) return true;
    return false;
}
// coloca al jugador junto a la base lunar (x lateral, d profundidad desde la puerta hacia fuera si es negativo; y sobre el suelo)
static void irALaBase(double x, double d, double y, double yaw, double pitch) {
    const double px = BASE_X + 0.5 + x, pz = BASE_Z + 0.5 + d;
    g_j.p = {px, LUNA_Y0 + y + 0.2, pz};
    if (y > 0.5 || d < 20) g_j.p.y = std::max(g_j.p.y, alturaLuna(px, pz) + 0.5);
    g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0}; g_j.volar = false; g_j.yaw = yaw; g_j.pitch = pitch;
}
// ---------------- v63: teclado, puerta corrediza y radar de la base lunar
static bool g_tecladoOn = false;          // escribiendo el codigo en el teclado de la puerta
static int g_baseDig = 0;                 // cuantos digitos del codigo ya se acertaron (0..4)
static double g_puertaT0 = -1.0;          // instante en que empezo a abrirse la puerta (<0 = no animando)
static int g_puertaPaso = 0;              // cubitos que ya se han corrido las hojas (0..3*SUB)
static bool g_puertaAbierta = false;
static double g_tecladoErr = -10.0;       // instante del ultimo digito equivocado
static int baseCodigo() {                 // el codigo depende de la semilla del mundo: 1234 o 1969
    uint32_t h = g_semilla * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return (h & 1) ? 1234 : 1969;
}
static int baseDigitoEsp(int i) { int c = baseCodigo(); for (int k = 3; k > i; k--) c /= 10; return c % 10; }
static void puertaSet(int open) {         // hojas corridas 'open' cubitos hacia los lados
    std::vector<ChunkP> su;
    for (int lx = -6 * SUB; lx < 6 * SUB; lx++) {
        const bool hojaI = lx >= -3 * SUB - open && lx <= -1 - open;
        const bool hojaD = lx >= open && lx <= 3 * SUB - 1 + open;
        for (int ld = SUB; ld < 2 * SUB; ld++) for (int ly = 0; ly < 5 * SUB; ly++)
            escribirCubito(baseCX(lx), baseCY(ly), baseCZ(ld), (hojaI || hojaD) ? BASE_PUERTA : AIRE, su);
    }
    for (auto& c : su) remallarYa(c);
    g_puertaPaso = open;
}
static bool puertaAbiertaReal() {
    bool c = false; uint8_t v = leerCubito(baseCX(0), baseCY(SUB), baseCZ(SUB), &c);
    return c && v == AIRE;
}
static void puertaTick(double now) {
    if (g_puertaT0 < 0) return;
    const int total = 3 * SUB;
    int want = std::min(total, (int)((now - g_puertaT0) / 1.6 * total));
    if (want > g_puertaPaso) puertaSet(want);
    if (g_puertaPaso >= total) { g_puertaT0 = -1.0; g_puertaAbierta = true; }
}
static bool choca(double px, double py, double pz) { return chocaMundo(px, py, pz) || chocaVeh(px, py, pz); }

static void moverEje(int eje, double delta) {
    double* p[3] = {&g_j.p.x, &g_j.p.y, &g_j.p.z};
    while (std::fabs(delta) > 1e-9) {
        double paso = std::max(-0.1, std::min(0.1, delta));
        delta -= paso;
        *p[eje] += paso;
        if (!choca(g_j.p.x, g_j.p.y, g_j.p.z)) continue;
        if (eje != 1 && g_pisaba) {          // subir escalones (los cubitos hacen escalones pequenos)
            bool subio = false;
            for (double s = VS; s <= 0.62; s += VS) {
                if (!choca(g_j.p.x, g_j.p.y + s, g_j.p.z)) { g_j.p.y += s; subio = true; break; }
            }
            if (subio) continue;
        }
        *p[eje] -= paso;
        if (eje == 1) { if (g_j.v.y < 0) g_j.suelo = true; g_j.v.y = 0; }
        else (eje == 0 ? g_j.v.x : g_j.v.z) = 0;
        return;
    }
}

static std::string archivoJugador() { return g_dir + "/jugador.txt"; }
static std::string archivoBarra() { return g_dir + "/barra.txt"; }
static std::string g_nombreMundo = "MUNDO";
static int g_material = -1;
static bool g_pincelBloque = false;
static bool g_destruir = false;      // herramienta actual: destruir (en vez de poner un material)
static bool g_menu = false;          // menu de materiales abierto (tecla E)
static double g_hora = 0.12;         // hora del dia (0..1)
// ---- clima: 0 despejado, 1 nublado, 2 lluvia, 3 tormenta, 4 niebla (cambia solo cada 80-240 s; tecla K lo cambia a mano)
static int g_climaE = 0; static bool g_climaFijo = false; static double g_climaT = 90.0;
static float g_clN = 0, g_clP = 0, g_clT = 0, g_clF = 0, g_clNieve = 0, g_clCub = 0;     // nubosidad, precipitacion, tormenta, niebla, nieve (en vez de lluvia), techo encima
static double g_rayoT = -100, g_rayoSig = 8; static std::vector<float> g_rayoPts;
static uint32_t g_climaRng = 0x9E3779B9u;
static float climaRand() { g_climaRng = g_climaRng * 1664525u + 1013904223u; return (float)(g_climaRng >> 8) / 16777216.0f; }
static inline float hf(uint32_t n) { n ^= n >> 16; n *= 0x7feb352dU; n ^= n >> 15; n *= 0x846ca68bU; n ^= n >> 16; return (float)(n & 0xFFFFFF) / 16777216.0f; }
static inline float fmodPos(double a, double m) { double r = std::fmod(a, m); if (r < 0) r += m; return (float)r; }
static double g_ultEspacio = -10;    // para el doble toque de Espacio (volar)
static int g_winW = 1280, g_winH = 720, g_fbW = 1280, g_fbH = 720;
// herramientas / materiales: el numero es el Tipo (0 = destruir)
static const char* NOMBRES_SLOT[NTIPOS] = {"DESTRUIR", "PASTO", "TIERRA", "PIEDRA", "ARENA", "AGUA", "MADERA", "HOJAS",
                                           "CARBON", "HIERRO", "COBRE", "ORO", "DIAMANTE", "CUARZO", "ARCILLA",
                                           "ESTANO", "ALUMINIO", "PLATA", "TITANIO", "LITIO", "URANIO", "ANTORCHA",
                                           "", "", "", "", "", "", "", "", "AZUFRE", "SALITRE",
                                           "LED ROJO", "LED NARANJA", "LED AMARILLO", "LED LIMA", "LED VERDE", "LED TURQUESA",
                                           "LED CELESTE", "LED AZUL", "LED VIOLETA", "LED MAGENTA", "LED ROSA", "LED BLANCO",
                                           "VIDRIO", "CARROCERIA", "ASIENTO", "TIMON", "ALA", "RUEDA", "HELICE", "CONTROL",
                                           "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA",
                                           "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA",
                                           "NIEVE", "HIELO", "CACTUS", "HOJAS ROJAS", "HOJAS NARANJA", "HOJAS AMARILLAS", "HOJAS ROSAS", "PINO", "PASTO SECO",
                                           "MADERA SELVA", "HOJAS SELVA", "LIANA", "BAMBU", "HOJAS NEVADAS", "ARBUSTO BAYAS", "FLOR ROJA", "FLOR AMARILLA", "FLOR AZUL", "FLOR ROSA", "HIELO COMPACTO", "LAVA",
                                           "CORAL ROJO", "CORAL ROSA", "CORAL NARANJA", "CORAL MORADO", "CORAL AMARILLO", "CORAL AZUL", "ALGA", "GRAVA", "TNT", "TNT ENCENDIDO",
                                           "CONCRETO", "METAL", "REJILLA", "TUBERIA", "PUERTA BASE", "TECLADO", "PANTALLA", "FRANJA", "PISO BASE", "PLACA", "PLASMA FRIO", "PLASMA VERDE", "PLASMA CELESTE", "PLASMA ROJO", "PLASMA AMBAR", "ACERO ROJO", "MURO AZUL", "CABLE", "AVISO", "ARMARIO", "REJILLA REDONDA", "PANTALLA IA APAGADA", "PANTALLA IA", "CONSOLA", "BOTON IA"};
// ---- herramientas (ids 230..241) y modo survival
static const int HERR_BASE = 230, HERR_N = 12;
static inline bool esHerr(int id) { return id >= HERR_BASE && id < HERR_BASE + HERR_N; }
static inline int herrTipo(int id) { return (id - HERR_BASE) / 4; }    // 0 pico, 1 hacha, 2 pala
static inline int herrNivel(int id) { return (id - HERR_BASE) % 4; }   // 0 madera, 1 piedra, 2 hierro, 3 diamante
static const char* NOMBRES_HERR[HERR_N] = {"PICO MADERA", "PICO PIEDRA", "PICO HIERRO", "PICO DIAMANTE",
                                           "HACHA MADERA", "HACHA PIEDRA", "HACHA HIERRO", "HACHA DIAMANTE",
                                           "PALA MADERA", "PALA PIEDRA", "PALA HIERRO", "PALA DIAMANTE"};
// ---- v81: objetos nuevos del survival (ids libres 242..249, 254, 255): materiales, kit del cohete, armadura y armas
static const int ID_ACERO = 242, ID_COMBUS = 243, ID_KITCOH = 244, ID_CASCO = 245, ID_PECHERA = 246, ID_BOTAS = 247,
                 ID_ESPADA = 248, ID_ESPADAP = 249, ID_MARTILLO = 254, ID_ESCUDO = 255;
static const int ID_CARNE = 207, ID_LANA = 208, ID_CAMA = 209;   // v84: oveja y cama
static const int ID_ARCO = 200, ID_FLECHA = 201, ID_HACHAB = 202, ID_LANZA = 203, ID_MAZA = 204, ID_DAGA = 205, ID_ESPADAL = 206;   // v82: armas medievales
static inline bool esObjNuevo(int id) { return (id >= 200 && id <= 209) || (id >= 242 && id <= 249) || id == 254 || id == 255; }
static inline bool esArma(int id) { return id == ID_ESPADA || id == ID_ESPADAP || id == ID_MARTILLO || id == ID_HACHAB || id == ID_LANZA || id == ID_MAZA || id == ID_DAGA || id == ID_ESPADAL; }
struct ArmaInfo { float dano; double cd, alcance; };
static ArmaInfo armaInfo(int id) {   // dano, segundos entre golpes y alcance
    switch (id) {
        case ID_DAGA: return {9.0f, 0.30, 4.5};     case ID_ESPADA: return {14.0f, 0.55, 5.5};   case ID_ESPADAL: return {18.0f, 0.70, 6.0};
        case ID_MAZA: return {20.0f, 1.00, 5.5};    case ID_HACHAB: return {22.0f, 0.90, 5.5};   case ID_LANZA: return {16.0f, 0.60, 8.0};
        case ID_ESPADAP: return {24.0f, 0.45, 5.5}; case ID_MARTILLO: return {38.0f, 1.10, 7.5};
    }
    return {5.0f, 0.5, 4.0};
}
static const char* nombreNuevo(int id) {
    switch (id) {
        case 242: return "ACERO"; case 243: return "COMBUSTIBLE"; case 244: return "KIT COHETE"; case 245: return "CASCO";
        case 246: return "PECHERA"; case 247: return "BOTAS"; case 248: return "ESPADA";
        case 249: return "ESP PLASMA"; case 254: return "MARTILLO"; case 255: return "ESCUDO";
        case 200: return "ARCO"; case 201: return "FLECHA"; case 202: return "HACHA BATALLA"; case 203: return "LANZA";
        case 204: return "MAZA"; case 205: return "DAGA"; case 206: return "ESPADA LARGA";
        case 207: return "CARNE"; case 208: return "LANA"; case 209: return "CAMA";
    }
    return "";
}
static const char* nombreSlot(int t) {
    if (esObjNuevo(t)) return nombreNuevo(t);
    if (t == ID_SPRAY) return "SPRAY";
    if (t == ID_POLVORA) return "POLVORA";
    if (t == ID_RADAR) return "RADAR";
    if (t == ID_PISTOLA) return "PISTOLA DE PLASMA";
    if (esHerr(t)) return NOMBRES_HERR[t - HERR_BASE];
    return (t >= 0 && t < NTIPOS) ? NOMBRES_SLOT[t] : "";
}
static bool g_survival = false, g_spawnOk = false;
static int g_inv[256];                                  // cuantos cubitos (o piezas) tienes de cada objeto
static float g_vida = 20.0f;                            // 20 = 10 corazones
static double g_vidaT = 0, g_danoFlash = -10, g_caidaMax = 0, g_spawnS[3] = {0, 0, 0};
static double g_protT = 0;                              // sin dano de caida hasta este instante (tras reaparecer)
static int g_recPag = 0;                                // v81: pagina de recetas (26 por pagina)
static int g_pagina = 0;                                // en el menu survival: 0 objetos, 1 recetas
struct Receta { int out, n, a, na, b, nb, c, nc; };
static const std::vector<Receta>& recetas() {
    static std::vector<Receta> r;
    if (r.empty()) {
        const int cm[4][2] = {{24, 0}, {16, 24}, {16, 24}, {16, 24}};          // madera y material del cabezal (pico y hacha)
        const int cp[4][2] = {{16, 0}, {8, 16}, {8, 16}, {8, 16}};              // pala
        const int mat[4] = {MADERA, PIEDRA, HIERRO, DIAMANTE};
        for (int ti = 0; ti < 3; ti++) for (int nv = 0; nv < 4; nv++) {
            const int* c = ti == 2 ? cp[nv] : cm[nv];
            if (nv == 0) r.push_back({HERR_BASE + ti * 4, 1, MADERA, c[0], 0, 0, 0, 0});
            else r.push_back({HERR_BASE + ti * 4 + nv, 1, MADERA, c[0], mat[nv], c[1], 0, 0});
        }
        r.push_back({ANTORCHA, 4, MADERA, 2, CARBON, 1, 0, 0});
        r.push_back({VIDRIO, 4, ARENA, 8, 0, 0, 0, 0});
        r.push_back({CARROCERIA, 8, HIERRO, 8, 0, 0, 0, 0});
        r.push_back({ASIENTO, 4, MADERA, 8, HIERRO, 4, 0, 0});
        r.push_back({TIMON, 1, HIERRO, 8, 0, 0, 0, 0});
        r.push_back({ALA, 8, ALUMINIO, 8, 0, 0, 0, 0});
        r.push_back({RUEDA, 8, CARBON, 8, HIERRO, 4, 0, 0});
        r.push_back({HELICE, 4, ALUMINIO, 8, 0, 0, 0, 0});
        r.push_back({CONTROL, 1, COBRE, 4, HIERRO, 4, 0, 0});
        r.push_back({ID_SPRAY, 1, ALUMINIO, 8, CARBON, 4, 0, 0});
        r.push_back({ID_RADAR, 1, COBRE, 4, ALUMINIO, 4, 0, 0});
        r.push_back({ID_PISTOLA, 1, ALUMINIO, 4, COBRE, 2, 0, 0});            // v67: pistola de plasma            // v63: radar de senal
        // v81: materiales, el cohete, armaduras y armas (cantidades en cubitos: 8 cubitos = 1 bloque)
        r.push_back({ID_ACERO, 4, HIERRO, 8, CARBON, 4, 0, 0});
        r.push_back({ID_COMBUS, 4, CARBON, 8, AZUFRE, 2, SALITRE, 2});
        r.push_back({ID_KITCOH, 1, ID_ACERO, 40, ALUMINIO, 24, ID_COMBUS, 16});
        r.push_back({ID_CASCO, 1, ID_ACERO, 12, VIDRIO, 2, 0, 0});
        r.push_back({ID_PECHERA, 1, ID_ACERO, 20, ALUMINIO, 8, 0, 0});
        r.push_back({ID_BOTAS, 1, ID_ACERO, 10, ALUMINIO, 4, 0, 0});
        r.push_back({ID_ESPADA, 1, ID_ACERO, 10, MADERA, 4, 0, 0});
        r.push_back({ID_DAGA, 1, ID_ACERO, 6, MADERA, 2, 0, 0});
        r.push_back({ID_ESPADAL, 1, ID_ACERO, 16, MADERA, 4, 0, 0});
        r.push_back({ID_HACHAB, 1, ID_ACERO, 18, MADERA, 6, 0, 0});
        r.push_back({ID_LANZA, 1, ID_ACERO, 8, MADERA, 10, 0, 0});
        r.push_back({ID_MAZA, 1, ID_ACERO, 14, MADERA, 6, 0, 0});
        r.push_back({ID_ARCO, 1, MADERA, 16, LIANA, 4, 0, 0});
        r.push_back({ID_FLECHA, 8, MADERA, 4, ID_ACERO, 2, 0, 0});
        r.push_back({ID_ESPADAP, 1, ALUMINIO, 8, COBRE, 6, TITANIO, 2});
        r.push_back({ID_MARTILLO, 1, ID_ACERO, 24, MADERA, 8, AZUFRE, 4});
        r.push_back({ID_ESCUDO, 1, ID_ACERO, 16, MADERA, 8, 0, 0});
        r.push_back({ID_CAMA, 1, ID_LANA, 3, MADERA, 24, 0, 0});            // v84: cama = 3 lana + 24 madera
        r.push_back({ID_POLVORA, 2, SALITRE, 3, CARBON, 1, AZUFRE, 1});     // v51: 3 salitre + 1 carbon + 1 azufre = 2 polvora
        r.push_back({TNT, 8, ID_POLVORA, 4, 0, 0, 0, 0});                 // 4 polvoras = 1 bloque de TNT (8 cubitos)
    }
    return r;
}
static bool puedeCraftear2(const Receta& r);
static bool recBloq(const Receta& r) { return (r.out == ID_ESPADAP || r.out == ID_MARTILLO) && g_inv[0] == 0; }   // v82: se desbloquean al derrotar al mecha (g_inv[0] = 1)
static bool puedeCraftear(const Receta& r) {
    if (recBloq(r)) return false;
    return puedeCraftear2(r);
}
static bool puedeCraftear2(const Receta& r) { return g_inv[r.a] >= r.na && (r.nb == 0 || g_inv[r.b] >= r.nb) && (r.nc == 0 || g_inv[r.c] >= r.nc); }
static int survClase(int tp) {   // 0 pala, 1 hacha, 2 pico, 3 sin herramienta
    if (tp == PASTO || tp == TIERRA || tp == ARENA || tp == ARCILLA || tp == NIEVE || tp == PASTO_SECO || tp == GRAVA) return 0;
    if (tp == MADERA || esHoja(tp) || tp == CACTUS || tp == MADERA_SELVA || tp == BAMBU || tp == ARBUSTO_BAYAS || tp == LIANA) return 1;
    if (tp == HIELO) return 2;
    if (tp == PIEDRA || (tp >= CARBON && tp <= URANIO && tp != ARCILLA && tp != ANTORCHA) || tp == AZUFRE || tp == SALITRE) return 2;
    return 3;
}
static int survNivelNec(int tp) {   // nivel de pico necesario (0 = ninguno)
    switch (tp) {
        case PIEDRA: case CARBON: case ESTANO: return 1;
        case HIERRO: case COBRE: case ALUMINIO: case PLATA: case CUARZO: case AZUFRE: case SALITRE: return 2;
        case ORO: case DIAMANTE: case TITANIO: case LITIO: case URANIO: return 3;
        default: return 0;
    }
}
static bool survPicoOK(int tp, int mat) {
    int have = (esHerr(mat) && herrTipo(mat) == 0) ? herrNivel(mat) + 1 : 0;
    return have >= survNivelNec(tp);
}
static int survDrop(int tp) {
    if (tp == AIRE || (tp >= AGUA && tp <= AGUA) || (tp >= AGUA_F1 && tp <= AGUA_F8)) return -1;
    if (tp >= CARR_0 && tp <= CARR_11) return CARROCERIA;
    if (tp == TNT_ENC) return TNT;
    return tp;
}
static float g_romFrac = 0.0f;                 // progreso de picar el bloque que miras (0..1)
static double g_romT = 0;
static long long g_romKey = -1;
static double survCooldown(int tp, int mat) {   // segundos entre cubitos picados
    int cl = survClase(tp);
    double dur = cl == 0 ? 0.8 : cl == 1 ? 1.4 : cl == 2 ? (survNivelNec(tp) >= 2 ? 3.0 : 2.2) : 1.0;
    double vel = 1.0;
    static const double V[4] = {2.0, 3.5, 5.0, 7.0};
    if (cl < 3 && esHerr(mat) && herrTipo(mat) == (cl == 0 ? 2 : cl == 1 ? 1 : 0)) vel = V[herrNivel(mat)];
    return std::max(0.05, 0.18 * dur / vel);
}
static float survReduccion() {   // v81: la armadura que llevas en el inventario y el escudo en la mano reducen el dano
    float r = 0.0f;
    if (g_inv[ID_CASCO] > 0) r += 0.12f;
    if (g_inv[ID_PECHERA] > 0) r += 0.25f;
    if (g_inv[ID_BOTAS] > 0) r += 0.10f;
    if (g_material == ID_ESCUDO && g_inv[ID_ESCUDO] > 0) r += 0.30f;
    return std::min(0.70f, r);
}
static void survDano(float hp) { g_vida -= hp * (1.0f - survReduccion()); g_danoFlash = glfwGetTime(); }
// ---------------------------------------------------------------- mapa de la Tierra vista desde el espacio (v54)
// Se calcula en un hilo aparte con las mismas funciones que genera el terreno (oceanos, biomas, hielo), alrededor del jugador,
// y el cielo lo pega sobre la esfera: asi el planeta que ves arriba ES tu mundo.
static const int MAPA_N = 256;
static const double MAPA_SPAN = 24000.0;
static std::vector<uint8_t> g_mapaBuf;
static std::atomic<int> g_mapaEstado{0};          // 0 libre, 1 calculando, 2 listo para subir
static double g_mapaCx = 0, g_mapaCz = 0, g_mapaPCx = 0, g_mapaPCz = 0;
static bool g_mapaTiene = false;
static float g_mapaOk = 0.0f;
static void mapaCalcular(double cx, double cz, uint32_t semilla) {
    (void)semilla;
    static const float BC[BIO_N][3] = {{0.30f, 0.55f, 0.20f}, {0.15f, 0.40f, 0.15f}, {0.10f, 0.32f, 0.12f}, {0.80f, 0.70f, 0.45f}, {0.70f, 0.35f, 0.18f}, {0.25f, 0.38f, 0.22f},
                                       {0.85f, 0.90f, 0.95f}, {0.60f, 0.58f, 0.25f}, {0.65f, 0.40f, 0.15f}, {0.85f, 0.87f, 0.90f}, {0.85f, 0.55f, 0.65f}, {0.60f, 0.38f, 0.25f},
                                       {0.06f, 0.35f, 0.10f}, {0.12f, 0.30f, 0.22f}, {0.40f, 0.60f, 0.30f}, {0.92f, 0.96f, 1.00f}, {0.85f, 0.80f, 0.60f}, {0.05f, 0.25f, 0.45f}};
    std::vector<uint8_t> px((size_t)MAPA_N * MAPA_N * 4);
    for (int j = 0; j < MAPA_N; j++) for (int i = 0; i < MAPA_N; i++) {
        double x = cx + ((i + 0.5) / MAPA_N - 0.5) * MAPA_SPAN, z = cz + ((j + 0.5) / MAPA_N - 0.5) * MAPA_SPAN;
        float w[BIO_N]; int dom = biomaPesos(x, z, w);
        float c[3];
        // v77: relieve y colores de superficie como el mundo lejano (sombreado por pendiente), para que se parezca a lo que veras al bajar
        const double ce = MAPA_SPAN / MAPA_N;
        float yc = (float)alturaTerreno(x, z);
        float hxp = (float)alturaTerreno(x + ce, z), hxm = (float)alturaTerreno(x - ce, z), hzp = (float)alturaTerreno(x, z + ce), hzm = (float)alturaTerreno(x, z - ce);
        float gx = (hxp - hxm) / (2.0f * (float)ce) * 6.0f, gz = (hzp - hzm) / (2.0f * (float)ce) * 6.0f;   // pendiente exagerada: a esta escala las lomas casi no se notan
        float nyy = 1.0f / std::sqrt(gx * gx + 1.0f + gz * gz);
        float shm = 0.62f + 0.38f * nyy * nyy;
        shm *= 1.0f + std::max(-0.25f, std::min(0.25f, (gx * 0.5f + gz * 0.3f) * 0.6f));   // luz de lado para marcar el relieve
        if (dom != BIO_OCEANO && yc >= WATER_LEVEL - 0.6f) {
            const uint8_t sup = tipoSuperficie(dom, yc, (int)std::floor(x), (int)std::floor(z), false);
            if (sup == PIEDRA) { c[0] = 0.50f; c[1] = 0.50f; c[2] = 0.52f; }
            else if (sup == TIERRA) { c[0] = 0.45f; c[1] = 0.32f; c[2] = 0.20f; }
            else if (sup == ARCILLA) { c[0] = 0.62f; c[1] = 0.64f; c[2] = 0.72f; }
            else if (sup == ARENA) { c[0] = 0.86f; c[1] = 0.80f; c[2] = 0.55f; }
            else if (sup == NIEVE) { c[0] = 0.93f; c[1] = 0.95f; c[2] = 0.99f; }
            else if (sup == PASTO_SECO) { c[0] = 0.66f; c[1] = 0.60f; c[2] = 0.30f; }
            else if (sup == HIELO_COMPACTO) { c[0] = 0.40f; c[1] = 0.62f; c[2] = 0.93f; }
            else if (sup == GRAVA) { c[0] = 0.50f; c[1] = 0.48f; c[2] = 0.45f; }
            else {
                c[0] = 0.34f; c[1] = 0.64f; c[2] = 0.21f;
                float m = std::min(0.6f, std::max(0.0f, (1.0f - nyy) * 1.4f));
                c[0] += (0.45f - c[0]) * m; c[1] += (0.32f - c[1]) * m; c[2] += (0.20f - c[2]) * m;
            }
            for (int k = 0; k < 3; k++) c[k] *= shm;
        } else if (dom == BIO_OCEANO) {
            float u = std::max(0.0f, -continental(x, z)), pr = std::min(1.0f, u / 0.17f);
            if (temp01(x, z) < 0.2f) { c[0] = 0.78f; c[1] = 0.88f; c[2] = 0.95f; }                       // mar polar
            else { c[0] = 0.10f + (0.02f - 0.10f) * pr; c[1] = 0.48f + (0.10f - 0.48f) * pr; c[2] = 0.62f + (0.30f - 0.62f) * pr; }
        } else { c[0] = 0.12f; c[1] = 0.42f; c[2] = 0.70f; }       // lagos y rios
        (void)BC;
        uint8_t* o = &px[((size_t)j * MAPA_N + i) * 4];
        for (int k = 0; k < 3; k++) o[k] = (uint8_t)std::max(0.0f, std::min(255.0f, c[k] * 255.0f));
        o[3] = 255;
    }
    g_mapaBuf = std::move(px);
    g_mapaCx = cx; g_mapaCz = cz;                       // (el hilo principal solo lo lee cuando el estado es 2)
    g_mapaEstado.store(2);
}

// ---------------------------------------------------------------- TNT (v51)
// Se enciende con una antorcha (clic en el TNT). Todo el bloque de TNT conectado parpadea ~3 s y explota: crater esférico,
// el TNT vecino se enciende en cadena, y en survival te hace daño y te empuja.
struct TntFusible { std::vector<std::array<int, 3>> celdas; double t0, dur; bool blanco; };
static std::vector<TntFusible> g_tnt;
static std::unordered_set<int64_t> g_tntEnc;      // celdas que ya estan encendidas
struct Explosion { double x, y, z, t0, R; };
static std::vector<Explosion> g_explos;
static double g_explDestello = -100, g_explFuerza = 0;
static inline int64_t tntClave(int x, int y, int z) { return ((int64_t)(x + 1048576) << 42) | ((int64_t)(y + 1048576) << 21) | (int64_t)(z + 1048576); }
static inline bool esTnt(uint8_t t) { return t == TNT || t == TNT_ENC; }
static void tntEncender(int x, int y, int z, double ahora, double dur) {
    if (!esTnt(leerCubito(x, y, z)) || g_tntEnc.count(tntClave(x, y, z))) return;
    TntFusible f; f.t0 = ahora; f.dur = dur; f.blanco = false;
    std::vector<std::array<int, 3>> cola; cola.push_back({x, y, z});
    g_tntEnc.insert(tntClave(x, y, z));
    static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (size_t i = 0; i < cola.size() && cola.size() < 60000; i++) {
        auto c = cola[i];
        f.celdas.push_back(c);
        for (auto& d : D) {
            int nx = c[0] + d[0], ny = c[1] + d[1], nz = c[2] + d[2];
            if (!esTnt(leerCubito(nx, ny, nz)) || g_tntEnc.count(tntClave(nx, ny, nz))) continue;
            g_tntEnc.insert(tntClave(nx, ny, nz)); cola.push_back({nx, ny, nz});
        }
    }
    g_tnt.push_back(std::move(f));
}
static void tntExplotar(const TntFusible& f, double ahora) {
    std::vector<ChunkP> su;
    double mx = 0, my = 0, mz = 0;
    for (auto& c : f.celdas) { mx += c[0]; my += c[1]; mz += c[2]; }
    const double n = (double)f.celdas.size();
    mx /= n; my /= n; mz /= n;
    for (auto& c : f.celdas) g_tntEnc.erase(tntClave(c[0], c[1], c[2]));
    const double bloques = n / (double)(SUB * SUB * SUB);
    const double R = std::min(12.0, 3.2 + 1.3 * std::cbrt(std::max(1.0, bloques)));      // radio en bloques (1 TNT = 4.5)
    const double Rc = R * SUB;
    const bool envio = g_redEnviar; g_redEnviar = true;                                     // la explosion tambien se manda por la red
    for (auto& c : f.celdas) escribirCubito(c[0], c[1], c[2], AIRE, su);
    const int r = (int)std::ceil(Rc * 1.15) + 1;
    const int cx = (int)std::floor(mx), cy = (int)std::floor(my), cz = (int)std::floor(mz);
    std::vector<std::array<int, 3>> encadenar;
    for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        double d2 = (double)dx * dx + (double)dy * dy + (double)dz * dz;
        if (d2 > (Rc * 1.15) * (Rc * 1.15)) continue;
        int x = cx + dx, y = cy + dy, z = cz + dz;
        double lim = Rc * (0.84 + 0.30 * hf((uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(z * 83492791)));   // borde irregular
        if (d2 > lim * lim) continue;
        uint8_t t = leerCubito(x, y, z);
        if (t == AIRE || t == LAVA) continue;
        if (esTnt(t)) { encadenar.push_back({x, y, z}); continue; }
        if (!esAgua(t)) escribirCubito(x, y, z, AIRE, su);
    }
    g_redEnviar = envio;
    for (auto& c : su) remallarAsync(c);
    marcarLuzSucia(cx - r - SUB, cx + r + SUB, cy - r - SUB, cy + r + SUB, cz - r - SUB, cz + r + SUB, su);
    for (auto& c : encadenar) tntEncender(c[0], c[1], c[2], ahora, 0.35 + 0.9 * hf((uint32_t)(c[0] * 31 + c[2] * 17 + c[1])));
    const double ex = (mx + 0.5) * VS, ey = (my + 0.5) * VS, ez = (mz + 0.5) * VS;
    g_explos.push_back({ex, ey, ez, ahora, R});
    // jugador: empuje y dano segun la distancia
    double dx = g_j.p.x - ex, dy = (g_j.p.y + 0.9) - ey, dz = g_j.p.z - ez, d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double alc = R * 2.0;
    if (d < alc) {
        double k = 1.0 - d / alc;
        if (d < 0.01) { dx = 0; dy = 1; dz = 0; d = 1; }
        double imp = k * 22.0;
        g_j.inercia.x += dx / d * imp * 0.7; g_j.inercia.z += dz / d * imp * 0.7; g_j.v.y += dy / d * imp * 0.5 + k * 8.0;
        g_j.suelo = false;
        if (g_survival) survDano((float)(k * k * 34.0));
        g_caidaMax = g_j.p.y;       // la caida despues de la explosion no hace dano extra
    }
    g_explDestello = ahora; g_explFuerza = (float)std::max(0.15, 1.0 - d / (alc * 2.5));
}
static void tntActualizar(double ahora) {
    for (size_t i = 0; i < g_tnt.size();) {
        TntFusible& f = g_tnt[i];
        double edad = ahora - f.t0;
        if (edad >= f.dur) { TntFusible c = std::move(f); g_tnt.erase(g_tnt.begin() + i); tntExplotar(c, ahora); continue; }
        bool bl = ((int)(edad / 0.25) & 1) != 0;
        if (bl != f.blanco) {
            f.blanco = bl;
            std::vector<ChunkP> su;
            for (auto& c : f.celdas) if (esTnt(leerCubito(c[0], c[1], c[2]))) escribirCubito(c[0], c[1], c[2], bl ? TNT_ENC : TNT, su);
            for (auto& c : su) remallarYa(c);
        }
        i++;
    }
}
static const int VACIO = -1;                                                                  // casilla vacia (mano libre: solo rompe)
static int g_barra[9] = {VACIO, VACIO, VACIO, VACIO, VACIO, VACIO, VACIO, VACIO, VACIO};   // barra de 9 casillas
static int g_mano = -100, g_manoOrigen = -1;      // objeto agarrado en el menu (-100 = nada) y de que casilla de la barra vino (-1 = del menu)
static double g_manoX = 0, g_manoY = 0;           // donde se pulso, para distinguir clic de arrastre
static int g_sel = 0;                                                                        // casilla elegida
static void aplicarSel() { g_material = g_barra[g_sel]; g_destruir = g_material == 0; }
static std::string archivoBarra();
static void guardarBarra() {
    FILE* f = std::fopen(archivoBarra().c_str(), "w");
    if (!f) return;
    for (int i = 0; i < 9; i++) std::fprintf(f, "%d ", g_barra[i]);
    std::fclose(f);
}
static void cargarBarra() {
    FILE* f = std::fopen(archivoBarra().c_str(), "r");
    if (!f) { aplicarSel(); return; }
    int v[9]; bool ok = true;
    for (int i = 0; i < 9 && ok; i++) ok = std::fscanf(f, "%d", &v[i]) == 1;
    std::fclose(f);
    if (!ok) return;
    for (int i = 0; i < 9; i++) if (v[i] == VACIO || v[i] == ID_SPRAY || v[i] == ID_POLVORA || v[i] == ID_RADAR || v[i] == ID_PISTOLA || esObjNuevo(v[i]) || (v[i] >= 0 && v[i] < NTIPOS)) g_barra[i] = v[i];
    aplicarSel();
}
static std::string archivoSurv() { return g_dir + "/survival.txt"; }
static void guardarSurv() {
    FILE* f = std::fopen(archivoSurv().c_str(), "w");
    if (!f) return;
    std::fprintf(f, "%d %.2f %.4f %.4f %.4f %d\n", g_survival ? 1 : 0, g_vida, g_spawnS[0], g_spawnS[1], g_spawnS[2], g_spawnOk ? 1 : 0);
    for (int i = 0; i < 256; i++) std::fprintf(f, "%d ", g_inv[i]);
    std::fclose(f);
}
static void cargarSurv() {
    for (int i = 0; i < 256; i++) g_inv[i] = 0;
    g_survival = false; g_vida = 20.0f; g_spawnOk = false;
    FILE* f = std::fopen(archivoSurv().c_str(), "r");
    if (!f) return;
    int sv = 0, ok = 0; float v = 20; double sx, sy, sz;
    if (std::fscanf(f, "%d %f %lf %lf %lf %d", &sv, &v, &sx, &sy, &sz, &ok) == 6) {
        g_survival = sv != 0; g_vida = std::max(1.0f, std::min(20.0f, v)); g_spawnS[0] = sx; g_spawnS[1] = sy; g_spawnS[2] = sz; g_spawnOk = ok != 0;
        for (int i = 0; i < 256; i++) { int c = 0; if (std::fscanf(f, "%d", &c) != 1) break; g_inv[i] = std::max(0, std::min(9999, c)); }
    }
    std::fclose(f);
}
static void guardarJugador() {
    guardarBarra();
    guardarSurv();
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    FILE* f = std::fopen(archivoJugador().c_str(), "w");
    if (!f) return;
    std::fprintf(f, "%.4f %.4f %.4f %.5f %.5f %d %d %d\n", g_jRespOk ? g_jResp[0] : g_j.p.x, g_jRespOk ? g_jResp[1] : g_j.p.y, g_jRespOk ? g_jResp[2] : g_j.p.z, g_j.yaw, g_j.pitch,
                 g_j.volar ? 1 : 0, g_material, g_pincelBloque ? 1 : 0);
    std::fclose(f);
}
static bool cargarJugador() {
    cargarSurv();
    cargarBarra();
    FILE* f = std::fopen(archivoJugador().c_str(), "r");
    if (!f) return false;
    int vol = 0, mat = PASTO, pin = 0;
    double x, y, z, yaw, pit;
    bool ok = std::fscanf(f, "%lf %lf %lf %lf %lf %d %d %d", &x, &y, &z, &yaw, &pit, &vol, &mat, &pin) == 8;
    std::fclose(f);
    if (!ok) return false;
    g_j.p = {x, y, z}; g_j.yaw = yaw; g_j.pitch = pit; g_j.volar = vol != 0;
    (void)mat;
    g_pincelBloque = pin != 0;
    return true;
}

// ---------------------------------------------------------------- rayo (para romper / poner)
struct Golpe { bool hay = false; int x = 0, y = 0, z = 0; int px = 0, py = 0, pz = 0; };
static Golpe lanzarRayo(V3 o, V3 d, double alcance) {
    Golpe g;
    double ox = o.x * SUB, oy = o.y * SUB, oz = o.z * SUB;
    int x = (int)std::floor(ox), y = (int)std::floor(oy), z = (int)std::floor(oz);
    int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    double ix = std::fabs(d.x) < 1e-12 ? 1e30 : std::fabs(1.0 / d.x);
    double iy = std::fabs(d.y) < 1e-12 ? 1e30 : std::fabs(1.0 / d.y);
    double iz = std::fabs(d.z) < 1e-12 ? 1e30 : std::fabs(1.0 / d.z);
    double tx = ((sx > 0 ? x + 1 - ox : ox - x)) * ix;
    double ty = ((sy > 0 ? y + 1 - oy : oy - y)) * iy;
    double tz = ((sz > 0 ? z + 1 - oz : oz - z)) * iz;
    double lim = alcance * SUB;
    int px = x, py = y, pz = z;
    for (int i = 0; i < 4000; i++) {
        if (esRompible(leerCubito(x, y, z))) { g.hay = true; g.x = x; g.y = y; g.z = z; g.px = px; g.py = py; g.pz = pz; return g; }
        px = x; py = y; pz = z;
        double t;
        if (tx < ty && tx < tz) { t = tx; x += sx; tx += ix; }
        else if (ty < tz) { t = ty; y += sy; ty += iy; }
        else { t = tz; z += sz; tz += iz; }
        if (t > lim) break;
    }
    return g;
}

// ---------------------------------------------------------------- shaders
static const char* VS_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aInfo;
uniform mat4 uVP;
uniform vec3 uRel;      // origen del chunk menos la camara
uniform vec3 uAbs;      // origen absoluto del chunk
uniform float uVS;
uniform float uTime;
uniform float uViento;
out vec3 vAbs;
out float vDist;
flat out vec3 vInfo;
out vec3 vLoc;
out vec3 vRel;
void main(){
    vLoc = aPos;
    vec3 rel = uRel + aPos * uVS;
    vAbs = uAbs + aPos * uVS;
    vDist = length(rel);
    vRel = rel;
    vInfo = aInfo.xyz;
    int tp = int(aInfo.x + 0.5);
    if (uViento > 0.5 && (tp == 7 || (tp >= 67 && tp <= 71) || tp == 74 || (tp >= 75 && tp <= 82 && tp != 76) || tp == 76)) {
        float amp = (tp == 75) ? 0.09 : (tp >= 79 && tp <= 82) ? 0.06 : (tp == 76) ? 0.035 : 0.05;   // hojas y plantas se mecen con el viento
        vec3 wp = vAbs;
        float r1 = sin(wp.x * 0.11 + wp.z * 0.08 + uTime * 0.9) * 0.6 + sin(wp.x * 0.37 - wp.z * 0.29 + uTime * 1.9) * 0.4;
        float r2 = sin(wp.z * 0.13 - wp.x * 0.07 + uTime * 0.8) * 0.6 + sin(wp.x * 0.31 + wp.z * 0.41 + uTime * 2.3) * 0.4;
        rel += vec3(r1, 0.35 * sin(wp.x * 0.9 + wp.z * 0.7 + uTime * 2.6), r2 * 0.7) * amp * (0.6 + 0.4 * r1);
    }
    gl_Position = uVP * vec4(rel, 1.0);
})";

static const char* FS_SRC = R"(#version 330 core
in vec3 vAbs;
in vec3 vLoc;
in vec3 vRel;
in float vDist;
flat in vec3 vInfo;
out vec4 frag;
uniform sampler3D uLuz;
uniform sampler2D uSombra;
uniform mat4 uSVP;
uniform vec3 uSolDir;
uniform float uSombraOn;
uniform float uSombraR;
uniform float uSolF;
uniform float uHDR;
uniform float uSombraTx;
uniform float uLMax;
uniform float uLN;
uniform float uSkyMul;
uniform float uSub;
uniform vec3 uSky;
uniform float uFogA;
uniform float uFogB;
uniform float uTime;
uniform float uAguaFx;
uniform float uBuceo;
const vec3 NRM[6] = vec3[6](vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1));
float hash(vec3 p){ p = fract(p * 0.1031); p += dot(p, p.yzx + 33.33); return fract((p.x + p.y) * p.z); }
float vnA(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(vec3(i, 1.0)), hash(vec3(i + vec2(1.0, 0.0), 1.0)), f.x), mix(hash(vec3(i + vec2(0.0, 1.0), 1.0)), hash(vec3(i + vec2(1.0, 1.0), 1.0)), f.x), f.y); }
float olaH(vec2 p, float t, float foot){   // altura de las olas; cada capa se apaga cuando es mas fina que un pixel (foot = tamano del pixel en el mundo): evita el centelleo
    float a1 = 1.0 - smoothstep(0.15, 0.45, foot * 0.9), a2 = 1.0 - smoothstep(0.15, 0.45, foot * 2.2), a3 = 1.0 - smoothstep(0.15, 0.45, foot * 5.3);
    return mix(0.5, vnA(p * 0.9 + vec2(t * 0.30, t * 0.18)), a1) * 0.55 + mix(0.5, vnA(p * 2.2 - vec2(t * 0.45, -t * 0.27)), a2) * 0.30 + mix(0.5, vnA(p * 5.3 + vec2(t * 0.85, t * 0.6)), a3) * 0.15;
}
const vec3 LEDC[12] = vec3[12](vec3(1.0,0.10,0.10), vec3(1.0,0.48,0.08), vec3(1.0,0.90,0.15), vec3(0.60,1.0,0.12), vec3(0.10,1.0,0.20), vec3(0.08,1.0,0.80),
                               vec3(0.15,0.70,1.0), vec3(0.15,0.20,1.0), vec3(0.55,0.18,1.0), vec3(1.0,0.15,0.80), vec3(1.0,0.50,0.72), vec3(1.0,1.0,1.0));
const vec3 CARRC[12] = vec3[12](vec3(0.80,0.12,0.12), vec3(0.95,0.45,0.08), vec3(0.95,0.85,0.15), vec3(0.55,0.80,0.15), vec3(0.12,0.62,0.22), vec3(0.10,0.65,0.62),
                                vec3(0.18,0.55,0.90), vec3(0.15,0.22,0.75), vec3(0.45,0.20,0.78), vec3(0.80,0.18,0.62), vec3(0.95,0.55,0.70), vec3(0.97,0.97,0.97));
const vec2 POIS[12] = vec2[12](vec2(-0.326,-0.406), vec2(-0.840,-0.074), vec2(-0.696,0.457), vec2(-0.203,0.621), vec2(0.962,-0.195), vec2(0.473,-0.480),
                               vec2(0.519,0.767), vec2(0.185,-0.893), vec2(0.507,0.064), vec2(0.896,0.412), vec2(-0.322,-0.933), vec2(-0.792,-0.598));
float sombraSol(vec3 n){
    float ndl = dot(n, uSolDir);
    float fade = 1.0 - smoothstep(uSombraR * 0.72, uSombraR * 0.97, vDist);
    if (ndl <= 0.02 || fade <= 0.0) return 1.0;
    vec3 pw = vRel + n * 0.07 + uSolDir * 0.02;
    vec3 sc = (uSVP * vec4(pw, 1.0)).xyz * 0.5 + 0.5;
    if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0) return 1.0;
    float rot = hash(vec3(gl_FragCoord.xy, 3.0)) * 6.2831853;
    float cs = cos(rot), sn = sin(rot);
    float bias = 0.0007 + 0.0016 * (1.0 - ndl);
    float rad = 1.8 * uSombraTx;
    float lit = 0.0;
    for (int i = 0; i < 12; i++) {
        vec2 o = vec2(POIS[i].x * cs - POIS[i].y * sn, POIS[i].x * sn + POIS[i].y * cs) * rad;
        lit += (sc.z - bias <= texture(uSombra, sc.xy + o).r) ? 1.0 : 0.0;
    }
    return mix(1.0, lit / 12.0, fade);
}
void main(){
    int tipo = int(vInfo.x + 0.5);
    int cara = int(vInfo.y + 0.5);
    vec3 n = NRM[cara];
    vec3 col; float alfa = 1.0; float lumi = 1.0;
    if (tipo == 1) col = (cara == 2) ? vec3(0.34,0.64,0.21) : vec3(0.45,0.32,0.20);
    else if (tipo == 2) col = vec3(0.45,0.32,0.20);
    else if (tipo == 3) col = vec3(0.50,0.50,0.52);
    else if (tipo == 4) col = vec3(0.86,0.80,0.55);
    else if (tipo == 5) { if (uAguaFx < 0.5) { col = vec3(0.18,0.38,0.80); alfa = 0.65; } else { col = vec3(0.07,0.34,0.55); alfa = 0.70; } }
    else if (tipo == 6) col = vec3(0.40,0.28,0.15);
    else if (tipo == 7) col = vec3(0.20,0.52,0.16);
    else if (tipo == 14) col = vec3(0.62,0.64,0.72);
    else if (tipo == 21) col = (cara == 2) ? vec3(1.0,0.93,0.50) : vec3(0.98,0.62,0.20);
    else if (tipo == 44) { col = vec3(0.62,0.80,0.90); alfa = 0.30; }
    else if (tipo == 45) col = vec3(0.85,0.86,0.88);
    else if (tipo == 46) col = vec3(0.55,0.12,0.12);
    else if (tipo == 47) col = vec3(0.15,0.15,0.18);
    else if (tipo == 48) col = vec3(0.92,0.92,0.95);
    else if (tipo == 49) col = vec3(0.07,0.07,0.08);
    else if (tipo == 50) col = vec3(0.55,0.38,0.20);
    else if (tipo == 51) col = vec3(0.95,0.65,0.10);
    else if (tipo >= 64) {
        if (tipo == 64) col = vec3(0.93,0.95,0.99);                       // nieve
        else if (tipo == 65) col = vec3(0.60,0.80,0.96);                  // hielo
        else if (tipo == 66) col = (cara == 2) ? vec3(0.30,0.58,0.26) : vec3(0.20,0.50,0.22);   // cactus
        else if (tipo == 67) col = vec3(0.78,0.13,0.10);                  // hojas rojas
        else if (tipo == 68) col = vec3(0.93,0.47,0.08);                  // hojas naranjas
        else if (tipo == 69) col = vec3(0.94,0.80,0.13);                  // hojas amarillas
        else if (tipo == 70) col = vec3(0.98,0.64,0.78);                  // hojas rosas (cerezo)
        else if (tipo == 71) col = vec3(0.09,0.33,0.19);                  // pino
        else if (tipo == 72) col = (cara == 2) ? vec3(0.66,0.60,0.30) : vec3(0.45,0.32,0.20);   // pasto seco
        else if (tipo == 73) col = vec3(0.36,0.22,0.14);                  // madera de selva
        else if (tipo == 74) col = vec3(0.07,0.40,0.12);                  // hojas de selva
        else if (tipo == 75) {                                            // liana: tira fina
            vec3 fr = fract((vAbs - n * 0.002) * uSub);
            vec2 q = abs(n.y) > 0.5 ? fr.xz : (abs(n.x) > 0.5 ? fr.zy : fr.xy);
            if (abs(q.x - 0.5) > 0.13 || (abs(n.y) > 0.5 && abs(q.y - 0.5) > 0.13)) discard;
            col = vec3(0.14,0.45,0.13);
        }
        else if (tipo == 76) {                                            // bambu con nudos
            col = vec3(0.56,0.74,0.26);
            if (fract((vAbs.y - n.y * 0.002) * uSub * 0.5) < 0.18) col = vec3(0.34,0.50,0.14);
        }
        else if (tipo == 77) {                                            // hojas con nieve
            col = vec3(0.92,0.95,0.97);
            if (hash(floor((vAbs - n * 0.002) * uSub * 2.0) + 7.0) > 0.62) col = vec3(0.10,0.34,0.20);
        }
        else if (tipo == 78) {                                            // arbusto de bayas
            col = vec3(0.16,0.42,0.14);
            if (hash(floor((vAbs - n * 0.002) * uSub * 2.0) + 11.0) > 0.78) col = vec3(0.82,0.10,0.18);
        }
        else if (tipo <= 82) {                                            // flores: tallo y cabeza (objetos delgados)
            vec3 fr = fract((vAbs - n * 0.002) * uSub);
            bool arriba = abs(n.y) > 0.5;
            vec2 q = arriba ? fr.xz : (abs(n.x) > 0.5 ? fr.zy : fr.xy);
            float dx = abs(q.x - 0.5);
            bool cabeza = arriba ? (dx < 0.34 && abs(q.y - 0.5) < 0.34) : (q.y > 0.5 && dx < 0.34);
            bool tallo = !arriba && q.y <= 0.5 && dx < 0.09;
            if (!cabeza && !tallo) discard;
            if (tallo) col = vec3(0.20,0.52,0.16);
            else if (tipo == 79) col = vec3(0.90,0.12,0.12);
            else if (tipo == 80) col = vec3(0.98,0.85,0.12);
            else if (tipo == 81) col = vec3(0.22,0.35,0.95);
            else col = vec3(0.98,0.45,0.75);
        }
        else if (tipo == 83) col = vec3(0.40,0.62,0.93);                  // hielo compacto (azul grueso)
        else if (tipo >= 85 && tipo <= 90) {                              // corales: bloques vivos con poros y cerebros
            vec3 cb = vec3(0.92,0.20,0.22);
            if (tipo == 86) cb = vec3(0.98,0.46,0.70);
            else if (tipo == 87) cb = vec3(0.98,0.52,0.14);
            else if (tipo == 88) cb = vec3(0.62,0.28,0.88);
            else if (tipo == 89) cb = vec3(0.98,0.84,0.18);
            else if (tipo == 90) cb = vec3(0.20,0.50,0.95);
            vec3 pc = (vAbs - n * 0.002) * uSub;
            float c1 = hash(floor(pc * 2.0) + 5.0);
            float c2 = hash(floor(pc * 4.0) + 9.0);
            col = cb * (0.78 + 0.40 * c1);
            if (c2 > 0.84) col *= 0.50;                                     // poros oscuros
            else if (c2 < 0.10) col = mix(col, vec3(1.0), 0.28);            // puntas claras
        }
        else if (tipo == 91) {                                            // alga: hoja fina que se mece con la corriente
            vec3 fr = fract((vAbs - n * 0.002) * uSub);
            bool arriba = abs(n.y) > 0.5;
            if (arriba) discard;
            vec2 q = abs(n.x) > 0.5 ? fr.zy : fr.xy;
            float sway = 0.12 * sin(uTime * 1.6 + vAbs.y * 2.4 + floor(vAbs.x * uSub) * 1.7 + floor(vAbs.z * uSub) * 1.1);
            if (abs(q.x - 0.5 + sway) > 0.26) discard;
            col = mix(vec3(0.06,0.34,0.13), vec3(0.22,0.62,0.24), clamp(q.y, 0.0, 1.0));
        }
        else if (tipo == 92) {                                            // grava: guijarros grises y pardos
            vec3 pg = (vAbs - n * 0.002) * uSub;
            float g1 = hash(floor(pg * 2.0) + 13.0);
            col = mix(vec3(0.42,0.41,0.40), vec3(0.62,0.58,0.52), g1);
            if (hash(floor(pg * 4.0) + 21.0) > 0.80) col *= 0.70;
        }
        else if (tipo == 93 || tipo == 94) {                              // TNT: cuerpo rojo, franja blanca con letras; 94 = encendido (blanco)
            vec3 fr = fract((vAbs - n * 0.002));
            if (abs(n.y) > 0.5) {
                float r = length(fr.xz - 0.5);
                col = mix(vec3(0.78,0.70,0.55), vec3(0.45,0.40,0.32), smoothstep(0.10, 0.14, r) * (1.0 - smoothstep(0.17, 0.21, r)));
                if (r < 0.06) col = vec3(0.20,0.18,0.15);
            } else {
                float u = abs(n.x) > 0.5 ? fr.z : fr.x;
                col = vec3(0.80,0.12,0.10) * (0.92 + 0.08 * hash(floor(fr * 8.0) + 3.0));
                if (fr.y > 0.28 && fr.y < 0.72) {
                    col = vec3(0.93,0.92,0.88);
                    float v = (fr.y - 0.28) / 0.44;
                    bool L = false;
                    if ((u > 0.08 && u < 0.30 && v > 0.78) || (u > 0.16 && u < 0.22)) L = true;
                    if ((u > 0.38 && u < 0.43) || (u > 0.55 && u < 0.60) || (u > 0.38 && u < 0.60 && abs(u - 0.38 - (1.0 - v) * 0.22) < 0.035)) L = true;
                    if ((u > 0.68 && u < 0.90 && v > 0.78) || (u > 0.76 && u < 0.82)) L = true;
                    if (L) col = vec3(0.12,0.10,0.10);
                }
            }
            if (tipo == 94) col = mix(col, vec3(1.0,0.97,0.9), 0.82);
        }
        else if (tipo >= 95 && tipo <= 119) {                             // v63: bloques de la base lunar
            vec3 fr = fract(vAbs - n * 0.002);
            vec3 pc = (vAbs - n * 0.002) * uSub;
            vec2 q = abs(n.y) > 0.5 ? fr.xz : (abs(n.x) > 0.5 ? fr.zy : fr.xy);
            float borde = min(min(q.x, 1.0 - q.x), min(q.y, 1.0 - q.y));
            if (tipo == 95) {                                             // concreto con juntas y manchas de oxido
                col = vec3(0.60,0.60,0.58) * (0.90 + 0.10 * hash(floor(pc * 2.0) + 31.0));
                col *= 0.80 + 0.20 * smoothstep(0.0, 0.05, borde);
                float mancha = vnA(vAbs.xz * 0.9 + vAbs.y * 1.7);
                col = mix(col, vec3(0.45,0.36,0.27), smoothstep(0.62, 0.85, mancha) * 0.30);
            } else if (tipo == 96) {                                      // metal oscuro con remaches
                col = vec3(0.27,0.30,0.35) * (0.92 + 0.08 * hash(floor(pc * 2.0) + 41.0));
                col *= 0.72 + 0.28 * smoothstep(0.0, 0.06, borde);
                if (length(vec2(min(q.x, 1.0 - q.x), min(q.y, 1.0 - q.y)) - 0.12) < 0.05) col = vec3(0.55,0.58,0.62);
            } else if (tipo == 97) {                                      // rejilla: barras claras y huecos oscuros
                vec2 g = fract(q * 4.0);
                bool barra = g.x < 0.22 || g.y < 0.22;
                col = barra ? vec3(0.52,0.54,0.58) : vec3(0.06,0.07,0.09);
            } else if (tipo == 98) {                                      // tuberia: cilindro brillante con juntas
                float u = abs(n.y) > 0.5 ? q.x : (abs(n.x) > 0.5 ? q.x : q.y);
                float cil = 0.55 + 0.45 * cos((u - 0.5) * 3.14159);
                col = vec3(0.50,0.52,0.56) * (0.55 + 0.55 * cil);
                if (abs(n.y) < 0.5 && abs(q.y - 0.5) < 0.06) col *= 0.55;
            } else if (tipo == 99) {                                      // hoja de la puerta: paneles biselados con tornillos
                col = vec3(0.43,0.44,0.46) * (0.94 + 0.06 * hash(floor(pc * 2.0) + 51.0));
                float i1 = min(min(q.x - 0.14, 0.86 - q.x), min(q.y - 0.14, 0.86 - q.y));
                if (i1 > 0.0 && i1 < 0.05) col *= 0.60;
                else if (i1 >= 0.05) col *= 1.10;
                if (borde < 0.05) col *= 0.60;
                if (length(vec2(min(q.x, 1.0 - q.x), min(q.y, 1.0 - q.y)) - 0.075) < 0.03) col = vec3(0.62,0.63,0.66);
            } else if (tipo == 100) {                                     // teclado numerico
                col = vec3(0.16,0.17,0.19);
                if (q.y > 0.68 && q.y < 0.92 && q.x > 0.14 && q.x < 0.86) col = vec3(0.10,0.34,0.22) + vec3(0.0,0.18,0.0) * (0.5 + 0.5 * sin(uTime * 3.0));
                else {
                    vec2 b = fract(vec2(q.x * 3.0, q.y * 4.0 / 0.68 * 0.68));
                    if (q.y < 0.64 && q.x > 0.10 && q.x < 0.90 && b.x > 0.18 && b.x < 0.82 && b.y > 0.2 && b.y < 0.8) col = vec3(0.62,0.64,0.68);
                }
            } else if (tipo == 101) {                                     // pantalla: lineas cian que corren
                col = vec3(0.03,0.08,0.11);
                float ln = smoothstep(0.80, 1.0, sin(q.y * 24.0 - uTime * 2.0 + hash(vec3(floor(pc.xz), 5.0)) * 6.0) * 0.5 + 0.5);
                col += vec3(0.05,0.55,0.75) * ln * (0.45 + 0.55 * hash(floor(pc * 2.0) + 61.0));
                if (borde < 0.06) col = vec3(0.12,0.14,0.17);
            } else if (tipo == 102) {                                     // franjas de peligro
                float d_ = fract((q.x + q.y) * 3.0);
                col = d_ < 0.5 ? vec3(0.92,0.76,0.08) : vec3(0.08,0.08,0.09);
            } else if (tipo == 103) {                                     // piso de cuadros
                float ck = mod(floor(q.x * 2.0) + floor(q.y * 2.0), 2.0);
                col = mix(vec3(0.30,0.32,0.36), vec3(0.42,0.44,0.48), ck) * (0.92 + 0.08 * hash(floor(pc * 2.0) + 71.0));
                col *= 0.80 + 0.20 * smoothstep(0.0, 0.04, borde);
            } else if (tipo == 110) {                                     // acero rojo de la torre: pintura roja con remaches y oxido
                col = vec3(0.62,0.12,0.08) * (0.88 + 0.12 * hash(floor(pc * 2.0) + 91.0));
                col *= 0.62 + 0.38 * smoothstep(0.0, 0.10, borde);
                col = mix(col, vec3(0.30,0.12,0.06), smoothstep(0.65, 0.9, vnA(vAbs.xz * 1.3 + vAbs.y * 2.1)) * 0.5);
                if (length(vec2(min(q.x, 1.0 - q.x), min(q.y, 1.0 - q.y)) - 0.16) < 0.06) col = vec3(0.78,0.32,0.20);
            } else if (tipo == 111) {                                     // muro azul del silo: paneles de concreto con manchas
                col = vec3(0.20,0.28,0.38) * (0.90 + 0.10 * hash(floor(pc * 2.0) + 101.0));
                col *= 0.70 + 0.30 * smoothstep(0.0, 0.05, borde);
                col *= 0.80 + 0.30 * vnA(vec2(vAbs.x + vAbs.z, vAbs.y * 0.35) * 0.8);
            } else if (tipo == 112) {                                     // cable negro con brillo
                col = vec3(0.05,0.05,0.06) + vec3(0.10) * smoothstep(0.35, 0.5, 0.5 - abs(q.x - 0.5));
            } else if (tipo == 113) {                                     // aviso amarillo con letras negras
                col = vec3(0.90,0.74,0.10);
                if (borde < 0.08) col = vec3(0.07,0.07,0.08);
                else if (q.y > 0.30 && q.y < 0.70 && hash(vec3(floor(q.x * 6.0), floor(pc.x + pc.z + pc.y), 7.0)) > 0.45) col = vec3(0.07,0.07,0.08);
            } else if (tipo == 114) {                                     // armario electrico gris con luces de estado
                col = vec3(0.38,0.40,0.42) * (0.94 + 0.06 * hash(floor(pc * 2.0) + 111.0));
                if (abs(q.x - 0.5) < 0.025 || borde < 0.05) col *= 0.55;
                if (length(q - vec2(0.25, 0.8)) < 0.08) col = vec3(0.95, 0.15, 0.10);
                if (length(q - vec2(0.75, 0.8)) < 0.08) col = vec3(0.95, 0.75, 0.15);
            } else if (tipo == 115) {                                     // rejilla circular: chapa con agujeros redondos y borde biselado
                vec2 gq = fract(q * 3.0) - 0.5;
                float rr = length(gq);
                col = vec3(0.50,0.52,0.56) * (0.90 + 0.10 * hash(floor(pc * 2.0) + 121.0));
                if (abs(n.y) > 0.5) {
                    if (rr < 0.30) col = vec3(0.02,0.02,0.03);
                    else if (rr < 0.37) col = vec3(0.78,0.80,0.84);
                }
                col *= 0.70 + 0.30 * smoothstep(0.0, 0.05, borde);
            } else if (tipo == 116) {                                     // pantalla de la IA apagada: cristal negro azulado con borde
                col = vec3(0.025,0.035,0.055) * (0.9 + 0.1 * hash(floor(pc * 2.0) + 131.0));
                col += vec3(0.0,0.012,0.03) * smoothstep(0.4, 0.5, vnA(vAbs.xy * 3.0 + vAbs.z));
            } else if (tipo == 117) {                                     // pantalla de la IA encendida: barras azules que suben y bajan
                vec2 sq = abs(n.z) > 0.5 ? vAbs.xy : (abs(n.x) > 0.5 ? vAbs.zy : vAbs.xz);
                float cx = floor(sq.x * uSub * 0.5);                      // columna (cada una mide un bloque)
                float ph = hash(vec3(cx, 3.0, 17.0)) * 6.2831;
                float sp = 0.6 + hash(vec3(cx, 9.0, 4.0)) * 1.4;
                float alt = 0.5 + 0.45 * sin(uTime * sp + ph);            // altura de la barra: sube y baja
                float yb = clamp((sq.y - 92.0) / 9.0, 0.0, 1.0);
                float onbar = step(yb, alt);
                float seg = smoothstep(0.0, 0.12, fract(sq.y * uSub * 0.5));
                float gl = 0.30 + 0.70 * onbar * seg;
                float punta = 1.0 - smoothstep(0.0, 0.06, abs(yb - alt));
                col = vec3(0.04,0.20,0.55) * gl + vec3(0.30,0.70,1.0) * punta + vec3(0.0,0.03,0.10);
                col *= 0.8 + 0.2 * sin(uTime * 2.0 + cx);
            } else if (tipo == 118) {                                     // consola: panel gris con botones y lucecitas
                col = vec3(0.30,0.32,0.36) * (0.92 + 0.08 * hash(floor(pc * 2.0) + 141.0));
                col *= 0.70 + 0.30 * smoothstep(0.0, 0.05, borde);
                vec2 gq = fract(q * 3.0) - 0.5;
                if (abs(n.y) > 0.5 || abs(n.z) > 0.5) {
                    float h3 = hash(vec3(floor(q * 3.0), 0.0) + floor(pc * 0.5) + 151.0);
                    if (length(gq) < 0.17) col = h3 > 0.66 ? vec3(0.15,0.75,1.0) : (h3 > 0.33 ? vec3(0.90,0.20,0.12) : vec3(0.08,0.09,0.10));
                }
            } else if (tipo == 119) {                                     // boton grande rojo luminoso
                float rr = length(q - 0.5);
                col = vec3(0.12,0.12,0.14);
                if (rr < 0.42) col = vec3(0.95,0.10,0.06) * (0.8 + 0.2 * sin(uTime * 4.0));
                if (rr < 0.20) col = vec3(1.0,0.55,0.45);
            } else if (tipo >= 105 && tipo <= 109) {                      // plasma: nucleo brillante con borde suave y pulso lento
                vec3 pl = tipo == 105 ? vec3(0.80,0.92,1.0) : (tipo == 106 ? vec3(0.35,1.0,0.50) : (tipo == 107 ? vec3(0.20,0.85,1.0) : (tipo == 108 ? vec3(1.0,0.15,0.10) : vec3(1.0,0.68,0.22))));
                float nuc = smoothstep(0.0, 0.30, borde);
                float pulso = 0.90 + 0.10 * sin(uTime * (tipo == 108 ? 4.0 : 1.5) + hash(floor(pc * 0.5)) * 6.0);
                col = mix(pl * 0.35, mix(pl, vec3(1.0), 0.35), nuc) * pulso;
            } else {                                                      // placa clara
                col = vec3(0.70,0.72,0.76) * (0.94 + 0.06 * hash(floor(pc * 2.0) + 81.0));
                col *= 0.75 + 0.25 * smoothstep(0.0, 0.06, borde);
            }
        }
        else {                                                            // lava: manchas que se mueven despacio, del rojo oscuro al amarillo
            vec2 lp = abs(n.y) > 0.5 ? vAbs.xz : (abs(n.x) > 0.5 ? vAbs.zy : vAbs.xy);
            float l1 = vnA(lp * 1.4 + vec2(uTime * 0.05, -uTime * 0.035));
            float l2 = vnA(lp * 3.6 - vec2(uTime * 0.09, uTime * 0.07));
            float lv = clamp(l1 * 0.75 + l2 * 0.55 - 0.15, 0.0, 1.0);
            col = mix(vec3(0.46,0.05,0.0), vec3(1.0,0.66,0.10), lv * lv * 1.2);
        }
    }
    else if (tipo >= 52) col = CARRC[tipo - 52];
    else if (tipo >= 32) col = LEDC[tipo - 32];
    else {
        // mineral: roca con motas de color (cada mota es medio cubito)
        col = vec3(0.50,0.50,0.52);
        vec3 oc = vec3(0.07,0.07,0.08);
        if (tipo == 9) oc = vec3(0.80,0.60,0.47);
        else if (tipo == 10) oc = vec3(0.82,0.47,0.22);
        else if (tipo == 11) oc = vec3(0.96,0.80,0.18);
        else if (tipo == 12) oc = vec3(0.30,0.92,0.95);
        else if (tipo == 13) oc = vec3(0.95,0.94,0.92);
        else if (tipo == 15) oc = vec3(0.66,0.62,0.55);   // estano
        else if (tipo == 16) oc = vec3(0.70,0.33,0.26);   // bauxita (aluminio)
        else if (tipo == 17) oc = vec3(0.72,0.78,0.88);   // plata
        else if (tipo == 18) oc = vec3(0.30,0.38,0.55);   // titanio
        else if (tipo == 19) oc = vec3(0.78,0.62,0.92);   // litio
        else if (tipo == 20) oc = vec3(0.45,0.95,0.18);   // uranio
        else if (tipo == 30) oc = vec3(0.90,0.92,0.28);   // azufre
        else if (tipo == 31) oc = vec3(0.93,0.82,0.80);   // salitre
        if (hash(floor((vAbs - n * 0.002) * uSub * 2.0) + 3.0) > 0.42) col = oc;
    }
    // textura "de pixeles" barata: variacion por cubito y por cuarto de cubito (no rompe el fusionado de caras)
    vec3 p = vAbs - n * 0.002;
    float r = (hash(floor(p * uSub)) - 0.5) * 0.16 + (hash(floor(p * uSub * 4.0) + 7.0) - 0.5) * 0.07;
    col *= 1.0 + r * ((tipo == 5 && uAguaFx > 0.5) ? 0.0 : 1.0);
    if ((tipo >= 32 && tipo < 44) || (tipo >= 105 && tipo <= 109) || tipo == 117 || tipo == 119) {
        col *= mix(1.1, 1.9, uHDR);       // los LED brillan por si solos
    } else if (tipo == 84) {
        col *= mix(1.25, 2.4, uHDR);      // la lava brilla por si sola
    } else if (tipo == 21) {
        col *= mix(1.15, 2.0, uHDR);      // la antorcha brilla por si sola
    } else {
        // luz como en Minecraft: nivel de cielo (segun la hora) o de bloque, el mayor; se lee de la textura 3D del chunk
        vec3 q = vLoc + n * 0.5;
        vec4 lz = texture(uLuz, (vec3(q.x, q.z, q.y) + 1.0) / uLN);      // la textura se guarda en orden x, z, y
        float cielo = lz.r * 255.0 / uLMax * uSkyMul;
        vec3 lcol = lz.gba * 255.0 / uLMax;                                // luz de bloque en rojo, verde y azul
        float bloque = max(lcol.r, max(lcol.g, lcol.b));
        float nivel = clamp(max(cielo, bloque), 0.0, 1.0);
        if (uBuceo > 0.5) nivel = max(nivel, 0.9 * uSkyMul);       // buceando: el agua no oscurece con la profundidad
        float br = 0.045 + 0.955 * exp(-3.35 * (1.0 - nivel));
        float cara_s = (cara == 2) ? 1.0 : ((cara == 3) ? 0.5 : ((cara < 2) ? 0.62 : 0.80));
        col *= br * cara_s;
        lumi = br * cara_s;
        if (uSombraOn > 0.5) {
            float sm = sombraSol(n);
            float ndl2 = max(dot(n, uSolDir), 0.0);
            float cz = clamp(cielo, 0.0, 1.0) * uSolF;
            col *= mix(vec3(1.0), mix(vec3(0.60, 0.67, 0.86), vec3(1.0), sm), cz);        // la sombra es azulada, como la luz del cielo
            col *= 1.0 + uHDR * 0.26 * cz * sm * ndl2;                                  // el sol ilumina mas fuerte (rango HDR)
            col *= mix(vec3(1.0), vec3(1.04, 1.0, 0.92), cz * sm * ndl2 * 0.7);          // sol calido
        }
        vec3 tono = bloque > 0.001 ? lcol / bloque : vec3(1.0);                             // color de la luz de bloque
        col *= mix(vec3(1.0), min(tono * 1.15, vec3(1.3)), clamp((bloque - cielo) * 1.4, 0.0, 1.0));   // antorcha calida o LED de color
    }
    float foot = length(fwidth(vAbs.xz));          // tamano de un pixel sobre el agua (crece con la distancia y con el angulo rasante)
    if (tipo == 5 && uAguaFx > 0.5 && cara == 2 && vRel.y > 0.0) {   // la superficie vista desde abajo (buceando): sin brillos ni espuma
        col = mix(vec3(0.10, 0.42, 0.62) * max(lumi, 0.20), uSky * 0.45, 0.30);
        alfa = 0.82;
    } else if (tipo == 5 && uAguaFx > 0.5) {   // agua estilo Photon: olas animadas, reflejo del cielo (Fresnel), brillo del sol y color profundo
        vec3 V = normalize(vRel);
        vec3 nw = n;
        if (cara == 2 && uAguaFx > 1.5) {
            vec2 pw = vAbs.xz; float e = 0.12;
            float h0 = olaH(pw, uTime, foot);
            vec2 g = vec2(olaH(pw + vec2(e, 0.0), uTime, foot) - h0, olaH(pw + vec2(0.0, e), uTime, foot) - h0) / e;
            float amp = 0.55 * smoothstep(0.04, 0.32, -V.y) * (1.0 - smoothstep(180.0, 520.0, vDist));   // de lado y a lo lejos el agua se alisa
            nw = normalize(vec3(-g.x * amp, 1.0, -g.y * amp));
        }
        float cv = clamp(dot(-V, nw), 0.0, 1.0);
        float fres = 0.03 + 0.97 * pow(1.0 - cv, 5.0);
        vec3 R = reflect(V, nw);
        float up = clamp(R.y, 0.0, 1.0);
        float dia = clamp(uSkyMul, 0.0, 1.0);
        vec3 cenit = vec3(0.20, 0.42, 0.88) * (0.10 + 0.90 * dia);
        vec3 refl = mix(uSky, cenit, pow(up, 0.55));
        float luzA = max(dot(col, vec3(0.33)), 0.0);
        vec3 prof = col;                                                   // color del agua ya iluminado (se oscurece en cuevas y de noche)
        float dB = 12.0, esp = 1.0;                                        // profundidad en bloques (solo la superficie la trae)
        if (cara == 2) {
            float rz = vnA(vAbs.xz * 1.7 + vec2(uTime * 0.2, -uTime * 0.15));
            dB = vInfo.z + (rz - 0.5) * 1.3 + 0.35;                         // el ruido suaviza los escalones de 1 bloque
            float tp = clamp(dB / 11.0, 0.0, 1.0);
            vec3 somero = vec3(0.17, 0.60, 0.62), hondo = vec3(0.015, 0.11, 0.30);
            prof *= mix(somero, hondo, pow(tp, 0.55)) / vec3(0.07, 0.34, 0.55);       // turquesa en lo somero, azul oscuro en lo hondo
            prof *= mix(1.0, 0.45, smoothstep(14.0, 31.0, dB));                       // mar profundo: casi negro
            esp = smoothstep(0.0, 5.0, dB);
        }
        prof *= 1.0 + 0.30 * (olaH(vAbs.xz * 0.6, uTime * 0.5, foot * 0.6) - 0.5);       // manchas
        col = mix(prof, refl * (0.25 + 0.75 * clamp(luzA * 6.0, 0.0, 1.0)), fres * 0.80);
        float sp = pow(max(dot(R, uSolDir), 0.0), mix(220.0, 45.0, clamp(foot * 2.5, 0.0, 1.0))) * uSolF;
        col += vec3(1.0, 0.93, 0.78) * sp * (1.5 + 3.0 * uHDR);
        col += vec3(1.0, 0.93, 0.78) * pow(max(dot(R, uSolDir), 0.0), 12.0) * 0.12 * uSolF;   // resplandor ancho
        alfa = clamp(mix(mix(0.30, 0.94, esp), 0.97, fres) + sp, 0.0, 1.0);        // somero = transparente, hondo = opaco
        if (cara == 2) {   // espuma en la orilla (donde el agua es muy somera)
            float cerca = 1.0 - smoothstep(0.0, 1.5, dB);
            float rz2 = vnA(vAbs.xz * 3.6 + vec2(uTime * 0.30, -uTime * 0.22)) * 0.6 + mix(vnA(vAbs.xz * 8.0 - vec2(uTime * 0.4, uTime * 0.3)), 0.5, smoothstep(0.15, 0.45, foot * 8.0)) * 0.4;
            float onda = 0.5 + 0.5 * sin(uTime * 1.6 - dB * 2.6 + rz2 * 5.0);          // la espuma va y viene
            float fo = clamp(cerca * smoothstep(0.38, 0.62, rz2 + 0.35 * cerca + 0.22 * onda), 0.0, 1.0);
            col = mix(col, vec3(0.95, 0.98, 1.0) * max(lumi, 0.07) * (1.0 + 0.2 * uHDR), fo);
            alfa = max(alfa, fo);
        }
    }
    float f = clamp((vDist - uFogA) / (uFogB - uFogA), 0.0, 1.0);
    frag = vec4(mix(col, uSky, f), alfa);
})";

static const char* VLOD_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aCol;
uniform mat4 uVP;
uniform vec3 uRel;
uniform float uCurv;     // 0 = plano, 1 = curvado como la esfera de la Tierra (radio 6000)
out vec3 vCol;
out float vDist;
out float vH;
void main(){
    vec3 rel = uRel + aPos;
    vCol = aCol;
    vDist = length(rel);
    float h = length(rel.xz);
    vH = h;
    if (uCurv > 0.0) {
        const float R = 6000.0;
        float hh = min(h, R * 0.98);
        rel.y -= uCurv * (R - sqrt(R * R - hh * hh));
    }
    gl_Position = uVP * vec4(rel, 1.0);
})";
static const char* FLOD_SRC = R"(#version 330 core
in vec3 vCol;
in float vDist;
in float vH;
out vec4 frag;
uniform vec3 uSky;
uniform float uFogK;     // multiplicador de niebla (baja con la altura)
uniform float uEdge;     // radio redondo del mundo lejano (borde difuminado)
uniform float uFogA;
uniform float uFogB;
uniform float uSkyMul;
uniform float uAlpha;
void main(){
    float br = 0.045 + 0.955 * exp(-3.35 * (1.0 - uSkyMul));
    vec3 col = vCol * br;
    float f = clamp((vDist - uFogA) / (uFogB - uFogA), 0.0, 1.0) * uFogK;
    if (vH > uEdge * 0.86) {   // borde redondo y difuminado (sin cuadros)
        float fe = clamp((vH - uEdge * 0.86) / (uEdge * 0.14), 0.0, 1.0);
        float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
        if (n < fe) discard;
    }
    frag = vec4(mix(col, uSky, f), uAlpha);
})";

static const char* VL_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP;
void main(){ gl_Position = uVP * vec4(aPos, 1.0); })";
static const char* FL_SRC = R"(#version 330 core
uniform vec4 uColor;
out vec4 frag;
void main(){ frag = uColor; })";

// ---- clima: lluvia, nieve y rayos (cuadrilateros con alfa por vertice)
static const char* VW_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aA;
uniform mat4 uVP;
out float vA;
void main(){ vA = aA; gl_Position = uVP * vec4(aPos, 1.0); })";
static const char* FW_SRC = R"(#version 330 core
uniform vec4 uColor;
in float vA;
out vec4 frag;
void main(){ frag = vec4(uColor.rgb, uColor.a * vA); })";

// ---- cielo: degradado, estrellas, sol y luna (un triangulo a pantalla completa, se dibuja antes del mundo)
static const char* VC_SRC = R"(#version 330 core
void main(){
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
static const char* FC_SRC = R"(#version 330 core
out vec4 frag;
uniform vec2 uRes;
uniform vec3 uFwd, uRight, uUp;
uniform vec2 uTan;
uniform vec3 uHor, uZen, uSun;
uniform float uNoche;
uniform float uTime;
uniform float uMejor;
uniform float uClima;
uniform float uAlt;
uniform vec3 uHaze;
uniform vec2 uCamXZ;
uniform float uEsp;
uniform float uAtmoF;
uniform sampler2D uMapa;
uniform vec2 uMapaOff;
uniform float uMapaOk;
uniform vec3 uOc;
uniform vec3 uLunaC;
uniform float uLunaR;
uniform float uLunaOn;
float hash(vec3 p){ p = fract(p * 0.1031); p += dot(p, p.yzx + 33.33); return fract((p.x + p.y) * p.z); }
float vn3(vec3 p){
    vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(hash(i), hash(i + vec3(1,0,0)), f.x), mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y);
    float b = mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x), mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y);
    return mix(a, b, f.z);
}
float crater(vec3 n, float sc, float sd){      // crateres sobre la esfera de la Luna (celdas 3D)
    vec3 p = n * sc, i = floor(p);
    float v = 0.0;
    for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) {
        vec3 g = i + vec3(float(x), float(y), float(z));
        if (hash(g + sd) < 0.45) continue;
        vec3 c = g + vec3(hash(g + sd + 1.7), hash(g + sd + 3.1), hash(g + sd + 5.3));
        float r = 0.22 + 0.22 * hash(g + sd + 7.9);
        float dd = length(p - c) / r;
        if (dd < 1.15) v += -(1.0 - smoothstep(0.0, 0.85, dd)) * 0.9 + smoothstep(0.75, 0.95, dd) * (1.0 - smoothstep(0.95, 1.15, dd)) * 0.55;
    }
    return v;
}
void main(){
    float tTierra = 1e30;
    vec2 nd = gl_FragCoord.xy / uRes * 2.0 - 1.0;
    vec3 d = normalize(uFwd + nd.x * uTan.x * uRight + nd.y * uTan.y * uUp);
    vec3 col = d.y > 0.0 ? mix(uHor, uZen, pow(d.y, 0.55)) : uHor;
    float ds = dot(d, uSun);
    if (uMejor > 0.5) {   // cielo con atmosfera: neblina en el horizonte, dispersion alrededor del sol y colores de atardecer
        float elev = uSun.y;
        float luzSol = smoothstep(-0.14, 0.10, elev);
        float ocaso = exp(-pow(elev / 0.24, 2.0)) * uAtmoF;
        float yy = clamp(d.y, 0.0, 1.0);
        float hor = pow(1.0 - yy, 4.0) * uAtmoF;
        vec2 az = normalize(d.xz + vec2(1e-5)), azS = normalize(uSun.xz + vec2(1e-5));
        float haciaSol = 0.5 + 0.5 * dot(az, azS);
        col = mix(uHor, uZen, pow(yy, 0.42));
        col = mix(col, uHor * 1.08 + vec3(0.03, 0.04, 0.05) * luzSol, hor * 0.40 * luzSol);          // neblina clara
        vec3 mie = mix(vec3(1.0, 0.86, 0.62), vec3(1.0, 0.42, 0.14), ocaso);
        float cs = max(ds, 0.0);
        col += mie * (pow(cs, 5.0) * 0.16 + pow(cs, 36.0) * 0.30) * luzSol * uAtmoF;                            // resplandor del sol
        vec3 calido = mix(vec3(1.0, 0.48, 0.20), vec3(0.95, 0.32, 0.40), clamp(d.y * 2.5, 0.0, 1.0));
        col = mix(col, calido, ocaso * haciaSol * pow(1.0 - yy, 1.6) * 0.62);                            // banda naranja hacia el sol
        col = mix(col, vec3(0.34, 0.28, 0.55), ocaso * (1.0 - haciaSol) * pow(yy, 0.7) * 0.32);        // rosa y violeta al lado opuesto
    }
    if (uNoche > 0.01 && (d.y > 0.02 || uEsp > 0.02)) {      // estrellas (en el espacio tambien hacia abajo)
        if (uMejor > 0.5) {
            vec3 c = floor(d * 170.0);
            float s = hash(c);
            if (s > 0.9965) {
                float tw = 0.72 + 0.28 * sin(uTime * 2.5 + hash(c + 1.0) * 30.0);
                vec3 tinte = mix(vec3(0.80, 0.90, 1.0), vec3(1.0, 0.90, 0.75), hash(c + 7.0));
                col += tinte * uNoche * (0.30 + 0.70 * hash(c + 3.0)) * tw * max(smoothstep(0.02, 0.25, d.y), uEsp);
            }
            vec3 ax = normalize(vec3(0.35, 0.75, 0.55));
            float banda = exp(-pow(dot(d, ax) * 3.2, 2.0));
            float nz = hash(floor(d * 55.0)) * 0.5 + hash(floor(d * 120.0) + 4.0) * 0.5;
            col += vec3(0.10, 0.10, 0.19) * banda * nz * uNoche * max(smoothstep(0.0, 0.3, d.y), uEsp) * 0.7;    // via lactea tenue
        } else {
            vec3 c = floor(d * 150.0);
            float s = hash(c);
            if (s > 0.9965) col += vec3(1.0) * uNoche * (0.45 + 0.55 * hash(c + 3.0));
        }
    }
    if (ds > 0.0) {                                          // sol: cuadrado como en Minecraft + brillo
        vec3 u = normalize(cross(vec3(0.0, 0.0, 1.0), uSun));
        vec3 v = cross(uSun, u);
        vec2 ab = vec2(dot(d, u), dot(d, v)) / ds;
        if (abs(ab.x) < 0.12 && abs(ab.y) < 0.12) col = mix(col, vec3(1.0, 0.96, 0.72), 1.0 - uClima);
        col += vec3(1.0, 0.80, 0.45) * pow(ds, 90.0) * 0.55 * (1.0 - uClima);
    }
    float dm = dot(d, -uSun);                                // luna con craterls
    float lf = 1.0 - uLunaOn;                                 // en el espacio la Luna es una esfera de verdad
    if (dm > 0.0 && lf > 0.01) {
        vec3 u = normalize(cross(vec3(0.0, 0.0, 1.0), -uSun));
        vec3 v = cross(-uSun, u);
        vec2 ab = vec2(dot(d, u), dot(d, v)) / dm;
        if (abs(ab.x) < 0.09 && abs(ab.y) < 0.09) {
            vec2 cel = floor(ab / 0.03);
            float m = 0.80 + 0.14 * hash(vec3(cel, 5.0)) - (hash(vec3(cel, 9.0)) > 0.8 ? 0.16 : 0.0);
            col = mix(col, vec3(0.86, 0.90, 1.0) * m, (1.0 - uClima) * lf);
        }
        col += vec3(0.35, 0.42, 0.65) * pow(dm, 120.0) * 0.30 * (1.0 - uClima) * lf;
        if (uMejor > 0.5) col += vec3(0.30, 0.36, 0.60) * pow(max(dm, 0.0), 14.0) * 0.10 * uNoche * lf;   // halo de la luna
    }
    if (uAlt > 150.0) {   // desde muy alto: la Tierra como una esfera enorme debajo (continentes, nubes, polos) con su halo de atmosfera
        const float RP = 6000.0;
        float pv = smoothstep(300.0, 1400.0, uAlt);
        vec3 oc = uOc;
        float bq = dot(oc, d);
        float disc = bq * bq - (dot(oc, oc) - RP * RP);
        vec3 haze = max(uHaze, vec3(0.025, 0.05, 0.12));
        if (bq < 0.0) {
            if (disc > 0.0) {
                float t = -bq - sqrt(disc);
                tTierra = t;
                vec3 p = oc + d * t, n = normalize(p);
                vec3 q = (p + vec3(uCamXZ.x, 0.0, uCamXZ.y)) / RP * 5.0;
                float luz = clamp(dot(n, uSun) * 1.4 + 0.25, 0.0, 1.0);
                float land = vn3(q) * 0.65 + vn3(q * 2.3) * 0.35;
                vec3 surf = mix(vec3(0.03, 0.15, 0.42), vec3(0.17, 0.40, 0.15), smoothstep(0.50, 0.56, land));
                surf = mix(surf, vec3(0.52, 0.44, 0.28), smoothstep(0.62, 0.70, land) * 0.75);
                surf = mix(surf, vec3(0.92, 0.95, 1.0), smoothstep(0.80, 0.93, abs(n.x)));
                {   // el mapa real de ESTE mundo (oceanos, biomas, hielo) bajo el jugador
                    // v77: proyeccion equidistante (la misma con la que la nave entra al mundo plano): el mapa coincide con el terreno de verdad
                    float th = acos(clamp(n.y, -1.0, 1.0));
                    vec2 hzv = length(n.xz) > 1e-5 ? normalize(n.xz) : vec2(1.0, 0.0);
                    vec2 uv = (hzv * RP * th + uMapaOff) / 24000.0 + 0.5;
                    uv += vec2(vn3(q * 37.0) - 0.5, vn3(q * 37.0 + 11.0) - 0.5) * 0.0045;   // deforma un poco: rompe los cuadros del mapa
                    float mb = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
                    float usa = smoothstep(0.0, 0.05, mb) * uMapaOk;
                    vec3 mc = textureLod(uMapa, uv, 0.0).rgb * (0.88 + 0.24 * vn3(q * 9.0));
                    surf = mix(surf, mc, usa);
                }
                float cl = vn3(q * 1.8 + 3.0) * 0.6 + vn3(q * 4.4) * 0.4;
                surf = mix(surf, vec3(1.0), smoothstep(0.56, 0.74, cl) * 0.5);
                float rim = pow(1.0 - clamp(dot(n, -d), 0.0, 1.0), 3.0);
                vec3 pc = surf * luz + haze * rim * (0.35 + 0.65 * luz);
                col = mix(col, pc, pv);
            } else {
                float dm = length(oc - d * bq) - RP;
                vec3 pm = normalize(oc - d * bq);
                float luz = clamp(dot(pm, uSun) * 1.4 + 0.25, 0.0, 1.0);
                col += haze * exp(-max(dm, 0.0) / 120.0) * luz * 0.95 * pv;
            }
        }
    }
    if (uLunaR > 0.0 && uLunaOn > 0.01) {    // la Luna: esfera gris con crateres y mares oscuros
        vec3 oc2 = -uLunaC;
        float bq2 = dot(oc2, d);
        float disc2 = bq2 * bq2 - (dot(oc2, oc2) - uLunaR * uLunaR);
        if (bq2 < 0.0 && disc2 > 0.0) {
            float t2 = -bq2 - sqrt(disc2);
            if (t2 < tTierra) {
                vec3 n2 = normalize(oc2 + d * t2);
                float maria = vn3(n2 * 2.3 + 7.0) * 0.6 + vn3(n2 * 5.0) * 0.4;
                vec3 alb = mix(vec3(0.74, 0.73, 0.71), vec3(0.33, 0.33, 0.36), smoothstep(0.52, 0.62, maria));
                float cr = crater(n2, 12.0, 1.0) + 0.7 * crater(n2, 30.0, 11.0) + 0.5 * crater(n2, 80.0, 23.0);
                alb *= clamp(1.0 + 0.42 * cr, 0.25, 1.5);
                float lum = clamp(dot(n2, uSun) * 1.5 + 0.10, 0.0, 1.0);
                vec3 dE = normalize(-uOc - (uLunaC + n2 * uLunaR));                  // hacia la Tierra: su luz ilumina el lado oscuro
                float es = clamp(dot(n2, dE) * 0.8 + 0.2, 0.0, 1.0);
                vec3 mc = alb * (lum + 0.16 * es) + vec3(0.010, 0.014, 0.025);
                col = mix(col, mc, uLunaOn);
            }
        }
    }
    if (uMejor > 0.5) col += (hash(vec3(gl_FragCoord.xy, 1.0)) - 0.5) / 160.0;   // ruido fino contra las bandas de color
    frag = vec4(col, 1.0);
})";

// ---- nubes (cajas planas que se desplazan con el viento)
static const char* VN_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aSh;
uniform mat4 uVP;
uniform vec3 uOff;
out float vSh;
out float vDist;
void main(){
    vec3 p = aPos + uOff;
    vDist = length(p);
    vSh = aSh;
    gl_Position = uVP * vec4(p, 1.0);
})";
static const char* FN_SRC = R"(#version 330 core
in float vSh;
in float vDist;
out vec4 frag;
uniform vec3 uCol;
uniform vec3 uSkyF;
uniform vec2 uFog;
uniform float uA;
void main(){
    float f = clamp((vDist - uFog.x) / (uFog.y - uFog.x), 0.0, 1.0);
    frag = vec4(mix(uCol * vSh, uSkyF, f), uA * (1.0 - f));
})";

// ---- nubes volumetricas (estilo Photon): un plano enorme; cada pixel avanza un rayo por la capa de nubes
static const char* VN2_SRC = R"(#version 330 core
uniform mat4 uVP;
uniform float uR;
uniform float uYm;
out vec3 vRel;
void main(){
    vec2 c[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
    vec2 q = c[gl_VertexID];
    vRel = vec3(q.x * uR, uYm, q.y * uR);
    gl_Position = uVP * vec4(vRel, 1.0);
})";
static const char* FN2_SRC = R"(#version 330 core
in vec3 vRel;
out vec4 frag;
uniform vec3 uCamAbs;
uniform vec2 uWind;
uniform float uY0;
uniform float uTh;
uniform vec3 uSun;
uniform vec3 uCol;
uniform vec3 uSkyF;
uniform vec2 uFog;
uniform float uCover;
float h3(vec3 p){ p = fract(p * 0.1031); p += dot(p, p.yzx + 33.33); return fract((p.x + p.y) * p.z); }
float n3(vec3 x){
    vec3 i = floor(x), f = fract(x); f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(h3(i), h3(i + vec3(1,0,0)), f.x), mix(h3(i + vec3(0,1,0)), h3(i + vec3(1,1,0)), f.x), f.y);
    float b = mix(mix(h3(i + vec3(0,0,1)), h3(i + vec3(1,0,1)), f.x), mix(h3(i + vec3(0,1,1)), h3(i + vec3(1,1,1)), f.x), f.y);
    return mix(a, b, f.z);
}
float dens(vec3 w){   // w = (x, altura sobre la base de la capa, z), en bloques
    float hh = w.y / uTh;
    if (hh < -0.02 || hh > 1.4) return 0.0;
    vec3 q = vec3(w.x * 0.010, w.y * 0.028, w.z * 0.010);
    float n = 0.60 * n3(q) + 0.30 * n3(q * 2.3 + 7.1) + 0.10 * n3(q * 5.2 + 3.7);
    float lim = uCover + 0.30 * hh * hh;
    return smoothstep(lim, lim + 0.17, n) * smoothstep(0.0, 0.07, hh);
}
void main(){
    vec3 dir = normalize(vRel);
    if (abs(dir.y) < 0.003) discard;
    float t0 = uY0 / dir.y, t1 = (uY0 + uTh) / dir.y;
    float tn = max(min(t0, t1), 0.0), tf = max(t0, t1);
    if (tf <= tn) discard;
    tf = min(tf, tn + 700.0);
    const int N = 12;
    float dt = (tf - tn) / float(N);
    tn += dt * h3(vec3(gl_FragCoord.xy, 5.0));
    float T = 1.0; vec3 acc = vec3(0.0);
    float cosS = max(dot(dir, uSun), 0.0);
    for (int i = 0; i < N; i++) {
        float t = tn + dt * float(i);
        vec3 P = uCamAbs + dir * t;
        vec3 w = vec3(P.x - uWind.x, P.y - (uCamAbs.y + uY0), P.z - uWind.y);
        float d = dens(w);
        if (d < 0.004) continue;
        float ls = dens(w + uSun * 5.0) + dens(w + uSun * 12.0) * 0.8;
        float luz = exp(-ls * 1.7);
        float hh = clamp(w.y / uTh, 0.0, 1.0);
        vec3 sombra = uCol * vec3(0.64, 0.70, 0.86) * (0.84 + 0.28 * hh);
        vec3 sol = uCol * vec3(1.10, 1.06, 0.98) + uCol * pow(cosS, 10.0) * 0.45;
        vec3 c = mix(sombra, sol, luz);
        float a = 1.0 - exp(-d * dt * 0.085);
        acc += T * a * c;
        T *= 1.0 - a;
        if (T < 0.02) break;
    }
    float alfa = 1.0 - T;
    if (alfa < 0.003) discard;
    vec3 col = acc / alfa;
    float fg = clamp((tn - uFog.x) / (uFog.y - uFog.x), 0.0, 1.0);
    frag = vec4(mix(col, uSkyF, fg), alfa * (1.0 - fg));
})";

static GLuint compilar(GLenum tipo, const char* src) {
    GLuint s = glCreateShader(tipo);
#ifdef ANDROID_GAME      // OpenGL ES 3.0: misma sintaxis, otra primera linea y precision explicita
    static thread_local std::string es;
    es = src;
    const std::string v330 = "#version 330 core";
    if (es.compare(0, v330.size(), v330) == 0)
        es = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp sampler3D;" + es.substr(v330.size());
    src = es.c_str();
#endif
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(s, 2048, nullptr, log); std::fprintf(stderr, "Error de shader:\n%s\n", log); }
    return s;
}
static GLuint programa(const char* vs, const char* fs) {
    GLuint v = compilar(GL_VERTEX_SHADER, vs), f = compilar(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(p, 2048, nullptr, log); std::fprintf(stderr, "Error al enlazar:\n%s\n", log); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

#include "post.h"

// ---------------------------------------------------------------- entrada
static bool g_capturado = false, g_vsync = true;
static double g_mx = 0, g_my = 0;
static bool g_primerMouse = true;
static double g_ultimoEsc = -10;
static bool g_guardarYa = false;
#ifdef MOVIL
constexpr bool SOLO_VISIBLE = true;      // celular: tambien sin mundo lejano se esconden los chunks enterrados bajo la superficie
#else
constexpr bool SOLO_VISIBLE = false;
#endif
#include "vehiculo.h"
#include "cohete.h"
#include "jugadores.h"
#include "jefe.h"
#include "mobs.h"


// ---------------------------------------------------------------- mundos (menu que aparece al iniciar)
struct InfoMundo { std::string carpeta, nombre; uint32_t semilla = 0; int chunks = 0; long long t = 0; };
static const char* RAIZ_MUNDOS = SUB == 2 ? "mundos" : (SUB == 1 ? "mundos_sub1" : (SUB == 4 ? "mundos_sub4" : "mundos_sub8"));   // cada SUB guarda sus mundos aparte (no son compatibles)
static std::vector<InfoMundo> g_mundos;
static int g_pantalla = 1;                 // 0 = lista de mundos, 1 = juego, 2 = inicio, 3 = multijugador
static bool g_argHost = false;
static std::string g_argUnir, g_argNombre;
static int g_argPuerto = RED_PUERTO;
static std::string g_exe0;                 // ruta del ejecutable (para abrir la otra version SUB)
static bool g_cambiarSub = false;          // el jugador pidio cambiar el tipo de mundo (SUB 1 <-> SUB 2): se abre el otro ejecutable
static std::string otroExe() {             // ruta del ejecutable del otro tipo de mundo (SUB 2 <-> SUB 1)
    namespace fs = std::filesystem;
    fs::path d = fs::path(g_exe0).parent_path();
#ifdef _WIN32
    const char* ext = ".exe";
#else
    const char* ext = "";
#endif
    return (d / (std::string(SUB == 2 ? "juego_sub1" : "juego") + ext)).string();
}
static bool g_volverMenu = false;          // "guardar y salir": al cerrar la partida se reinicia el juego en el menu de inicio
static std::string g_unirElegido, g_ipTexto, g_errorInicio;
static bool g_conectando = false;
static int g_mundoElegido = -1;
static bool g_crearNuevo = false;
static int g_confBorrar = -1, g_scrollM = 0;

static void escribirInfoMundo(const std::string& dir, const std::string& nombre, uint32_t semilla) {
    FILE* f = std::fopen((dir + "/mundo.txt").c_str(), "w");
    if (!f) return;
    std::fprintf(f, "%u\n%s\n", semilla, nombre.c_str());
    std::fclose(f);
}
// el mundo de versiones anteriores (carpeta mundo_SUB2) pasa a ser "MUNDO 1" dentro de mundos/
static void migrarMundoViejo() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string viejo = "mundo_SUB" + std::to_string(SUB), nuevo = std::string(RAIZ_MUNDOS) + "/mundo_1";
    if (!fs::is_directory(viejo, ec) || fs::exists(nuevo, ec)) return;
    fs::create_directories(RAIZ_MUNDOS, ec);
    fs::rename(viejo, nuevo, ec);
    if (!ec) escribirInfoMundo(nuevo, "MUNDO 1", 0);
}
static std::vector<InfoMundo> listarMundos() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<InfoMundo> r;
    if (!fs::is_directory(RAIZ_MUNDOS, ec)) return r;
    for (auto& e : fs::directory_iterator(RAIZ_MUNDOS, ec)) {
        if (!e.is_directory(ec)) continue;
        std::string dir = e.path().generic_string();
        FILE* f = std::fopen((dir + "/mundo.txt").c_str(), "r");
        if (!f) continue;
        InfoMundo m;
        m.carpeta = dir;
        unsigned sem = 0;
        char nom[128] = "MUNDO";
        if (std::fscanf(f, "%u", &sem) == 1) { m.semilla = sem; std::fgetc(f); if (!std::fgets(nom, sizeof nom, f)) std::strcpy(nom, "MUNDO"); }
        std::fclose(f);
        m.nombre = nom;
        while (!m.nombre.empty() && (m.nombre.back() == '\n' || m.nombre.back() == '\r')) m.nombre.pop_back();
        if (m.nombre.empty()) m.nombre = "MUNDO";
        for (auto& c : fs::directory_iterator(dir, ec)) {
            std::string fn = c.path().filename().string();
            if (fn.size() > 2 && fn[0] == 'c' && fn[1] == '_') m.chunks++;
        }
        auto tj = fs::last_write_time(dir + "/jugador.txt", ec);
        if (ec) { ec.clear(); tj = fs::last_write_time(dir + "/mundo.txt", ec); }
        m.t = (long long)tj.time_since_epoch().count();
        r.push_back(m);
    }
    std::sort(r.begin(), r.end(), [](const InfoMundo& a, const InfoMundo& b) { return a.t > b.t; });   // el ultimo jugado primero
    return r;
}
static InfoMundo crearMundo() {
    namespace fs = std::filesystem;
    std::error_code ec;
    int n = 1;
    while (fs::exists(std::string(RAIZ_MUNDOS) + "/mundo_" + std::to_string(n), ec)) n++;
    InfoMundo m;
    m.carpeta = std::string(RAIZ_MUNDOS) + "/mundo_" + std::to_string(n);
    m.nombre = "MUNDO " + std::to_string(n);
    std::random_device rd;
    m.semilla = (rd() & 0xFFFFFF) | 1u;
    if (g_nuevoProf) m.semilla |= (1u << 24);          // mares profundos: el bit 24 de la semilla
    fs::create_directories(m.carpeta, ec);
    escribirInfoMundo(m.carpeta, m.nombre, m.semilla);
    return m;
}
static void borrarMundo(const InfoMundo& m) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (m.carpeta.rfind(std::string(RAIZ_MUNDOS) + "/mundo_", 0) != 0) return;     // seguridad: solo carpetas de mundos/
    fs::remove_all(m.carpeta, ec);
}

// geometria del menu de mundos (compartida por dibujo y clic)
struct MRect { float x, y, w, h; };
struct MBase { float k, x0, w, yNuevo, hFila, gap, yLista; int vis; };
static MBase mundosBase(int fbw, int fbh) {
    MBase b;
    b.k = std::max(0.8f, fbh / 720.0f);
    b.w = std::min(fbw * 0.88f, 860.0f * b.k);
    b.x0 = (fbw - b.w) / 2.0f;
    b.yNuevo = 150 * b.k; b.hFila = 66 * b.k; b.gap = 10 * b.k;
    b.yLista = b.yNuevo + b.hFila + 28 * b.k;
    b.vis = std::max(1, (int)((fbh - b.yLista - 44 * b.k) / (b.hFila + b.gap)));
    return b;
}
static MRect mRNuevo(const MBase& b) { return {b.x0, b.yNuevo, b.w * 0.60f, b.hFila}; }
static MRect mRProf(const MBase& b) { return {b.x0 + b.w * 0.62f, b.yNuevo, b.w * 0.38f, b.hFila}; }
static MRect mRFila(const MBase& b, int v) { return {b.x0, b.yLista + v * (b.hFila + b.gap), b.w, b.hFila}; }
static MRect mRBorrar(const MBase& b, int v) { MRect r = mRFila(b, v); float bw = 150 * b.k; return {r.x + r.w - bw - 10 * b.k, r.y + 10 * b.k, bw, r.h - 20 * b.k}; }
static MRect mRNo(const MBase& b, int v) { MRect r = mRFila(b, v); float bw = 110 * b.k; return {r.x + r.w - bw - 10 * b.k, r.y + 10 * b.k, bw, r.h - 20 * b.k}; }
static MRect mRSi(const MBase& b, int v) { MRect n = mRNo(b, v); return {n.x - n.w - 10 * b.k, n.y, n.w, n.h}; }
static bool mDentro(const MRect& r, double px, double py) { return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h; }
// tipo: 0 nuevo, 1 fila (jugar), 2 borrar, 3 confirmar si, 4 confirmar no
static void mundosHit(double mx, double my, int& tipo, int& idx) {
    double px = mx * g_fbW / std::max(1, g_winW), py = my * g_fbH / std::max(1, g_winH);
    MBase b = mundosBase(g_fbW, g_fbH);
    tipo = -1; idx = -1;
    if (mDentro(mRNuevo(b), px, py)) { tipo = 0; return; }
    if (mDentro(mRProf(b), px, py)) { tipo = 5; return; }
    for (int v = 0; v < b.vis; v++) {
        int gi = g_scrollM + v;
        if (gi >= (int)g_mundos.size()) break;
        if (gi == g_confBorrar) {
            if (mDentro(mRSi(b, v), px, py)) { tipo = 3; idx = gi; return; }
            if (mDentro(mRNo(b, v), px, py)) { tipo = 4; idx = gi; return; }
        } else {
            if (mDentro(mRBorrar(b, v), px, py)) { tipo = 2; idx = gi; return; }
            if (mDentro(mRFila(b, v), px, py)) { tipo = 1; idx = gi; return; }
        }
    }
}
static void mundosClamp() {
    MBase b = mundosBase(g_fbW, g_fbH);
    g_scrollM = std::max(0, std::min(g_scrollM, (int)g_mundos.size() - b.vis));
}


// ---- partidas en la red local (WiFi): el anfitrion anuncia por UDP y el menu las lista
struct LanSrv { std::string ip, nombre; int puerto = 0, jug = 0; double t = 0; };
static std::vector<LanSrv> g_lan;
static int g_lanSock = -1, g_scrollL = 0;
static const int LAN_PUERTO = 25577;
static double g_lanTx = 0;
static void lanAbrir() { if (g_lanSock < 0) g_lanSock = rs_udp_abrir(LAN_PUERTO); }
static void lanCerrar() { if (g_lanSock >= 0) { rs_close(g_lanSock); g_lanSock = -1; } g_lan.clear(); }
static void lanSondear(double ahora) {
    if (g_lanSock < 0) return;
    char b[256], ip[64];
    for (int i = 0; i < 40; i++) {
        int n = rs_udp_recibir(g_lanSock, b, 255, ip, sizeof ip);
        if (n <= 0) break;
        b[n] = 0; if (std::getenv("LAN_DEBUG")) std::fprintf(stderr, "LAN rx: %s de %s\n", b, ip);
        int puerto = 0, jug = 0; char nom[128] = "";
        if (std::sscanf(b, "JVOX1|%d|%d|%127[^\n]", &puerto, &jug, nom) < 2 || puerto <= 0) continue;
        bool hallado = false;
        for (auto& l : g_lan) if (l.ip == ip && l.puerto == puerto) { l.jug = jug; l.nombre = nom; l.t = ahora; hallado = true; }
        if (!hallado) { LanSrv l; l.ip = ip; l.puerto = puerto; l.jug = jug; l.nombre = nom; l.t = ahora; g_lan.push_back(l); }
    }
    for (size_t i = 0; i < g_lan.size();) { if (ahora - g_lan[i].t > (std::getenv("LAN_EXP") ? std::atof(std::getenv("LAN_EXP")) : 5.0)) g_lan.erase(g_lan.begin() + i); else i++; }
}
static void lanAnunciar(double ahora, const std::string& nombre, int puerto, int jug) {
    if (ahora - g_lanTx < 1.0) return;
    g_lanTx = ahora;
    char b[200];
    int n = std::snprintf(b, sizeof b, "JVOX1|%d|%d|%s", puerto, jug, nombre.c_str());
    if (n > 0) rs_udp_difundir(LAN_PUERTO, b, std::min(n, (int)sizeof b - 1));
}
// geometria del menu de inicio y del multijugador
static MRect tituloRect(int fbw, int fbh, int i) {
    float k = std::max(0.8f, fbh / 720.0f), w = 460 * k, h = 62 * k, gap = 16 * k;
    return {(fbw - w) / 2.0f, fbh * 0.34f + i * (h + gap), w, h};
}
struct MLBase { float k, x0, w, y0, hFila, gap; int vis; };
static MLBase lanBase(int fbw, int fbh) {
    MLBase b; b.k = std::max(0.8f, fbh / 720.0f);
    b.w = std::min(fbw * 0.88f, 760.0f * b.k); b.x0 = (fbw - b.w) / 2.0f;
    b.y0 = 150 * b.k; b.hFila = 58 * b.k; b.gap = 10 * b.k;
    b.vis = std::max(1, (int)((fbh - b.y0 - 230 * b.k) / (b.hFila + b.gap)));
    return b;
}
static MRect lanFila(const MLBase& b, int v) { return {b.x0, b.y0 + v * (b.hFila + b.gap), b.w, b.hFila}; }
static MRect lanCampo(const MLBase& b, int fbh) { return {b.x0, fbh - 168 * b.k, b.w * 0.62f, 46 * b.k}; }
static MRect lanUnir(const MLBase& b, int fbh) { return {b.x0 + b.w * 0.64f, fbh - 168 * b.k, b.w * 0.36f, 46 * b.k}; }
static MRect lanVolver(const MLBase& b, int fbh) { return {b.x0, fbh - 108 * b.k, b.w * 0.30f, 44 * b.k}; }
// pantalla 2: 0..3 botones.  pantalla 3: 10 volver, 11 unir por IP, 12+i servidor i
static int inicioHit(double mx, double my) {
    double px = mx * g_fbW / std::max(1, g_winW), py = my * g_fbH / std::max(1, g_winH);
    if (g_pantalla == 2) { 
        const int nb = 5;
        for (int i = 0; i < nb; i++) if (mDentro(tituloRect(g_fbW, g_fbH, i), px, py)) return i; return -1; }
    MLBase b = lanBase(g_fbW, g_fbH);
    if (mDentro(lanVolver(b, g_fbH), px, py)) return 10;
    if (mDentro(lanUnir(b, g_fbH), px, py)) return 11;
    for (int v = 0; v < b.vis; v++) { int gi = g_scrollL + v; if (gi >= (int)g_lan.size()) break; if (mDentro(lanFila(b, v), px, py)) return 12 + gi; }
    return -1;
}
static void inicioAccion(GLFWwindow* w, int h) {
    if (g_pantalla == 2) {
        if (h == 0) g_crearNuevo = true;
        else if (h == 1) { g_mundos = listarMundos(); g_scrollM = 0; g_confBorrar = -1; g_pantalla = 0; }
        else if (h == 2) { g_errorInicio.clear(); lanAbrir(); g_pantalla = 3; }
        else if (h == 3) glfwSetWindowShouldClose(w, 1);
#ifdef ANDROID_GAME
        else if (h == 4) {       // el APK trae las dos versiones: se guarda cual abrir y la app se relanza
            FILE* f = std::fopen("sub.txt", "w");
            if (f) { std::fprintf(f, "%d", SUB == 2 ? 1 : 2); std::fclose(f); g_cambiarSub = true; glfwSetWindowShouldClose(w, 1); }
        }
#else
        else if (h == 4) {
            std::error_code ec;
            if (!std::filesystem::exists(otroExe(), ec)) g_errorInicio = std::string("FALTA ") + std::filesystem::path(otroExe()).filename().string() + ": USA COMPILAR_Y_JUGAR.BAT";
            else { g_errorInicio.clear(); g_cambiarSub = true; glfwSetWindowShouldClose(w, 1); }
        }
#endif
    } else if (g_pantalla == 3) {
        if (h == 10) { lanCerrar(); g_pantalla = 2; }
        else if (h == 11 && !g_ipTexto.empty()) g_unirElegido = g_ipTexto;
        else if (h >= 12 && h - 12 < (int)g_lan.size()) g_unirElegido = g_lan[h - 12].ip + ":" + std::to_string(g_lan[h - 12].puerto);
    }
}

static void alMouse(GLFWwindow*, double x, double y) {
    if (!g_capturado) { g_mx = x; g_my = y; return; }
    if (g_primerMouse) { g_mx = x; g_my = y; g_primerMouse = false; }
    g_j.yaw += (x - g_mx) * 0.0025;
    g_j.pitch -= (y - g_my) * 0.0025;
    g_j.pitch = std::max(-1.55, std::min(1.55, g_j.pitch));
    g_mx = x; g_my = y;
}
static void menuReconstruir();
static void abrirMenu(GLFWwindow* w) {
    g_pagina = 0; menuReconstruir();
    g_menu = true; g_capturado = false;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}
static void cerrarMenu(GLFWwindow* w) {
    g_mano = -100; g_manoOrigen = -1; guardarBarra();
    g_menu = false; g_capturado = true; g_primerMouse = true;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}
static void abrirPausa(GLFWwindow* w) { g_pausa = true; g_capturado = false; glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
static void reanudar(GLFWwindow* w) { g_pausa = false; g_capturado = true; g_primerMouse = true; glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
// botones de la pausa: 0 continuar, 1 mundo lejano, 2 cambios lejanos, 3 graficos, 4 guardar y salir
static MRect pausaRect(int fbw, int fbh, int i) {
    float k = std::max(0.8f, fbh / 720.0f), w = 420 * k, h = 44 * k, gap = 8 * k;
    return {(fbw - w) / 2.0f, fbh * 0.28f + i * (h + gap), w, h};
}
static MRect graficosRect(int fbw, int fbh) { MRect r = pausaRect(fbw, fbh, 3); float k = std::max(0.8f, fbh / 720.0f); r.w -= 128 * k; return r; }
static MRect ajustesRect(int fbw, int fbh) { MRect r = pausaRect(fbw, fbh, 3); float k = std::max(0.8f, fbh / 720.0f); r.x += r.w - 120 * k; r.w = 120 * k; return r; }
static int pausaHit(double mx, double my) {
    double px = mx * g_fbW / std::max(1, g_winW), py = my * g_fbH / std::max(1, g_winH);
    if (g_pausaSub) { for (int i = 0; i < 8; i++) if (mDentro(pausaRect(g_fbW, g_fbH, i), px, py)) return i; return -1; }
    if (mDentro(ajustesRect(g_fbW, g_fbH), px, py)) return 10;
    if (mDentro(graficosRect(g_fbW, g_fbH), px, py)) return 3;
    for (int i = 0; i < 9; i++) if (mDentro(pausaRect(g_fbW, g_fbH, i), px, py)) return i;
    return -1;
}
static std::vector<int> g_menuItems;       // objetos que muestra el menu (creativo: todos; survival: los que tienes)
static int g_menuPag = 0;                  // pagina actual del menu de materiales (5 filas x 10 columnas = 50 por pagina)
static const int MENU_PAG = 50;
static int menuPaginas() { return std::max(1, ((int)g_menuItems.size() + MENU_PAG - 1) / MENU_PAG); }
static void alRueda(GLFWwindow*, double, double dy) {
    if (g_pantalla == 0) { g_scrollM += dy > 0 ? -1 : 1; mundosClamp(); return; }
    if (g_pantalla == 3) { MLBase b = lanBase(g_fbW, g_fbH); g_scrollL = std::max(0, std::min(g_scrollL + (dy > 0 ? -1 : 1), (int)g_lan.size() - b.vis)); return; }
    if (g_pantalla == 2) return;
    if (g_menu && !g_pausa) { if (!(g_survival && g_pagina == 1)) { g_menuPag = std::max(0, std::min(menuPaginas() - 1, g_menuPag + (dy > 0 ? -1 : 1))); } return; }
    if (g_menu || g_pausa) return;
    g_sel = (g_sel + (dy > 0 ? 1 : -1) + 9) % 9;
    aplicarSel();
}
// geometria del menu de materiales (compartida por el dibujo y el clic)
static float g_k = 1.0f;
static void menuGeom(int fbw, int fbh, int i, float& x, float& y, float& s) {
    float k = std::max(0.8f, fbh / 720.0f);
    s = 46.0f * k;
    float gap = 24.0f * k;
    float total = 10 * s + 9 * gap;
    float x0 = (fbw - total) / 2.0f, y0 = fbh * 0.17f;
    x = x0 + (i % 10) * (s + gap);
    y = y0 + (i / 10) * (s + gap + 18.0f * k);
}
static void menuReconstruir() {
    g_menuItems.clear();
    if (!g_survival) {
        for (int i = 0; i < NTIPOS_MENU; i++) if (menuTipo(i) != TNT_ENC) g_menuItems.push_back(menuTipo(i));
        for (int t = 0; t < HERR_N; t++) g_menuItems.push_back(HERR_BASE + t);
        g_menuItems.push_back(ID_CAMA);
    } else {
        for (int id = 1; id < 256; id++) if (g_inv[id] > 0 && !(id >= AGUA_F1 && id <= AGUA_F8) && id != AGUA) g_menuItems.push_back(id);
    }
    g_menuPag = std::max(0, std::min(g_menuPag, menuPaginas() - 1));
}
static MRect recRect(int fbw, int fbh, int i) {   // recetas en dos columnas
    float k = std::max(0.8f, fbh / 720.0f), w = 400 * k, gap = 20 * k, h = 34 * k;
    float x0 = (fbw - (2 * w + gap)) / 2.0f;
    return {x0 + (i / 13) * (w + gap), fbh * 0.17f + (i % 13) * 37 * k, w, h};
}
static MRect tabRect(int fbw, int fbh, int j) {
    float k = std::max(0.8f, fbh / 720.0f);
    return {fbw - (2 - j) * (138 * k) - 12 * k, 36 * k, 130 * k, 30 * k};
}
static MRect flechaRect(int fbw, int fbh, int dir) {   // dir 0 = izquierda, 1 = derecha (a los lados del titulo)
    float k = std::max(0.8f, fbh / 720.0f), s = 44 * k;
    return {dir == 0 ? fbw / 2.0f - 300 * k - s : fbw / 2.0f + 300 * k, fbh * 0.17f - 98 * k, s, s};
}
static int menuTileEn(double mx, double my) {
    double px = mx * g_fbW / std::max(1, g_winW), py = my * g_fbH / std::max(1, g_winH);
    for (int j = 0; j < MENU_PAG; j++) {
        int i = g_menuPag * MENU_PAG + j;
        if (i >= (int)g_menuItems.size()) break;
        float x, y, sz;
        menuGeom(g_fbW, g_fbH, j, x, y, sz);
        if (px >= x && px <= x + sz && py >= y && py <= y + sz) return g_menuItems[i];
    }
    return -1;
}
// geometria de la barra de acceso rapido (la usa el dibujo y el clic del menu)
static void barraGeom(int fbw, int fbh, int i, float& x, float& y, float& S) {
    float k = std::max(0.8f, fbh / 720.0f);
    S = std::floor(48.0f * k);
    float gap = std::floor(5.0f * k), total = 9 * S + 8 * gap, x0 = (fbw - total) / 2.0f;
    x = x0 + i * (S + gap); y = fbh - S - 16 * k;
}
static int barraEn(double mx, double my) {
    double px = mx * g_fbW / std::max(1, g_winW), py = my * g_fbH / std::max(1, g_winH);
    for (int i = 0; i < 9; i++) {
        float x, y, S;
        barraGeom(g_fbW, g_fbH, i, x, y, S);
        if (px >= x && px <= x + S && py >= y && py <= y + S) return i;
    }
    return -1;
}
// pone el objeto agarrado en la casilla j (si venia de otra casilla, intercambian)
static void manoSoltarEn(int j) {
    if (g_mano == -100 || j < 0) return;
    if (g_manoOrigen >= 0 && g_manoOrigen != j) g_barra[g_manoOrigen] = g_barra[j];
    g_barra[j] = g_mano;
    g_mano = -100; g_manoOrigen = -1;
    g_sel = j; aplicarSel(); guardarBarra();
}
static void survCraftear(int idx) {
    const auto& R = recetas();
    if (idx < 0 || idx >= (int)R.size()) return;
    const Receta& r = R[idx];
    if (recBloq(r)) { g_aviso = "BLOQUEADO: DERROTA AL MECHA DE LA LUNA"; g_avisoT = glfwGetTime(); return; }
    if (!puedeCraftear(r)) { g_aviso = "TE FALTAN MATERIALES"; g_avisoT = glfwGetTime(); return; }
    g_inv[r.a] -= r.na; if (r.nb) g_inv[r.b] -= r.nb; if (r.nc) g_inv[r.c] -= r.nc;
    g_inv[r.out] = std::min(9999, g_inv[r.out] + r.n);
    g_aviso = std::string("FABRICADO: ") + nombreSlot(r.out); g_avisoT = glfwGetTime();
    guardarSurv();
}
static void survCambiarModo() {
    g_survival = !g_survival;
    if (g_survival) {
        if (!g_spawnOk) { g_spawnS[0] = g_j.p.x; g_spawnS[1] = g_j.p.y; g_spawnS[2] = g_j.p.z; g_spawnOk = true; }
        g_vida = 20.0f; g_j.volar = false; g_caidaMax = g_j.p.y;
        for (int i = 0; i < 9; i++) if (g_barra[i] == 0) g_barra[i] = VACIO;
        g_aviso = "SURVIVAL: PICA PARA CONSEGUIR MATERIALES (E: INVENTARIO Y RECETAS)";
    } else g_aviso = "MODO CREATIVO";
    g_avisoT = glfwGetTime();
    aplicarSel(); guardarSurv(); guardarBarra();
}
static void tecladoDigito(int d) {
    if (g_puertaAbierta || g_baseDig >= 4) return;
    if (d == baseDigitoEsp(g_baseDig)) {
        g_baseDig++;
        if (g_baseDig >= 4) { g_tecladoOn = false; g_puertaT0 = glfwGetTime(); g_aviso = "CODIGO CORRECTO: LA PUERTA SE ABRE"; g_avisoT = glfwGetTime(); }
    } else { g_tecladoErr = glfwGetTime(); }
}
static bool* g_bloqIzqP = nullptr;
static void qBotar() {      // v86: tecla Q = botar 1 objeto de la casilla elegida (creativo: vaciar la casilla)
    if (g_menu || g_pausa || g_chat || g_pantalla != 1 || g_ck.activo) return;
    const int id = g_material;
    if (id < 0) { aviso("CASILLA VACIA"); return; }
    if (!g_survival || g_inv[id] <= 0) { g_barra[g_sel] = VACIO; aplicarSel(); guardarBarra(); aviso("CASILLA VACIADA"); return; }
    g_inv[id]--;
    const double fx = std::sin(g_j.yaw), fz = -std::cos(g_j.yaw);
    sueltoBotar(id, g_j.p.x + fx * 0.5, g_j.p.y + g_j.alto * 0.8, g_j.p.z + fz * 0.5, fx, fz, glfwGetTime());
}
static void alChar(GLFWwindow*, unsigned int cp) {
    if (g_pantalla == 3) { if (((cp >= '0' && cp <= '9') || cp == '.' || cp == ':') && g_ipTexto.size() < 24) g_ipTexto += (char)cp; return; }
    if (g_chat && cp >= 32 && cp < 127 && g_chatBuf.size() < 100) g_chatBuf += (char)cp;
}
static void alTecla(GLFWwindow* w, int tecla, int, int accion, int) {
    if (g_pantalla == 3 && tecla == GLFW_KEY_BACKSPACE && (accion == GLFW_PRESS || accion == GLFW_REPEAT) && !g_ipTexto.empty()) { g_ipTexto.pop_back(); return; }
    if (g_chat && (accion == GLFW_PRESS || accion == GLFW_REPEAT)) {      // escribiendo un mensaje
        if (tecla == GLFW_KEY_ESCAPE) g_chat = false;
        else if (tecla == GLFW_KEY_ENTER || tecla == GLFW_KEY_KP_ENTER) {
            if (!g_chatBuf.empty()) {
                std::string cm = g_chatBuf; for (auto& ch : cm) ch = (char)std::tolower((unsigned char)ch);
                bool cmdNuevo = true;
                if (cm == "/dia" || cm == "/noche" || cm == "/amanecer") {
                    if (g_red.cli) aviso("EN PARTIDA AJENA SOLO EL ANFITRION CAMBIA LA HORA");
                    else { g_hora = cm == "/dia" ? 0.25 : cm == "/noche" ? 0.62 : 0.0; aviso(cm == "/dia" ? "ES DE DIA" : cm == "/noche" ? "ES DE NOCHE" : "AMANECE"); }
                }
                else if (cm == "/creativo") { if (g_survival) survCambiarModo(); else aviso("YA ESTAS EN CREATIVO"); }
                else if (cm == "/survival") { if (!g_survival) survCambiarModo(); else aviso("YA ESTAS EN SURVIVAL"); }
                else if (cm == "/vaciar") { for (int i = 0; i < 9; i++) g_barra[i] = VACIO; aplicarSel(); guardarBarra(); aviso("BARRA VACIADA"); }
                else if (cm == "/ayuda") aviso("/DIA /NOCHE /AMANECER /CREATIVO /SURVIVAL /VACIAR /COHETE /LUNA /BASE");
                else cmdNuevo = false;
                if (cm == "/cohete") { if (g_survival && g_inv[ID_KITCOH] <= 0) aviso("NECESITAS UN KIT COHETE (RECETAS)"); else { if (g_survival) g_inv[ID_KITCOH]--; g_pedirCohete = true; } }
                if (cm == "/cohetemoderno" || cm == "/cohetebase") { if (g_ck.activo && !g_modoLuna) aviso("BAJA PRIMERO DE LA NAVE"); else if (g_survival && g_inv[ID_KITCOH] <= 0) aviso("NECESITAS UN KIT COHETE (RECETAS)"); else { if (g_survival) g_inv[ID_KITCOH]--; g_cohRepon = true; g_cohRepTp = false; if (!g_modoLuna) g_lunaPend = true; } }       // v71c: repone el cohete moderno de la base lunar
                if (cm == "/luna") g_lunaPend = true;       // (prueba) ir directo al mundo de la Luna
                if (cm == "/base") { if (g_modoLuna) irALaBase(0, 9, 1, 0, 0); else { g_irBase = true; g_lunaPend = true; } }   // (prueba) ir a la puerta de la base lunar
                else if (cm[0] == '/' && !cmdNuevo && cm != "/cohete" && cm != "/cohetemoderno" && cm != "/cohetebase" && cm != "/luna") { g_aviso = "COMANDO DESCONOCIDO. ESCRIBE /AYUDA"; g_avisoT = glfwGetTime(); }
                else if (g_red.activo()) g_red.chatEnviar(g_chatBuf);
            }
            g_chatBuf.clear(); g_chat = false;
        }
        else if (tecla == GLFW_KEY_BACKSPACE && !g_chatBuf.empty()) g_chatBuf.pop_back();
        return;
    }
    if (g_tecladoOn && (accion == GLFW_PRESS || accion == GLFW_REPEAT) && !g_chat) {       // teclado de la puerta de la base
        if (tecla == GLFW_KEY_ESCAPE || tecla == GLFW_KEY_BACKSPACE) { g_tecladoOn = false; return; }
        int d = (tecla >= 48 && tecla <= 57) ? tecla - 48 : (tecla >= 320 && tecla <= 329) ? tecla - 320 : -1;
        if (d >= 0) { if (accion == GLFW_PRESS) tecladoDigito(d); return; }
    }
    if (accion != GLFW_PRESS) return;
    if (g_pantalla == 2) { if (tecla == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, 1); return; }
    if (g_pantalla == 3) {
        if (tecla == GLFW_KEY_ESCAPE) inicioAccion(w, 10);
        else if (tecla == GLFW_KEY_ENTER || tecla == GLFW_KEY_KP_ENTER) inicioAccion(w, 11);
        return;
    }
    if (g_pantalla == 0) {
        if (tecla == GLFW_KEY_ESCAPE) { if (g_confBorrar >= 0) g_confBorrar = -1; else g_pantalla = 2; }
        else if (tecla == GLFW_KEY_N) g_crearNuevo = true;
        else if (tecla == GLFW_KEY_P) { g_nuevoProf = !g_nuevoProf; guardarOpciones(); }
        else if ((tecla == GLFW_KEY_ENTER || tecla == GLFW_KEY_KP_ENTER) && !g_mundos.empty() && g_confBorrar < 0) g_mundoElegido = 0;
        return;
    }
    if (tecla == GLFW_KEY_ESCAPE) {
        if (g_menu) cerrarMenu(w);
        else if (g_pausa) { if (g_pausaSub) g_pausaSub = 0; else reanudar(w); }
        else abrirPausa(w);
        return;
    }
    if (g_pausa) return;
    if (tecla == GLFW_KEY_ENTER && !g_menu) { g_chat = true; g_chatBuf.clear(); return; }     // abrir el chat
    if (g_ck.activo) {      // volando en el cohete
        if (tecla == GLFW_KEY_G) ckSalir();
        else if (tecla == GLFW_KEY_TAB) {
            g_ck.cab = !g_ck.cab;
            V3 f_, r_, u_; vehBase(g_ck.e2, f_, r_, u_);
            g_j.yaw = g_ck.cab ? 0.0 : std::atan2(f_.x, -f_.z); g_j.pitch = g_ck.cab ? 0.0 : 0.12;
        }
        else if (tecla == GLFW_KEY_F5) g_guardarYa = true;
        else if (tecla == GLFW_KEY_T) g_hora = std::fmod(std::floor(g_hora * 4.0 + 1e-6) * 0.25 + 0.25, 1.0);
        else if (tecla == GLFW_KEY_V) { g_vsync = !g_vsync; glfwSwapInterval(g_vsync ? 1 : 0); }
        return;
    }
    if (g_veh.activo && g_veh.pilotando) {      // mientras vuelas solo valen estas teclas
        if (tecla == GLFW_KEY_G) g_pedirG = true;
        else if (tecla == GLFW_KEY_TAB) g_veh.cabina = !g_veh.cabina;
        else if (tecla == GLFW_KEY_F5) g_guardarYa = true;
        else if (tecla == GLFW_KEY_T) g_hora = std::fmod(std::floor(g_hora * 4.0 + 1e-6) * 0.25 + 0.25, 1.0);
        else if (tecla == GLFW_KEY_V) { g_vsync = !g_vsync; glfwSwapInterval(g_vsync ? 1 : 0); }
        return;
    }
    if (tecla == GLFW_KEY_E) { if (g_menu) cerrarMenu(w); else abrirMenu(w); }
    if (tecla == GLFW_KEY_TAB && g_menu && g_survival) g_pagina ^= 1;
    if (g_menu) return;
    if (tecla == GLFW_KEY_G) g_pedirG = true;
    if (tecla == GLFW_KEY_H) g_pedirH = true;
    if (tecla == GLFW_KEY_L) g_pedirLanzar = true;
    if (tecla == GLFW_KEY_Z || tecla == GLFW_KEY_C) {      // color del spray / tamano de la rueda
        int d = tecla == GLFW_KEY_C ? 1 : -1;
        if (g_material == ID_SPRAY) g_sprayColor = (g_sprayColor + d + 12) % 12;
        else if (g_material == RUEDA) g_ruedaD = std::max(2, std::min(24, g_ruedaD + d));
    }
    if (tecla == GLFW_KEY_F && !g_survival) { g_j.volar = !g_j.volar; g_j.v = {0, 0, 0}; }
    if (tecla == GLFW_KEY_SPACE) {
        double t = glfwGetTime();
        if (t - g_ultEspacio < 0.30) { if (!g_survival) { g_j.volar = !g_j.volar; g_j.v = {0, 0, 0}; } g_ultEspacio = -10; }
        else g_ultEspacio = t;
    }
    if (tecla == GLFW_KEY_T) g_hora = std::fmod(std::floor(g_hora * 4.0 + 1e-6) * 0.25 + 0.25, 1.0);   // saltar a la siguiente fase del dia
    if (tecla == GLFW_KEY_J && !g_survival) { g_j.p.y += 500.0; g_j.v = {0, 0, 0}; g_j.volar = true; }   // creativo: subir 500 bloques de golpe (para ver la Tierra desde el espacio)
    if (tecla == GLFW_KEY_K) { g_climaE = (g_climaE + 1) % 5; g_climaT = 150.0; g_climaFijo = false; }   // cambiar el clima a mano
    if (tecla == GLFW_KEY_F5) g_guardarYa = true;
    if (tecla == GLFW_KEY_F9) g_pedirCiudad = true;       // prueba de rendimiento: ciudad de luces
    if (tecla == GLFW_KEY_B || tecla == GLFW_KEY_LEFT_CONTROL) g_pincelBloque = !g_pincelBloque;
    if (tecla == GLFW_KEY_V) { g_vsync = !g_vsync; glfwSwapInterval(g_vsync ? 1 : 0); }
    if (tecla == GLFW_KEY_X || tecla == GLFW_KEY_0) { g_barra[g_sel] = VACIO; aplicarSel(); guardarBarra(); }
    if (tecla == GLFW_KEY_Q) qBotar();
    if (tecla >= GLFW_KEY_1 && tecla <= GLFW_KEY_9) { g_sel = tecla - GLFW_KEY_1; aplicarSel(); }
}
static void alClic(GLFWwindow* w, int boton, int accion, int) {
    if (accion == GLFW_RELEASE && g_menu && g_pantalla == 1 && !g_pausa && boton == GLFW_MOUSE_BUTTON_LEFT && g_mano != -100) {
        double dx = g_mx - g_manoX, dy = g_my - g_manoY;
        if (dx * dx + dy * dy > 36.0) {                       // fue un arrastre: soltar donde esta el cursor (si no hay casilla, se cancela)
            int j = barraEn(g_mx, g_my);
            if (j >= 0) manoSoltarEn(j); else { g_mano = -100; g_manoOrigen = -1; }
        }                                                      // si fue un simple clic, el objeto queda agarrado hasta elegir casilla
        return;
    }
    if (accion != GLFW_PRESS) return;
    if (g_pantalla == 2 || g_pantalla == 3) {
        if (boton == GLFW_MOUSE_BUTTON_LEFT) { int h = inicioHit(g_mx, g_my); if (h >= 0) inicioAccion(w, h); }
        return;
    }
    if (g_pantalla == 0) {
        if (boton != GLFW_MOUSE_BUTTON_LEFT) return;
        int tipo, idx;
        mundosHit(g_mx, g_my, tipo, idx);
        if (tipo == 0) g_crearNuevo = true;
        else if (tipo == 5) { g_nuevoProf = !g_nuevoProf; guardarOpciones(); }
        else if (tipo == 1 && g_confBorrar < 0) g_mundoElegido = idx;
        else if (tipo == 2) g_confBorrar = idx;
        else if (tipo == 3) { borrarMundo(g_mundos[idx]); g_mundos = listarMundos(); g_confBorrar = -1; mundosClamp(); }
        else g_confBorrar = -1;
        return;
    }
    if (g_pausa) {
        if (boton != GLFW_MOUSE_BUTTON_LEFT) return;
        int b = pausaHit(g_mx, g_my);
        if (g_pausaSub) {   // ajustes de graficos
            if (b == 0) { g_nubes = (g_nubes + 2) % 3; guardarOpciones(); }        // volumetricas -> simples -> apagadas
            else if (b == 1) { g_agua = (g_agua + 2) % 3; guardarOpciones(); }     // con olas -> mejorada -> simple
            else if (b == 2) { g_cielo = !g_cielo; guardarOpciones(); }
            else if (b == 3) { g_viento = !g_viento; guardarOpciones(); }
            else if (b == 4) { g_graf = !g_graf; guardarOpciones(); }
            else if (b == 5) { g_espAtard = !g_espAtard; guardarOpciones(); }
            else if (b == 6) { g_horReal = !g_horReal; if (g_horReal && !g_lodOn) g_lodOn = true; guardarOpciones(); lodLiberarTodo(); }
            else if (b == 7) g_pausaSub = 0;
            return;
        }
        if (b == 10) { g_pausaSub = 1; return; }
        if (b == 0) reanudar(w);
        else if (b == 1) { g_lodOn = !g_lodOn; guardarOpciones(); if (!g_lodOn) lodLiberarTodo(); }
        else if (b == 2) { g_anclaOn = !g_anclaOn; guardarOpciones(); }
        else if (b == 3) { g_graf = !g_graf; guardarOpciones(); }
        else if (b == 4) survCambiarModo();
        else if (b == 5) { g_perf = (g_perf + 1) % 3; aplicarPerf(); guardarOpciones(); }
        else if (b == 6) { g_conservar = !g_conservar; guardarOpciones(); }
        else if (b == 7) {      // abrir la partida a la red local (WiFi)
            if (!g_red.activo()) {
                if (g_red.hospedar(g_argPuerto)) { g_argHost = true; aviso(("RED ABIERTA: " + std::string(rs_ip_local()) + ":" + std::to_string(g_argPuerto)).c_str()); }
                else aviso(g_red.error.c_str());
            }
        }
        else if (b == 8) { g_volverMenu = true; glfwSetWindowShouldClose(w, 1); }
        return;
    }
    if (g_menu) {
        int j = barraEn(g_mx, g_my);
        if (g_survival && boton == GLFW_MOUSE_BUTTON_LEFT) {   // pestanas y recetas
            double qx = g_mx * g_fbW / std::max(1, g_winW), qy = g_my * g_fbH / std::max(1, g_winH);
            for (int tj = 0; tj < 2; tj++) if (mDentro(tabRect(g_fbW, g_fbH, tj), qx, qy)) { g_pagina = tj; return; }
            if (g_pagina == 1) {
                const int NP = ((int)recetas().size() + 25) / 26;
                if (g_recPag < NP - 1 && mDentro(flechaRect(g_fbW, g_fbH, 1), qx, qy)) { g_recPag++; return; }
                if (g_recPag > 0 && mDentro(flechaRect(g_fbW, g_fbH, 0), qx, qy)) { g_recPag--; return; }
                for (int li = 0; li < 26 && g_recPag * 26 + li < (int)recetas().size(); li++) if (mDentro(recRect(g_fbW, g_fbH, li), qx, qy)) { survCraftear(g_recPag * 26 + li); menuReconstruir(); return; }
            }
        }
        if (boton == GLFW_MOUSE_BUTTON_LEFT && !(g_survival && g_pagina == 1)) {   // flechas de pagina
            double qx = g_mx * g_fbW / std::max(1, g_winW), qy = g_my * g_fbH / std::max(1, g_winH);
            if (g_menuPag < menuPaginas() - 1 && mDentro(flechaRect(g_fbW, g_fbH, 1), qx, qy)) { g_menuPag++; return; }
            if (g_menuPag > 0 && mDentro(flechaRect(g_fbW, g_fbH, 0), qx, qy)) { g_menuPag--; return; }
        }
        if (boton == GLFW_MOUSE_BUTTON_RIGHT) {                // clic derecho: soltar lo agarrado o vaciar la casilla
            if (g_mano != -100) { g_mano = -100; g_manoOrigen = -1; }
            else if (j >= 0) { g_barra[j] = VACIO; if (j == g_sel) aplicarSel(); guardarBarra(); }
            return;
        }
        if (boton != GLFW_MOUSE_BUTTON_LEFT) return;
        g_manoX = g_mx; g_manoY = g_my;
        if (j >= 0) {
            if (g_mano != -100) manoSoltarEn(j);
            else {
                g_sel = j; aplicarSel();
                if (g_barra[j] != VACIO) { g_mano = g_barra[j]; g_manoOrigen = j; }
            }
        } else {
            int t = (g_survival && g_pagina == 1) ? -1 : menuTileEn(g_mx, g_my);
            if (t >= 0) { g_mano = t; g_manoOrigen = -1; } else { g_mano = -100; g_manoOrigen = -1; }
        }
        return;
    }
    if (!g_capturado) {
        g_capturado = true; g_primerMouse = true;
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

// ---------------------------------------------------------------- HUD 2D (rectangulos + letras 5x7)
static const char* VH_SRC = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
uniform vec2 uRes;
out vec4 vCol;
void main(){
    vCol = aCol;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
})";
static const char* FH_SRC = R"(#version 330 core
in vec4 vCol;
out vec4 frag;
void main(){ frag = vCol; })";

static std::vector<float> g_hud;
static void hRect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
    float q[6][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y}, {x + w, y + h}, {x, y + h}};
    for (auto& v : q) { g_hud.push_back(v[0]); g_hud.push_back(v[1]); g_hud.push_back(r); g_hud.push_back(g); g_hud.push_back(b); g_hud.push_back(a); }
}
static void hMarco(float x, float y, float w, float h, float e, float r, float g, float b, float a = 1.0f) {
    hRect(x, y, w, e, r, g, b, a); hRect(x, y + h - e, w, e, r, g, b, a);
    hRect(x, y + e, e, h - 2 * e, r, g, b, a); hRect(x + w - e, y + e, e, h - 2 * e, r, g, b, a);
}
static const char FUENTE_CH[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-:.|+/()";
static const char* FUENTE_DAT[] = {
    ".###.#...##...#######...##...##...#",
    "####.#...##...#####.#...##...#####.",
    ".#####....#....#....#....#.....####",
    "####.#...##...##...##...##...#####.",
    "######....#....####.#....#....#####",
    "######....#....####.#....#....#....",
    ".#####....#....#.####...##...#.###.",
    "#...##...##...#######...##...##...#",
    "#####..#....#....#....#....#..#####",
    "..###...#....#....#....#.#..#..##..",
    "#...##..#.#.#..##...#.#..#..#.#...#",
    "#....#....#....#....#....#....#####",
    "#...###.###.#.##.#.##...##...##...#",
    "#...###..##.#.##..###...##...##...#",
    ".###.#...##...##...##...##...#.###.",
    "####.#...##...#####.#....#....#....",
    ".###.#...##...##...##.#.##..#..##.#",
    "####.#...##...#####.#.#..#..#.#...#",
    ".#####....#.....###.....#....#####.",
    "#####..#....#....#....#....#....#..",
    "#...##...##...##...##...##...#.###.",
    "#...##...##...##...##...#.#.#...#..",
    "#...##...##...##.#.##.#.###.###...#",
    "#...##...#.#.#...#...#.#.#...##...#",
    "#...##...#.#.#...#....#....#....#..",
    "#####....#...#...#...#...#....#####",
    ".###.#...##..###.#.###..##...#.###.",
    "..#...##....#....#....#....#...###.",
    ".###.#...#....#...#...#...#...#####",
    "####.....#....#.###.....#....#####.",
    "...#...##..#.#.#..#.#####...#....#.",
    "######....####.....#....##...#.###.",
    ".###.#....#....####.#...##...#.###.",
    "#####....#...#...#...#....#....#...",
    ".###.#...##...#.###.#...##...#.###.",
    ".###.#...##...#.####....#....#.###.",
    "...............#####...............",
    ".......#....#.........#....#.......",
    "................................#..",
    "..#....#....#....#....#....#....#..",
    ".......#....#..#####..#....#.......",
    "....#....#...#...#...#...#....#....",
    "...#...#....#....#....#....#.....#.",
    ".#.....#....#....#....#....#...#..."
};
static float hAncho(const char* t, float px) { return (float)std::strlen(t) * 6.0f * px; }
static void hTextoSolo(const char* t, float x, float y, float px, float r, float g, float b) {
    for (; *t; t++, x += 6.0f * px) {
        char c = *t;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        const char* pos = c ? std::strchr(FUENTE_CH, c) : nullptr;
        if (!pos) continue;
        const char* gl = FUENTE_DAT[pos - FUENTE_CH];
        for (int j = 0; j < 7; j++) for (int i = 0; i < 5; i++)
            if (gl[j * 5 + i] == '#') hRect(x + i * px, y + j * px, px, px, r, g, b);
    }
}
static void hTexto(const char* t, float x, float y, float px, float r, float g, float b) {
    hTextoSolo(t, x + px, y + px, px, 0.0f, 0.0f, 0.0f);   // sombra
    hTextoSolo(t, x, y, px, r, g, b);
}
static float hh(int a, int b, int c) {
    unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u + (unsigned)c * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h & 0xffff) / 65535.0f;
}
static const float ICON_C[NTIPOS][3] = {{0.45f, 0.10f, 0.10f}, {0.34f, 0.64f, 0.21f}, {0.45f, 0.32f, 0.20f}, {0.50f, 0.50f, 0.52f},
                                       {0.86f, 0.80f, 0.55f}, {0.18f, 0.38f, 0.80f}, {0.40f, 0.28f, 0.15f}, {0.20f, 0.52f, 0.16f},
                                       {0.07f, 0.07f, 0.08f}, {0.80f, 0.60f, 0.47f}, {0.82f, 0.47f, 0.22f}, {0.96f, 0.80f, 0.18f},
                                       {0.30f, 0.92f, 0.95f}, {0.95f, 0.94f, 0.92f}, {0.62f, 0.64f, 0.72f},
                                       {0.66f, 0.62f, 0.55f}, {0.70f, 0.33f, 0.26f}, {0.72f, 0.78f, 0.88f}, {0.30f, 0.38f, 0.55f},
                                       {0.78f, 0.62f, 0.92f}, {0.45f, 0.95f, 0.18f}, {1.0f, 0.72f, 0.2f},
                                       {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}, {0.90f, 0.92f, 0.28f}, {0.93f, 0.82f, 0.80f},
                                       {1.0f, 0.10f, 0.10f}, {1.0f, 0.48f, 0.08f}, {1.0f, 0.90f, 0.15f}, {0.60f, 1.0f, 0.12f}, {0.10f, 1.0f, 0.20f}, {0.08f, 1.0f, 0.80f},
                                       {0.15f, 0.70f, 1.0f}, {0.15f, 0.20f, 1.0f}, {0.55f, 0.18f, 1.0f}, {1.0f, 0.15f, 0.80f}, {1.0f, 0.50f, 0.72f}, {1.0f, 1.0f, 1.0f}};
static const float ICON_NEW[NTIPOS - NIEVE][3] = {{0.93f, 0.95f, 0.99f}, {0.60f, 0.80f, 0.96f}, {0.25f, 0.55f, 0.25f}, {0.78f, 0.13f, 0.10f}, {0.93f, 0.47f, 0.08f}, {0.94f, 0.80f, 0.13f}, {0.98f, 0.64f, 0.78f}, {0.09f, 0.33f, 0.19f}, {0.66f, 0.60f, 0.30f},
                                              {0.36f, 0.22f, 0.14f}, {0.07f, 0.40f, 0.12f}, {0.14f, 0.45f, 0.13f}, {0.56f, 0.74f, 0.26f}, {0.92f, 0.95f, 0.97f}, {0.16f, 0.42f, 0.14f},
                                              {0.90f, 0.12f, 0.12f}, {0.98f, 0.85f, 0.12f}, {0.22f, 0.35f, 0.95f}, {0.98f, 0.45f, 0.75f}, {0.40f, 0.62f, 0.93f}, {0.98f, 0.45f, 0.08f},
                                              {0.92f, 0.20f, 0.22f}, {0.98f, 0.46f, 0.70f}, {0.98f, 0.52f, 0.14f}, {0.62f, 0.28f, 0.88f}, {0.98f, 0.84f, 0.18f}, {0.20f, 0.50f, 0.95f}, {0.14f, 0.50f, 0.20f}, {0.50f, 0.48f, 0.45f}, {0.80f, 0.12f, 0.10f}, {0.98f, 0.95f, 0.92f},
                                              {0.60f, 0.60f, 0.58f}, {0.27f, 0.30f, 0.35f}, {0.45f, 0.47f, 0.52f}, {0.50f, 0.52f, 0.56f}, {0.43f, 0.44f, 0.46f}, {0.30f, 0.45f, 0.40f}, {0.10f, 0.45f, 0.60f}, {0.92f, 0.76f, 0.08f}, {0.36f, 0.38f, 0.42f}, {0.70f, 0.72f, 0.76f},
                                              {0.80f, 0.92f, 1.0f}, {0.35f, 1.0f, 0.50f}, {0.20f, 0.85f, 1.0f}, {1.0f, 0.15f, 0.10f},
                                              {1.0f, 0.68f, 0.22f}, {0.62f, 0.12f, 0.08f}, {0.20f, 0.28f, 0.38f}, {0.06f, 0.06f, 0.07f}, {0.90f, 0.74f, 0.10f}, {0.38f, 0.40f, 0.42f}, {0.50f, 0.52f, 0.56f},
                                              {0.04f, 0.06f, 0.10f}, {0.20f, 0.60f, 1.0f}, {0.30f, 0.32f, 0.36f}, {0.90f, 0.15f, 0.10f}};
static inline const float* iconColor(int id) { return id >= NIEVE ? ICON_NEW[id - NIEVE] : ICON_C[id]; }
// icono de un material (o de la herramienta "destruir" en el hueco 0)
static void hIconoNuevo(int id, float x, float y, float c) {   // v81: iconos de los objetos nuevos (cuadricula de 6x6)
    static const char* P[20][6] = {
        {"......", ".####.", "#hhhh#", "######", ".####.", "......"},            // acero: lingote
        {"..oo..", ".####.", ".#rr#.", ".#rr#.", ".####.", "......"},            // combustible: bidon
        {"..W...", ".WWW..", ".WRW..", ".WWW..", "WWWWW.", ".O.O.."},            // kit cohete
        {".####.", "######", "#vvvv#", "#....#", "#....#", "......"},            // casco
        {"##..##", "######", ".####.", ".####.", ".####.", "......"},            // pechera
        {"......", ".#..#.", ".#..#.", ".#..#.", "##..##", "......"},            // botas
        {"....bb", "...bb.", "..bb..", "h.b...", ".hh...", "h.h..."},            // espada de acero
        {"....bb", "...bb.", "..bb..", "h.b...", ".hh...", "h.h..."},            // espada de plasma
        {"CCCC..", "CCCCh.", "CCCC.h", "..h..h", "...h..", "....h."},            // martillo
        {"######", "#CCCC#", "#CCCC#", ".#CC#.", "..##..", "......"},            // escudo
        {".h....", "h.s...", "h..s..", "h..s..", "h.s...", ".h...."},            // arco
        {"....pp", "...pp.", "..h...", ".h....", "fh....", "f....."},            // flecha
        {".CC...", "CCCC..", "CCCCh.", ".CChh.", "..h...", "..h..."},            // hacha de batalla
        {"...bbb", "...bb.", "..h...", ".h....", "h.....", "h....."},            // lanza
        {"..CC..", ".CCCC.", ".CCCC.", "..CC..", "..h...", "..h..."},            // maza
        {"....b.", "...b..", "..b...", ".h....", "h.h...", "......"},            // daga
        {"....bb", "...bbb", "..bbb.", ".bbb..", "hbb...", "hh...."},             // espada larga
        {"......", ".MMM..", "MMmMM.", "MMMMM.", ".MMM.s", "....s."},            // carne
        {"......", ".WWW..", "WWWWW.", "WWWWW.", ".WWW..", "......"},            // lana
        {"h.....", "hWW...", "hWWWW.", "hWWWWh", "hhhhhh", "h....h"}};            // cama
    const int k = id == 254 ? 8 : id == 255 ? 9 : id >= 242 ? id - 242 : 10 + (id - 200);
    float cr = 0.62f, cg = 0.66f, cb = 0.72f, br = 0.78f, bg = 0.80f, bb = 0.84f;
    if (id == 249) { br = 0.30f; bg = 0.95f; bb = 1.0f; }                    // plasma
    if (id == 254) { cr = 0.45f; cg = 0.47f; cb = 0.52f; }
    for (int j = 0; j < 6; j++) for (int i = 0; i < 6; i++) {
        char ch = P[k][j][i];
        if (ch == '.') continue;
        float r = cr, g = cg, b = cb;
        if (ch == 'h') { r = 0.42f; g = 0.28f; b = 0.16f; if (id == 209) { r = 0.74f; g = 0.52f; b = 0.29f; } }
        else if (ch == 'b') { r = br; g = bg; b = bb; }
        else if (ch == 'v') { r = 0.25f; g = 0.80f; b = 1.0f; }
        else if (ch == 's' || ch == 'f') { r = 0.92f; g = 0.92f; b = 0.90f; }
        else if (ch == 'p') { r = 0.70f; g = 0.72f; b = 0.76f; }
        else if (ch == 'W') { r = 0.92f; g = 0.93f; b = 0.95f; }
        else if (ch == 'M') { r = 0.82f; g = 0.28f; b = 0.28f; }
        else if (ch == 'm') { r = 0.96f; g = 0.62f; b = 0.60f; }
        else if (ch == 'R') { r = 0.85f; g = 0.15f; b = 0.12f; }
        else if (ch == 'O') { r = 1.0f; g = 0.60f; b = 0.12f; }
        else if (ch == 'o') { r = 0.95f; g = 0.50f; b = 0.10f; }
        else if (ch == 'r') { r = 0.80f; g = 0.12f; b = 0.10f; }
        else if (ch == 'C') { r = id == 255 ? 0.20f : cr; g = id == 255 ? 0.45f : cg; b = id == 255 ? 0.85f : cb; }
        else if (ch == '#' && id == 243) { r = 0.55f; g = 0.15f; b = 0.12f; }
        float v = 0.9f + (hh(i, j, 300 + id) - 0.5f) * 0.2f;
        hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, r * v, g * v, b * v);
    }
}
static void hIcono(int slot, float x, float y, float s) {
    const int N = 6;
    float c = s / N;
    if (esObjNuevo(slot)) { hIconoNuevo(slot, x, y, c); return; }
    if (slot == ID_POLVORA) {   // montoncito de polvo gris oscuro con motas
        for (int j = 2; j < N; j++) for (int i = 0; i < N; i++) {
            int alt = 5 - std::abs(2 * i - 5) / 2 - 1;       // forma de monton
            if (j < N - 1 - alt) continue;
            float v = 0.85f + (hh(i, j, 251) - 0.5f) * 0.5f;
            hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.24f * v, 0.24f * v, 0.27f * v);
        }
        hRect(x + 2 * c, y + 3 * c, c, c, 0.55f, 0.55f, 0.58f);
        hRect(x + 4 * c, y + 4 * c, c, c, 0.40f, 0.40f, 0.44f);
        return;
    }
    if (slot == ID_PISTOLA) {   // pistola de plasma: cuerpo oscuro, cañon celeste brillante
        for (int i = 0; i < 6; i++) hRect(x + i * c, y + 2 * c, c + 0.5f, c + 0.5f, 0.35f, 0.37f, 0.42f);
        for (int i = 0; i < 6; i++) hRect(x + i * c, y + 3 * c, c + 0.5f, c + 0.5f, 0.25f, 0.27f, 0.32f);
        hRect(x + 6 * c, y + 2 * c, c + 0.5f, c * 2 + 0.5f, 0.35f, 0.90f, 1.0f);
        for (int j = 4; j < 7; j++) hRect(x + 1 * c, y + j * c, c + 0.5f, c + 0.5f, 0.22f, 0.24f, 0.28f);
        hRect(x + 3 * c, y + 2 * c, c + 0.5f, c * 0.6f, 0.2f, 0.8f, 1.0f);
        return;
    }
    if (slot == ID_RADAR) {   // radar: cuerpo gris oscuro, pantalla verde con un punto y antena
        for (int j = 1; j < N; j++) for (int i = 0; i < N; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.20f, 0.22f, 0.26f);
        for (int j = 2; j < 5; j++) for (int i = 1; i < 5; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.05f, 0.30f, 0.20f);
        hRect(x + 3 * c, y + 3 * c, c + 0.5f, c + 0.5f, 0.50f, 1.0f, 0.60f);
        hRect(x + 5 * c, y + 0 * c, c * 0.5f, c * 1.2f, 0.75f, 0.77f, 0.80f);
        return;
    }
    if (slot == ID_SPRAY) {   // lata de spray con la tapa del color elegido
        const float* k = COLOR_CARR[g_sprayColor];
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.10f, 0.10f, 0.12f);
        for (int j = 1; j < N; j++) for (int i = 2; i < 5; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.62f, 0.64f, 0.70f);
        for (int i = 2; i < 5; i++) hRect(x + i * c, y + 0 * c, c + 0.5f, c + 0.5f, k[0], k[1], k[2]);
        hRect(x + 3 * c, y + 0 * c - c * 0.5f, c, c * 0.6f, 0.9f, 0.9f, 0.9f);
        return;
    }
    if (esHerr(slot)) {   // herramienta: mango de madera y cabezal del color del material
        static const float HC[4][3] = {{0.62f, 0.44f, 0.22f}, {0.55f, 0.55f, 0.58f}, {0.88f, 0.88f, 0.92f}, {0.30f, 0.92f, 0.95f}};
        const float* h = HC[herrNivel(slot)];
        auto P = [&](int i, int j, float r, float g, float b) { hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, r, g, b); };
        for (int d = 0; d < 5; d++) P(d, N - 1 - d, 0.42f, 0.28f, 0.12f);                       // mango en diagonal
        int ti = herrTipo(slot);
        if (ti == 0) { for (int i = 1; i < 6; i++) P(i, 0, h[0], h[1], h[2]); P(1, 1, h[0], h[1], h[2]); P(5, 1, h[0], h[1], h[2]); P(3, 1, 0.42f, 0.28f, 0.12f); }
        else if (ti == 1) { for (int j = 0; j < 3; j++) for (int i = 3; i < 6; i++) P(i, j, h[0], h[1], h[2]); P(2, 1, h[0], h[1], h[2]); }
        else { for (int j = 0; j < 2; j++) for (int i = 3; i < 6; i++) P(i, j, h[0], h[1], h[2]); P(2, 2, h[0], h[1], h[2]); P(4, 2, h[0], h[1], h[2]); }
        return;
    }
    if (slot >= VIDRIO && slot < NIEVE) {   // piezas de vehiculo
        float pc[3]; colorPieza((uint8_t)slot, pc);
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
            float v = 1.0f + (hh(i, j, slot) - 0.5f) * 0.16f;
            hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, pc[0] * v, pc[1] * v, pc[2] * v);
        }
        if (slot == VIDRIO) { for (int i = 0; i < N; i++) { hRect(x + i * c, y, c, c * 0.5f, 1, 1, 1, 0.8f); hRect(x, y + i * c, c * 0.5f, c, 1, 1, 1, 0.8f); } }
        if (slot == RUEDA) for (int j = 2; j < 4; j++) for (int i = 2; i < 4; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.6f, 0.6f, 0.65f);
        if (slot == HELICE) { for (int i = 0; i < N; i++) hRect(x + i * c, y + (N - 1 - i) * c, c + 0.5f, c + 0.5f, 0.25f, 0.17f, 0.08f); }
        if (slot == TIMON) { for (int i = 1; i < 5; i++) { hRect(x + i * c, y + c, c, c, 0.6f, 0.6f, 0.65f); hRect(x + c, y + i * c, c, c, 0.6f, 0.6f, 0.65f); } }
        if (slot == CONTROL) { hRect(x + 2 * c, y + 2 * c, 2 * c, 2 * c, 0.2f, 0.2f, 0.25f); }
        return;
    }
    if ((slot >= FLOR_ROJA && slot <= FLOR_ROSA) || slot == LIANA) {   // objetos delgados: tallo (y cabeza si es flor)
        const float* k = iconColor(slot);
        if (slot == LIANA) { for (int j = 0; j < N; j++) hRect(x + 2 * c, y + j * c, 2 * c, c + 0.5f, k[0], k[1], k[2]); return; }
        for (int j = 2; j < N; j++) hRect(x + 2.5f * c, y + j * c, c, c + 0.5f, 0.20f, 0.52f, 0.16f);
        for (int j = 0; j < 3; j++) for (int i = 1; i < 5; i++) if (!((j == 0 || j == 2) && (i == 1 || i == 4))) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, k[0], k[1], k[2]);
        hRect(x + 2 * c, y + c, 2 * c, c, 1.0f, 0.90f, 0.30f);
        return;
    }
    bool mineral = esMineral((uint8_t)slot);
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        const float* col = iconColor(slot);
        float r = col[0], g = col[1], b = col[2];
        if ((slot == 1 || slot == PASTO_SECO) && j >= 2) { r = 0.45f; g = 0.32f; b = 0.20f; }      // pasto: costado de tierra
        if (mineral && hh(i, j, 99) <= 0.55f) { r = 0.50f; g = 0.50f; b = 0.52f; }   // roca con vetas de mineral
        float v = 1.0f + (hh(i, j, slot) - 0.5f) * 0.22f;
        hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, r * v, g * v, b * v);
    }
    if (slot == ANTORCHA) {   // palito de madera con llama
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.16f, 0.11f, 0.08f);
        for (int j = 2; j < N; j++) for (int i = 2; i < 4; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.45f, 0.30f, 0.14f);
        for (int j = 0; j < 2; j++) for (int i = 2; i < 4; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, j == 0 ? 1.0f : 1.0f, j == 0 ? 0.93f : 0.62f, j == 0 ? 0.45f : 0.15f);
        hRect(x + 1 * c, y + 1 * c, c, c, 1.0f, 0.62f, 0.15f); hRect(x + 4 * c, y + 1 * c, c, c, 1.0f, 0.62f, 0.15f);
    }
    if (slot == 0) {   // una X blanca
        for (int i = 0; i < N; i++) {
            hRect(x + i * c, y + i * c, c, c, 1, 1, 1);
            hRect(x + (N - 1 - i) * c, y + i * c, c, c, 1, 1, 1);
        }
    }
}
#ifdef MOVIL
static void tactilDibujar(int fbw, int fbh, float t) {
    g_tac.fbw = fbw; g_tac.fbh = fbh;
    g_tac.layout();
    float k = std::max(0.8f, fbh / 720.0f);
    if (g_tac.joystickActivo()) {      // joystick flotante
        float R = g_tac.radioJoy();
        hMarco(g_tac.jx0 - R, g_tac.jy0 - R, 2 * R, 2 * R, std::max(2.0f, 2 * k), 1, 1, 1, 0.35f);
        hRect(g_tac.jx - 0.35f * R, g_tac.jy - 0.35f * R, 0.7f * R, 0.7f * R, 1, 1, 1, 0.45f);
    }
    for (auto& b : g_tac.btns) {
        bool dn = g_tac.boton(b.id);
        float a = dn ? 0.60f : 0.28f;
        hRect(b.cx - b.r, b.cy - b.r, 2 * b.r, 2 * b.r, 0.05f, 0.05f, 0.08f, a);
        hMarco(b.cx - b.r, b.cy - b.r, 2 * b.r, 2 * b.r, std::max(2.0f, 2 * k), 1, 1, 1, dn ? 0.95f : 0.55f);
        float px = std::max(2.0f, t - 1);
        while (px > 1 && hAncho(b.txt, px) > 1.8f * b.r) px -= 1;
        hTexto(b.txt, b.cx - hAncho(b.txt, px) / 2.0f, b.cy - 3.5f * px, px, 1, 1, 1);
    }
}
#endif
// ---------------------------------------------------------------- red: avioneta compartida y chat
static void vehLiberarMallas() {
    g_veh.cuerpo.liberar(); g_veh.vidrio.liberar();
    for (auto& h : g_veh.helices) h.malla.liberar();
    g_veh.helices.clear(); g_veh.pts.clear();
}
static void vehEnviarSet(int paraId) {      // paraId < 0: a todos
    Veh& v = g_veh;
    if (!v.activo) return;
    RedW w; w.f((float)v.p.x); w.f((float)v.p.y); w.f((float)v.p.z); w.f((float)v.yaw); w.f((float)v.pitch); w.f((float)v.roll);
    w.u32((uint32_t)v.celdas.size());
    for (auto& c : v.celdas) { w.u32((uint32_t)c.x); w.u32((uint32_t)c.y); w.u32((uint32_t)c.z); w.u8(c.t); }
    if (paraId < 0) g_red.difundir(RM_VEH_SET, w); else g_red.enviarA(paraId, RM_VEH_SET, w);
}
static void vehEnviarPose(int piloto) {
    Veh& v = g_veh;
    RedW w; w.u8((uint8_t)piloto);
    w.f((float)v.p.x); w.f((float)v.p.y); w.f((float)v.p.z); w.f((float)v.yaw); w.f((float)v.pitch); w.f((float)v.roll);
    w.f((float)v.v.x); w.f((float)v.v.y); w.f((float)v.v.z); w.f((float)v.gas); w.u8(v.suelo ? 1 : 0);
    g_red.difundir(RM_VEH_POSE, w);
}
static void vehRecibir(uint8_t tipo, RedR& r, int) {
    Veh& v = g_veh;
    if (tipo == RM_VEH_SET) {
        if (v.pilotando) return;
        float px = r.f(), py = r.f(), pz = r.f(), yw = r.f(), pt = r.f(), rl = r.f();
        uint32_t n = r.u32();
        if (!r.ok || n == 0 || n > 200000) return;
        std::vector<VCelda> cs; cs.reserve(n);
        for (uint32_t i = 0; i < n && r.ok; i++) { VCelda c; c.x = (int)r.u32(); c.y = (int)r.u32(); c.z = (int)r.u32(); c.t = r.u8(); cs.push_back(c); }
        if (!r.ok) return;
        if (g_sobreVeh) vehSalir();
        vehLiberarMallas();
        v.celdas.swap(cs);
        v.p = {px, py, pz}; v.yaw = yw; v.pitch = pt; v.roll = rl; v.v = {0, 0, 0}; v.gas = 0;
        v.activo = true; v.pilotando = false; v.suelo = true; v.pilotoRed = -1;
        vehReconstruir(v);
        v.objP = v.p; v.objYaw = yw; v.objPitch = pt; v.objRoll = rl; v.objV = {0, 0, 0}; v.objGas = 0; v.objSuelo = true; v.objValido = true;
        aviso("OTRO JUGADOR ENSAMBLO UNA AVIONETA");
    } else if (tipo == RM_VEH_CLEAR) {
        if (!v.activo || v.pilotando) return;
        if (g_sobreVeh) vehSalir();
        vehLiberarMallas();
        v.celdas.clear(); v.activo = false; v.objValido = false; v.pilotoRed = -1;
        aviso("LA AVIONETA FUE DESARMADA");
    } else if (tipo == RM_VEH_POSE) {
        int piloto = r.u8();
        float px = r.f(), py = r.f(), pz = r.f(), yw = r.f(), pt = r.f(), rl = r.f(), vx = r.f(), vy = r.f(), vz = r.f(), gs = r.f();
        bool su = r.u8() != 0;
        if (!r.ok || !v.activo || v.pilotando) return;
        v.pilotoRed = piloto == 255 ? -1 : piloto;
        v.objP = {px, py, pz}; v.objYaw = yw; v.objPitch = pt; v.objRoll = rl; v.objV = {vx, vy, vz}; v.objGas = gs; v.objSuelo = su; v.objValido = true;
    }
}
static std::string chatNombre(int id) {
    if (id == g_red.miId) return g_red.miNombre;
    auto it = g_red.rem.find(id);
    return it != g_red.rem.end() && !it->second.nombre.empty() ? it->second.nombre : "?";
}
static void chatRecibir(int id, const std::string& t) {
    g_chatLog.push_back({chatNombre(id) + ": " + t, glfwGetTime()});
    while (g_chatLog.size() > 40) g_chatLog.pop_front();
    std::printf("CHAT %s: %s\n", chatNombre(id).c_str(), t.c_str());
}
static void redIniciarCallbacks() {
    g_red.alMensaje = vehRecibir;
    g_red.alChat = chatRecibir;
    g_red.alNuevo = [](int id) { if (g_veh.activo) { vehEnviarSet(id); vehEnviarPose(g_veh.pilotando ? g_red.miId : (g_veh.pilotoRed >= 0 ? g_veh.pilotoRed : 255)); } };
    g_vehHookSet = [] { if (g_red.activo()) vehEnviarSet(-1); };
    g_vehHookClear = [] { if (g_red.activo()) { RedW w; g_red.difundir(RM_VEH_CLEAR, w); } };
}

// ---------------------------------------------------------------- red: ediciones remotas, nombres, informacion
struct EtiquetaJ { float x, y; std::string txt; };
static std::vector<EtiquetaJ> g_etiquetas;       // nombres de los otros jugadores (en pantalla)
static std::string g_redInfo;                    // linea de estado de la red
static std::unordered_map<int64_t, std::vector<RedEdit>> g_redPend;   // ediciones de chunks que aun no estan cargados
static std::unordered_set<int64_t> g_redCols;    // anfitrion: columnas cerca de los demas jugadores (no se descargan)
static bool redAplicarEdit(const RedEdit& e, std::vector<ChunkP>& su, int* b) {   // false si el chunk no esta listo
    int cy = divPiso(e.y, CS);
    if (cy < 0 || cy >= HYC) return true;
    ChunkP c = buscar(divPiso(e.x, CS), cy, divPiso(e.z, CS));
    if (!c || c->state.load(std::memory_order_acquire) < ST_READY) return false;
    bool prev = g_redEnviar; g_redEnviar = false;
    escribirCubito(e.x, e.y, e.z, e.t, su);
    g_redEnviar = prev;
    b[0] = std::min(b[0], e.x); b[1] = std::max(b[1], e.x); b[2] = std::min(b[2], e.y); b[3] = std::max(b[3], e.y); b[4] = std::min(b[4], e.z); b[5] = std::max(b[5], e.z);
    return true;
}
static void redTerminarLote(std::vector<ChunkP>& su, int* b) {
    for (auto& c : su) remallarAsync(c);
    if (!su.empty()) marcarLuzSucia(b[0] - SUB, b[1] + SUB, b[2] - SUB, b[3] + SUB, b[4] - SUB, b[5] + SUB, su);
}
static void redRecibirEdits(const RedEdit* v, int n) {
    std::vector<ChunkP> su; int b[6] = {1 << 30, -(1 << 30), 1 << 30, -(1 << 30), 1 << 30, -(1 << 30)};
    for (int i = 0; i < n; i++) {
        if (!redAplicarEdit(v[i], su, b)) g_redPend[clave(divPiso(v[i].x, CS), divPiso(v[i].y, CS), divPiso(v[i].z, CS))].push_back(v[i]);
    }
    redTerminarLote(su, b);
}
static void redPendientes() {
    if (g_redPend.empty()) return;
    std::vector<ChunkP> su; int b[6] = {1 << 30, -(1 << 30), 1 << 30, -(1 << 30), 1 << 30, -(1 << 30)};
    for (auto it = g_redPend.begin(); it != g_redPend.end();) {
        bool todo = true;
        std::vector<RedEdit> resto;
        for (auto& e : it->second) if (!redAplicarEdit(e, su, b)) { todo = false; resto.push_back(e); }
        if (todo) it = g_redPend.erase(it); else { it->second.swap(resto); ++it; }
    }
    redTerminarLote(su, b);
}

static void itemColor(int id, float* rgb) {
    rgb[0] = rgb[1] = rgb[2] = 0.7f;
    if (id >= 0 && (id < VIDRIO || (id >= NIEVE && id < NTIPOS))) { const float* ic = iconColor(id); rgb[0] = ic[0]; rgb[1] = ic[1]; rgb[2] = ic[2]; }
    else if (id >= VIDRIO && id < 250) colorPieza((uint8_t)id, rgb);
}
static bool itemHerrSprite(int id, float col[8][8][3], bool fill[8][8]) {
    if (!esHerr(id)) return false;
    for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) fill[r][c] = false;
    static const float HC[4][3] = {{0.62f, 0.44f, 0.22f}, {0.55f, 0.55f, 0.58f}, {0.88f, 0.88f, 0.92f}, {0.30f, 0.92f, 0.95f}};
    const float* h = HC[herrNivel(id)];
    auto P = [&](int x, int y, const float* c) { if (x >= 0 && x < 8 && y >= 0 && y < 8) { fill[y][x] = true; col[y][x][0] = c[0]; col[y][x][1] = c[1]; col[y][x][2] = c[2]; } };
    const float mango[3] = {0.42f, 0.28f, 0.12f};
    for (int d = 0; d < 6; d++) P(d, 7 - d, mango);
    int ti = herrTipo(id);
    if (ti == 0) { P(3, 0, h); P(4, 0, h); P(5, 0, h); P(6, 0, h); P(2, 1, h); P(7, 1, h); P(1, 2, h); P(7, 2, h); P(4, 1, mango); P(5, 2, mango); }
    else if (ti == 1) { P(4, 0, h); P(5, 0, h); P(6, 0, h); P(4, 1, h); P(5, 1, h); P(6, 1, h); P(7, 1, h); P(5, 2, h); P(6, 2, h); P(7, 2, h); P(6, 3, h); }
    else { P(5, 0, h); P(6, 0, h); P(5, 1, h); P(6, 1, h); P(7, 1, h); P(6, 2, h); P(7, 2, h); P(7, 3, h); P(6, 3, h); }
    return true;
}
static bool g_golpea = false;   // este cuadro estas picando/poniendo (para la animacion de la mano y para los otros jugadores)
static uint8_t objetoEnMano() {
    if (g_material < 0 || g_material == 0 || g_material == ID_POLVORA || g_material == ID_RADAR || esObjNuevo(g_material)) return 255;
    if (g_material == ID_PISTOLA) return 253;
    if (g_survival && g_inv[g_material] <= 0) return 255;
    return (uint8_t)g_material;
}
static void hBoton(const MRect& r, float k, float cr, float cg, float cb, bool hov, const char* txt, float px);
static void hCantidad(int id, float x, float y, float s, float k, float t) {   // cantidad (solo survival) y oscurecer si no hay
    if (!g_survival || id < 0) return;
    int c = g_inv[id];
    if (c <= 0) { hRect(x, y, s, s, 0, 0, 0, 0.6f); return; }
    if (esHerr(id) || id == ID_SPRAY) return;
    char b[16]; std::snprintf(b, sizeof b, "%d", c);
    float px = std::max(2.0f, t - 1);
    hTexto(b, x + s - hAncho(b, px) - 2 * k, y + s - 8 * px, px, 1, 1, 0.6f);
}
static void hCorazon(float x, float y, float s, int lleno) {   // lleno: 0 vacio, 1 medio, 2 entero
    static const char* P[6] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
    float c = s / 7.0f;
    for (int j = 0; j < 6; j++) for (int i = 0; i < 7; i++) if (P[j][i] == '#') {
        bool on = lleno == 2 || (lleno == 1 && i < 4);
        if (on) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.92f, 0.10f, 0.12f);
        else hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.22f, 0.05f, 0.06f);
    }
}
static void hdibujar(int fbw, int fbh, int fps, bool cargando, bool guardado, double px_, double py_, double pz_) {
    float k = std::max(0.8f, fbh / 720.0f);
    float t = std::max(2.0f, std::floor(2.0f * k + 0.5f));           // tamano de pixel de letra
    char b[128];
    // ---- panel de informacion (arriba izquierda)
    float x = 12 * k, y = 10 * k, lh = 11.0f * t;
    std::snprintf(b, sizeof b, "FPS %d", fps); hTexto(b, x, y, t, 1, 1, 1); y += lh;
    std::snprintf(b, sizeof b, "X %d  Y %d  Z %d", (int)std::floor(px_), (int)std::floor(py_), (int)std::floor(pz_)); hTexto(b, x, y, t, 0.9f, 0.9f, 0.9f); y += lh;
    hTexto(g_pincelBloque ? "BLOQUE 2X2 (CTRL)" : "CUBITO (CTRL)", x, y, t, 1.0f, 0.85f, 0.3f); y += lh;
    hTexto(g_ck.activo ? "EN EL COHETE" : (g_veh.activo && g_veh.pilotando) ? "PILOTANDO" : g_j.volar ? "VOLANDO (F)" : "CAMINANDO (F)", x, y, t, 0.6f, 0.9f, 1.0f); y += lh;
    if (!g_redInfo.empty()) { hTexto(g_redInfo.c_str(), x, y, t, 0.5f, 1.0f, 0.6f); y += lh; }
    if (g_red.activo() || g_chat) {
        double nc = glfwGetTime();
        float cy0 = (g_chat && g_tac.activo) ? fbh * 0.40f : fbh - 96 * k;      // con teclado tactil el chat sube para que no lo tape
        int n = 0;
        for (auto it = g_chatLog.rbegin(); it != g_chatLog.rend() && n < 8; ++it) {
            double edad = nc - it->t;
            if (!g_chat && edad > 12.0) break;
            std::string tx = it->txt.size() > 70 ? it->txt.substr(0, 70) : it->txt;
            float yy = cy0 - n * 11.0f * t, tw = hAncho(tx.c_str(), t);
            hRect(10 * k, yy - t, tw + 6 * t, 10 * t, 0.0f, 0.0f, 0.0f);
            hTexto(tx.c_str(), 12 * k, yy, t, 1.0f, 1.0f, 1.0f);
            n++;
        }
        if (g_chat) {
            std::string tx = "> " + g_chatBuf + ((int)(nc * 2) % 2 ? "_" : " ");
            float yy = cy0 + 14.0f * t;
            hRect(10 * k, yy - t, hAncho(tx.c_str(), t) + 6 * t, 10 * t, 0.0f, 0.0f, 0.0f);
            hTexto(tx.c_str(), 12 * k, yy, t, 1.0f, 0.95f, 0.5f);
        }
    }
    for (auto& e : g_etiquetas) {
        float tw = hAncho(e.txt.c_str(), t);
        hRect(e.x - tw / 2 - 3 * t, e.y - 2 * t, tw + 6 * t, 11 * t, 0.0f, 0.0f, 0.0f);
        hTexto(e.txt.c_str(), e.x - tw / 2, e.y, t, 1, 1, 1);
    }
    {
        double hh_ = std::fmod(6.0 + g_hora * 24.0, 24.0);
        std::snprintf(b, sizeof b, "HORA %02d:%02d (T)", (int)hh_, (int)((hh_ - (int)hh_) * 60.0));
        hTexto(b, x, y, t, 1.0f, 0.95f, 0.7f); y += lh;
    }
    if (cargando) { hTexto("CARGANDO...", x, y, t, 1, 0.6f, 0.4f); y += lh; }
    if (guardado) { hTexto("GUARDADO", x, y, t, 0.5f, 1, 0.5f); y += lh; }
#ifndef MOVIL
    {
        const char* a = g_survival ? "E: INVENTARIO" : "E: MATERIALES";
        hTexto(a, fbw - hAncho(a, t) - 12 * k, 10 * k, t, 0.85f, 0.85f, 0.85f);
    }
#endif
    // ---- barra de acceso rapido
    float S = std::floor(48.0f * k), gap = std::floor(5.0f * k);
    float total = 9 * S + 8 * gap, x0 = (fbw - total) / 2.0f, y0 = fbh - S - 16 * k;
    int sel = g_sel;
    for (int i = 0; i < 9 && !(g_veh.activo && g_veh.pilotando) && !g_ck.activo; i++) {
        float sx = x0 + i * (S + gap);
        hRect(sx, y0, S, S, 0, 0, 0, 0.55f);
        float in = 5 * k;
        if (g_barra[i] >= 0) { hIcono(g_barra[i], sx + in, y0 + in, S - 2 * in); hCantidad(g_barra[i], sx + in, y0 + in, S - 2 * in, k, t); }
        if (i == sel) hMarco(sx - 2 * k, y0 - 2 * k, S + 4 * k, S + 4 * k, std::max(2.0f, 3 * k), 1, 1, 1);
        else hMarco(sx, y0, S, S, std::max(1.0f, k), 0.45f, 0.45f, 0.45f);
        std::snprintf(b, sizeof b, "%d", i + 1);
        hTexto(b, sx + 4 * k, y0 + 3 * k, std::max(2.0f, t - 1), 1, 1, 1);
    }
    if ((g_survival || g_jefeAct()) && !(g_veh.activo && g_veh.pilotando) && !g_ck.activo) {
        float hs = 18 * k;
        for (int i = 0; i < 10; i++) {
            int lleno = g_vida >= 2 * (i + 1) ? 2 : (g_vida >= 2 * i + 1 ? 1 : 0);
            hCorazon(x0 + i * (hs + 3 * k), y0 - hs - 8 * k, hs, lleno);
        }
    }
    if (glfwGetTime() - g_explDestello < 0.5) hRect(0, 0, (float)fbw, (float)fbh, 1.0f, 0.95f, 0.8f, (float)(0.85 * g_explFuerza * (1.0 - (glfwGetTime() - g_explDestello) / 0.5)));
    if ((g_survival || g_jefeAct()) && glfwGetTime() - g_danoFlash < 0.4) hRect(0, 0, (float)fbw, (float)fbh, 0.85f, 0.0f, 0.0f, 0.35f * (float)(1.0 - (glfwGetTime() - g_danoFlash) / 0.4));
    if (g_modoLuna && g_mecha.viva) {        // barra de vida del mecha (v67)
        const float bw = fbw * 0.5f, bx = (fbw - bw) / 2.0f, by = 18.0f * k, fr = std::max(0.0f, g_mecha.hp / g_mecha.hpMax);
        hRect(bx - 3 * k, by - 3 * k, bw + 6 * k, 16 * k, 0.0f, 0.0f, 0.0f, 0.75f);
        hRect(bx, by, bw * fr, 10 * k, 0.85f, 0.08f, 0.06f);
        hRect(bx, by, bw * fr, 3 * k, 1.0f, 0.45f, 0.35f);
        hTexto("MECHA DE LA IA", (fbw - hAncho("MECHA DE LA IA", t)) / 2.0f, by + 14 * k, t, 1.0f, 0.5f, 0.45f);
    }
    if (!g_menu && !(g_veh.activo && g_veh.pilotando) && !g_ck.activo) {
        char nm[64];
        if (g_material < 0) std::snprintf(nm, sizeof nm, "MANO VACIA");
        else if (g_destruir) std::snprintf(nm, sizeof nm, "DESTRUIR");
        else if (g_material == ID_SPRAY) std::snprintf(nm, sizeof nm, "SPRAY %s (Z/C COLOR) CLIC DER PINTA", NOMBRE_COLOR[g_sprayColor]);
        else if (g_material == RUEDA) std::snprintf(nm, sizeof nm, "PONER: RUEDA DIAMETRO %d (Z/C TAMANO)", g_ruedaD);
        else std::snprintf(nm, sizeof nm, "PONER: %s", nombreSlot(g_material));
        hTexto(nm, (fbw - hAncho(nm, t + 1)) / 2.0f, y0 - 12 * k - 7 * (t + 1) - (g_survival ? 26 * k : 0), t + 1, 1, 1, 1);
    }
    if (g_veh.activo && g_veh.pilotando) {   // instrumentos de la avioneta
        double vel = std::sqrt(g_veh.v.x * g_veh.v.x + g_veh.v.y * g_veh.v.y + g_veh.v.z * g_veh.v.z);
        char ib[96];
        float iy = fbh * 0.30f;
        std::snprintf(ib, sizeof ib, "VELOCIDAD %d KM/H", (int)(vel * 3.6)); hTexto(ib, 12 * k, iy, t, 0.8f, 1.0f, 0.8f); iy += lh;
        std::snprintf(ib, sizeof ib, "ALTURA %d BLOQUES", (int)g_veh.p.y); hTexto(ib, 12 * k, iy, t, 0.8f, 1.0f, 0.8f); iy += lh;
        std::snprintf(ib, sizeof ib, "GAS %d%%", (int)(g_veh.gas * 100 + 0.5)); hTexto(ib, 12 * k, iy, t, 1.0f, 0.9f, 0.5f); iy += lh;
        hTexto(g_veh.suelo ? "EN TIERRA" : "EN EL AIRE", 12 * k, iy, t, 0.7f, 0.85f, 1.0f); iy += lh;
#ifndef MOVIL
        const char* ayuda = "SHIFT/CTRL GAS  W/S PICAR  A/D INCLINAR  Q/E TIMON  ESPACIO FRENO  TAB CAMARA  G BAJAR";
        float pa = t;
        while (pa > 1 && hAncho(ayuda, pa) > fbw - 24 * k) pa -= 1;
        hTexto(ayuda, (fbw - hAncho(ayuda, pa)) / 2.0f, fbh - 14 * k - 7 * pa, pa, 0.9f, 0.9f, 0.9f);
#endif
    }
    if (g_ck.activo) {      // instrumentos del cohete
        const Cohete& c = g_ck;
        const bool espacio = !g_modoLuna && !c.reentrada;
        char ib[120];
        float iy = fbh * 0.30f;
        double alt = (c.fase < 2 ? c.P.y : c.e2.p.y), vel = c.fase == 0 ? 0.0 : (c.fase == 1 ? c.sp : std::sqrt(c.V.x * c.V.x + c.V.y * c.V.y + c.V.z * c.V.z));
        std::snprintf(ib, sizeof ib, "VELOCIDAD %d KM/H", (int)(vel * 3.6)); hTexto(ib, 12 * k, iy, t, 0.8f, 1.0f, 0.8f); iy += lh;
        std::snprintf(ib, sizeof ib, "ALTURA %d BLOQUES", (int)alt); hTexto(ib, 12 * k, iy, t, 0.8f, 1.0f, 0.8f); iy += lh;
        const char* fase = (c.bloques && c.pad && !c.quema) ? "EN LA PLATAFORMA: MANTEN 2 PARA DESPEGAR" : c.reentrada ? (c.espCarga ? "CARGANDO LA TIERRA..." : "DESCENSO: FRENA CON 2 (PROA AL CIELO)") : c.vuelta ? "RUMBO A LA TIERRA (SHIFT+2: IMPULSO DE SALTO)" : c.fase == 0 ? "CUENTA ATRAS" : (c.fase == 1 ? "ETAPA 1 ENCENDIDA" : (c.llegada ? "LUNA ALCANZADA" : c.quema ? "MOTOR ENCENDIDO" : g_terraFija ? "RUMBO A LA LUNA" : "ORBITA - GRAVEDAD 0"));
        hTexto(fase, 12 * k, iy, t, 1.0f, 0.9f, 0.5f); iy += lh;
        if (c.fase == 1) { std::snprintf(ib, sizeof ib, "SEPARACION EN %d BLOQUES", std::max(0, (int)(1500.0 - (c.P.y - c.y0)))); hTexto(ib, 12 * k, iy, t, 0.7f, 0.85f, 1.0f); iy += lh; }
        if (c.fase == 2) {
            std::snprintf(ib, sizeof ib, "COMBUSTIBLE %d", (int)(c.fuel + 0.5)); hTexto(ib, 12 * k, iy, t, c.fuel > 20 ? 0.8f : 1.0f, c.fuel > 20 ? 1.0f : 0.5f, 0.6f); iy += lh;
            if (espacio && !c.vuelta) { double lz = ckLuzSuelo(c.e2.p); std::snprintf(ib, sizeof ib, "LUZ EN EL SUELO %d %s", (int)std::max(0.0, lz * 100.0), lz < 0.30 ? "- OSCURO: BUSCA EL LADO CLARO" : "- SE PUEDE ATERRIZAR"); hTexto(ib, 12 * k, iy, t, lz < 0.30 ? 1.0f : 0.5f, lz < 0.30 ? 0.6f : 1.0f, 0.5f); iy += lh; }
            if (espacio) { std::snprintf(ib, sizeof ib, c.vuelta ? "TIERRA A %d BLOQUES" : "LUNA A %d BLOQUES", (int)g_ckM.dist); hTexto(ib, 12 * k, iy, t, 0.85f, 0.85f, 1.0f); iy += lh; }
            double gr = g_ckM.ang * 57.29578;
            if (espacio) { std::snprintf(ib, sizeof ib, "PUNTERIA %.1f GRADOS%s", gr, gr < 3.0 ? " - BIEN" : ""); hTexto(ib, 12 * k, iy, t, gr < 3.0 ? 0.4f : 1.0f, gr < 3.0 ? 1.0f : 0.8f, 0.4f); iy += lh; }
            // marcadores
            auto marca = [&](float mx, float my, bool dentro, const char* txt, float r, float g, float b, float sz) {
                float h = sz * k;
                if (dentro) hMarco(mx - h, my - h, 2 * h, 2 * h, std::max(2.0f, 2 * k), r, g, b);
                else hRect(mx - h * 0.5f, my - h * 0.5f, h, h, r, g, b);
                hTexto(txt, mx - hAncho(txt, t) / 2.0f, my + h + 3 * k, t, r, g, b);
            };
            if (!espacio) {}
            else if (c.llegada == false) marca(g_ckM.lx, g_ckM.ly, g_ckM.lDentro, c.vuelta ? "TIERRA" : "LUNA", 0.85f, 0.9f, 1.0f, 22);
            else if (espacio) marca(g_ckM.sx, g_ckM.sy, g_ckM.sDentro, "SOL", 1.0f, 0.9f, 0.4f, 18);
            if (espacio) { float h = 9 * k; float px_ = g_ckM.px, py_ = g_ckM.py; float cr = gr < 3.0 ? 0.4f : 1.0f, cg = 1.0f, cb = gr < 3.0 ? 0.4f : 0.7f;
              hRect(px_ - h, py_ - 1 * k, 2 * h, 2 * k, cr, cg, cb); hRect(px_ - 1 * k, py_ - h, 2 * k, 2 * h, cr, cg, cb);
              hTexto("PROA", px_ - hAncho("PROA", t) / 2.0f, py_ + h + 3 * k, t, cr, cg, cb); }
        }
        if (c.fase == 0) {
            char nb[8]; std::snprintf(nb, sizeof nb, "%d", std::max(1, (int)std::ceil(5.0 - c.t)));
            float tb = t * 7;
            hTexto(nb, (fbw - hAncho(nb, tb)) / 2.0f, fbh * 0.30f, tb, 1.0f, 0.85f, 0.3f);
        }
#ifndef MOVIL
        const char* ayuda = c.vuelta ? "W/S A/D Q/E: GIRAR   MANTEN 2: MOTOR   SHIFT+2: IMPULSO DE SALTO   MOUSE: CAMARA   TAB: CABINA" : c.reentrada ? "MANTEN 2 PARA FRENAR   W/S A/D Q/E: GIRAR   MOUSE: CAMARA   TAB: CABINA" : c.bloques ? "W/S A/D Q/E: GIRAR   MANTEN 2: MOTOR   MOUSE: CAMARA   TAB: CABINA   G: BAJAR (SOLO EN LA PLATAFORMA)" : c.fase == 2 ? "W/S A/D Q/E: GIRAR   MANTEN 2: MOTOR   3: DESCENDER (SOLO DONDE HAY LUZ)   MOUSE: CAMARA   TAB: CABINA"
                                        : "MOUSE: CAMARA   TAB: CABINA   G: CANCELAR Y VOLVER A LA TIERRA";
        float pa = t;
        while (pa > 1 && hAncho(ayuda, pa) > fbw - 24 * k) pa -= 1;
        hTexto(ayuda, (fbw - hAncho(ayuda, pa)) / 2.0f, fbh - 14 * k - 7 * pa, pa, 0.9f, 0.9f, 0.9f);
#endif
    }
    if (g_modoLuna && !g_ck.activo && g_material == ID_RADAR) {      // radar de senal (v63)
        const double ahoraR = glfwGetTime();
        const double dx = (BASE_X + 0.5) - g_j.p.x, dz = (BASE_Z + 0.5) - g_j.p.z;
        const double dist = std::sqrt(dx * dx + dz * dz);
        const double rel = std::atan2(dx, -dz) - g_j.yaw;
        const float S = 46 * k, cx = fbw - 12 * k - S - 60 * k, cy = 70 * k + S;
        hRect(cx - S - 6 * k, cy - S - 6 * k, 2 * S + 12 * k + 120 * k, 2 * S + 12 * k + 30 * k, 0.03f, 0.08f, 0.06f, 0.82f);
        hMarco(cx - S - 6 * k, cy - S - 6 * k, 2 * S + 12 * k + 120 * k, 2 * S + 12 * k + 30 * k, std::max(2.0f, 2 * k), 0.2f, 0.8f, 0.5f);
        for (int i = 0; i < 24; i++) { double a = i * 6.2831853 / 24; hRect(cx + (float)std::sin(a) * S - k, cy - (float)std::cos(a) * S - k, 2 * k, 2 * k, 0.15f, 0.55f, 0.35f); }
        const double bip = 0.5 + 0.5 * std::sin(ahoraR * 5.0);
        for (int i = 1; i <= 10; i++) { double r = S * 0.85 * i / 10.0; hRect(cx + (float)(std::sin(rel) * r) - k, cy - (float)(std::cos(rel) * r) - k, 2 * k + (i == 10 ? 2 * k : 0), 2 * k + (i == 10 ? 2 * k : 0), 0.4f, 1.0f, 0.6f, 0.4f + 0.6f * (float)bip * i / 10.0f); }
        hRect(cx - 2 * k, cy - 2 * k, 4 * k, 4 * k, 0.9f, 1.0f, 0.9f);
        char rb[64];
        float tx = cx + S + 14 * k, ty = cy - S + 2 * k;
        std::snprintf(rb, sizeof rb, "BASE %d M", (int)dist); hTexto(rb, tx, ty, t, 0.6f, 1.0f, 0.7f); ty += lh;
        if (!g_puertaAbierta && dist < 40 && puertaAbiertaReal()) { g_puertaAbierta = true; g_baseDig = 4; }
        if (g_puertaAbierta || g_baseDig >= 4) hTexto("ABIERTA", tx, ty, t, 0.5f, 1.0f, 0.5f);
        else if (dist > 160.0) hTexto("SENAL DEBIL", tx, ty, t, 0.8f, 0.9f, 0.5f);
        else {
            const int dg = baseDigitoEsp(g_baseDig);
            static const char* MORSE[10] = {"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."};
            const char* pm = MORSE[dg];
            double dur = 0; for (const char* q = pm; *q; ++q) dur += (*q == '.' ? 0.25 : 0.75) + 0.25;
            const double ciclo = dur + 2.0;
            double tt = std::fmod(ahoraR, ciclo); bool on = false; double acc = 0;
            for (const char* q = pm; *q; ++q) { double l = (*q == '.' ? 0.25 : 0.75); if (tt >= acc && tt < acc + l) on = true; acc += l + 0.25; }
            std::snprintf(rb, sizeof rb, "DIGITO %d DE 4", g_baseDig + 1); hTexto(rb, tx, ty, t, 0.9f, 0.95f, 0.8f); ty += lh;
            const float L = 22 * k;
            hRect(tx, ty + 2 * k, L, L, on ? 1.0f : 0.15f, on ? 0.9f : 0.12f, on ? 0.2f : 0.05f);
            hMarco(tx, ty + 2 * k, L, L, std::max(1.0f, k), 0.8f, 0.8f, 0.7f);
        }
    }
    if (g_tecladoOn) {      // teclado de la puerta de la base
        const float W = 300 * k, H = 120 * k, x0 = (fbw - W) / 2.0f, y0 = fbh * 0.28f;
        const bool err = glfwGetTime() - g_tecladoErr < 1.2;
        hRect(x0, y0, W, H, 0.02f, 0.05f, 0.07f, 0.9f);
        hMarco(x0, y0, W, H, std::max(2.0f, 2 * k), err ? 1.0f : 0.2f, err ? 0.3f : 0.8f, err ? 0.3f : 0.9f);
        hTexto("CODIGO DE ACCESO", x0 + (W - hAncho("CODIGO DE ACCESO", t)) / 2.0f, y0 + 10 * k, t, 0.6f, 0.9f, 1.0f);
        char cb[16]; for (int i = 0; i < 4; i++) cb[i] = i < g_baseDig ? '*' : '_'; cb[4] = 0;
        const float tb = t * 3; hTexto(cb, x0 + (W - hAncho(cb, tb)) / 2.0f, y0 + 36 * k, tb, 0.5f, 1.0f, 0.8f);
        char db[48]; std::snprintf(db, sizeof db, "DIGITO %d DE 4", g_baseDig + 1);
        hTexto(err ? "INCORRECTO" : db, x0 + (W - hAncho(err ? "INCORRECTO" : db, t)) / 2.0f, y0 + H - 22 * k, t, err ? 1.0f : 0.8f, err ? 0.4f : 0.9f, err ? 0.4f : 0.8f);
    }
    if (glfwGetTime() - g_avisoT < 4.0 && !g_aviso.empty()) {
        float ta = t + 1;
        while (ta > 1 && hAncho(g_aviso.c_str(), ta) > fbw - 24 * k) ta -= 1;
        hTexto(g_aviso.c_str(), (fbw - hAncho(g_aviso.c_str(), ta)) / 2.0f, fbh * 0.22f, ta, 1.0f, 0.9f, 0.4f);
    }
    // ---- menu de materiales
    if (g_menu) {
        hRect(0, 0, (float)fbw, (float)fbh, 0, 0, 0, 0.62f);
        float tt = t + 3;
        const char* ti = !g_survival ? "MATERIALES" : (g_pagina == 0 ? "INVENTARIO" : "RECETAS");
        float xs, ys, ss;
        menuGeom(fbw, fbh, 0, xs, ys, ss);
        hTexto(ti, (fbw - hAncho(ti, tt)) / 2.0f, ys - 78 * k, tt, 1, 1, 1);
        char h1[96];
        if (g_survival && g_pagina == 1) std::snprintf(h1, sizeof h1, "CLIC EN UNA RECETA PARA FABRICAR   TAB: OBJETOS   E: CERRAR");
        else std::snprintf(h1, sizeof h1, "ARRASTRA A LA BARRA   CLIC DER: VACIAR   E: CERRAR%s", g_survival ? "   TAB: RECETAS" : "");
        float th = t;
        while (th > 1 && hAncho(h1, th) > fbw - 24 * k) th -= 1;
        hTexto(h1, (fbw - hAncho(h1, th)) / 2.0f, ys - 24 * k, th, 0.9f, 0.9f, 0.6f);
        int hov = menuTileEn(g_mx, g_my);
        if (g_survival) {
            double qx = g_mx * fbw / std::max(1, g_winW), qy = g_my * fbh / std::max(1, g_winH);
            hBoton(tabRect(fbw, fbh, 0), k, g_pagina == 0 ? 0.20f : 0.10f, g_pagina == 0 ? 0.45f : 0.20f, g_pagina == 0 ? 0.25f : 0.14f, mDentro(tabRect(fbw, fbh, 0), qx, qy), "OBJETOS", t);
            hBoton(tabRect(fbw, fbh, 1), k, g_pagina == 1 ? 0.20f : 0.10f, g_pagina == 1 ? 0.45f : 0.20f, g_pagina == 1 ? 0.25f : 0.14f, mDentro(tabRect(fbw, fbh, 1), qx, qy), "RECETAS", t);
            if (g_pagina == 1) {
                const auto& R = recetas();
                const int NP = ((int)R.size() + 25) / 26;
                g_recPag = std::max(0, std::min(g_recPag, NP - 1));
                if (NP > 1) {
                    char pg[24]; std::snprintf(pg, sizeof pg, "PAG %d/%d", g_recPag + 1, NP);
                    hBoton(flechaRect(fbw, fbh, 0), k, 0.10f, 0.20f, 0.14f, mDentro(flechaRect(fbw, fbh, 0), qx, qy), "<", t * 1.5f);
                    hBoton(flechaRect(fbw, fbh, 1), k, 0.10f, 0.20f, 0.14f, mDentro(flechaRect(fbw, fbh, 1), qx, qy), ">", t * 1.5f);
                    hTexto(pg, tabRect(fbw, fbh, 0).x - hAncho(pg, t) - 14 * k, 44 * k, t, 0.9f, 0.9f, 0.9f);
                }
                for (int li = 0; li < 26 && g_recPag * 26 + li < (int)R.size(); li++) {
                    const int ri = g_recPag * 26 + li;
                    MRect rr = recRect(fbw, fbh, li);
                    bool ok = puedeCraftear(R[ri]), hv = mDentro(rr, qx, qy);
                    hRect(rr.x, rr.y, rr.w, rr.h, ok ? 0.10f : 0.08f, ok ? 0.28f : 0.08f, ok ? 0.12f : 0.08f, 0.92f);
                    hMarco(rr.x, rr.y, rr.w, rr.h, std::max(2.0f, 2 * k), hv ? 1.0f : 0.3f, hv ? 0.85f : 0.3f, hv ? 0.2f : 0.3f);
                    hIcono(R[ri].out, rr.x + 4 * k, rr.y + 4 * k, rr.h - 8 * k);
                    char l1[64]; std::snprintf(l1, sizeof l1, "%s%s", R[ri].n > 1 ? "" : "", nombreSlot(R[ri].out));
                    if (R[ri].n > 1) std::snprintf(l1, sizeof l1, "%s X%d", nombreSlot(R[ri].out), R[ri].n);
                    char l2[96];
                    if (R[ri].nc) std::snprintf(l2, sizeof l2, "%d %s + %d %s + %d %s", R[ri].na, nombreSlot(R[ri].a), R[ri].nb, nombreSlot(R[ri].b), R[ri].nc, nombreSlot(R[ri].c));
                    else if (R[ri].nb) std::snprintf(l2, sizeof l2, "%d %s + %d %s", R[ri].na, nombreSlot(R[ri].a), R[ri].nb, nombreSlot(R[ri].b));
                    else std::snprintf(l2, sizeof l2, "%d %s", R[ri].na, nombreSlot(R[ri].a));
                    float p1 = std::max(2.0f, t - 1), p2 = std::max(1.0f, t - 1);
                    hTexto(l1, rr.x + rr.h + 4 * k, rr.y + 4 * k, p1, 1, 1, 1);
                    hTexto(l2, rr.x + rr.h + 4 * k, rr.y + 4 * k + 9 * p1, p2, ok ? 0.6f : 0.9f, ok ? 1.0f : 0.5f, ok ? 0.6f : 0.5f);
                    if (recBloq(R[ri])) { hRect(rr.x, rr.y, rr.w, rr.h, 0.0f, 0.0f, 0.0f, 0.55f); hTexto("BLOQUEADO: DERROTA AL MECHA", rr.x + rr.w - hAncho("BLOQUEADO: DERROTA AL MECHA", p2) - 6 * k, rr.y + 4 * k, p2, 1.0f, 0.55f, 0.25f); }
                }
            }
        }
        if (!(g_survival && g_pagina == 1) && menuPaginas() > 1) {      // flechas y numero de pagina
            double qx = g_mx * fbw / std::max(1, g_winW), qy = g_my * fbh / std::max(1, g_winH);
            for (int dir = 0; dir < 2; dir++) {
                bool hay = dir == 1 ? g_menuPag < menuPaginas() - 1 : g_menuPag > 0;
                MRect fr = flechaRect(fbw, fbh, dir);
                bool hv = hay && mDentro(fr, qx, qy);
                hRect(fr.x, fr.y, fr.w, fr.h, hay ? (hv ? 0.30f : 0.16f) : 0.08f, hay ? (hv ? 0.55f : 0.34f) : 0.08f, hay ? (hv ? 0.30f : 0.20f) : 0.08f, 0.95f);
                hMarco(fr.x, fr.y, fr.w, fr.h, std::max(2.0f, 2 * k), hv ? 1.0f : 0.4f, hv ? 0.85f : 0.4f, hv ? 0.2f : 0.4f);
                const int NC = 8; float cw = fr.w * 0.5f / NC, ah = fr.h * 0.62f;
                for (int c = 0; c < NC; c++) {
                    float hc = ah * (1.0f - (float)c / NC);
                    float xx = dir == 1 ? fr.x + fr.w * 0.25f + c * cw : fr.x + fr.w * 0.75f - (c + 1) * cw;
                    hRect(xx, fr.y + (fr.h - hc) / 2.0f, cw + 0.5f, hc, hay ? 1.0f : 0.3f, hay ? 1.0f : 0.3f, hay ? 0.85f : 0.3f);
                }
            }
            char pg[32]; std::snprintf(pg, sizeof pg, "PAGINA %d/%d", g_menuPag + 1, menuPaginas());
            hTexto(pg, (fbw - hAncho(pg, t)) / 2.0f, ys - 112 * k, t, 1, 0.9f, 0.5f);
        }
        for (int j = 0; j < (g_survival && g_pagina == 1 ? 0 : MENU_PAG); j++) {
            int i = g_menuPag * MENU_PAG + j;
            if (i >= (int)g_menuItems.size()) break;
            float sx, sy, sz;
            menuGeom(fbw, fbh, j, sx, sy, sz);
            hRect(sx - 6 * k, sy - 6 * k, sz + 12 * k, sz + 12 * k, 0.1f, 0.1f, 0.1f, 0.9f);
            const int ti2 = g_menuItems[i];
            hIcono(ti2, sx, sy, sz);
            hCantidad(ti2, sx, sy, sz, k, t);
            if (ti2 == hov) hMarco(sx - 6 * k, sy - 6 * k, sz + 12 * k, sz + 12 * k, std::max(3.0f, 4 * k), 1.0f, 0.85f, 0.2f);
            else if (ti2 == g_barra[g_sel]) hMarco(sx - 6 * k, sy - 6 * k, sz + 12 * k, sz + 12 * k, std::max(2.0f, 3 * k), 1, 1, 1);
            float np = t;
            while (np > 1 && hAncho(nombreSlot(ti2), np) > sz + 24 * k) np -= 1;
            hTexto(nombreSlot(ti2), sx + (sz - hAncho(nombreSlot(ti2), np)) / 2.0f, sy + sz + 10 * k, np, 1, 1, 1);
        }
        {   // la barra otra vez, encima del oscurecido, para arrastrar objetos a ella
            int hj = barraEn(g_mx, g_my);
            for (int i = 0; i < 9; i++) {
                float bx, by, bs;
                barraGeom(fbw, fbh, i, bx, by, bs);
                hRect(bx, by, bs, bs, 0.05f, 0.05f, 0.05f, 0.95f);
                if (g_barra[i] >= 0) { hIcono(g_barra[i], bx + 5 * k, by + 5 * k, bs - 10 * k); hCantidad(g_barra[i], bx + 5 * k, by + 5 * k, bs - 10 * k, k, t); }
                if (i == g_sel) hMarco(bx - 2 * k, by - 2 * k, bs + 4 * k, bs + 4 * k, std::max(2.0f, 3 * k), 1, 1, 1);
                else hMarco(bx, by, bs, bs, std::max(1.0f, k), 0.45f, 0.45f, 0.45f);
                if (i == hj && g_mano != -100) hMarco(bx - 3 * k, by - 3 * k, bs + 6 * k, bs + 6 * k, std::max(3.0f, 4 * k), 1.0f, 0.85f, 0.2f);
                char nb[4]; std::snprintf(nb, sizeof nb, "%d", i + 1);
                hTexto(nb, bx + 4 * k, by + 3 * k, std::max(2.0f, t - 1), 1, 1, 1);
            }
        }
        if (g_mano != -100) {   // el objeto agarrado sigue al cursor
            float cs = 48.0f * k;
            float cx = (float)(g_mx * fbw / std::max(1, g_winW)), cy = (float)(g_my * fbh / std::max(1, g_winH));
            hIcono(g_mano, cx - cs / 2, cy - cs / 2, cs);
        }
    } else {
        // mira
        float cx = fbw / 2.0f, cy = fbh / 2.0f, L = 9 * k, e = std::max(2.0f, 2 * k);
        hRect(cx - L, cy - e / 2, 2 * L, e, 1, 1, 1, 0.9f);
        hRect(cx - e / 2, cy - L, e, 2 * L, 1, 1, 1, 0.9f);
        if (g_romFrac > 0.02f) { float bw = 60 * k; hRect(cx - bw / 2, cy + L + 8 * k, bw, 6 * k, 0, 0, 0, 0.6f); hRect(cx - bw / 2, cy + L + 8 * k, bw * std::min(1.0f, g_romFrac), 6 * k, 1.0f, 0.85f, 0.2f, 0.95f); }
    }
#ifdef MOVIL
    tactilDibujar(fbw, fbh, t);
#endif
}


static void hBoton(const MRect& r, float k, float cr, float cg, float cb, bool hov, const char* txt, float px) {
    float m = hov ? 1.25f : 1.0f;
    hRect(r.x, r.y, r.w, r.h, std::min(1.0f, cr * m), std::min(1.0f, cg * m), std::min(1.0f, cb * m), 0.95f);
    hMarco(r.x, r.y, r.w, r.h, std::max(2.0f, 2 * k), hov ? 1.0f : 0.0f, hov ? 0.9f : 0.0f, hov ? 0.4f : 0.0f, hov ? 1.0f : 0.5f);
    hTexto(txt, r.x + (r.w - hAncho(txt, px)) / 2.0f, r.y + (r.h - 7 * px) / 2.0f, px, 1, 1, 1);
}
static void mundosDibujar(int fbw, int fbh) {
    MBase b = mundosBase(fbw, fbh);
    float k = b.k, t = std::max(2.0f, std::floor(2.0f * k + 0.5f));
    int tipoH, idxH;
    mundosHit(g_mx, g_my, tipoH, idxH);
    // fondo: franjas de "tierra" y cabecera
    hRect(0, 0, (float)fbw, 118 * k, 0.04f, 0.05f, 0.08f, 0.8f);
    float cuadro = 36 * k;
    for (int i = 0; i * cuadro < fbw; i++) {
        float h = 0.8f + 0.4f * hh(i, 3, 7);
        hRect(i * cuadro, fbh - cuadro, cuadro + 1, cuadro, 0.30f * h, 0.20f * h, 0.12f * h, 1.0f);
        hRect(i * cuadro, fbh - cuadro, cuadro + 1, cuadro * 0.3f, 0.26f * h, 0.52f * h, 0.17f * h, 1.0f);
    }
    float tp = std::max(4.0f, std::floor(6.0f * k));
    const char* ti = "JUEGO VOXEL";
    hTexto(ti, (fbw - hAncho(ti, tp)) / 2.0f, 18 * k, tp, 1.0f, 0.88f, 0.35f);
    const char* sub = "ELIGE TU MUNDO";
    hTexto(sub, (fbw - hAncho(sub, t)) / 2.0f, 18 * k + 9 * tp + 6 * k, t, 0.85f, 0.88f, 0.95f);
    hBoton(mRNuevo(b), k, 0.16f, 0.50f, 0.22f, tipoH == 0, "+ NUEVO MUNDO", t * 1.5f);
    hBoton(mRProf(b), k, g_nuevoProf ? 0.08f : 0.22f, g_nuevoProf ? 0.22f : 0.24f, g_nuevoProf ? 0.55f : 0.28f, tipoH == 5, g_nuevoProf ? "MARES PROFUNDOS: SI" : "MARES PROFUNDOS: NO", t * 0.95f);
    if (g_mundos.empty()) {
        const char* m = "TODAVIA NO HAY MUNDOS GUARDADOS";
        hTexto(m, (fbw - hAncho(m, t)) / 2.0f, b.yLista + 20 * k, t, 0.8f, 0.8f, 0.8f);
    }
    for (int v = 0; v < b.vis; v++) {
        int gi = g_scrollM + v;
        if (gi >= (int)g_mundos.size()) break;
        const InfoMundo& m = g_mundos[gi];
        MRect r = mRFila(b, v);
        bool hov = (tipoH == 1 && idxH == gi);
        bool conf = gi == g_confBorrar;
        hRect(r.x, r.y, r.w, r.h, conf ? 0.30f : (hov ? 0.20f : 0.10f), conf ? 0.08f : (hov ? 0.22f : 0.12f), conf ? 0.08f : (hov ? 0.30f : 0.17f), 0.92f);
        hMarco(r.x, r.y, r.w, r.h, std::max(2.0f, 2 * k), hov ? 1.0f : 0.3f, hov ? 0.85f : 0.3f, hov ? 0.3f : 0.35f, 1.0f);
        hIcono(gi % 2 ? PASTO : TIERRA, r.x + 10 * k, r.y + 10 * k, r.h - 20 * k);
        float tx = r.x + r.h + 6 * k;
        hTexto(m.nombre.c_str(), tx, r.y + 9 * k, t * 1.5f, 1, 1, 1);
        char inf[96];
        std::snprintf(inf, sizeof inf, "SEMILLA: %u  |  CHUNKS EDITADOS: %d", m.semilla, m.chunks);
        hTexto(inf, tx, r.y + r.h - 8 * k - 7 * t * 0.75f, std::max(1.0f, t * 0.75f), 0.7f, 0.75f, 0.85f);
        if (conf) {
            const char* q = "BORRAR MUNDO?";
            hTexto(q, tx + hAncho(m.nombre.c_str(), t * 1.5f) + 16 * k, r.y + 9 * k + 2 * k, t, 1.0f, 0.6f, 0.5f);
            hBoton(mRSi(b, v), k, 0.65f, 0.12f, 0.12f, tipoH == 3 && idxH == gi, "SI", t * 1.3f);
            hBoton(mRNo(b, v), k, 0.30f, 0.32f, 0.38f, tipoH == 4 && idxH == gi, "NO", t * 1.3f);
        } else {
            hBoton(mRBorrar(b, v), k, 0.45f, 0.14f, 0.14f, tipoH == 2 && idxH == gi, "BORRAR", t);
        }
    }
    if ((int)g_mundos.size() > b.vis) {
        char mm[64]; std::snprintf(mm, sizeof mm, "RUEDA: VER MAS (%d/%d)", g_scrollM + 1, (int)g_mundos.size() - b.vis + 1);
        hTexto(mm, b.x0, fbh - 36 * k - 26 * t, t, 0.9f, 0.9f, 0.9f);
    }
    const char* ay = "CLIC: JUGAR    N: NUEVO    P: MARES PROFUNDOS    ENTER: ULTIMO MUNDO    ESC: SALIR";
    hTexto(ay, (fbw - hAncho(ay, t)) / 2.0f, fbh - 36 * k - 12 * t, t, 1.0f, 1.0f, 1.0f);
}

static void fondoInicio(int fbw, int fbh, float k, float t, const char* sub) {
    hRect(0, 0, (float)fbw, 118 * k, 0.04f, 0.05f, 0.08f, 0.8f);
    float cuadro = 36 * k;
    for (int i = 0; i * cuadro < fbw; i++) {
        float h = 0.8f + 0.4f * hh(i, 3, 7);
        hRect(i * cuadro, fbh - cuadro, cuadro + 1, cuadro, 0.30f * h, 0.20f * h, 0.12f * h, 1.0f);
        hRect(i * cuadro, fbh - cuadro, cuadro + 1, cuadro * 0.3f, 0.26f * h, 0.52f * h, 0.17f * h, 1.0f);
    }
    float tp = std::max(4.0f, std::floor(6.0f * k));
    const char* ti = "JUEGO VOXEL";
    hTexto(ti, (fbw - hAncho(ti, tp)) / 2.0f, 18 * k, tp, 1.0f, 0.88f, 0.35f);
    hTexto(sub, (fbw - hAncho(sub, t)) / 2.0f, 18 * k + 9 * tp + 6 * k, t, 0.85f, 0.88f, 0.95f);
}
static void inicioDibujar(int fbw, int fbh) {
    float k = std::max(0.8f, fbh / 720.0f), t = std::max(2.0f, std::floor(2.0f * k + 0.5f));
    int hov = inicioHit(g_mx, g_my);
    if (g_pantalla == 2) {
        fondoInicio(fbw, fbh, k, t, "MENU PRINCIPAL");
        hBoton(tituloRect(fbw, fbh, 0), k, 0.16f, 0.50f, 0.22f, hov == 0, "CREAR MUNDO", t * 1.6f);
        hBoton(tituloRect(fbw, fbh, 1), k, 0.14f, 0.30f, 0.52f, hov == 1, "MIS MUNDOS", t * 1.6f);
        hBoton(tituloRect(fbw, fbh, 2), k, 0.38f, 0.24f, 0.55f, hov == 2, "MULTIJUGADOR", t * 1.6f);
        hBoton(tituloRect(fbw, fbh, 3), k, 0.45f, 0.14f, 0.14f, hov == 3, "SALIR", t * 1.6f);
        hBoton(tituloRect(fbw, fbh, 4), k, 0.30f, 0.30f, 0.12f, hov == 4, SUB == 2 ? "MUNDO SUB 2 NORMAL (CAMBIAR)" : "MUNDO SUB 1 AMPLIO (CAMBIAR)", t * 0.95f);
        if (!g_errorInicio.empty()) { MRect r4 = tituloRect(fbw, fbh, 4); hTexto(g_errorInicio.c_str(), r4.x, r4.y + r4.h + 8 * k, t, 1.0f, 0.5f, 0.45f); }
        return;
    }
    fondoInicio(fbw, fbh, k, t, "MULTIJUGADOR - PARTIDAS EN TU WIFI");
    MLBase b = lanBase(fbw, fbh);
    if (g_lan.empty()) {
        const char* m = g_conectando ? "CONECTANDO Y BAJANDO EL MUNDO..." : "BUSCANDO PARTIDAS EN LA RED...";
        hTexto(m, (fbw - hAncho(m, t)) / 2.0f, b.y0 + 14 * k, t, 0.85f, 0.85f, 0.85f);
        if (!g_conectando) { const char* m2 = "EL ANFITRION DEBE PULSAR ABRIR A LA RED WIFI EN SU PAUSA"; hTexto(m2, (fbw - hAncho(m2, t * 0.9f)) / 2.0f, b.y0 + 14 * k + 12 * t, t * 0.9f, 0.75f, 0.78f, 0.85f); }
    }
    for (int v = 0; v < b.vis; v++) {
        int gi = g_scrollL + v;
        if (gi >= (int)g_lan.size()) break;
        const LanSrv& l = g_lan[gi];
        MRect r = lanFila(b, v);
        bool h = hov == 12 + gi;
        hRect(r.x, r.y, r.w, r.h, h ? 0.20f : 0.10f, h ? 0.24f : 0.13f, h ? 0.32f : 0.18f, 0.92f);
        hMarco(r.x, r.y, r.w, r.h, std::max(2.0f, 2 * k), h ? 1.0f : 0.3f, h ? 0.85f : 0.3f, h ? 0.3f : 0.35f, 1.0f);
        hTexto(l.nombre.c_str(), r.x + 14 * k, r.y + 5 * k, t * 1.2f, 1, 1, 1);
        char inf[96]; std::snprintf(inf, sizeof inf, "%s:%d  |  JUGADORES: %d", l.ip.c_str(), l.puerto, l.jug);
        hTexto(inf, r.x + 14 * k, r.y + r.h - 6 * k - 7 * t * 0.8f, std::max(1.0f, t * 0.8f), 0.7f, 0.8f, 0.9f);
        hTexto("UNIRSE", r.x + r.w - hAncho("UNIRSE", t * 1.2f) - 14 * k, r.y + (r.h - 7 * t * 1.2f) / 2.0f, t * 1.2f, 0.4f, 1.0f, 0.5f);
    }
    MRect c = lanCampo(b, fbh);
    hTexto("O ESCRIBE LA IP DEL ANFITRION (EJ 192.168.1.5):", c.x, c.y - 14 * k - 7 * t * 0.9f, std::max(1.0f, t * 0.9f), 0.85f, 0.88f, 0.95f);
    hRect(c.x, c.y, c.w, c.h, 0.05f, 0.06f, 0.09f, 0.95f);
    hMarco(c.x, c.y, c.w, c.h, std::max(2.0f, 2 * k), 0.8f, 0.8f, 0.4f, 1.0f);
    std::string ip = g_ipTexto + (((int)(glfwGetTime() * 2)) % 2 ? "_" : "");
    hTexto(ip.c_str(), c.x + 10 * k, c.y + (c.h - 7 * t * 1.3f) / 2.0f, t * 1.3f, 1, 1, 1);
    hBoton(lanUnir(b, fbh), k, 0.16f, 0.50f, 0.22f, hov == 11, "UNIRSE", t * 1.4f);
    hBoton(lanVolver(b, fbh), k, 0.30f, 0.32f, 0.38f, hov == 10, "VOLVER", t * 1.3f);
    if (!g_errorInicio.empty()) hTexto(g_errorInicio.c_str(), b.x0 + b.w * 0.32f + 14 * k, fbh - 108 * k + 14 * k, t, 1.0f, 0.5f, 0.45f);
}

// ---- dormir en la cama (v84): la pantalla se oscurece, pasa la noche y amanece a las 6 AM
static double g_suenoT0 = -1; static bool g_suenoHecho = false;
static bool esDeNoche() { return g_hora >= 0.54; }       // desde las 19:00 hasta las 6:00 (g_hora 0 = 6 AM)
static bool esSolidoMob(int x, int y, int z) { uint8_t t = leerCubito(x, y, z); return t != AIRE && !esAgua(t) && t != LAVA; }
static void suenoTick(double ahora) {
    if (g_suenoT0 < 0) return;
    const double t = ahora - g_suenoT0;
    if (t >= 1.2 && !g_suenoHecho) { g_suenoHecho = true; g_hora = 0.0; g_vida = std::min(20.0f, g_vida + 6.0f); }
    if (t > 2.8) g_suenoT0 = -1;
}
static void suenoDibujar(int fbw, int fbh, double ahora) {
    if (g_suenoT0 < 0) return;
    const double t = ahora - g_suenoT0;
    float a = t < 0.8 ? (float)(t / 0.8) : (t < 2.0 ? 1.0f : (float)(1.0 - (t - 2.0) / 0.8));
    a = std::max(0.0f, std::min(1.0f, a));
    hRect(0, 0, (float)fbw, (float)fbh, 0.01f, 0.01f, 0.03f, a);
}
static void fuegoDibujar(int fbw, int fbh, double ahora) {
    if (g_ardeHasta > ahora) { const float f = 0.09f + 0.04f * std::sin((float)ahora * 18.0f); hRect(0, 0, (float)fbw, (float)fbh, 1.0f, 0.35f, 0.0f, f); const char* q = "TE QUEMAS - BUSCA AGUA"; hTexto(q, (fbw - hAncho(q, 3)) / 2.0f, fbh * 0.2f, 3, 1.0f, 0.7f, 0.2f); }
    if (g_suenoT0 < 0) return;
    const double t = ahora - g_suenoT0;
    float a = t < 0.8 ? (float)(t / 0.8) : (t < 2.0 ? 1.0f : (float)(1.0 - (t - 2.0) / 0.8));
    a = std::max(0.0f, std::min(1.0f, a));
    if (a > 0.6f) { const char* z = g_suenoHecho ? "BUENOS DIAS" : "ZZZ..."; float px = std::max(3.0f, fbh / 120.0f); hTexto(z, (fbw - hAncho(z, px)) / 2.0f, fbh * 0.45f, px, 0.9f, 0.9f, 1.0f); }
}

static void pausaDibujar(int fbw, int fbh) {
    float k = std::max(0.8f, fbh / 720.0f), t = std::max(2.0f, std::floor(2.0f * k + 0.5f));
    hRect(0, 0, (float)fbw, (float)fbh, 0, 0, 0, 0.58f);
    const char* ti = "PAUSA";
    float tp = std::max(4.0f, std::floor(6.0f * k));
    hTexto(ti, (fbw - hAncho(ti, tp)) / 2.0f, fbh * 0.28f - 9 * tp, tp, 1.0f, 0.88f, 0.35f);
    int hov = pausaHit(g_mx, g_my);
    if (g_pausaSub) {   // ajustes de graficos
        static const char* NN[3] = {"NUBES: APAGADAS", "NUBES: SIMPLES", "NUBES: VOLUMETRICAS"};
        static const char* NA[3] = {"AGUA: SIMPLE", "AGUA: MEJORADA", "AGUA: MEJORADA + OLAS"};
        hBoton(pausaRect(fbw, fbh, 0), k, g_nubes ? 0.16f : 0.45f, g_nubes ? 0.36f : 0.20f, g_nubes ? 0.60f : 0.20f, hov == 0, NN[g_nubes], t * 1.25f);
        hBoton(pausaRect(fbw, fbh, 1), k, g_agua ? 0.16f : 0.45f, g_agua ? 0.36f : 0.20f, g_agua ? 0.60f : 0.20f, hov == 1, NA[g_agua], t * 1.25f);
        hBoton(pausaRect(fbw, fbh, 2), k, g_cielo ? 0.16f : 0.45f, g_cielo ? 0.36f : 0.20f, g_cielo ? 0.60f : 0.20f, hov == 2,
               g_cielo ? "CIELO: ATMOSFERA" : "CIELO: SIMPLE", t * 1.25f);
        hBoton(pausaRect(fbw, fbh, 3), k, g_viento ? 0.16f : 0.45f, g_viento ? 0.36f : 0.20f, g_viento ? 0.60f : 0.20f, hov == 3,
               g_viento ? "VIENTO EN HOJAS: ON" : "VIENTO EN HOJAS: OFF", t * 1.25f);
        hBoton(pausaRect(fbw, fbh, 4), k, g_graf ? 0.16f : 0.45f, g_graf ? 0.36f : 0.20f, g_graf ? 0.60f : 0.20f, hov == 4,
               g_graf ? "SOMBRAS Y EFECTOS: ON" : "SOMBRAS Y EFECTOS: OFF", t * 1.25f);
        hBoton(pausaRect(fbw, fbh, 5), k, g_espAtard ? 0.16f : 0.45f, g_espAtard ? 0.36f : 0.20f, g_espAtard ? 0.60f : 0.20f, hov == 5,
               g_espAtard ? "ATARDECER EN EL ESPACIO: SI" : "ATARDECER EN EL ESPACIO: NO", t * 1.25f);
        hBoton(pausaRect(fbw, fbh, 6), k, g_horReal ? 0.16f : 0.45f, g_horReal ? 0.36f : 0.20f, g_horReal ? 0.60f : 0.20f, hov == 6,
               g_horReal ? "HORIZONTE REAL: ON" : "HORIZONTE REAL: OFF", t * 1.25f);
        hBoton(pausaRect(fbw, fbh, 7), k, 0.16f, 0.50f, 0.22f, hov == 7, "VOLVER", t * 1.5f);
        const char* ay2 = "LAS NUBES VOLUMETRICAS NECESITAN SOMBRAS Y EFECTOS ON";
        hTexto(ay2, (fbw - hAncho(ay2, t)) / 2.0f, pausaRect(fbw, fbh, 9).y, t, 0.8f, 0.8f, 0.85f);
        return;
    }
    hBoton(pausaRect(fbw, fbh, 0), k, 0.16f, 0.50f, 0.22f, hov == 0, "CONTINUAR", t * 1.5f);
    hBoton(pausaRect(fbw, fbh, 1), k, g_lodOn ? 0.16f : 0.45f, g_lodOn ? 0.36f : 0.20f, g_lodOn ? 0.60f : 0.20f, hov == 1,
           g_lodOn ? "MUNDO LEJANO: ON" : "MUNDO LEJANO: OFF", t * 1.25f);
    hBoton(pausaRect(fbw, fbh, 2), k, g_anclaOn ? 0.16f : 0.45f, g_anclaOn ? 0.36f : 0.20f, g_anclaOn ? 0.60f : 0.20f, hov == 2,
           g_anclaOn ? "CAMBIOS LEJANOS: ON" : "CAMBIOS LEJANOS: OFF", t * 1.25f);
    hBoton(graficosRect(fbw, fbh), k, g_graf ? 0.16f : 0.45f, g_graf ? 0.36f : 0.20f, g_graf ? 0.60f : 0.20f, hov == 3,
           g_graf ? "GRAFICOS: ON" : "GRAFICOS: OFF", t * 1.25f);
    hBoton(ajustesRect(fbw, fbh), k, 0.30f, 0.26f, 0.50f, hov == 10, "AJUSTES", t * 1.1f);
    hBoton(pausaRect(fbw, fbh, 4), k, 0.45f, 0.32f, 0.10f, hov == 4, g_survival ? "MODO: SURVIVAL" : "MODO: CREATIVO", t * 1.25f);
    {
        static const char* NP[3] = {"RENDIMIENTO: NORMAL", "RENDIMIENTO: ALTO", "RENDIMIENTO: MAXIMO"};
        hBoton(pausaRect(fbw, fbh, 5), k, 0.30f + 0.20f * g_perf, 0.22f, 0.45f - 0.10f * g_perf, hov == 5, NP[g_perf], t * 1.25f);
    }
    hBoton(pausaRect(fbw, fbh, 6), k, g_conservar ? 0.16f : 0.45f, g_conservar ? 0.36f : 0.20f, g_conservar ? 0.60f : 0.20f, hov == 6,
           g_conservar ? "CONSERVAR CHUNKS: SI" : "CONSERVAR CHUNKS: NO", t * 1.25f);
    {
        bool rojo = g_red.activo();
        hBoton(pausaRect(fbw, fbh, 7), k, rojo ? 0.10f : 0.12f, rojo ? 0.45f : 0.34f, rojo ? 0.30f : 0.50f, hov == 7,
               g_red.host ? "RED WIFI: ABIERTA" : (g_red.cli ? "RED: CONECTADO" : "ABRIR A LA RED WIFI"), t * 1.25f);
    }
    hBoton(pausaRect(fbw, fbh, 8), k, 0.45f, 0.14f, 0.14f, hov == 8, "GUARDAR Y SALIR", t * 1.5f);
    const char* ay = "ESC: CONTINUAR";
    hTexto(ay, (fbw - hAncho(ay, t)) / 2.0f, pausaRect(fbw, fbh, 9).y, t, 0.8f, 0.8f, 0.85f);
}

// ---------------------------------------------------------------- principal
static void leerArgs(int argc, char** argv) {   // --hospedar [puerto]   --unir IP[:puerto]   --nombre X
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--hospedar") { g_argHost = true; if (i + 1 < argc && argv[i + 1][0] != '-') g_argPuerto = std::atoi(argv[++i]); }
        else if (a == "--unir" && i + 1 < argc) g_argUnir = argv[++i];
        else if (a == "--nombre" && i + 1 < argc) g_argNombre = argv[++i];
    }
    if (const char* e = std::getenv("RED_UNIR")) g_argUnir = e;
    if (std::getenv("RED_HOST")) g_argHost = true;
    if (const char* e = std::getenv("RED_NOMBRE")) g_argNombre = e;
    if (const char* e = std::getenv("RED_PUERTO")) g_argPuerto = std::atoi(e);
}
// ---------------------------------------------------------------- prueba de rendimiento: ciudad de luces (F9)
// Construye delante de ti, a escala de cubitos, 16 edificios de 20x20 bloques (de 20 a 56 de alto) con pisos, ventanas, miles de luces LED
// de 12 colores en los techos, postes de luz y antorchas en las calles. Cuando termina de mallarse, mide los FPS girando la camara
// 20 segundos y escribe el resultado en rendimiento.txt (y lo muestra en pantalla).
struct Bench { std::vector<ChunkP> chunks; int estado = 0; double t0 = 0, tFase = 0, peorCarga = 0; std::vector<float> dts; int nLed = 0, nCub = 0, nChunks = 0; } g_bench;
static void construirCiudad() {
    std::unordered_set<Chunk*> sucios, editados;
    long nLed = 0, nCub = 0, omitidos = 0;
    auto poner = [&](int vx, int vy, int vz, uint8_t t) {
        int cy = divPiso(vy, CS);
        if (cy < 0 || cy >= HYC) return;
        int cx = divPiso(vx, CS), cz = divPiso(vz, CS);
        ChunkP c = buscar(cx, cy, cz);
        if (!c || c->state.load() < ST_READY) { omitidos++; return; }
        if (!c->dat.d) {
            if (c->dat.uni == t) return;
            c->dat.d = std::make_shared<std::vector<uint8_t>>((size_t)CS * CS * CS, c->dat.uni);
        }
        int lx = vx - cx * CS, ly = vy - cy * CS, lz = vz - cz * CS;
        (*c->dat.d)[IDX(lx, ly, lz)] = t;
        nCub++; if (t >= LED_ROJO && t <= LED_BLANCO) nLed++;
        if (editados.insert(c.get()).second) { c->sucioDisco = true; g_editCols.insert(colKey(cx, cz)); }   // SIEMPRE se marca para guardar (antes un chunk anotado antes como vecino no se guardaba y la ciudad desaparecia al volver)
        sucios.insert(c.get());
        if (lx == 0) { ChunkP k = buscar(cx - 1, cy, cz); if (k) sucios.insert(k.get()); }
        if (lx == CS - 1) { ChunkP k = buscar(cx + 1, cy, cz); if (k) sucios.insert(k.get()); }
        if (ly == 0) { ChunkP k = buscar(cx, cy - 1, cz); if (k) sucios.insert(k.get()); }
        if (ly == CS - 1) { ChunkP k = buscar(cx, cy + 1, cz); if (k) sucios.insert(k.get()); }
        if (lz == 0) { ChunkP k = buscar(cx, cy, cz - 1); if (k) sucios.insert(k.get()); }
        if (lz == CS - 1) { ChunkP k = buscar(cx, cy, cz + 1); if (k) sucios.insert(k.get()); }
    };
    // centro: 70 bloques delante de ti
    const double fx = std::sin(g_j.yaw), fz = -std::cos(g_j.yaw);
    const int TAM = 40, CALLE = 20, NB = 4;                       // en cubitos: edificio 40 (=20 bloques), calle 20 (=10 bloques)
    const int LADO = NB * TAM + (NB - 1) * CALLE;                 // 220 cubitos (110 bloques)
    const int cxm = (int)std::floor((g_j.p.x + fx * 70.0) * SUB), czm = (int)std::floor((g_j.p.z + fz * 70.0) * SUB);
    const int x0 = cxm - LADO / 2, z0 = czm - LADO / 2;
    // suelo plano: sobre el punto mas alto del terreno de la zona
    int gy = 0;
    for (int a = 0; a <= 8; a++) for (int b = 0; b <= 8; b++)
        gy = std::max(gy, (int)std::floor(alturaTerreno((x0 + LADO * a / 8.0) / SUB, (z0 + LADO * b / 8.0) / SUB) * SUB));
    gy += 2;
    for (int z = z0; z < z0 + LADO; z++) for (int x = x0; x < x0 + LADO; x++) {
        int th = (int)std::floor(alturaTerreno((x + 0.5) / SUB, (z + 0.5) / SUB) * SUB);
        for (int y = std::max(0, th - 1); y <= gy; y++) poner(x, y, z, PIEDRA);
    }
    for (int bi = 0; bi < NB; bi++) for (int bj = 0; bj < NB; bj++) {
        const int bx = x0 + bi * (TAM + CALLE), bz = z0 + bj * (TAM + CALLE);
        const int alto = (20 + 12 * ((bi * 3 + bj * 5) % 4)) * SUB;      // 20, 32, 44 o 56 bloques
        const int piso = 6 * SUB;                                       // piso cada 6 bloques
        for (int y = 1; y <= alto; y++) {
            const int ly = y % piso;
            const bool ventana = ly >= piso / 3 && ly <= 2 * piso / 3;
            for (int z = 0; z < TAM; z++) for (int x = 0; x < TAM; x++) {
                const bool borde = x < 2 || z < 2 || x >= TAM - 2 || z >= TAM - 2;
                uint8_t t = AIRE;
                if (borde) {
                    t = (ventana && ((x + z) / 2) % 3 != 0) ? VIDRIO : ((y / piso) % 2 ? PIEDRA : MADERA);
                    if (bj == 0 || true) { if (z < 2 && x >= TAM / 2 - 3 && x < TAM / 2 + 3 && y < 10 * SUB) t = AIRE; }      // puerta
                }
                else if (ly == 0 || y == alto) t = PIEDRA;                                       // piso / techo
                else if (ly == piso - 1) {                                                       // luces de colores en el techo de cada piso
                    if (x % 6 == 3 && z % 6 == 3) t = (uint8_t)(LED_ROJO + ((x / 6 + z / 6 + y / piso + bi + bj) % 12));
                }
                else if ((x == TAM / 2 || x == TAM / 2 + 1) && (z == TAM / 2 || z == TAM / 2 + 1)) t = PIEDRA;   // pilar central
                if (t != AIRE || (!borde && ly != 0)) poner(bx + x, gy + y, bz + z, t);
            }
        }
        // azotea: antenas con luces
        for (int k = 0; k < 6; k++) { poner(bx + 4 + k * 6, gy + alto + 1, bz + 4, (uint8_t)(LED_ROJO + (bi * 4 + bj + k) % 12)); poner(bx + 4 + k * 6, gy + alto + 2, bz + TAM - 5, (uint8_t)(LED_ROJO + (bi + bj * 4 + k) % 12)); }
    }
    // calles: postes de luz de colores y antorchas
    for (int a = 0; a < LADO; a += 10) for (int c = 0; c < NB - 1; c++) {
        const int off = (c + 1) * TAM + c * CALLE + CALLE / 2;
        for (int h = 1; h <= 10; h++) { poner(x0 + off, gy + h, z0 + a, h == 10 ? (uint8_t)(LED_ROJO + (a / 10 + c) % 12) : PIEDRA); poner(x0 + a, gy + h, z0 + off, h == 10 ? (uint8_t)(LED_ROJO + (a / 10 + c + 5) % 12) : PIEDRA); }
        poner(x0 + off + 2, gy + 2, z0 + a, ANTORCHA); poner(x0 + a, gy + 2, z0 + off + 2, ANTORCHA);
    }
    g_bench = Bench();
    for (Chunk* c : sucios) { ChunkP cp = buscar(c->cx, c->cy, c->cz); if (cp) { remallarAsync(cp); g_bench.chunks.push_back(cp); } }
    g_bench.estado = 1; g_bench.t0 = glfwGetTime(); g_bench.nLed = (int)nLed; g_bench.nCub = (int)nCub; g_bench.nChunks = (int)sucios.size();
    char b[160];
    std::snprintf(b, sizeof b, "CIUDAD DE LUCES: %ld LED, %ld CUBITOS, %d CHUNKS. ESPERANDO MALLAS...", nLed, nCub, (int)sucios.size());
    if (omitidos > 0) { std::snprintf(b, sizeof b, "CIUDAD INCOMPLETA: %ld CUBITOS EN CHUNKS SIN CARGAR. ESPERA A QUE CARGUE EL MUNDO Y PULSA F9 DE NUEVO", omitidos); }
    g_aviso = b; g_avisoT = glfwGetTime();
    std::printf("%s | centro bloques x=%.1f z=%.1f | chunks marcados para guardar %d | cubitos omitidos (chunk sin cargar) %ld\n", b, cxm / (double)SUB, czm / (double)SUB, (int)editados.size(), omitidos);
}
// ---------------------------------------------------------------- cohete de dos etapas (comando /cohete)
// Escala: 1 cubito = ~0.6 m (110 cubitos de alto = ~66 m). La forma del cohete esta en cohete.h (cohGeom).
// Junto al cohete se levanta una torre con 4 tramos de escaleras en zigzag (77 escalones de 1 cubito) hasta la puerta de la cabina,
// que esta en la etapa 2 (para que la cabina no se suelte con la etapa 1).
static void construirCohete() {
    if (g_ck.activo) { aviso("YA ESTAS VOLANDO EN EL COHETE"); return; }
    std::unordered_set<Chunk*> sucios, editados;
    long nCub = 0, omitidos = 0;
    auto poner = [&](int vx, int vy, int vz, uint8_t t) {
        int cy = divPiso(vy, CS);
        if (cy < 0 || cy >= HYC) return;
        int cx = divPiso(vx, CS), cz = divPiso(vz, CS);
        ChunkP c = buscar(cx, cy, cz);
        if (!c || c->state.load() < ST_READY) { omitidos++; return; }
        if (!c->dat.d) {
            if (c->dat.uni == t) return;
            c->dat.d = std::make_shared<std::vector<uint8_t>>((size_t)CS * CS * CS, c->dat.uni);
        }
        int lx = vx - cx * CS, ly = vy - cy * CS, lz = vz - cz * CS;
        if ((*c->dat.d)[IDX(lx, ly, lz)] == t) return;
        (*c->dat.d)[IDX(lx, ly, lz)] = t;
        nCub++;
        if (editados.insert(c.get()).second) { c->sucioDisco = true; g_editCols.insert(colKey(cx, cz)); }
        sucios.insert(c.get());
        if (lx == 0) { ChunkP k = buscar(cx - 1, cy, cz); if (k) sucios.insert(k.get()); }
        if (lx == CS - 1) { ChunkP k = buscar(cx + 1, cy, cz); if (k) sucios.insert(k.get()); }
        if (ly == 0) { ChunkP k = buscar(cx, cy - 1, cz); if (k) sucios.insert(k.get()); }
        if (ly == CS - 1) { ChunkP k = buscar(cx, cy + 1, cz); if (k) sucios.insert(k.get()); }
        if (lz == 0) { ChunkP k = buscar(cx, cy, cz - 1); if (k) sucios.insert(k.get()); }
        if (lz == CS - 1) { ChunkP k = buscar(cx, cy, cz + 1); if (k) sucios.insert(k.get()); }
    };
    const double fx = std::sin(g_j.yaw), fz = -std::cos(g_j.yaw);
    int ax, az;                                   // direccion de la puerta y de la torre (hacia ti)
    if (std::fabs(fx) > std::fabs(fz)) { ax = fx > 0 ? -1 : 1; az = 0; } else { ax = 0; az = fz > 0 ? -1 : 1; }
    const int cxm = (int)std::floor((g_j.p.x + fx * 40.0) * SUB), czm = (int)std::floor((g_j.p.z + fz * 40.0) * SUB);
    // (u = hacia fuera por el eje de la puerta, v = lado)
    auto X = [&](int u, int v) { return cxm + ax * u - az * v; };
    auto Z = [&](int u, int v) { return czm + az * u + ax * v; };
    auto terr = [&](int x, int z) { return (int)std::floor(alturaTerreno((x + 0.5) / SUB, (z + 0.5) / SUB) * SUB); };
    int gy = 0;
    for (int a = -12; a <= 60; a += 4) for (int b = -14; b <= 16; b += 4) gy = std::max(gy, terr(X(a, b), Z(a, b)));
    gy += 2;
    if ((gy + 1 + CK_H + 4) / CS >= HYC) {
        g_aviso = "AQUI NO HAY ALTURA PARA EL COHETE: PRUEBA EN UN LUGAR MAS BAJO"; g_avisoT = glfwGetTime();
        return;
    }
    auto yh = [&](int h) { return gy + 1 + h; };
    // plataforma de lanzamiento (circulo de radio 13) y limpieza del aire encima
    for (int dz = -13; dz <= 13; dz++) for (int dx = -13; dx <= 13; dx++) {
        if (dx * dx + dz * dz > 13 * 13) continue;
        int x = cxm + dx, z = czm + dz;
        for (int y = std::max(0, terr(x, z) - 1); y <= gy; y++) poner(x, y, z, PIEDRA);
        for (int h = 0; h <= CK_H + 2; h++) if (leerCubito(x, yh(h), z) != AIRE) poner(x, yh(h), z, AIRE);
    }
    // limpiar el aire de la torre
    for (int u = 3; u <= 56; u++) for (int v = -4; v <= 12; v++) {
        int x = X(u, v), z = Z(u, v);
        for (int h = 0; h <= CK_FLOOR + 12; h++) if (leerCubito(x, yh(h), z) != AIRE) poner(x, yh(h), z, AIRE);
    }
    // ---- torre de escaleras
    const int FL = CK_FLOOR;
    auto Hs = [&](int j) { return FL - 78 + j; };            // altura del cubito de arriba del escalon j (1..77)
    auto losa = [&](int u, int v, int h, uint8_t t) { poner(X(u, v), yh(h), Z(u, v), t); poner(X(u, v), yh(h - 1), Z(u, v), t); };
    auto baranda = [&](int u, int v, int h) { poner(X(u, v), yh(h + 1), Z(u, v), ALUMINIO); poner(X(u, v), yh(h + 2), Z(u, v), ALUMINIO); };
    auto columna = [&](int u, int v, int hTop) {
        int x = X(u, v), z = Z(u, v);
        for (int y = std::max(0, terr(x, z) - 1); y <= yh(hTop - 2); y++) poner(x, y, z, TITANIO);
    };
    auto vuelo = [&](int j0, int j1, int u0, int dir, int v0, int v1) {     // escalones j0..j1; dir +1 se aleja del cohete, -1 se acerca
        for (int j = j0; j <= j1; j++) {
            int k = j - j0, h = Hs(j);
            for (int m = 0; m < 2; m++) {
                int u = u0 + dir * (2 * k + m);
                for (int v = v0; v <= v1; v++) losa(u, v, h, (j & 1) ? PIEDRA : ARCILLA);
                baranda(u, v0, h); baranda(u, v1, h);
                if (m == 0 && (k % 6 == 0 || j == j1)) { columna(u, v0, h); columna(u, v1, h); }
            }
        }
    };
    auto descanso = [&](int u0, int u1, int h, bool bV0, bool bV1, bool bU0, bool bU1, int uCerrado) {
        for (int u = u0; u <= u1; u++) for (int v = -3; v <= 11; v++) losa(u, v, h, PIEDRA);
        for (int u = u0; u <= u1; u++) { if (bV0) baranda(u, -3, h); if (bV1) baranda(u, 11, h); }
        for (int v = -3; v <= 11; v++) { if (bU0) baranda(u0, v, h); if (bU1 && v <= uCerrado) baranda(u1, v, h); }
        for (int u : {u0, (u0 + u1) / 2, u1}) for (int v : {-3, 4, 11}) columna(u, v, h);
    };
    vuelo(1, 19, 12, +1, -3, 3);                       // A
    descanso(50, 56, Hs(19), true, true, false, true, 99);
    vuelo(20, 38, 49, -1, 5, 11);                      // B
    descanso(6, 11, Hs(38), true, true, true, false, 99);
    vuelo(39, 57, 12, +1, -3, 3);                      // C
    descanso(50, 56, Hs(57), true, true, false, true, 99);
    vuelo(58, 77, 49, -1, 5, 11);                      // D
    descanso(3, 9, FL, true, true, true, true, 4);     // puerta de la cabina
    // ---- el cohete
    {
        std::vector<CCel> cel;
        cohGeom(cel);
        const int cfx = -ax, cfz = -az, crx = -cfz, crz = cfx;
        for (auto& c : cel) poner(cxm + c.x * crx + c.z * cfx, yh(c.y), czm + c.x * crz + c.z * cfz, c.t);
    }
    g_cohInfo = {true, cxm, czm, gy, ax, az};
    ckGuardarInfo();
    for (Chunk* c : sucios) { ChunkP cp = buscar(c->cx, c->cy, c->cz); if (cp) remallarAsync(cp); }
    if (const char* cv = std::getenv("COHETE_VER")) {            // (pruebas) 1: en el descanso de la puerta, 2: dentro de la cabina, 3: al pie de la torre
        const int m = std::atoi(cv);
        if (m == 3) g_j.p = {(X(20, 0) + 0.5) / SUB, (gy + 1) / (double)SUB + 0.1, (Z(20, 0) + 0.5) / SUB};
        else if (m == 1) g_j.p = {(X(7, 0) + 0.5) / SUB, (yh(FL) + 1) / (double)SUB + 0.05, (Z(7, 0) + 0.5) / SUB};
        else g_j.p = {(cxm + 0.5) / SUB, (yh(FL) + 1) / (double)SUB + 0.05, (czm + 0.5) / SUB};
        g_j.yaw = std::atan2((double)-ax, (double)az); g_j.pitch = 0.0; g_j.v = {0, 0, 0};
        if (m == 3) { g_j.yaw += 3.14159265; g_j.pitch = 0.5; }
        if (m == 9) if (const char* cp = std::getenv("COHETE_POS")) {      // (pruebas) COHETE_POS=u,v,h,giro(grados),cabeceo(grados)
            double u, v, h, gi, ca; if (std::sscanf(cp, "%lf,%lf,%lf,%lf,%lf", &u, &v, &h, &gi, &ca) == 5) {
                g_j.p = {(X((int)u, (int)v) + 0.5) / SUB, (yh((int)h) + 0.5) / (double)SUB, (Z((int)u, (int)v) + 0.5) / SUB};
                g_j.yaw = std::atan2((double)-ax, (double)az) + gi * 3.14159265 / 180.0; g_j.pitch = ca * 3.14159265 / 180.0; g_j.volar = true;
            }
        }
    }
    char b[200];
    if (omitidos > 0) std::snprintf(b, sizeof b, "COHETE INCOMPLETO: %ld CUBITOS EN CHUNKS SIN CARGAR. ESPERA A QUE CARGUE EL MUNDO Y ESCRIBE /COHETE DE NUEVO", omitidos);
    else std::snprintf(b, sizeof b, "COHETE LISTO: SUBE POR LA TORRE HASTA LA PUERTA, ENTRA A LA CABINA Y PULSA L");
    g_aviso = b; g_avisoT = glfwGetTime();
    std::printf("%s | centro bloques x=%.1f z=%.1f gy=%d | %ld cubitos, %d chunks, omitidos %ld\n", b, cxm / (double)SUB, czm / (double)SUB, gy, nCub, (int)sucios.size(), omitidos);
}
// llamado cada cuadro con el tiempo real entre cuadros
static void benchTick(double dtReal, double ahora) {
    if (g_bench.estado == 1) {          // esperando a que se malle y se ilumine todo; se anota el peor tiron
        g_bench.peorCarga = std::max(g_bench.peorCarga, dtReal);
        bool listo = g_meshPend.load() == 0 && g_genPend.load() == 0;
        for (const ChunkP& c : g_bench.chunks) if (c->state.load() != ST_MESHED) { listo = false; break; }      // todos los chunks de la ciudad ya con su malla y su luz
        if (ahora - g_bench.t0 > 3.0 && listo) {
            g_bench.estado = 2; g_bench.tFase = ahora; g_bench.dts.clear();
            char b[120]; std::snprintf(b, sizeof b, "MALLAS LISTAS EN %.1f S. MIDIENDO 20 S (NO TE MUEVAS)...", ahora - g_bench.t0);
            g_aviso = b; g_avisoT = ahora;
            g_bench.t0 = ahora - g_bench.t0;     // t0 pasa a ser el tiempo de carga
        } else if (ahora - g_bench.t0 > 120.0) { g_bench.estado = 0; g_aviso = "PRUEBA CANCELADA (LAS MALLAS TARDARON MAS DE 2 MIN)"; g_avisoT = ahora; }
    } else if (g_bench.estado == 2) {
        g_bench.dts.push_back((float)dtReal);
        static const bool sinGiro = std::getenv("PRUEBA_CIUDAD") && std::getenv("PRUEBA_CIUDAD")[0] == '2';
        if (!sinGiro) g_j.yaw += (float)(6.2831853 / 20.0 * dtReal);          // giro completo en 20 s
        g_avisoT = ahora;
        if (ahora - g_bench.tFase >= 20.0) {
            std::vector<float> v = g_bench.dts;
            std::sort(v.begin(), v.end());
            double suma = 0; for (float d : v) suma += d;
            double prom = v.empty() ? 0 : v.size() / suma;
            double p99 = v.empty() ? 0 : 1.0 / v[std::min(v.size() - 1, (size_t)(v.size() * 0.99))];
            double peor = v.empty() ? 0 : v.back() * 1000.0;
            char b[300];
            std::snprintf(b, sizeof b, "RESULTADO: %.0f FPS PROMEDIO | 1%% PEORES: %.0f FPS | PEOR CUADRO: %.1f MS | MALLAS: %.1f S", prom, p99, peor, g_bench.t0);
            g_aviso = b; g_avisoT = ahora;
            FILE* f = std::fopen("rendimiento.txt", "a");
            if (f) {
                std::fprintf(f, "SUB=%d | %d LED, %d cubitos, %d chunks | FPS promedio %.1f | 1%% peores %.1f | peor cuadro %.1f ms | peor tiron al construir %.1f ms | mallado %.1f s\n",
                             SUB, g_bench.nLed, g_bench.nCub, g_bench.nChunks, prom, p99, peor, g_bench.peorCarga * 1000.0, g_bench.t0);
                std::fclose(f);
            }
            std::printf("%s\n", b);
            g_bench.estado = 0;
        }
    }
}

// se conecta al anfitrion (ip o ip:puerto) y baja su mundo; deja g_dir/g_semilla listos
static bool unirseAMundo(const std::string& dest) {
    std::string ip = dest; int puerto = RED_PUERTO;
    size_t dp = ip.rfind(':');
    if (dp != std::string::npos) { puerto = std::atoi(ip.c_str() + dp + 1); ip = ip.substr(0, dp); }
    std::string safe = ip; for (char& ch : safe) if (!std::isalnum((unsigned char)ch)) ch = '_';
    std::printf("RED: conectando a %s:%d ...\n", ip.c_str(), puerto);
    std::fflush(stdout);
    g_dir = std::string(RAIZ_MUNDOS) + "/red_" + safe;
    if (!g_red.unirse(ip, puerto, g_dir)) return false;
    g_semilla = g_red.semilla; g_nombreMundo = "red " + ip; g_argUnir = dest;
    return true;
}

static int juego_main() {
    if (!glfwInit()) { std::fprintf(stderr, "No se pudo iniciar GLFW\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow* ventana = glfwCreateWindow(1280, 720, "Juego Voxel", nullptr, nullptr);
    if (!ventana) { std::fprintf(stderr, "No se pudo crear la ventana (necesitas OpenGL 3.3)\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(ventana);
    glfwSwapInterval(1);
    if (!cargarGL()) return 1;
    std::printf("GPU: %s | OpenGL %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    std::printf("SUB=%d (cada bloque = %d cubitos) | chunk %d | vision %d bloques\n", SUB, SUB * SUB * SUB, CS, VIEW_BLOCKS);

    glfwSetCursorPosCallback(ventana, alMouse);
    glfwSetScrollCallback(ventana, alRueda);
    glfwSetKeyCallback(ventana, alTecla);
    glfwSetCharCallback(ventana, alChar);
    glfwSetMouseButtonCallback(ventana, alClic);
    migrarMundoViejo();
    cargarOpciones();
    bool redSpawn = false;
    if (!g_argNombre.empty()) g_red.miNombre = g_argNombre;
    if (!g_argUnir.empty()) {       // unirse a la partida de otro jugador: se baja su mundo y se salta el menu
        if (!unirseAMundo(g_argUnir)) {
            std::printf("RED: %s\n", g_red.error.c_str());
            glfwTerminate();
            return 1;
        }
        redSpawn = true;
        g_pantalla = 1;
    } else if (const char* em = std::getenv("MUNDO")) {      // (pruebas) saltar el menu: MUNDO=<carpeta> [SEMILLA=n]
        g_dir = std::string(RAIZ_MUNDOS) + "/" + em;
        std::error_code ec0;
        std::filesystem::create_directories(g_dir, ec0);
        if (const char* es = std::getenv("SEMILLA")) g_semilla = (uint32_t)std::strtoul(es, nullptr, 10);
        g_nombreMundo = em;
        g_pantalla = 1;
    } else g_pantalla = 2;
    if (g_pantalla != 1) {
        g_capturado = false;
        glfwSetInputMode(ventana, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else {
        g_capturado = true;
        glfwSetInputMode(ventana, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(ventana, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    GLuint progMundo = programa(VS_SRC, FS_SRC);
    GLuint progLinea = programa(VL_SRC, FL_SRC);
    GLint uVP = glGetUniformLocation(progMundo, "uVP"), uRel = glGetUniformLocation(progMundo, "uRel"),
          uAbs = glGetUniformLocation(progMundo, "uAbs"), uVS = glGetUniformLocation(progMundo, "uVS"),
          uSub = glGetUniformLocation(progMundo, "uSub"), uSky = glGetUniformLocation(progMundo, "uSky"),
          uFogA = glGetUniformLocation(progMundo, "uFogA"), uFogB = glGetUniformLocation(progMundo, "uFogB"),
          uLuz = glGetUniformLocation(progMundo, "uLuz"), uLMax = glGetUniformLocation(progMundo, "uLMax"),
          uLN = glGetUniformLocation(progMundo, "uLN"), uSkyMul = glGetUniformLocation(progMundo, "uSkyMul");
    GLint lVP = glGetUniformLocation(progLinea, "uVP"), lColor = glGetUniformLocation(progLinea, "uColor");

    GLuint progLod = programa(VLOD_SRC, FLOD_SRC);
    GLuint progCielo = programa(VC_SRC, FC_SRC);
    GLuint progNube = programa(VN_SRC, FN_SRC);
    GLuint progNube2 = programa(VN2_SRC, FN2_SRC);
    GLuint vaoVacio, vaoN, vboN;
    glGenVertexArrays(1, &vaoVacio);
    glGenVertexArrays(1, &vaoN);
    glGenBuffers(1, &vboN);
    int nubeVerts = 0, nubeCix = -999999, nubeCiz = -999999;
    GLuint progHud = programa(VH_SRC, FH_SRC);
    GLint hRes = glGetUniformLocation(progHud, "uRes");
    GLuint vaoH, vboH;
    glGenVertexArrays(1, &vaoH);
    glGenBuffers(1, &vboH);

    Post pp;
#ifndef MOVIL
    ppIniciar(pp);
#endif
    GLint uSomTex = glGetUniformLocation(progMundo, "uSombra"), uSVPm = glGetUniformLocation(progMundo, "uSVP"),
          uSolDir = glGetUniformLocation(progMundo, "uSolDir"), uSomOn = glGetUniformLocation(progMundo, "uSombraOn"),
          uSomR = glGetUniformLocation(progMundo, "uSombraR"), uSolF = glGetUniformLocation(progMundo, "uSolF"),
          uHDRm = glGetUniformLocation(progMundo, "uHDR"), uSomTx = glGetUniformLocation(progMundo, "uSombraTx");
    GLint uSVPs = glGetUniformLocation(pp.pSombra, "uSVP"), uRels = glGetUniformLocation(pp.pSombra, "uRel"),
          uVSs = glGetUniformLocation(pp.pSombra, "uVS");

    GLuint vaoL, vboL;
    glGenVertexArrays(1, &vaoL);
    glGenBuffers(1, &vboL);
    GLuint progClima = programa(VW_SRC, FW_SRC), vaoW, vboW;
    glGenVertexArrays(1, &vaoW);
    glGenBuffers(1, &vboW);
    std::vector<float> climaV;

    auto subirHud = [&](int fbw, int fbh) {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(progHud);
        glUniform2f(hRes, (float)fbw, (float)fbh);
        glBindVertexArray(vaoH);
        glBindBuffer(GL_ARRAY_BUFFER, vboH);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_hud.size() * sizeof(float)), g_hud.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 24, (const void*)8);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(g_hud.size() / 6));
        glDisable(GL_BLEND);
    };

    // ---- menu de inicio (siempre al iniciar): crear mundo / mis mundos / multijugador
    if (g_pantalla != 1) {
        g_mundos = listarMundos();
        auto frameInicio = [&]() {
            g_tac.ui = true; g_tac.uiJuego = false; g_tac.chat = false; g_tac.texto = g_pantalla == 3;
            glfwPollEvents();
            int fbw, fbh;
            glfwGetFramebufferSize(ventana, &fbw, &fbh);
            g_fbW = fbw; g_fbH = fbh;
            glfwGetWindowSize(ventana, &g_winW, &g_winH);
            if (fbw < 1 || fbh < 1) { glfwSwapBuffers(ventana); return; }
            if (g_pantalla == 3) lanSondear(glfwGetTime());
            mundosClamp();
            glViewport(0, 0, fbw, fbh);
            glClearColor(0.42f, 0.62f, 0.90f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            g_hud.clear();
            if (g_pantalla == 0) mundosDibujar(fbw, fbh); else inicioDibujar(fbw, fbh);
            subirHud(fbw, fbh);
            glfwSwapBuffers(ventana);
        };
        for (;;) {
            while (!glfwWindowShouldClose(ventana) && g_mundoElegido < 0 && !g_crearNuevo && g_unirElegido.empty()) frameInicio();
            if (glfwWindowShouldClose(ventana)) { lanCerrar(); glfwTerminate(); return g_cambiarSub ? 3 : 0; }
            if (!g_unirElegido.empty()) {
                g_conectando = true; g_errorInicio.clear();
                frameInicio(); frameInicio();
                bool ok = unirseAMundo(g_unirElegido);
                g_conectando = false;
                if (ok) { redSpawn = true; break; }
                g_errorInicio = g_red.error; g_red.cerrar(); g_unirElegido.clear();
                continue;
            }
            InfoMundo m = g_crearNuevo ? crearMundo() : g_mundos[g_mundoElegido];
            g_dir = m.carpeta; g_semilla = m.semilla; g_nombreMundo = m.nombre;
            break;
        }
        lanCerrar();
        g_capturado = true; g_primerMouse = true; g_pantalla = 1;
        glfwSetInputMode(ventana, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(ventana, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    int hilos = (int)std::thread::hardware_concurrency() - 1;
    hilos = std::max(2, std::min(8, hilos));
#ifdef MOVIL
    hilos = std::min(hilos, 3);          // celular: deja libres los nucleos grandes para dibujar y no calientes de mas
#endif
    g_hilosBase = hilos;
    {   // se crean mas hilos de los normales; el perfil de rendimiento decide cuantos trabajan
        int hm = (int)std::thread::hardware_concurrency() - 1;
#ifdef MOVIL
        hm = std::min(hm, 5);
#else
        hm = std::min(hm, 16);
#endif
        g_hilosMax = std::max(hilos, hm);
    }
    g_pool.iniciar(g_hilosMax);
    aplicarPerf();

    // lista de posiciones de chunk ordenadas de cerca a lejos
    struct Off { int dx, dz; int d2; };
    std::vector<Off> offsets;
    for (int dz = -(RC + 1); dz <= RC + 1; dz++)
        for (int dx = -(RC + 1); dx <= RC + 1; dx++) {
            int d2 = dx * dx + dz * dz;
            if (d2 <= (RC + 1) * (RC + 1)) offsets.push_back({dx, dz, d2});
        }
    std::sort(offsets.begin(), offsets.end(), [](const Off& a, const Off& b) { return a.d2 < b.d2; });

    escanearEditados();
    camasCargar();
    {   // punto de inicio: lo mas cerca del centro que quede en tierra firme (no en un lago)
        double sx = 0.5, sz = 0.5;
        bool hallado = false;
        for (int r = 0; r <= 600 && !hallado; r += 6) {
            int n = r == 0 ? 1 : std::max(8, r / 2);
            for (int a = 0; a < n && !hallado; a++) {
                double ang = a * 6.2831853 / n, x = 0.5 + r * std::cos(ang), z = 0.5 + r * std::sin(ang);
                if (alturaTerreno(x, z) > WATER_LEVEL + 2.0) { sx = x; sz = z; hallado = true; }
            }
        }
        g_j.p = {sx, alturaTerreno(sx, sz) + 4.0, sz};
    }
    bool jugadorCargado = cargarJugador();
    if (redSpawn && g_red.hayAnfitrionPos) { g_j.p = {g_red.spawn[0] + 1.5, g_red.spawn[1] + 0.5, g_red.spawn[2]}; g_j.v = {0, 0, 0}; g_j.volar = false; }
    else vehCargar();
    ckCargarInfo();
    if (const char* cm_ = std::getenv("CK_MULT")) g_ckMult = std::max(1.0, std::atof(cm_));   // (pruebas)
    if (const char* ey = std::getenv("RED_YAW")) g_j.yaw = std::atof(ey);   // (pruebas)
    g_red.dirMundo = g_dir; g_red.semilla = g_semilla;
    g_red.aplicar = redRecibirEdits;
    redIniciarCallbacks();
    g_red.guardarYa = [] { guardarTodosLosSucios(); };
    if (g_argHost && g_argUnir.empty()) {
        if (g_red.hospedar(g_argPuerto)) std::printf("RED: hospedando en %s:%d  (tus amigos usan: --unir %s)\n", rs_ip_local(), g_argPuerto, rs_ip_local());
        else std::printf("RED: %s\n", g_red.error.c_str());
    }
    if (const char* eh = std::getenv("HORA")) g_hora = std::atof(eh);   // (pruebas)
    if (const char* ec = std::getenv("CLIMA")) { g_climaE = std::atoi(ec) % 5; g_climaFijo = true; g_clN = g_clP = g_clT = g_clF = -1; g_rayoSig = 0; }   // (pruebas) clima fijo, ya al maximo
    if (const char* te = std::getenv("TELE")) {   // (pruebas) TELE=x,z[,yaw[,alto]]: aparecer en esas coordenadas
        double tx = 0, tz = 0, ty = 0, ta = 5.0; std::sscanf(te, "%lf,%lf,%lf,%lf", &tx, &tz, &ty, &ta);
        g_j.p = {tx, alturaTerreno(tx, tz) + ta, tz}; g_j.yaw = ty; g_j.pitch = std::getenv("PITCH") ? std::atof(std::getenv("PITCH")) : -0.15; g_j.v = {0, 0, 0};
    }
    if (const char* sv = std::getenv("SURVIVAL")) {   // (pruebas) survival con materiales
        g_survival = true; g_spawnOk = true; g_spawnS[0] = g_j.p.x; g_spawnS[1] = g_j.p.y; g_spawnS[2] = g_j.p.z; g_j.volar = false;
        if (std::atoi(sv) > 1) { g_vida = 13; g_inv[MADERA] = 60; g_inv[PIEDRA] = 30; g_inv[HIERRO] = 25; g_inv[ARENA] = 8; g_inv[CARBON] = 3; g_inv[PASTO] = 12; g_inv[HERR_BASE] = 1; g_inv[HERR_BASE + 5] = 1; g_barra[1] = HERR_BASE; g_barra[2] = PASTO; g_barra[3] = MADERA; g_barra[4] = HERR_BASE + 5; g_barra[0] = VACIO; aplicarSel(); }
    }
    if (std::getenv("PRUEBA_OBJ")) { for (int id : {200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 242, 243, 244, 245, 246, 247, 248, 249, 254, 255}) g_inv[id] = 50; if (std::atoi(std::getenv("PRUEBA_OBJ")) > 1) g_pagina = std::atoi(std::getenv("PRUEBA_OBJ")) - 1; g_recPag = std::getenv("PRUEBA_REC") ? std::atoi(std::getenv("PRUEBA_REC")) : 0; }   // (pruebas) objetos nuevos
    if (const char* em = std::getenv("MATERIAL")) { g_barra[g_sel] = std::atoi(em); aplicarSel(); }   // (pruebas)
    std::printf("Carpeta del mundo: %s%s\n", g_dir.c_str(), jugadorCargado ? " (mundo anterior cargado)" : " (mundo nuevo)");
    double tGuardado = glfwGetTime(), tMensaje = -10, tAgua = 0;
    int frame = 0, fpsCuenta = 0;
    double tFps = glfwGetTime(), tAnterior = glfwGetTime(), tAccion = 0;
    int fps = 0, triDib = 0, mallasDib = 0;
    bool cargando = true;

    while (!glfwWindowShouldClose(ventana)) {
        g_tac.fbw = g_fbW; g_tac.fbh = g_fbH;
        g_tac.ui = g_menu || g_pausa || !g_capturado;
        g_tac.uiJuego = g_menu || g_pausa;
        g_tac.pil = (g_veh.activo && g_veh.pilotando) || g_ck.activo;
        g_tac.extra = g_material == ID_SPRAY || g_material == RUEDA;
        g_tac.volando = g_j.volar;
        g_tac.chat = g_chat; g_tac.texto = g_chat;
        glfwPollEvents();
#ifdef ANDROID_GAME
        if (A_pausado) { A_pausado = false; g_guardarYa = true; }      // al pasar a segundo plano se guarda
#endif
        double ahora = glfwGetTime();
        double dtReal = ahora - tAnterior;
        double dt = std::min(0.05, ahora - tAnterior);
        tAnterior = ahora;
        if (std::getenv("LUNA_TEST") && frame == 20) g_lunaPend = true;       // (pruebas) directo al mundo lunar
        if (g_pedirCiudad || (std::getenv("PRUEBA_CIUDAD") && frame == 250)) { g_pedirCiudad = false; construirCiudad(); }
        if (g_pedirLanzar || (std::getenv("PRUEBA_LANZAR") && frame == std::atoi(std::getenv("PRUEBA_LANZAR")))) { g_pedirLanzar = false; ckLanzar(); }
        if (g_pedirCohete || (std::getenv("PRUEBA_COHETE") && frame == std::atoi(std::getenv("PRUEBA_COHETE")))) { g_pedirCohete = false; construirCohete(); }
        if (g_bench.estado) benchTick(dtReal, ahora);
        frame++; fpsCuenta++;

        // ---- v59: cambio al mundo de la Luna (se vacian los chunks de la Tierra y se generan los lunares)
        if (g_lunaPend) {
            g_lunaPend = false;
            if (g_red.activo()) { aviso("LA LUNA NO ESTA DISPONIBLE EN RED"); g_ck.esperaLuna = false; }
            else if (!g_modoLuna) {
                guardarTodosLosSucios(); guardarJugador(); vehGuardar();
                for (auto& kv : g_mapa) { Chunk& ch = *kv.second; ch.viva.store(false); std::lock_guard<std::mutex> lk(ch.mtx); ch.gOp.liberar(); ch.gAg.liberar(); ch.gLuz.liberar(); }
                g_mapa.clear();
                g_lodOnAntes = g_lodOn;
                if (g_lodOn) { lodLiberarTodo(); g_lodOn = false; }
                g_dirTierra = g_dir; g_dir = g_dir + "/luna";
                { std::error_code ec0; std::filesystem::create_directories(g_dir, ec0); }
                g_modoLuna = true; cargando = true;
                if (g_ck.activo) ckLunaTeleport();
                else {
                    g_j.p = {0.5, alturaLuna(0.0, 0.0) + 2.0, 0.5}; g_j.v = {0, 0, 0}; g_j.volar = false; g_vehCinematico = false; aviso("ESTAS EN LA LUNA (PRUEBA)");
                    if (g_irBase) { irALaBase(0, 9, 1, 0, 0); g_irBase = false; }
                    if (const char* lp = std::getenv("LUNA_POS")) {      // (pruebas) LUNA_POS=x,d,y,yaw,pitch: coordenadas locales de la base (d = hacia dentro)
                        double a[5] = {0, -9, 1, 0, 0}; std::sscanf(lp, "%lf,%lf,%lf,%lf,%lf", &a[0], &a[1], &a[2], &a[3], &a[4]);
                        g_j.p = {BASE_X + 0.5 + a[0], LUNA_Y0 + a[2] + 0.1, BASE_Z + 0.5 - a[1]}; g_j.yaw = a[3]; g_j.pitch = a[4]; g_j.volar = std::getenv("LUNA_VOLAR") != nullptr;
                    }
                }
            }
        }
        // ---- v71: salida de la Luna hacia el espacio (se vacian los chunks lunares y se vuelve al mundo de la Tierra)
        if (g_tierraPend) {
            g_tierraPend = false;
            if (g_modoLuna && g_ck.activo && !g_dirTierra.empty()) {
                guardarTodosLosSucios(); vehGuardar();
                for (auto& kv : g_mapa) { Chunk& ch = *kv.second; ch.viva.store(false); std::lock_guard<std::mutex> lk(ch.mtx); ch.gOp.liberar(); ch.gAg.liberar(); ch.gLuz.liberar(); }
                g_mapa.clear();
                g_dir = g_dirTierra;
                g_modoLuna = false; cargando = true;
                if (g_lodOnAntes) g_lodOn = true;
                ckEspacioTeleport();
            } else g_ck.esperaLuna = false;
        }
        // ---- carga y descarga de chunks
        int pcx = divPiso((int)std::floor(g_j.p.x * SUB), CS), pcz = divPiso((int)std::floor(g_j.p.z * SUB), CS);
        if (frame % 2 == 0) {
            for (const Off& o : offsets) {
                for (int cy = 0; cy < HYC; cy++) {
                    int cx = pcx + o.dx, cz = pcz + o.dz;
                    ChunkP c = buscar(cx, cy, cz);
                    if (!c) {
                        if (g_genPend.load() >= genMax()) continue;
                        c = std::make_shared<Chunk>(cx, cy, cz);
                        g_mapa[clave(cx, cy, cz)] = c;
                        pedirGenerar(c);
                    } else if (c->state.load(std::memory_order_acquire) == ST_READY && o.d2 <= RC * RC) {
                        if (!c->dat.d && c->dat.uni == AIRE) { c->state.store(ST_MESHED); continue; }   // aire: nada que dibujar
                        if (g_meshPend.load() < mallaMax()) pedirMalla(c, o.d2);
                    }
                }
            }
        }
        if (frame % 30 == 0) {
            anclarActualizar(g_j.p.x, g_j.p.z);
            auto liberar = [&](const ChunkP& ch) {
                ch->viva.store(false);
                if (ch->sucioDisco.load() && ch->state.load() >= ST_READY) guardarChunk(*ch);
                std::lock_guard<std::mutex> lk(ch->mtx);
                ch->gOp.liberar();
                ch->gAg.liberar();
                ch->gLuz.liberar();
            };
            auto protegido = [&](const Chunk& ch) { return g_requeridos.count(colKey(ch.cx, ch.cz)) || g_redCols.count(colKey(ch.cx, ch.cz)); };
            for (auto it = g_mapa.begin(); it != g_mapa.end();) {
                int dx = it->second->cx - pcx, dz = it->second->cz - pcz;
                // con "conservar chunks" no se descartan por distancia (solo si se llena el limite de RAM, mas abajo)
                if (!g_conservar && dx * dx + dz * dz > (RC + 3) * (RC + 3) && !protegido(*it->second)) {
                    liberar(it->second);
                    it = g_mapa.erase(it);
                } else ++it;
            }
            if (g_conservar) {   // limite de RAM: unos 8000 chunks con datos (~250 MB); se descartan primero los mas lejanos
                const size_t TOPE = 8000;
                size_t conDatos = 0;
                for (auto& kv : g_mapa) if (kv.second->dat.d) conDatos++;
                if (conDatos > TOPE) {
                    std::vector<std::pair<int, int64_t>> lej;
                    for (auto& kv : g_mapa) {
                        const Chunk& ch = *kv.second;
                        int dx = ch.cx - pcx, dz = ch.cz - pcz, d2 = dx * dx + dz * dz;
                        if (ch.dat.d && d2 > (RC + 3) * (RC + 3) && !protegido(ch)) lej.push_back({d2, kv.first});
                    }
                    std::sort(lej.begin(), lej.end(), [](const std::pair<int, int64_t>& a, const std::pair<int, int64_t>& b) { return a.first > b.first; });
                    size_t quitar = conDatos - TOPE * 9 / 10;
                    for (size_t i = 0; i < lej.size() && i < quitar; i++) {
                        auto it = g_mapa.find(lej[i].second);
                        if (it == g_mapa.end()) continue;
                        liberar(it->second);
                        g_mapa.erase(it);
                    }
                }
            }
        }
        if (g_red.activo()) {
            static V3 pPrev = g_j.p; static double velS = 0;
            double vh = std::sqrt((g_j.p.x - pPrev.x) * (g_j.p.x - pPrev.x) + (g_j.p.z - pPrev.z) * (g_j.p.z - pPrev.z)) / std::max(dt, 1e-3);
            if (vh > 40) vh = 0;
            velS += (vh - velS) * std::min(1.0, dt * 8.0); pPrev = g_j.p;
            bool sent = (g_veh.activo && g_veh.pilotando);
            g_red.yo = {(float)g_j.p.x, (float)g_j.p.y, (float)g_j.p.z, (float)g_j.yaw, (float)g_j.pitch, (float)velS, (uint8_t)((g_j.volar ? 1 : 0) | (sent ? 2 : 0) | (g_golpea ? 4 : 0)), objetoEnMano()};
            g_red.horaLocal = (float)g_hora;
            g_red.paso(ahora);
            if (g_red.host) lanAnunciar(ahora, g_nombreMundo, g_argPuerto, (int)g_red.rem.size() + 1);
            if (g_red.cli && g_red.horaRecibida >= 0) g_hora = g_red.horaRecibida;
            {   // avioneta compartida: la simula quien la pilota; si nadie, el anfitrion; los demas solo siguen su pose
                static double tVeh = 0; static bool pilPrev = false;
                bool pil = g_veh.activo && g_veh.pilotando;
                g_vehCinematico = g_veh.activo && (g_red.cli || g_veh.pilotoRed >= 0);
                if (g_veh.activo) {
                    bool dueno = pil || !g_vehCinematico;
                    if ((dueno && ahora - tVeh > 0.05) || (pilPrev && !pil)) { tVeh = ahora; vehEnviarPose(pil ? g_red.miId : 255); }
                }
                pilPrev = pil;
            }
            redPendientes();
            jugActualizar(dt);
            if (g_red.host && frame % 10 == 0) {
                g_redCols.clear();
                for (auto& kv : g_red.rem) if (kv.second.visto) {
                    int rcx = divPiso((int)std::floor(kv.second.x * SUB), CS), rcz = divPiso((int)std::floor(kv.second.z * SUB), CS);
                    for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
                        g_redCols.insert(colKey(rcx + dx, rcz + dz));
                        for (int cy = 0; cy < HYC; cy++) if (!buscar(rcx + dx, cy, rcz + dz) && g_genPend.load() < genMax()) {
                            ChunkP c = std::make_shared<Chunk>(rcx + dx, cy, rcz + dz);
                            g_mapa[clave(rcx + dx, cy, rcz + dz)] = c;
                            pedirGenerar(c);
                        }
                    }
                }
            }
            char ib[160];
            if (g_red.host) std::snprintf(ib, sizeof ib, "RED: ANFITRION %s:%d - %d JUGADORES", rs_ip_local(), g_argPuerto, (int)g_red.rem.size() + 1);
            else std::snprintf(ib, sizeof ib, "RED: CONECTADO - %d JUGADORES", (int)g_red.rem.size() + 1);
            g_redInfo = ib;
        } else { g_vehCinematico = false; if (!g_red.error.empty()) g_redInfo = g_red.error; }
        recogerMallas(subidasMax());     // (celular: pocas mallas por cuadro para evitar tirones)

        // ---- guardado (automatico cada 30 s, con F5, y al salir)
        if (g_guardarYa || ahora - tGuardado > 30.0) {
            int n = guardarTodosLosSucios();
            guardarJugador();
            vehGuardar();
            tGuardado = ahora; tMensaje = ahora; g_guardarYa = false;
            if (n) std::printf("Guardado: %d chunks\n", n);
        }

        // ---- fisica
        if (g_survival) g_j.volar = false;
        bool conocido = true;
        leerCubito((int)std::floor(g_j.p.x * SUB), (int)std::floor((g_j.p.y - 0.2) * SUB), (int)std::floor(g_j.p.z * SUB), &conocido);
        if (cargando && conocido) cargando = false;
        const bool pilV = g_veh.activo && g_veh.pilotando;
        const bool pil = pilV || g_ck.activo;
        if (!cargando && g_ck.activo && !g_pausa && !g_menu) ckPaso(ventana, std::min(dt, 0.05));
        if (!cargando && g_veh.activo && !g_pausa && !g_menu) {
            vehPaso(ventana, std::min(dt, 0.05));
            if (g_sobreVeh && !pil) g_j.p = vehAmundo(g_L);      // viajas con la avioneta
        }
        if (!cargando && !pil) {
            V3 adelante = {std::sin(g_j.yaw), 0, -std::cos(g_j.yaw)};
            V3 derecha = {std::cos(g_j.yaw), 0, std::sin(g_j.yaw)};
            V3 mov = {0, 0, 0};
            if (glfwGetKey(ventana, GLFW_KEY_W) == GLFW_PRESS) mov = mov + adelante;
            if (glfwGetKey(ventana, GLFW_KEY_S) == GLFW_PRESS) mov = mov - adelante;
            if (glfwGetKey(ventana, GLFW_KEY_D) == GLFW_PRESS) mov = mov + derecha;
            if (glfwGetKey(ventana, GLFW_KEY_A) == GLFW_PRESS) mov = mov - derecha;
            mov = norm(mov);
            bool correr = !g_menu && !g_pausa && glfwGetKey(ventana, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            bool salto = !g_menu && !g_pausa && glfwGetKey(ventana, GLFW_KEY_SPACE) == GLFW_PRESS;
            if (g_menu || g_pausa) mov = {0, 0, 0};
            if (g_j.volar) {
                // modo creativo: Espacio sube, Shift baja, R = turbo
                double vel = (!g_menu && !g_pausa && glfwGetKey(ventana, GLFW_KEY_R) == GLFW_PRESS) ? 45.0 : 14.0;
                double vy = 0;
                if (salto) vy += 1;
                if (correr) vy -= 1;
                g_j.p = g_j.p + (mov + V3{0, vy, 0}) * (vel * dt);
                g_sobreVeh = false;
            } else if (g_sobreVeh) {
                vehCaminar(mov, salto, correr, dt);
            } else {
                bool enAgua = esAgua(leerCubito((int)std::floor(g_j.p.x * SUB), (int)std::floor((g_j.p.y + 0.9) * SUB), (int)std::floor(g_j.p.z * SUB)));
                // lava: te hundes despacio, no puedes correr y en survival te quema (2 de dano cada medio segundo)
                const bool enLava = esLava(leerCubito((int)std::floor(g_j.p.x * SUB), (int)std::floor((g_j.p.y + 0.3) * SUB), (int)std::floor(g_j.p.z * SUB))) ||
                                    esLava(leerCubito((int)std::floor(g_j.p.x * SUB), (int)std::floor((g_j.p.y + 0.9) * SUB), (int)std::floor(g_j.p.z * SUB)));
                if (enLava) {
                    enAgua = true;
                    static double tQuema = 0;
                    if (g_survival && glfwGetTime() - tQuema > 0.5) { tQuema = glfwGetTime(); survDano(2.0f); }
                }
                double vel = correr ? 7.0 : 4.3;
                if (enAgua) vel *= enLava ? 0.35 : 0.6;
                g_j.v.x = mov.x * vel + g_j.inercia.x;
                g_j.v.z = mov.z * vel + g_j.inercia.z;
                g_j.v.y -= (enAgua ? 8.0 : (g_modoLuna ? 4.7 : 28.0)) * dt;
                if (g_j.v.y < (enAgua ? -3.0 : -50.0)) g_j.v.y = enAgua ? -3.0 : -50.0;
                if (salto && enAgua) g_j.v.y = 4.0;
                else if (salto && g_j.suelo) g_j.v.y = g_modoLuna ? 5.2 : 9.0;
                if (g_veh.activo && chocaVeh(g_j.p.x, g_j.p.y, g_j.p.z) && !chocaMundo(g_j.p.x, g_j.p.y, g_j.p.z)) {   // el vehiculo subio y te alcanzo: te empuja hacia arriba
                    for (int n = 0; n < 40 && chocaVeh(g_j.p.x, g_j.p.y, g_j.p.z); n++) g_j.p.y += 0.05;
                    g_j.suelo = true; g_j.v.y = 0;
                }
                g_pisaba = g_j.suelo;
                g_j.suelo = false;
                moverEje(0, g_j.v.x * dt);
                moverEje(2, g_j.v.z * dt);
                moverEje(1, g_j.v.y * dt);
                // apoyado sobre el vehiculo (y no sobre el terreno)?
                bool sobre = false;
                if (g_veh.activo && g_j.v.y <= 1.0) {
                    V3 f_, r_, u_; vehBase(g_veh, f_, r_, u_);
                    sobre = u_.y > 0.64 && !chocaMundo(g_j.p.x, g_j.p.y - 0.08, g_j.p.z) && chocaVeh(g_j.p.x, g_j.p.y - 0.08, g_j.p.z);
                }
                if (g_survival) {                              // dano por caida
                    if (g_j.suelo || enAgua) {
                        if (g_j.suelo && !enAgua && glfwGetTime() > g_protT && g_caidaMax - g_j.p.y > 3.0) survDano((float)(g_caidaMax - g_j.p.y - 3.0));
                        g_caidaMax = g_j.p.y;
                    } else g_caidaMax = std::max(g_caidaMax, g_j.p.y);
                }
                if (sobre) vehEntrar();                       // pisaste el vehiculo: pasas a caminar en su sistema de referencia
                else if (g_j.suelo) g_j.inercia = {0, 0, 0};
                else g_j.inercia = g_j.inercia * std::exp(-0.6 * dt);
            }
        }

        if ((g_survival || g_jefeAct()) && !cargando) {                 // vida: se regenera sola y al morir reapareces
            if (pil || g_sobreVeh) g_caidaMax = g_j.p.y;
            if (g_vida > 0 && g_vida < 20.0f) { g_vidaT += dt; if (g_vidaT > 4.0) { g_vida = std::min(20.0f, g_vida + 1.0f); g_vidaT = 0; } } else g_vidaT = 0;
            if (g_vida <= 0) {
                if (g_survival) g_j.p = {g_spawnS[0], g_spawnS[1] + 0.5, g_spawnS[2]}; else if (g_pozoAbierto) g_j.p = {BASE_X - 22.5, LUNA_Y0 - 46.8, BASE_Z - 241.0}; else g_j.p = {BASE_X + 0.5, LUNA_Y0 - 15.8, BASE_Z - 196.0};
                g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0};
                g_vida = 20.0f; g_caidaMax = g_j.p.y; g_vidaT = 0; g_protT = glfwGetTime() + 3.0;
                g_aviso = "MORISTE: REAPARECES EN TU PUNTO"; g_avisoT = glfwGetTime();
            }
        }
        if (!cargando) jefeTick(dt, ahora);
        if (!cargando) ovejasTick(dt, ahora, g_j.p.x, g_j.p.y, g_j.p.z, !g_modoLuna && !g_ck.activo && g_pantalla == 1);
        suenoTick(ahora);
        { std::vector<int> cog; sueltosTick(dt, g_j.p.x, g_j.p.y, g_j.p.z, ahora, cog);
          for (int id : cog) if (id >= 0 && id < 256) g_inv[id] = std::min(9999, g_inv[id] + 1); }
        neansTick(dt, ahora, g_j.p.x, g_j.p.y, g_j.p.z, !g_modoLuna && !g_ck.activo && !cargando && g_pantalla == 1, esDeNoche());
        // ---- camara
        V3 mira = {std::cos(g_j.pitch) * std::sin(g_j.yaw), std::sin(g_j.pitch), -std::cos(g_j.pitch) * std::cos(g_j.yaw)};
        V3 cam = {g_j.p.x, g_j.p.y + g_j.ojo, g_j.p.z};
        V3 camUp = {0, 1, 0};
        if (g_ck.activo) ckCamara(mira, cam, camUp);
        else if (pil) vehCamara(mira, cam, camUp);
        int fbw, fbh;
        glfwGetFramebufferSize(ventana, &fbw, &fbh);
        g_fbW = fbw; g_fbH = fbh;
        glfwGetWindowSize(ventana, &g_winW, &g_winH);
        if (fbw < 1 || fbh < 1) { glfwSwapBuffers(ventana); continue; }
        float proy[16], vista[16], vp[16];
        matPerspectiva(proy, 1.22f + (g_ck.activo && g_ck.fase == 2 && !g_modoLuna ? (float)std::min(0.50, std::sqrt(g_ck.V.x * g_ck.V.x + g_ck.V.y * g_ck.V.y + g_ck.V.z * g_ck.V.z) / 2500.0 * 0.50) : 0.0f), (float)fbw / fbh, 0.05f, g_lodOn ? 1500.0f : 800.0f);
        matVista(vista, mira, camUp);
        matMul(vp, proy, vista);
        g_ckM.ok = g_ck.activo && g_ck.fase == 2 && !g_modoLuna && !g_ck.reentrada;
        if (g_ckM.ok) {      // marcadores en pantalla: la Luna y hacia donde apunta la proa
            V3 mr = g_ck.vuelta ? V3{g_terraX - cam.x, -CK_RP - cam.y, g_terraZ - cam.z} : ckLunaRel(cam);
            double dm = std::sqrt(mr.x * mr.x + mr.y * mr.y + mr.z * mr.z);
            V3 dM = mr * (1.0 / std::max(1.0, dm));
            V3 f_, r_, u_; vehBase(g_ck.e2, f_, r_, u_);
            g_ckM.ang = std::acos(std::max(-1.0, std::min(1.0, u_.x * dM.x + u_.y * dM.y + u_.z * dM.z)));
            g_ckM.dist = dm - (g_ck.vuelta ? CK_RP : CK_LUNA_R);
            auto proy2 = [&](V3 d, float& sx_, float& sy_) -> bool {
                float px = (float)(d.x * 1000.0), py = (float)(d.y * 1000.0), pz = (float)(d.z * 1000.0);
                float cx_ = vp[0] * px + vp[4] * py + vp[8] * pz + vp[12], cy_ = vp[1] * px + vp[5] * py + vp[9] * pz + vp[13], cw = vp[3] * px + vp[7] * py + vp[11] * pz + vp[15];
                float inv = 1.0f / (std::fabs(cw) > 1e-4f ? cw : 1e-4f);
                float nx = cx_ * inv, ny = cy_ * inv;
                if (cw < 0) { nx = -nx; ny = -ny; }                   // detras: se marca el borde por el lado contrario
                float m = std::max(std::fabs(nx), std::fabs(ny));
                bool dentro = cw > 0 && m < 0.94f;
                if (!dentro) { float k_ = 0.90f / std::max(0.001f, m); nx *= k_; ny *= k_; }
                sx_ = (nx * 0.5f + 0.5f) * fbw; sy_ = (1.0f - (ny * 0.5f + 0.5f)) * fbh;
                return dentro;
            };
            g_ckM.lDentro = proy2(dM, g_ckM.lx, g_ckM.ly);
            g_ckM.pDentro = proy2(u_, g_ckM.px, g_ckM.py);
            { V3 ps = norm(mr + ckSolEsp() * CK_LUNA_R); g_ckM.sDentro = proy2(ps, g_ckM.sx, g_ckM.sy); }      // el punto de la Luna donde el sol cae de frente
        }

        if (g_modoLuna) {      // base lunar: puerta corrediza y teclado
            puertaTick(ahora);
            if (g_tecladoOn) {
                double kx = BASE_X + 4.5 - g_j.p.x, kz = BASE_Z + 0.5 - g_j.p.z;
                if (kx * kx + kz * kz > 49.0 || g_pausa || g_menu) g_tecladoOn = false;
            }
        }
        // ---- romper / poner
        Golpe golpe;
        if (!cargando && !pil) golpe = lanzarRayo(cam, norm(mira), 8.0);
        bool izq = !pil && !g_chat && g_capturado && glfwGetMouseButton(ventana, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool der = !pil && !g_chat && g_capturado && glfwGetMouseButton(ventana, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        g_golpea = izq || der;
        g_redEnviar = true;
        if (g_pedirG) { g_pedirG = false; if (g_ck.activo) {} else if (pilV) vehSubirBajar(); else if (g_veh.activo) vehSubirBajar(); else if (golpe.hay) vehEnsamblar(golpe.x, golpe.y, golpe.z); else aviso("MIRA UNA PIEZA DEL VEHICULO Y PULSA G"); if (g_veh.activo) g_guardarYa = true; }
        if (g_pedirH) { g_pedirH = false; if (g_veh.activo && !g_veh.pilotando) { vehDesarmar(); g_guardarYa = true; } }
        double cdAcc = 0.18;
        bool romListo = true;       // v86: en survival picar lleva tiempo segun el bloque y la herramienta (cubito = 1/4 del tiempo de un bloque)
        if (g_survival && izq && golpe.hay && !g_menu) {
            const int kx = g_pincelBloque ? divPiso(golpe.x, SUB) : golpe.x, ky = g_pincelBloque ? divPiso(golpe.y, SUB) : golpe.y, kz = g_pincelBloque ? divPiso(golpe.z, SUB) : golpe.z;
            const long long key = ((long long)kx * 73856093LL) ^ ((long long)ky * 19349663LL) ^ ((long long)kz * 83492791LL) ^ (g_pincelBloque ? 1LL : 0LL);
            if (key != g_romKey) { g_romKey = key; g_romT = 0; }
            g_romT += dt;
            const double tNec = survCooldown(leerCubito(golpe.x, golpe.y, golpe.z), g_material) / 0.18 * 1.2 * ((g_pincelBloque || SUB == 1) ? 1.0 : 0.25);   // en SUB 1 un cubito ya ES un bloque completo
            romListo = g_romT >= tNec;
            g_romFrac = (float)std::min(1.0, g_romT / std::max(0.05, tNec));
            cdAcc = 0;
        } else { g_romT = 0; g_romKey = -1; g_romFrac = 0.0f; }
        static double tMelee = -10;
        {   // v84: golpear ovejas con el clic izquierdo (no rompe el bloque de atras)
            static double tOv = -10;
            if (izq && !g_menu && !g_modoLuna && g_material != ID_ARCO && !g_ck.activo) {
                const ArmaInfo ai0 = armaInfo(g_material);
                const double alc = esArma(g_material) ? std::max(4.5, ai0.alcance) : 4.5;
                double dOv = 0; int oi = ovejaApuntada(cam, norm(mira), alc, &dOv);
                double dNe = 0; const int ni = neanApuntado(cam, norm(mira), alc, &dNe);
                const bool usaNe = ni >= 0 && (oi < 0 || dNe < dOv);
                if (usaNe) dOv = dNe;
                if (oi >= 0 || usaNe) {
                    double dBl = 1e9;
                    if (golpe.hay) { const double cx_ = (golpe.x + 0.5) / SUB - cam.x, cy_ = (golpe.y + 0.5) / SUB - cam.y, cz_ = (golpe.z + 0.5) / SUB - cam.z; dBl = std::sqrt(cx_ * cx_ + cy_ * cy_ + cz_ * cz_) - 0.4; }
                    if (dOv < dBl) {
                        izq = false;
                        const double cd = esArma(g_material) ? ai0.cd : 0.45;
                        if (ahora - tOv > cd) {
                            tOv = ahora;
                            const double dano = esArma(g_material) && !((g_material == ID_ESPADAP || g_material == ID_MARTILLO) && g_survival && g_inv[0] == 0) ? ai0.dano : (esHerr(g_material) ? 4.0 : 2.0);
                            int lana = 0, carne = 0;
                            if (usaNe) {
                                int madera = 0;
                                if (neanGolpear(ni, dano, g_j.p.x, g_j.p.z, ahora, &carne, &madera)) {
                                    g_inv[MADERA] = std::min(9999, g_inv[MADERA] + madera); g_inv[ID_CARNE] = std::min(9999, g_inv[ID_CARNE] + carne);
                                    char mm[64]; std::snprintf(mm, sizeof mm, "NEANDERTAL DERROTADO: +%d CARNE  +%d MADERA", carne, madera); aviso(mm);
                                }
                            } else
                            if (ovejaGolpear(oi, dano, g_j.p.x, g_j.p.z, ahora, &lana, &carne)) {
                                g_inv[ID_LANA] = std::min(9999, g_inv[ID_LANA] + lana); g_inv[ID_CARNE] = std::min(9999, g_inv[ID_CARNE] + carne);
                                char mm[64]; std::snprintf(mm, sizeof mm, "+%d LANA  +%d CARNE", lana, carne); aviso(mm);
                            }
                        }
                    }
                }
            }
        }
        { static bool bloqIzq = false; if (!izq) bloqIzq = false; if (bloqIzq) izq = false; g_bloqIzqP = &bloqIzq; }
        const int camaT = (!g_menu && !g_modoLuna && !g_ck.activo) ? camaApuntada(cam, norm(mira), 5.0) : -1;
        if (camaT >= 0 && der && ahora - tAccion > 0.6) {            // clic derecho en la cama: dormir
            tAccion = ahora;
            if (g_red.cli) aviso("EN PARTIDA AJENA SOLO EL ANFITRION CAMBIA LA HORA");
            else if (!esDeNoche()) aviso("SOLO PUEDES DORMIR DE NOCHE");
            else if (g_suenoT0 < 0) {
                g_suenoT0 = ahora; g_suenoHecho = false;
                g_spawnS[0] = g_camas[camaT].x; g_spawnS[1] = g_camas[camaT].y + 0.3; g_spawnS[2] = g_camas[camaT].z; g_spawnOk = true;   // reapareces en tu cama
            }
        } else
        if (camaT >= 0 && izq) {            // clic izquierdo: recoger la cama (un solo toque; no sigue picando lo de atras)
          if (ahora - tAccion > 0.3) {
            *g_bloqIzqP = true;
            tAccion = ahora;
            g_camas.erase(g_camas.begin() + camaT); camasGuardar();
            if (g_survival) g_inv[ID_CAMA] = std::min(9999, g_inv[ID_CAMA] + 1);
            aviso("CAMA RECOGIDA");
          }
        } else
        if (der && g_material == ID_CAMA && !g_menu && !g_modoLuna && golpe.hay && ahora - tAccion > 0.5 && (!g_survival || g_inv[ID_CAMA] > 0)) {   // poner la cama
            tAccion = ahora;
            const double bx = (std::floor(golpe.px / (double)SUB) + 0.5), bz = (std::floor(golpe.pz / (double)SUB) + 0.5), by = golpe.py / (double)SUB;
            bool ok = esSolidoMob(golpe.px, golpe.py - 1, golpe.pz);
            for (auto& c : g_camas) if (std::fabs(c.x - bx) < 1.2 && std::fabs(c.z - bz) < 1.2 && std::fabs(c.y - by) < 1.0) ok = false;
            if (!ok) aviso("PON LA CAMA SOBRE EL SUELO, LEJOS DE OTRA CAMA");
            else {
                int d = (int)std::lround((g_j.yaw + 3.14159265) / 1.5707963); d = ((d % 4) + 4) % 4;
                g_camas.push_back({bx, by, bz, d}); camasGuardar();
                if (g_survival) g_inv[ID_CAMA]--;
            }
        } else
        if (der && g_material == ID_CARNE && !g_menu && ahora - tAccion > 0.7 && g_survival && g_inv[ID_CARNE] > 0) {   // comer carne
            tAccion = ahora;
            if (g_vida >= 20.0f) aviso("YA ESTAS LLENO");
            else { g_inv[ID_CARNE]--; g_vida = std::min(20.0f, g_vida + 6.0f); aviso("COMISTE CARNE (+3 CORAZONES)"); }
        } else
        if (izq && g_material == ID_ARCO && !g_menu && (!g_survival || g_inv[ID_ARCO] > 0)) {   // v82: arco y flechas
            static double tArco = -10;
            if (ahora - tArco > 0.7) {
                tArco = ahora;
                if (!g_modoLuna) aviso("POR AHORA EL ARCO SOLO SE VE VOLAR EN LA LUNA");
                else if (g_survival && g_inv[ID_FLECHA] <= 0) aviso("NO TIENES FLECHAS (RECETAS)");
                else { if (g_survival) g_inv[ID_FLECHA]--; flechaDisparar(cam, norm(mira), ahora); }
            }
        } else
        if (izq && esArma(g_material) && !g_menu && (!g_survival || g_inv[g_material] > 0)) {   // v81: armas cuerpo a cuerpo (contra el mecha de la Luna)
            const ArmaInfo ai = armaInfo(g_material);
            if (ahora - tMelee > ai.cd) {
                tMelee = ahora;
                if ((g_material == ID_ESPADAP || g_material == ID_MARTILLO) && g_survival && g_inv[0] == 0) { /* bloqueadas */ }
                else if (g_modoLuna && g_mecha.viva && !g_mecha.muriendo) {
                    const double mx = g_mecha.x - g_j.p.x, mz = g_mecha.z - g_j.p.z, dh = std::sqrt(mx * mx + mz * mz);
                    V3 dm = norm(mira); const double fr = dh > 1e-3 ? (dm.x * mx + dm.z * mz) / dh : 1.0;
                    if (dh < ai.alcance + 2.4 && fr > 0.25) {
                        mechaDano(ai.dano, ahora);
                        if (g_material == ID_MARTILLO) g_booms.push_back({g_mecha.x, g_mecha.y + 1.0, g_mecha.z, ahora});
                    }
                }
            }
        } else
        if (der && g_material == ID_KITCOH && !g_menu && ahora - tAccion > 0.8 && (!g_survival || g_inv[ID_KITCOH] > 0)) {   // el kit se usa con clic derecho: arma el cohete
            tAccion = ahora;
            if (g_ck.activo) aviso("YA TIENES UN COHETE ARMADO");
            else { if (g_survival) g_inv[ID_KITCOH]--; g_pedirCohete = true; aviso("ARMANDO EL COHETE..."); }
        } else
        if (izq && g_material == ID_PISTOLA && g_modoLuna && !g_menu) {
            pistolaDisparar(cam, norm(mira), ahora);          // pistola de plasma (v67)
        } else
        if (golpe.hay && der && g_modoLuna && ahora - tAccion > 0.4 && leerCubito(golpe.x, golpe.y, golpe.z) == BASE_BOTON) {
            tAccion = ahora;                      // boton de la computadora: enciende la IA
            if (jefeCercaCohete()) jefeAbordar(ahora); else if (g_iaOn) aviso("LA IA YA ESTA ACTIVADA"); else jefeActivarIA(ahora);
        } else
        if (golpe.hay && der && g_modoLuna && ahora - tAccion > 0.4 && leerCubito(golpe.x, golpe.y, golpe.z) == BASE_TECLADO) {
            tAccion = ahora;                      // clic derecho sobre el teclado de la puerta
            if (!g_puertaAbierta && puertaAbiertaReal()) { g_puertaAbierta = true; g_baseDig = 4; }
            if (g_puertaAbierta || g_puertaT0 >= 0) aviso("LA PUERTA YA ESTA ABIERTA");
            else { g_tecladoOn = true; aviso("ESCRIBE EL CODIGO CON LAS TECLAS 0-9 (ESC: CERRAR)"); }
        } else
        if (golpe.hay && (izq || der) && g_material == ANTORCHA && (!g_survival || g_inv[ANTORCHA] > 0) && esTnt(leerCubito(golpe.x, golpe.y, golpe.z)) && ahora - tAccion > 0.4) {
            tAccion = ahora;                      // antorcha + clic sobre TNT = encender (no rompe ni pone nada)
            tntEncender(golpe.x, golpe.y, golpe.z, ahora, 3.0);
        } else
        if (golpe.hay && (izq || der) && !(der && !izq && (g_material < 0 || esHerr(g_material))) && ahora - tAccion > cdAcc && !(g_survival && izq && !romListo)) {
            tAccion = ahora; g_romT = 0; g_romFrac = 0.0f;
            std::vector<ChunkP> sucios;
            auto celda = [&](int x, int y, int z, int& x0, int& y0, int& z0, int& n) {
                if (g_pincelBloque) { x0 = divPiso(x, SUB) * SUB; y0 = divPiso(y, SUB) * SUB; z0 = divPiso(z, SUB) * SUB; n = SUB; }
                else { x0 = x; y0 = y; z0 = z; n = 1; }
            };
            if (izq || (der && g_destruir && !g_survival)) {
                int x0, y0, z0, n;
                celda(golpe.x, golpe.y, golpe.z, x0, y0, z0, n);
                for (int y = y0; y < y0 + n; y++) for (int z = z0; z < z0 + n; z++) for (int x = x0; x < x0 + n; x++)
                    { uint8_t tp = leerCubito(x, y, z);
                      if (esRompible(tp)) {
                          if (g_survival && survPicoOK(tp, g_material)) { int d = survDrop(tp); if (d >= 0) g_inv[d] = std::min(9999, g_inv[d] + 1); }
                          escribirCubito(x, y, z, AIRE, sucios);
                      } }
            } else if (g_material == ID_SPRAY) {      // pintar la carroceria
                int x0, y0, z0, n;
                celda(golpe.x, golpe.y, golpe.z, x0, y0, z0, n);
                for (int y = y0; y < y0 + n; y++) for (int z = z0; z < z0 + n; z++) for (int x = x0; x < x0 + n; x++) {
                    uint8_t a = leerCubito(x, y, z);
                    if (a >= CARROCERIA && a != ASIENTO && a != TIMON && a != ALA && a != RUEDA && a != HELICE && a != CONTROL && a != (uint8_t)(CARR_0 + g_sprayColor))
                        escribirCubito(x, y, z, (uint8_t)(CARR_0 + g_sprayColor), sucios);
                }
            } else if (g_material == RUEDA) {          // rueda: disco de diametro g_ruedaD y 2 cubitos de ancho, con el eje hacia donde miras
                bool ejeX = std::fabs(mira.x) > std::fabs(mira.z);
                int d = g_ruedaD, c0 = d / 2;
                for (int a = 0; a < d; a++) for (int b2 = 0; b2 < d; b2++) {
                    double ux = a + 0.5 - d * 0.5, uy = b2 + 0.5 - d * 0.5;
                    if (ux * ux + uy * uy > d * d * 0.25) continue;
                    for (int e = 0; e < 2; e++) {
                        int x = golpe.px + (ejeX ? e : a - c0), y = golpe.py + b2 - c0, z = golpe.pz + (ejeX ? a - c0 : e);
                        uint8_t actual = leerCubito(x, y, z);
                        if (actual != AIRE && !esAgua(actual)) continue;
                        if (g_survival) { if (g_inv[RUEDA] <= 0) continue; g_inv[RUEDA]--; }
                        escribirCubito(x, y, z, RUEDA, sucios);
                    }
                }
            } else if (esObjNuevo(g_material) || esHerr(g_material) || g_material < 0 || g_material == ID_POLVORA || g_material == ID_RADAR || g_material == ID_PISTOLA) {
                // las herramientas y la mano vacia no ponen nada
            } else {
                int x0, y0, z0, n;
                celda(golpe.px, golpe.py, golpe.pz, x0, y0, z0, n);
                if (g_material == ANTORCHA) { x0 = golpe.px; y0 = golpe.py; z0 = golpe.pz; n = 1; }   // la antorcha siempre es 1 cubito
                for (int y = y0; y < y0 + n; y++) for (int z = z0; z < z0 + n; z++) for (int x = x0; x < x0 + n; x++) {
                    uint8_t actual = leerCubito(x, y, z);
                    if (actual != AIRE && !esAgua(actual)) continue;
                    // no ponerlo dentro de ti
                    double bx0 = x * VS, by0 = y * VS, bz0 = z * VS;
                    bool dentro = !g_j.volar &&
                        bx0 < g_j.p.x + g_j.mitad && bx0 + VS > g_j.p.x - g_j.mitad &&
                        by0 < g_j.p.y + g_j.alto && by0 + VS > g_j.p.y &&
                        bz0 < g_j.p.z + g_j.mitad && bz0 + VS > g_j.p.z - g_j.mitad;
                    if (dentro) continue;
                    if (g_survival) { if (g_inv[g_material] <= 0) continue; g_inv[g_material]--; }
                    escribirCubito(x, y, z, (uint8_t)g_material, sucios);
                }
            }
            auto tR0 = std::chrono::steady_clock::now();
            for (auto& c : sucios) remallarYa(c);
            if (!sucios.empty()) {
                int ex0 = golpe.x, ex1 = golpe.x, ey0 = golpe.y, ey1 = golpe.y, ez0 = golpe.z, ez1 = golpe.z;
                for (int k = 0; k < 3; k++) {            // cubre tambien el borde de la zona editada (pincel de bloque)
                    ex0 = std::min(ex0, (k == 0 ? golpe.px : golpe.x) - SUB); ex1 = std::max(ex1, (k == 0 ? golpe.px : golpe.x) + SUB);
                    ey0 = std::min(ey0, (k == 0 ? golpe.py : golpe.y) - SUB); ey1 = std::max(ey1, (k == 0 ? golpe.py : golpe.y) + SUB);
                    ez0 = std::min(ez0, (k == 0 ? golpe.pz : golpe.z) - SUB); ez1 = std::max(ez1, (k == 0 ? golpe.pz : golpe.z) + SUB);
                }
                marcarLuzSucia(ex0, ex1, ey0, ey1, ez0, ez1, sucios);
            }
            if (std::getenv("DBGROMPER") && !sucios.empty()) std::printf("ROMPER frame %d: %zu chunks sync, %.2f ms hilo principal, luz marcados %d\n", frame, sucios.size(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tR0).count(), g_marcadosN);
        }
        g_redEnviar = false;

        if (std::getenv("FAKE") && !g_red.activo() && frame > 5) { auto it9 = g_red.rem.find(9); if (it9 != g_red.rem.end()) { it9->second.y = (float)g_j.p.y; it9->second.sy = (float)g_j.p.y; } jugActualizar(dt); }
        if (const char* fk = std::getenv("FAKE")) if (frame == 5) {   // (pruebas) un jugador falso a 3 bloques de frente: FAKE=<objeto>,<flags>
            int ob = 255, fl = 0; std::sscanf(fk, "%d,%d", &ob, &fl);
            Remoto& r = g_red.rem[9]; r.id = 9; r.nombre = "FALSO";
            r.x = (float)(g_j.p.x + std::sin(g_j.yaw) * 3.0); r.z = (float)(g_j.p.z - std::cos(g_j.yaw) * 3.0); r.y = (float)g_j.p.y;
            r.yaw = (float)(g_j.yaw + 3.14159265 - 0.5); r.pitch = 0.2f; r.flags = (uint8_t)fl; r.held = (uint8_t)ob; r.vel = 0; r.sx = r.x; r.sy = r.y; r.sz = r.z; r.syaw = r.yaw; r.visto = true;
        }
        if (std::getenv("DBGINV") && frame % 5 == 0) std::printf("INV cm=%.1f vy=%.1f suelo=%d dt=%.4f f=%d arena=%d tierra=%d pasto=%d piedra=%d pico_p=%d vida=%.1f y=%.1f\n", g_caidaMax, g_j.v.y, (int)g_j.suelo, dt, frame, g_inv[ARENA], g_inv[TIERRA], g_inv[PASTO], g_inv[PIEDRA], g_inv[HERR_BASE + 1], g_vida, g_j.p.y);
        if (const char* fc2 = std::getenv("CAIDA")) if (frame == std::atoi(fc2)) g_j.p.y += 14;   // (pruebas) soltarte desde 14 bloques
        if (const char* pm = std::getenv("PRUEBA_MENU")) if (frame == 40) { abrirMenu(ventana); g_menuPag = std::atoi(pm); if (std::getenv("PRUEBA_OBJ") && std::atoi(std::getenv("PRUEBA_OBJ")) > 1) g_pagina = 1; menuReconstruir(); }   // (pruebas) abre el menu en esa pagina
        if (std::getenv("PRUEBA_OVEJA") && std::atoi(std::getenv("PRUEBA_OVEJA")) >= 3 && frame == 200) {
            const double fx = std::sin(g_j.yaw), fz = -std::cos(g_j.yaw);
            if (std::atoi(std::getenv("PRUEBA_OVEJA")) == 3) { Oveja o; o.x = g_j.p.x + fx * 2.5; o.z = g_j.p.z + fz * 2.5; o.y = alturaTerreno(o.x, o.z) + 0.3; o.yaw = g_j.yaw + 3.14; o.tSig = 1e9; g_ovejas.push_back(o); }
            else { double bx = g_j.p.x + fx * 2.5, bz = g_j.p.z + fz * 2.5; g_camas.push_back({std::floor(bx) + 0.5, alturaTerreno(bx, bz), std::floor(bz) + 0.5, 1}); }
        } else
        if (const char* pn = std::getenv("PRUEBA_NEAN")) if (frame == 200) {   // (pruebas) 3 neandertales delante: PRUEBA_NEAN=<distancia>
            const double fx = std::sin(g_j.yaw), fz = -std::cos(g_j.yaw), rx = -fz, rz = fx, d = std::atof(pn);
            for (int i = 0; i < 3; i++) {
                Nean n; n.x = g_j.p.x + fx * d + rx * (i - 1) * 2.0; n.z = g_j.p.z + fz * d + rz * (i - 1) * 2.0;
                n.y = alturaTerreno(n.x, n.z) + 0.3; n.yaw = g_j.yaw + 3.14159; n.tipo = i; n.tSig = 1e9; n.tProxAtk = std::getenv("NEAN_ATACA") ? 0.0 : 1e9; if (std::getenv("NEAN_TIPO")) n.tipo = std::atoi(std::getenv("NEAN_TIPO")); g_neans.push_back(n);
            }
        }
        if (std::getenv("PRUEBA_OVEJA") && frame == 200) {   // (pruebas) dos ovejas y una cama delante
            const double fx = std::sin(g_j.yaw), fz = -std::cos(g_j.yaw), rx = -fz, rz = fx;
            for (int i = 0; i < 2; i++) {
                Oveja o; o.x = g_j.p.x + fx * 4.5 + rx * (i ? 1.3 : -1.3); o.z = g_j.p.z + fz * 4.5 + rz * (i ? 1.3 : -1.3);
                o.y = alturaTerreno(o.x, o.z) + 0.3; o.yaw = g_j.yaw + 3.14159 + (i ? 0.5 : -0.5); o.tipo = i; o.tSig = 1e9; g_ovejas.push_back(o);
            }
            if (std::atoi(std::getenv("PRUEBA_OVEJA")) > 1) { double bx = g_j.p.x + fx * 5.0 - rx * 3.2, bz = g_j.p.z + fz * 5.0 - rz * 3.2; g_camas.push_back({std::floor(bx) + 0.5, alturaTerreno(bx, bz), std::floor(bz) + 0.5, 1}); }
        }
        if (const char* pt = std::getenv("PRUEBA_TNT")) if (frame == std::atoi(pt)) {   // (pruebas) cubo de TNT delante, encendido a 1 s
            std::vector<ChunkP> su;
            int bx = (int)std::floor((g_j.p.x + std::sin(g_j.yaw) * 7.0) * SUB), bz = (int)std::floor((g_j.p.z - std::cos(g_j.yaw) * 7.0) * SUB);
            int gy = (int)std::floor(alturaTerreno(bx / (double)SUB, bz / (double)SUB) * SUB);
            for (int dy = 0; dy < 6; dy++) for (int dz = 0; dz < 6; dz++) for (int dx = 0; dx < 6; dx++) escribirCubito(bx + dx, gy + dy, bz + dz, TNT, su);
            for (auto& c : su) remallarYa(c);
            tntEncender(bx, gy, bz, ahora, std::getenv("TNT_LARGO") ? 1.0 : 100000.0);
            std::printf("PRUEBA_TNT en %d %d %d\n", bx, gy, bz);
        }
        if (std::getenv("PRUEBA_FOSO2") && frame == 250) {   // (pruebas) pozo y tunel excavados dentro del terreno, con una antorcha
            std::vector<ChunkP> su;
            int bx = (int)std::floor(g_j.p.x * SUB) + 20, bz = (int)std::floor(g_j.p.z * SUB);
            int gy = (int)std::floor(alturaTerreno(bx / (double)SUB, bz / (double)SUB) * SUB);
            for (int dy = -30; dy <= 2; dy++) for (int dz = 0; dz < 12; dz++) for (int dx = 0; dx < 12; dx++) escribirCubito(bx + dx, gy + dy, bz + dz, AIRE, su);
            for (int dx = 12; dx < 60; dx++) for (int dy = -30; dy < -20; dy++) for (int dz = 3; dz < 8; dz++) escribirCubito(bx + dx, gy + dy, bz + dz, AIRE, su);
            escribirCubito(bx + 5, gy - 29, bz + 5, ANTORCHA, su);
            std::printf("PRUEBA_FOSO2 en bloques x=%.1f z=%.1f\n", bx / (double)SUB, bz / (double)SUB);
            for (auto& c : su) remallarYa(c);
        }
        if (const char* fc = std::getenv("PRUEBA_CHAT")) if (frame == std::atoi(fc) && g_red.activo()) g_red.chatEnviar("HOLA DESDE " + g_red.miNombre);   // (pruebas)
        if (std::getenv("PRUEBA_RED_CHK") && frame % 250 == 0 && frame >= 250 && g_red.activo()) {   // (pruebas) lee las celdas de las dos torres
            double hx = g_red.host ? g_j.p.x : g_red.spawn[0], hz = g_red.host ? g_j.p.z : g_red.spawn[2];
            int bxh = (int)std::floor(hx * SUB) + 12, bzh = (int)std::floor(hz * SUB) - 30, bxc = (int)std::floor((hx + 1.5) * SUB) + 12, bzc = (int)std::floor(hz * SUB) + 30;
            int gyh = (int)std::floor(alturaTerreno(bxh / (double)SUB, bzh / (double)SUB) * SUB), gyc = (int)std::floor(alturaTerreno(bxc / (double)SUB, bzc / (double)SUB) * SUB);
            std::printf("CHK frame %d  torreAnfitrion=%d torreCliente=%d  remotos=%d\n", frame, (int)leerCubito(bxh + 3, gyh + 3, bzh + 3), (int)leerCubito(bxc + 3, gyc + 3, bzc + 3), (int)g_red.rem.size());
        }
        if (const char* ft = std::getenv("PRUEBA_RED_TORRE")) if (frame == std::atoi(ft)) {   // (pruebas) torre pequena que SE ENVIA por la red
            std::vector<ChunkP> su;
            int bx = (int)std::floor(g_j.p.x * SUB) + 12, bz = (int)std::floor(g_j.p.z * SUB) + (g_argUnir.empty() ? -30 : 30);
            int gy = (int)std::floor(alturaTerreno(bx / (double)SUB, bz / (double)SUB) * SUB);
            g_redEnviar = true;
            for (int dy = 0; dy < 40; dy++) for (int dz = 0; dz < 16; dz++) for (int dx = 0; dx < 16; dx++) escribirCubito(bx + dx, gy + dy, bz + dz, (dy / 8) % 2 ? PIEDRA : MADERA, su);
            g_redEnviar = false;
            for (auto& c : su) remallarYa(c);
        }
        if (std::getenv("PRUEBA_TORRE") && frame == 250) {   // (pruebas) torre de piedra de 10x10 bloques y 40 de alto
            std::vector<ChunkP> su;
            int bx = (int)std::floor(g_j.p.x * SUB) + 20, bz = (int)std::floor(g_j.p.z * SUB);
            int gy = (int)std::floor(alturaTerreno(bx / (double)SUB, bz / (double)SUB) * SUB);
            for (int dy = 0; dy < 80; dy++) for (int dz = 0; dz < 20; dz++) for (int dx = 0; dx < 20; dx++) escribirCubito(bx + dx, gy + dy, bz + dz, (dy / 8) % 2 ? PIEDRA : MADERA, su);
            std::printf("PRUEBA_TORRE en bloques x=%.1f z=%.1f\n", bx / (double)SUB, bz / (double)SUB);
            for (auto& c : su) remallarYa(c);
        }
        if (std::getenv("PRUEBA_LEDS") && frame == 250) {   // (pruebas) fila de los 12 LED + una antorcha, delante del jugador
            std::vector<ChunkP> su;
            int bx0 = (int)std::floor(g_j.p.x * SUB) + 16, bz0 = (int)std::floor(g_j.p.z * SUB) - 40;
            int gy = (int)std::floor(alturaTerreno(bx0 / (double)SUB, bz0 / (double)SUB) * SUB);
            for (int k = 0; k < 12; k++) for (int dy = 0; dy < 2; dy++) for (int dz = 0; dz < 2; dz++) for (int dx = 0; dx < 2; dx++)
                escribirCubito(bx0, gy + dy, bz0 + k * 7 + dz + dx * 0, LED_ROJO + k, su);
            escribirCubito(bx0, gy, bz0 + 12 * 7 + 3, ANTORCHA, su);
            for (auto& c : su) remallarYa(c);
        }
        if (std::getenv("PRUEBA_AVION") && frame == 250) {   // (pruebas) avioneta de cubitos sobre una pista de piedra, lista para ensamblar
            std::vector<ChunkP> su;
            int ox = (int)std::floor(g_j.p.x * SUB) + 40, oz = (int)std::floor(g_j.p.z * SUB);
            int gy = (int)std::floor(alturaTerreno(ox / (double)SUB, oz / (double)SUB) * SUB) + 1;
            for (int z = oz - 330; z < oz + 70; z++) for (int x = ox - 40; x < ox + 40; x++) {
                escribirCubito(x, gy - 1, z, PIEDRA, su);
                for (int y = gy; y < gy + 40; y++) escribirCubito(x, y, z, AIRE, su);
            }
            int oy = gy + 6;
            auto P = [&](int x, int y, int z, uint8_t t) { escribirCubito(ox + x, oy + y, oz + z, t, su); };
            for (int x = -4; x <= 3; x++) for (int y = 0; y <= 7; y++) for (int z = 0; z <= 47; z++) {
                bool hueco = x >= -3 && x <= 2 && y >= 1 && y <= 6 && z >= 8 && z <= 40;
                P(x, y, z, hueco ? AIRE : (y == 7 && z >= 14 && z <= 34 ? VIDRIO : CARR_6));
            }
            for (int x = -2; x <= 1; x++) for (int z = 20; z <= 25; z++) for (int y = 1; y <= 2; y++) P(x, y, z, ASIENTO);
            for (int y = 1; y <= 3; y++) P(0, y, 17, TIMON);
            P(0, 1, 30, CONTROL);
            for (int z = 18; z <= 30; z++) for (int y = 3; y <= 4; y++) for (int x = 4; x <= 19; x++) { P(x, y, z, ALA); P(-1 - x, y, z, ALA); }
            for (int y = 8; y <= 15; y++) for (int z = 42; z <= 47; z++) for (int x = 0; x <= 1; x++) P(x, y, z, CARR_0);
            for (int z = 18; z <= 30; z++) { P(19, 4, z, LED_ROJO); P(-20, 4, z, LED_VERDE); }
            for (int x = 0; x <= 1; x++) { P(x, 15, 47, LED_BLANCO); P(x, 14, 47, LED_BLANCO); }
            for (int x = -1; x <= 0; x++) for (int y = 3; y <= 4; y++) P(x, y, -1, HELICE);
            for (int x = -7; x <= 6; x++) for (int y = 3; y <= 4; y++) P(x, y, -1, HELICE);
            for (int y = -3; y <= 10; y++) for (int x = -1; x <= 0; x++) P(x, y, -1, HELICE);
            for (int y = 1; y <= 3; y++) for (int z = 13; z <= 15; z++) { P(4, y, z, CARROCERIA); P(-5, y, z, CARROCERIA); }
            for (int a = 0; a < 10; a++) for (int b2 = 0; b2 < 10; b2++) {
                double ux = a + 0.5 - 5, uy = b2 + 0.5 - 5;
                if (ux * ux + uy * uy > 25) continue;
                for (int e = 0; e < 2; e++) { P(5 + e, b2 - 6 + 0, 10 + a, RUEDA); P(-7 + e, b2 - 6, 10 + a, RUEDA); }
            }
            for (auto& c : su) remallarYa(c);
            std::printf("PRUEBA_AVION en cubitos x=%d y=%d z=%d\n", ox, oy, oz);
            g_avionOrg[0] = ox; g_avionOrg[1] = oy + 1; g_avionOrg[2] = oz + 30;
        }
        if (const char* pa = std::getenv("PRUEBA_AVION")) if (pa[0] == '2' && frame == 255) {   // sin ensamblar: el jugador mira el costado del avion
            g_j.p = {g_avionOrg[0] / (double)SUB - 6.0, (g_avionOrg[1] - 7) / (double)SUB, g_avionOrg[2] / (double)SUB};
            g_j.volar = true; g_j.yaw = 1.5708;
            g_j.pitch = std::getenv("AVPITCH") ? std::atof(std::getenv("AVPITCH")) : 0.5;
        }
        if (std::getenv("PRUEBA_AVION") && std::getenv("PRUEBA_AVION")[0] != '2' && frame == 262) {    // ensamblar y subir
            vehEnsamblar(g_avionOrg[0], g_avionOrg[1], g_avionOrg[2]);
            g_j.p = g_veh.p + V3{3.0, 0.5, 0}; if (!std::getenv("PRUEBA_SINPILOTO")) g_pedirG = true;
            std::printf("AVION celdas=%d largo=%d enverg=%d sust=%.2f hel=%d asientos=%d timon=%d puntos=%d\n", (int)g_veh.celdas.size(), g_veh.largo, g_veh.enverg, g_veh.sust, g_veh.nHel, g_veh.nAsiento, g_veh.nTimon, (int)g_veh.pts.size());
        }
        if (std::getenv("PRUEBA_AVION_TECHO") && frame == 560) g_j.p.y += 6.0;      // (pruebas) saltar al techo del avion en pleno vuelo
        if (std::getenv("PRUEBA_AVION") && frame % 20 == 0 && g_veh.activo)
            std::printf("f=%d pos=%.1f %.1f %.1f v=%.1f %.1f %.1f pitch=%.2f gas=%.2f suelo=%d pil=%d | jug=%.1f %.1f %.1f sobre=%d\n", frame, g_veh.p.x, g_veh.p.y, g_veh.p.z, g_veh.v.x, g_veh.v.y, g_veh.v.z, g_veh.pitch, g_veh.gas, g_veh.suelo ? 1 : 0, g_veh.pilotando ? 1 : 0, g_j.p.x, g_j.p.y, g_j.p.z, g_sobreVeh ? 1 : 0);
        if (const char* fg = std::getenv("PRUEBA_RED_G")) if (frame == std::atoi(fg) && g_veh.activo) { g_j.p = g_veh.p + V3{3.0, 0.5, 0}; g_pedirG = true; }   // (pruebas) subir a la avioneta
        if (const char* fg = std::getenv("PRUEBA_RED_G2")) if (frame == std::atoi(fg) && g_veh.activo) g_pedirG = true;                                       // (pruebas) bajar
        if (std::getenv("PRUEBA_RED_VEH") && frame % 100 == 0 && frame >= 200)   // (pruebas) estado de la avioneta compartida
            std::printf("VEHRED frame %d activo=%d pil=%d pilotoRed=%d celdas=%d pos=%.1f %.1f %.1f gas=%.2f cinem=%d\n", frame, g_veh.activo ? 1 : 0, g_veh.pilotando ? 1 : 0, g_veh.pilotoRed, (int)g_veh.celdas.size(), g_veh.p.x, g_veh.p.y, g_veh.p.z, g_veh.gas, g_vehCinematico ? 1 : 0);
        if (const char* ps = std::getenv("PAUSA")) if (frame == 30) { g_pausa = true; g_pausaSub = std::atoi(ps) == 2 ? 1 : 0; }   // (pruebas)
        if (std::getenv("PRUEBA_AGUA") && frame == 250) {   // (pruebas) plataforma con una fuente encima: debe repartirse y caer por los bordes
            std::vector<ChunkP> su;
            int bx = (int)std::floor(g_j.p.x * SUB) + 14, bz = (int)std::floor(g_j.p.z * SUB);
            int gy = (int)std::floor(alturaTerreno(bx / (double)SUB, bz / (double)SUB) * SUB);
            for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) escribirCubito(bx + dx, gy + 14, bz + dz, PIEDRA, su);
            for (int dy = 0; dy < 14; dy++) escribirCubito(bx, gy + dy, bz, PIEDRA, su);
            escribirCubito(bx, gy + 15, bz, AGUA, su);
            std::printf("PRUEBA_AGUA en bloques x=%.1f y=%.1f z=%.1f\n", bx / (double)SUB, gy / (double)SUB, bz / (double)SUB);
            for (auto& c : su) remallarYa(c);
        }
        // ---- agua que fluye
        if (!cargando && ahora - tAgua >= 0.10) { tAgua = ahora; tickAgua(); }
        if (!cargando && !g_tnt.empty()) tntActualizar(ahora);

        // ---- dibujo
        glViewport(0, 0, fbw, fbh);
        // hora del dia: 0 amanecer, 0.25 mediodia, 0.5 atardecer, 0.75 medianoche
        g_hora = std::fmod(g_hora + dt / 600.0, 1.0);
        if (g_modoLuna) g_hora = 0.09;                 // Luna: sol fijo y bajo (sombras largas)
        float sh = (float)std::sin(g_hora * 6.2831853);
        float dia = std::max(0.0f, std::min(1.0f, (sh + 0.25f) / 0.60f));
        dia = dia * dia * (3.0f - 2.0f * dia);
        float skyMul = 0.14f + 0.86f * dia;
        float cielo[3] = {0.015f + 0.535f * dia, 0.02f + 0.73f * dia, 0.06f + 0.89f * dia};
        float ocaso = std::exp(-(sh / 0.28f) * (sh / 0.28f)) * 0.45f;
        cielo[0] += (0.95f - cielo[0]) * ocaso; cielo[1] += (0.50f - cielo[1]) * ocaso; cielo[2] += (0.30f - cielo[2]) * ocaso;
        // ---- clima: maquina de estados, techo encima, color del cielo, niebla y rayos
        float clK = 0.0f, clFog = 0.0f, rayoF = 0.0f, climaAlt = 1.0f;
        {
            if (!g_climaFijo && !cargando) {
                g_climaT -= dt;
                if (g_climaT <= 0) {
                    float r = climaRand(); int n = g_climaE;
                    if (g_climaE == 0) n = r < 0.35f ? 1 : r < 0.65f ? 2 : r < 0.75f ? 3 : 4;
                    else if (g_climaE == 2 && r < 0.30f) n = 3;
                    else if (r < 0.60f) n = 0;   // vuelve a despejar
                    else n = 1 + (int)(climaRand() * 4.0f) % 4;
                    if (n == g_climaE) n = 0;
                    g_climaE = n; g_climaT = 80.0 + climaRand() * 160.0;
                }
            }
            static const float oN[5] = {0.0f, 0.65f, 0.85f, 1.0f, 0.35f}, oP[5] = {0, 0, 0.75f, 1.0f, 0}, oT[5] = {0, 0, 0, 1.0f, 0}, oF[5] = {0, 0.12f, 0.30f, 0.40f, 1.0f};
            auto acerca = [&](float& v, float obj, float vel) {
                if (v < -0.5f) v = obj;
                else v += std::max(-vel * (float)dt, std::min(vel * (float)dt, obj - v));
            };
            acerca(g_clN, oN[g_climaE], 0.05f); acerca(g_clP, oP[g_climaE], 0.06f);
            acerca(g_clT, oT[g_climaE], 0.05f); acerca(g_clF, oF[g_climaE], 0.04f);
            static int cubFr = 0; static float cubObj = 0;
            if (cubFr++ % 6 == 0) {            // hay algo encima (techo, cueva, arbol)?
                int cx0 = (int)std::floor(cam.x * SUB), cy0 = (int)std::floor(cam.y * SUB), cz0 = (int)std::floor(cam.z * SUB);
                cubObj = 0;
                for (int k = 2; k <= 100; k++) {
                    uint8_t t = leerCubito(cx0, cy0 + k, cz0);
                    if (t != AIRE && !esAgua(t) && !esPlanta(t)) { cubObj = 1; break; }
                }
            }
            acerca(g_clCub, cubObj, 1.5f);
            static const bool forzarNieve = std::getenv("NIEVE") != nullptr;   // (pruebas)
            static int frio = 0, frioFr = 0;
            if (frioFr++ % 20 == 0) {    // bioma frio bajo el jugador: nieve en vez de lluvia
                float wb[BIO_N]; int dm = biomaPesos(cam.x, cam.z, wb);
                frio = (dm == BIO_TUNDRA || dm == BIO_GLACIAR || dm == BIO_TAIGA || dm == BIO_PICOS || (dm == BIO_OCEANO && temp01(cam.x, cam.z) < 0.2f)) ? 1 : 0;
            }
            acerca(g_clNieve, (forzarNieve || frio) ? 1.0f : 0.0f, forzarNieve ? 5.0f : 0.30f);
            // rayos
            static const bool testRayo = std::getenv("RAYOTEST") != nullptr;   // (pruebas)
            if (testRayo) g_rayoSig = 0;
            if (g_clT > 0.6f && !cargando && ahora > g_rayoSig) {
                g_rayoT = ahora; g_rayoSig = ahora + 4.0 + climaRand() * 13.0;
                float a = climaRand() * 6.2831853f, d = 45.0f + climaRand() * 110.0f;
                float gx = (float)cam.x + std::cos(a) * d, gz = (float)cam.z + std::sin(a) * d;
                float gy = (float)alturaTerreno(gx, gz), top = (float)(110.0 + OFFY + 10.0);
                g_rayoPts.clear();
                const int NS = 14;
                float px = gx + (climaRand() - 0.5f) * 30.0f, pz = gz + (climaRand() - 0.5f) * 30.0f;
                for (int i = 0; i <= NS; i++) {
                    float f = (float)i / NS;
                    float jx = (i == 0 || i == NS) ? 0.0f : (climaRand() - 0.5f) * 14.0f, jz = (i == 0 || i == NS) ? 0.0f : (climaRand() - 0.5f) * 14.0f;
                    float x = gx + (px - gx) * (1.0f - f) + jx, z = gz + (pz - gz) * (1.0f - f) + jz;
                    g_rayoPts.push_back(x); g_rayoPts.push_back(top + (gy - top) * f); g_rayoPts.push_back(z);
                }
            }
            double dr = ahora - g_rayoT;
            if (dr >= 0 && dr < 0.6) rayoF = (dr < 0.09 ? 1.0f : dr < 0.15 ? 0.15f : dr < 0.30 ? 0.85f * (float)(1.0 - (dr - 0.15) / 0.15) + 0.1f : 0.1f * (float)(1.0 - (dr - 0.3) / 0.3));
            float sobre = std::max(0.0f, std::min(1.0f, ((float)cam.y - 230.0f) / 90.0f)); sobre = sobre * sobre * (3.0f - 2.0f * sobre);
            climaAlt = 1.0f - sobre;                  // por encima de las nubes (y mas arriba) el clima ya no afecta
            clK = std::max(0.95f * g_clN, 0.90f * g_clF) * climaAlt;
            clFog = std::max(g_clF, std::max(0.50f * g_clP, 0.30f * g_clN)) * climaAlt;
            rayoF *= climaAlt;
            if (clK > 0.001f) {
                const float lum = (0.10f + 0.90f * dia) * (0.80f - 0.38f * g_clT);
                const float gris[3] = {0.60f * lum + 0.04f, 0.64f * lum + 0.05f, 0.70f * lum + 0.07f};
                for (int k = 0; k < 3; k++) cielo[k] += (gris[k] - cielo[k]) * clK;
                skyMul *= 1.0f - 0.32f * clK - 0.22f * g_clT * climaAlt;
            }
            if (rayoF > 0.001f) { for (int k = 0; k < 3; k++) cielo[k] += (1.0f - cielo[k]) * 0.55f * rayoF; skyMul = std::min(1.0f, skyMul + 0.55f * rayoF); }
            if (g_modoLuna) { clK = 0; clFog = 0; rayoF = 0; g_clN = g_clP = g_clT = g_clF = 0; g_clNieve = 0; cielo[0] = cielo[1] = cielo[2] = 0.0f; skyMul = 1.0f; }   // sin atmosfera
        }
        // ---- bajo el agua: niebla azul que se oscurece y se acorta con la profundidad (mar profundo)
        const bool camAgua = esAgua(leerCubito((int)std::floor(cam.x * SUB), (int)std::floor(cam.y * SUB), (int)std::floor(cam.z * SUB)));
        float aguaFogA = -1.0f, aguaFogB = -1.0f;
        if (camAgua) {
            float pf = (float)std::max(0.0, WATER_LEVEL - cam.y) / 42.0f;
            pf = std::min(1.0f, pf); pf = pf * pf * (3.0f - 2.0f * pf);
            const float luzD = 0.22f + 0.78f * dia;
            const float somero[3] = {0.07f, 0.38f, 0.55f}, hondo[3] = {0.008f, 0.045f, 0.115f};
            for (int k = 0; k < 3; k++) cielo[k] = (somero[k] + (hondo[k] - somero[k]) * pf) * luzD;
            aguaFogA = 1.0f; aguaFogB = 120.0f - 92.0f * pf;
        }
        if (!camAgua && clFog > 0.02f) {      // niebla del clima: acorta la distancia de vision
            const float bB = (float)(g_lodOn ? LOD_FOG_MAX : VIEW_BLOCKS) * 0.97f, bA = (float)(g_lodOn ? LOD_FOG_MAX * 0.55 : VIEW_BLOCKS * 0.60);
            aguaFogB = std::max(55.0f, bB * (1.0f - 0.93f * clFog));
            aguaFogA = std::max(2.0f, std::min(aguaFogB * 0.8f, bA * (1.0f - clFog)));
        }
        // ---- graficos mejorados: sombras de sol (mapa de profundidad desde el sol) y escena en un framebuffer HDR
        const bool gfx = g_graf && pp.ok;
        float sombraOn = 0.0f, solF = 0.0f;
        float solV[3] = {0, 1, 0};
        float SVP[16] = {};
        const float SOMB_R = 100.0f, SOMB_D = 250.0f;
        {
            float th0 = (float)(g_hora * 6.2831853);
            float ax = std::cos(th0), ay = std::sin(th0), az = 0.30f, al = std::sqrt(ax * ax + ay * ay + az * az);
            solV[0] = ax / al; solV[1] = ay / al; solV[2] = az / al;
            float e = std::max(0.0f, std::min(1.0f, solV[1] * 4.0f));
            solF = e * e * (3.0f - 2.0f * e);
            if (g_modoLuna) { solV[0] = (float)g_solLuna.x; solV[1] = (float)g_solLuna.y; solV[2] = (float)g_solLuna.z; float e2_ = std::max(0.0f, std::min(1.0f, solV[1] * 4.0f)); solF = e2_ * e2_ * (3.0f - 2.0f * e2_); }
            solF *= 1.0f - 0.9f * clK;      // nublado: sombras suaves o ninguna
        }
        if (gfx) {
            ppTamano(pp, fbw, fbh);
            if (solF > 0.01f) {
                sombraOn = 1.0f;
                double Lx = solV[0], Ly = solV[1], Lz = solV[2];
                double rx = Ly * 0.0 - Lz * 1.0 * 0.0, ry = 0, rz = 0;   // r = normalizado(arriba x L), arriba = (0,1,0)
                rx = 1.0 * Lz - 0.0 * Ly; ry = 0.0 * Lx - 0.0 * Lz; rz = 0.0 * Ly - 1.0 * Lx;
                double rl = std::sqrt(rx * rx + ry * ry + rz * rz); rx /= rl; ry /= rl; rz /= rl;
                double ux = Ly * rz - Lz * ry, uy = Lz * rx - Lx * rz, uz = Lx * ry - Ly * rx;      // u = L x r
                double texel = 2.0 * SOMB_R / pp.shRes;
                double cx_ = cam.x * rx + cam.y * ry + cam.z * rz, cy_ = cam.x * ux + cam.y * uy + cam.z * uz;
                double offX = cx_ - std::floor(cx_ / texel) * texel, offY = cy_ - std::floor(cy_ / texel) * texel;
                // matriz (columna por columna): x=(r.p+offX)/R, y=(u.p+offY)/R, z=-(L.p)/D
                SVP[0] = (float)(rx / SOMB_R); SVP[4] = (float)(ry / SOMB_R); SVP[8] = (float)(rz / SOMB_R); SVP[12] = (float)(offX / SOMB_R);
                SVP[1] = (float)(ux / SOMB_R); SVP[5] = (float)(uy / SOMB_R); SVP[9] = (float)(uz / SOMB_R); SVP[13] = (float)(offY / SOMB_R);
                SVP[2] = (float)(-Lx / SOMB_D); SVP[6] = (float)(-Ly / SOMB_D); SVP[10] = (float)(-Lz / SOMB_D); SVP[14] = 0.0f;
                SVP[3] = 0; SVP[7] = 0; SVP[11] = 0; SVP[15] = 1.0f;
                // el mapa de sombras esta fijo al mundo (celdas de texel): solo se rehace si cambia la celda, se mueve el sol o pasan 4 cuadros
                static long long shCx = 0, shCy = 0; static float shSol[3] = {9, 9, 9}; static int shEdad = 1000;
                long long ncx = (long long)std::floor(cx_ / texel), ncy = (long long)std::floor(cy_ / texel);
                float dsol = std::fabs(solV[0] - shSol[0]) + std::fabs(solV[1] - shSol[1]) + std::fabs(solV[2] - shSol[2]);
                bool rehacer = ncx != shCx || ncy != shCy || dsol > 0.0004f || ++shEdad >= 4;
                if (rehacer) {
                shCx = ncx; shCy = ncy; shSol[0] = solV[0]; shSol[1] = solV[1]; shSol[2] = solV[2]; shEdad = 0;
                glBindFramebuffer(GL_FRAMEBUFFER, pp.fboSh);
                glViewport(0, 0, pp.shRes, pp.shRes);
                glClear(GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
                glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(2.0f, 4.0f);
                glUseProgram(pp.pSombra);
                glUniformMatrix4fv(uSVPs, 1, GL_FALSE, SVP);
                glUniform1f(uVSs, (float)VS);
                const double tamS = CS * VS;
                std::unordered_map<int64_t, float> sombSup;       // superficie real 3x3 por columna (una vez por pasada)
                const int pcxS = divPiso((int)std::floor(cam.x), 16), pczS = divPiso((int)std::floor(cam.z), 16);
                for (auto& kv : g_mapa) {
                    Chunk* c = kv.second.get();
                    if (c->state.load(std::memory_order_acquire) == ST_GEN || !c->gOp.n) continue;
                    double ddx = (c->cx + 0.5) * tamS - cam.x, ddz = (c->cz + 0.5) * tamS - cam.z;
                    if (std::fabs(ddx) > SOMB_R + tamS || std::fabs(ddz) > SOMB_R + tamS) continue;
                    // lo enterrado bajo la superficie no puede dar sombra a nada visible (salvo junto al jugador, donde se excava)
                    if (std::abs(c->cx - pcxS) > 3 || std::abs(c->cz - pczS) > 3) {
                        const int64_t ck = colKey(c->cx, c->cz);
                        auto is = sombSup.find(ck);
                        float sm;
                        if (is != sombSup.end()) sm = is->second;
                        else { sm = supMin3(c->cx, c->cz); sombSup[ck] = sm; }
                        if (sm > -1e8f && (double)(c->cy + 1) * tamS < sm - 2.0) continue;
                    }
                    glUniform3f(uRels, (float)(c->cx * tamS - cam.x), (float)(c->cy * tamS - cam.y), (float)(c->cz * tamS - cam.z));
                    glBindVertexArray(c->gOp.vao);
                    glDrawElements(GL_TRIANGLES, c->gOp.n, GL_UNSIGNED_INT, (const void*)0);
                }
                glDisable(GL_POLYGON_OFFSET_FILL);
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, pp.fboEsc);
            glViewport(0, 0, fbw, fbh);
        }
        glClearColor(cielo[0], cielo[1], cielo[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        {   // cielo: degradado + estrellas + sol + luna
            V3 fw = norm(mira), rg = norm(cruz(fw, camUp));
            V3 up = cruz(rg, fw);
            float tanY = std::tan(1.22f * 0.5f), tanX = tanY * (float)fbw / fbh;
            float th = (float)(g_hora * 6.2831853);
            float sx = std::cos(th), sy = std::sin(th), sz = 0.30f, sl = std::sqrt(sx * sx + sy * sy + sz * sz);
            float zen[3] = {0.0f + 0.22f * dia, 0.005f + 0.42f * dia, 0.03f + 0.82f * dia};
            for (int k = 0; k < 3; k++) zen[k] += (cielo[k] * 0.88f - zen[k]) * clK;
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glUseProgram(progCielo);
            glUniform2f(glGetUniformLocation(progCielo, "uRes"), (float)fbw, (float)fbh);
            glUniform3f(glGetUniformLocation(progCielo, "uFwd"), (float)fw.x, (float)fw.y, (float)fw.z);
            glUniform3f(glGetUniformLocation(progCielo, "uRight"), (float)rg.x, (float)rg.y, (float)rg.z);
            glUniform3f(glGetUniformLocation(progCielo, "uUp"), (float)up.x, (float)up.y, (float)up.z);
            glUniform2f(glGetUniformLocation(progCielo, "uTan"), tanX, tanY);
            float ea = std::max(0.0f, std::min(1.0f, ((float)cam.y - 250.0f) / 1350.0f)); ea = ea * ea * (3.0f - 2.0f * ea);
            if (g_modoLuna) ea = 1.0f;
            const float atmo = 1.0f - ea;                     // 1 = aire normal, 0 = espacio
            glUniform3f(glGetUniformLocation(progCielo, "uHor"), cielo[0] * atmo, cielo[1] * atmo, cielo[2] * atmo);
            glUniform3f(glGetUniformLocation(progCielo, "uHaze"), cielo[0], cielo[1], cielo[2]);
            glUniform1f(glGetUniformLocation(progCielo, "uAlt"), g_modoLuna ? 3000.0f : (float)cam.y);
            double tEx, tEz; ckTierraXZ(cam, tEx, tEz);        // centro de la Tierra (bajo la camara, o fijo si ya saliste rumbo a la Luna)
            if (g_modoLuna) {     // desde la Luna la Tierra es una esfera enorme en el cielo (altura 35 grados, al lado contrario del sol)
                glUniform3f(glGetUniformLocation(progCielo, "uOc"), (float)(-g_tierraM.x), (float)(-g_tierraM.y), (float)(-g_tierraM.z));
            } else glUniform3f(glGetUniformLocation(progCielo, "uOc"), (float)(cam.x - tEx), (float)(cam.y + CK_RP), (float)(cam.z - tEz));
            glUniform2f(glGetUniformLocation(progCielo, "uCamXZ"), (float)std::fmod(tEx, 60000.0), (float)std::fmod(tEz, 60000.0));
            {
                V3 mr = ckLunaRel(cam);
                glUniform3f(glGetUniformLocation(progCielo, "uLunaC"), (float)mr.x, (float)mr.y, (float)mr.z);
                glUniform1f(glGetUniformLocation(progCielo, "uLunaR"), cam.y > 400.0 ? (float)CK_LUNA_R : 0.0f);
            }
            glUniform1f(glGetUniformLocation(progCielo, "uEsp"), ea);
            glUniform1f(glGetUniformLocation(progCielo, "uLunaOn"), ea);
            glUniform1f(glGetUniformLocation(progCielo, "uAtmoF"), g_modoLuna ? 0.0f : (g_espAtard ? 1.0f : atmo));
            {   // mapa de este mundo para la esfera de la Tierra
                static GLuint texMapa = 0;
                if (cam.y > 200.0) {
                    if (g_mapaEstado.load() == 2) {
                        if (!texMapa) { glGenTextures(1, &texMapa); }
                        glActiveTexture(GL_TEXTURE0 + 15);
                        glBindTexture(GL_TEXTURE_2D, texMapa);
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, MAPA_N, MAPA_N, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_mapaBuf.data());
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        glActiveTexture(GL_TEXTURE0);
                        g_mapaTiene = true; g_mapaEstado.store(0);
                    }
                    double dd = std::max(std::fabs(tEx - g_mapaCx), std::fabs(tEz - g_mapaCz));
                    if (g_mapaEstado.load() == 0 && (!g_mapaTiene || dd > 5000.0)) {
                        g_mapaEstado.store(1);
                        std::thread(mapaCalcular, tEx, tEz, g_semilla).detach();
                    }
                }
                if (g_mapaTiene) g_mapaOk = std::min(1.0f, g_mapaOk + (float)dt * 0.8f);
                if (g_mapaTiene && texMapa) {
                    glActiveTexture(GL_TEXTURE0 + 15);
                    glBindTexture(GL_TEXTURE_2D, texMapa);
                    glActiveTexture(GL_TEXTURE0);
                    glUniform1i(glGetUniformLocation(progCielo, "uMapa"), 15);
                }
                glUniform2f(glGetUniformLocation(progCielo, "uMapaOff"), (float)(tEx - g_mapaCx), (float)(tEz - g_mapaCz));
                glUniform1f(glGetUniformLocation(progCielo, "uMapaOk"), g_mapaTiene ? g_mapaOk : 0.0f);
            }

            glUniform3f(glGetUniformLocation(progCielo, "uZen"), zen[0] * atmo, zen[1] * atmo, zen[2] * atmo);
            {
                V3 su_ = {sx / sl, sy / sl, sz / sl};
                if (g_modoLuna) su_ = g_solLuna;
                else g_solHora = norm(su_);
                glUniform3f(glGetUniformLocation(progCielo, "uSun"), (float)su_.x, (float)su_.y, (float)su_.z);
            }
            glUniform1f(glGetUniformLocation(progCielo, "uNoche"), std::max((1.0f - dia) * (1.0f - clK), ea));
            glUniform1f(glGetUniformLocation(progCielo, "uClima"), clK);
            glUniform1f(glGetUniformLocation(progCielo, "uTime"), (float)std::fmod(ahora, 4000.0));
            glUniform1f(glGetUniformLocation(progCielo, "uMejor"), g_cielo ? 1.0f : 0.0f);
            glBindVertexArray(vaoVacio);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glEnable(GL_MULTISAMPLE);

        {   // v75: horizonte real: muy alto el suelo de abajo tambien se dibuja simple
            const bool espacio = g_ck.activo && g_ck.fase == 2 && g_terraFija;
            if (!g_horReal || !g_lodOn || g_modoLuna || espacio) g_altoLod = false;
            else if (!g_altoLod && cam.y > 420.0) g_altoLod = true;
            else if (g_altoLod && cam.y < 340.0) g_altoLod = false;
            g_lodRin0 = g_altoLod ? 0.0 : 100.0;
            if (espacio && !g_lod.empty()) lodLiberarTodo();   // v77: en orbita el planeta es solo la textura fija (el dibujo lejano ya no te sigue)
            g_lodEsp = espacio;
        }
        if (g_lodOn && !g_lodEsp) {   // ---- mundo lejano: se dibuja primero con su propia proyeccion y luego se borra la profundidad
            for (auto& kv : g_lod) if (kv.second->estado.load(std::memory_order_acquire) == 1) { lodSubir(*kv.second); break; }
            lodReemplazar();
            if (frame % 6 == 0) { lodProcesarRegen(); lodActualizar(cam.x, cam.z); }
            float projL[16], vpL[16], plL[6][4];
            matPerspectiva(projL, 1.22f, (float)fbw / fbh, 1.5f, 30000.0f);
            matMul(vpL, projL, vista);
            planosFrustum(vpL, plL);
            glUseProgram(progLod);
            glUniformMatrix4fv(glGetUniformLocation(progLod, "uVP"), 1, GL_FALSE, vpL);
            glUniform3f(glGetUniformLocation(progLod, "uSky"), cielo[0], cielo[1], cielo[2]);
            glUniform1f(glGetUniformLocation(progLod, "uFogA"), aguaFogA > 0 ? aguaFogA : (float)(LOD_FOG_MAX * 0.55));
            glUniform1f(glGetUniformLocation(progLod, "uFogB"), aguaFogB > 0 ? aguaFogB : (float)(LOD_FOG_MAX * 0.97));
            glUniform1f(glGetUniformLocation(progLod, "uSkyMul"), skyMul);
            {   // v76: horizonte real: curvatura segun la altura, sin niebla desde el espacio y borde redondo
                const float kAlt = g_horReal ? (float)std::min(1.0, std::max(0.0, (cam.y - 200.0) / 1000.0)) : 0.0f;
                g_lodCurv = kAlt * kAlt * (3.0f - 2.0f * kAlt);
                if (const char* ec_ = std::getenv("LODCURV")) g_lodCurv = (float)std::atof(ec_);
                glUniform1f(glGetUniformLocation(progLod, "uCurv"), g_lodCurv);
                glUniform1f(glGetUniformLocation(progLod, "uFogK"), g_horReal ? 1.0f - (float)std::min(1.0, std::max(0.0, (cam.y - 600.0) / 2000.0)) : 1.0f);
                glUniform1f(glGetUniformLocation(progLod, "uEdge"), g_horReal ? (float)lodFogMax() : 1e9f);
            }
            GLint lRel = glGetUniformLocation(progLod, "uRel"), lAlfa = glGetUniformLocation(progLod, "uAlpha");
            std::vector<LodTile*> vis;
            for (auto& kv : g_lod) {
                LodTile* t = kv.second.get();
                if (t->estado.load(std::memory_order_acquire) != 2) continue;
                if (!lodNecesario(t->niv, t->tx, t->tz, cam.x, cam.z, 0.0)) continue;
                double ts = (double)LOD_N * LOD_CELDA[t->niv];
                double mnx = t->tx * ts - cam.x, mny = -cam.y, mnz = t->tz * ts - cam.z, hy = 100.0;
                if (g_lodCurv > 0.0f) {   // la curvatura baja el terreno lejano: se amplia la caja para no recortarlo de mas
                    double hm = std::sqrt(std::max(mnx * mnx, (mnx + ts) * (mnx + ts)) + std::max(mnz * mnz, (mnz + ts) * (mnz + ts)));
                    hm = std::min(hm, 5880.0);
                    double drop = g_lodCurv * (6000.0 - std::sqrt(36000000.0 - hm * hm));
                    mny -= drop; hy += drop;
                }
                bool fuera = false;
                for (int k = 0; k < 6 && !fuera && g_lodCurv < 0.3f; k++) {   // con curvatura fuerte no se recorta (la caja plana ya no sirve)
                    float qx = plL[k][0] > 0 ? (float)(mnx + ts) : (float)mnx;
                    float qy = plL[k][1] > 0 ? (float)(mny + hy) : (float)mny;
                    float qz = plL[k][2] > 0 ? (float)(mnz + ts) : (float)mnz;
                    if (plL[k][0] * qx + plL[k][1] * qy + plL[k][2] * qz + plL[k][3] < 0) fuera = true;
                }
                if (!fuera) vis.push_back(t);
            }
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
            glUniform1f(lAlfa, 1.0f);
            for (LodTile* t : vis) if (t->n[0]) {
                double ts = (double)LOD_N * LOD_CELDA[t->niv];
                (void)ts;
                glUniform3f(lRel, (float)(t->tx * ts - cam.x), (float)(-cam.y), (float)(t->tz * ts - cam.z));
                glBindVertexArray(t->vao[0]);
                glDrawElements(GL_TRIANGLES, t->n[0], GL_UNSIGNED_SHORT, (const void*)0);
            }
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_CULL_FACE);
            glUniform1f(lAlfa, 0.65f);
            for (LodTile* t : vis) if (t->n[1]) {
                double ts = (double)LOD_N * LOD_CELDA[t->niv];
                glUniform3f(lRel, (float)(t->tx * ts - cam.x), (float)(-cam.y), (float)(t->tz * ts - cam.z));
                glBindVertexArray(t->vao[1]);
                glDrawElements(GL_TRIANGLES, t->n[1], GL_UNSIGNED_SHORT, (const void*)0);
            }
            glDisable(GL_BLEND);
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_CULL_FACE);
            // el terreno lejano TAMBIEN tapa: se vuelve a escribir su profundidad (bajada unos bloques) con la proyeccion del mundo real,
            // asi lo que esta por debajo de el (cuevas y subsuelo de zonas editadas lejanas) queda escondido, como si fuera una pared
            static const bool lodTapa = std::getenv("NOLODTAPA") == nullptr;
            if (lodTapa) {
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glUniformMatrix4fv(glGetUniformLocation(progLod, "uVP"), 1, GL_FALSE, vp);
                glUniform1f(lAlfa, 1.0f);
                for (LodTile* t : vis) if (t->n[0]) {
                    double ts = (double)LOD_N * LOD_CELDA[t->niv];
                    glUniform3f(lRel, (float)(t->tx * ts - cam.x), (float)(-cam.y - 3.0), (float)(t->tz * ts - cam.z));
                    glBindVertexArray(t->vao[0]);
                    glDrawElements(GL_TRIANGLES, t->n[0], GL_UNSIGNED_SHORT, (const void*)0);
                }
                glDisable(GL_CULL_FACE);
                for (LodTile* t : vis) if (t->n[1]) {      // el agua lejana tambien tapa el fondo (oscuro) de los chunks reales que hay debajo
                    double ts = (double)LOD_N * LOD_CELDA[t->niv];
                    glUniform3f(lRel, (float)(t->tx * ts - cam.x), (float)(-cam.y - 2.0), (float)(t->tz * ts - cam.z));
                    glBindVertexArray(t->vao[1]);
                    glDrawElements(GL_TRIANGLES, t->n[1], GL_UNSIGNED_SHORT, (const void*)0);
                }
                glEnable(GL_CULL_FACE);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            }
        }

        // planos del frustum (en espacio relativo a la camara)
        float pl[6][4];
        {
            const float* m = vp;
            float r0[4] = {m[0], m[4], m[8], m[12]}, r1[4] = {m[1], m[5], m[9], m[13]};
            float r2[4] = {m[2], m[6], m[10], m[14]}, r3[4] = {m[3], m[7], m[11], m[15]};
            for (int k = 0; k < 4; k++) {
                pl[0][k] = r3[k] + r0[k]; pl[1][k] = r3[k] - r0[k];
                pl[2][k] = r3[k] + r1[k]; pl[3][k] = r3[k] - r1[k];
                pl[4][k] = r3[k] + r2[k]; pl[5][k] = r3[k] - r2[k];
            }
        }
        glUseProgram(progMundo);
        glUniformMatrix4fv(uVP, 1, GL_FALSE, vp);
        glUniform1f(uVS, (float)VS);
        glUniform1f(uSub, (float)SUB);
        glUniform3f(uSky, cielo[0], cielo[1], cielo[2]);
        glUniform1i(uLuz, 0);
        glUniform1f(uLMax, (float)LMAX);
        glUniform1f(uLN, (float)LN);
        glUniform1f(uSkyMul, skyMul);
        glUniform1f(glGetUniformLocation(progMundo, "uTime"), (float)std::fmod(ahora, 4000.0));
        glUniform1f(glGetUniformLocation(progMundo, "uAguaFx"), (float)g_agua);
        glUniform1f(glGetUniformLocation(progMundo, "uViento"), g_viento ? 1.0f : 0.0f);
        glUniform1f(glGetUniformLocation(progMundo, "uBuceo"), esAgua(leerCubito((int)std::floor(cam.x * SUB), (int)std::floor(cam.y * SUB), (int)std::floor(cam.z * SUB))) ? 1.0f : 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glUniform1f(uFogB, aguaFogB > 0 ? aguaFogB : (float)(g_lodOn ? LOD_FOG_MAX : VIEW_BLOCKS) * 0.97f);
        glUniform1f(uFogA, aguaFogA > 0 ? aguaFogA : (float)(g_lodOn ? LOD_FOG_MAX * 0.55 / 0.60 : VIEW_BLOCKS) * 0.60f);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, pp.texSh);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(uSomTex, 1);
        glUniformMatrix4fv(uSVPm, 1, GL_FALSE, SVP);
        glUniform3f(uSolDir, solV[0], solV[1], solV[2]);
        glUniform1f(uSomOn, sombraOn);
        glUniform1f(uSomR, SOMB_R);
        glUniform1f(uSolF, solF);
        glUniform1f(uHDRm, gfx ? 1.0f : 0.0f);
        glUniform1f(uSomTx, 3.0f / pp.shRes);

        // ---- oclusion: no se dibuja lo tapado por el relieve (detras de una montana) ni lo enterrado hondo bajo la superficie
        static std::unordered_map<int64_t, float> s_colMin;      // altura minima del terreno en cada columna de chunks (se calcula una vez)
        static uint32_t s_colSem = 0xFFFFFFFFu;
        if (s_colSem != g_semilla) { s_colMin.clear(); s_colSem = g_semilla; }
        static const bool oclOn = std::getenv("NOOCL") == nullptr;
        auto minCol = [&](int cx_, int cz_) -> float {
            int64_t k = colKey(cx_, cz_);
            if (!g_editCols.empty() && g_editCols.count(k)) return supMinColumna(cx_, cz_);     // columna editada: su superficie real (con tus cambios)
            auto it = s_colMin.find(k);
            if (it != s_colMin.end()) return it->second;
            float mh = 1e9f;
            const double bx0 = (double)cx_ * CS / SUB, bz0 = (double)cz_ * CS / SUB, anc = (double)CS / SUB;
            for (int a = 0; a <= 4; a++) for (int b = 0; b <= 4; b++) mh = std::min(mh, (float)alturaTerreno(bx0 + anc * a / 4.0, bz0 + anc * b / 4.0));
            if (s_colMin.size() > 20000) s_colMin.clear();
            s_colMin[k] = mh;
            return mh;
        };
        const double tamB = CS * VS;                                 // lado del chunk en bloques
        const bool camSobre = oclOn && cam.y > alturaTerreno(cam.x, cam.z) + 1.0;     // solo se oculta si estas sobre el suelo
        auto rayoTapado = [&](double tx, double ty, double tz, int tcx, int tcz) -> bool {
            double dx = tx - cam.x, dy = ty - cam.y, dz = tz - cam.z;
            double dist = std::sqrt(dx * dx + dz * dz);
            int n = (int)(dist / (tamB * 0.5)) + 1;
            for (int i = 1; i < n; i++) {
                double t = (double)i / n;
                double px = cam.x + dx * t, pz = cam.z + dz * t, py = cam.y + dy * t;
                int ccx = (int)std::floor(px / tamB), ccz = (int)std::floor(pz / tamB);
                if ((ccx == tcx && ccz == tcz) || (ccx == pcx && ccz == pcz)) continue;
                if (py < minCol(ccx, ccz) - 1.5f) return true;
            }
            return false;
        };
        int ocluidos = 0;
        std::vector<Chunk*> visibles;
        std::unordered_map<int64_t, bool> colOk;
        std::unordered_map<int64_t, double> colMinH;
        std::unordered_map<int64_t, float> colSup3;
        const double tam = CS * VS;
        for (auto& kv : g_mapa) {
            Chunk* c = kv.second.get();
            if (c->state.load(std::memory_order_acquire) == ST_GEN) continue;     // se sigue dibujando la malla anterior mientras se rehace
            if (!c->gOp.n && !c->gAg.n) continue;
            if (g_altoLod && !g_ancla.count(colKey(c->cx, c->cz)) && !g_requeridos.count(colKey(c->cx, c->cz))) continue;   // v75: desde muy alto lo de abajo es mundo lejano
            if (g_conservar) {      // chunks conservados fuera del radio: no se dibujan (de eso se encarga el mundo lejano)
                int ex = c->cx - pcx, ez = c->cz - pcz;
                if (ex * ex + ez * ez > (RC + 3) * (RC + 3) && !g_requeridos.count(colKey(c->cx, c->cz)) && !g_redCols.count(colKey(c->cx, c->cz))) continue;
            }
            if (g_lodOn || SOLO_VISIBLE) {   // en el borde del mundo real (y en columnas lejanas) solo se dibuja la columna si esta completa: si no, se veria el subsuelo sobre el terreno lejano
                int ddx = c->cx - pcx, ddz = c->cz - pcz;
                if (ddx * ddx + ddz * ddz > (RC - 2) * (RC - 2)) {
                    int64_t ck = colKey(c->cx, c->cz);
                    auto it = colOk.find(ck);
                    bool ok;
                    if (it != colOk.end()) ok = it->second;
                    else {
                        ok = true;
                        for (int cyy = 0; cyy < HYC && ok; cyy++) {
                            ChunkP n = buscar(c->cx, cyy, c->cz);
                            if (!n) { ok = false; break; }
                            bool aire = !n->dat.d && n->dat.uni == AIRE;
                            if (n->state.load(std::memory_order_acquire) < ST_MESHED && !n->gOp.n && !n->gAg.n && !aire) ok = false;
                        }
                        colOk[ck] = ok;
                    }
                    if (!ok) continue;
                    // el terreno lejano tapa lo subterraneo: no se dibujan chunks enterrados bajo la superficie (cuevas, minas)
                    auto ih = colMinH.find(ck);
                    double minH;
                    if (ih != colMinH.end()) minH = ih->second;
                    else {
                        minH = 1e9;
                        const double bx0 = (double)c->cx * CS / SUB, bz0 = (double)c->cz * CS / SUB, anc = (double)CS / SUB;
                        for (int a = 0; a <= 4; a++) for (int b = 0; b <= 4; b++)
                            minH = std::min(minH, alturaTerreno(bx0 + anc * a / 4.0, bz0 + anc * b / 4.0));
                        colMinH[ck] = minH;
                    }
                    if ((double)(c->cy + 1) * CS / SUB < minH - 1.0) continue;
                    if (!g_editCols.empty() && g_editCols.count(ck)) {      // columna editada: tambien se oculta lo enterrado bajo su superficie REAL (cuevas)
                        auto is = colSup3.find(ck);
                        float sm;
                        if (is != colSup3.end()) sm = is->second;
                        else { sm = supMin3(c->cx, c->cz); colSup3[ck] = sm; }
                        if (sm > -1e8f && (double)(c->cy + 1) * CS / SUB < sm - 2.0) continue;
                    }
                }
            }
            double mnx = c->cx * tam - cam.x, mny = c->cy * tam - cam.y, mnz = c->cz * tam - cam.z;
            bool fuera = false;
            for (int k = 0; k < 6 && !fuera; k++) {
                float px = pl[k][0] > 0 ? (float)(mnx + tam) : (float)mnx;
                float py = pl[k][1] > 0 ? (float)(mny + tam) : (float)mny;
                float pz = pl[k][2] > 0 ? (float)(mnz + tam) : (float)mnz;
                if (pl[k][0] * px + pl[k][1] * py + pl[k][2] * pz + pl[k][3] < 0) fuera = true;
            }
            if (fuera) continue;
            if (camSobre) {
                int ddx = c->cx - pcx, ddz = c->cz - pcz;
                if (ddx * ddx + ddz * ddz >= 16) {
                    const double top = (double)(c->cy + 1) * tamB;
                    if (top < minCol(c->cx, c->cz) - 24.0f) continue;                      // enterrado hondo
                    const double x0 = c->cx * tamB, z0 = c->cz * tamB;
                    if (rayoTapado(x0 + tamB * 0.5, top + 1, z0 + tamB * 0.5, c->cx, c->cz) &&
                        rayoTapado(x0 + 1, top + 1, z0 + 1, c->cx, c->cz) && rayoTapado(x0 + tamB - 1, top + 1, z0 + 1, c->cx, c->cz) &&
                        rayoTapado(x0 + 1, top + 1, z0 + tamB - 1, c->cx, c->cz) && rayoTapado(x0 + tamB - 1, top + 1, z0 + tamB - 1, c->cx, c->cz)) { ocluidos++; continue; }
                }
            }
            visibles.push_back(c);
        }
        g_ocluidosN = ocluidos;
        triDib = 0; mallasDib = (int)visibles.size();
        // de cerca a lejos: el z-buffer descarta pronto lo tapado (menos pixeles pintados); el agua se pinta al reves (de lejos a cerca)
        std::sort(visibles.begin(), visibles.end(), [&](Chunk* a, Chunk* b) {
            double ax = (a->cx + 0.5) * tam - cam.x, ay = (a->cy + 0.5) * tam - cam.y, az = (a->cz + 0.5) * tam - cam.z;
            double bx = (b->cx + 0.5) * tam - cam.x, by = (b->cy + 0.5) * tam - cam.y, bz = (b->cz + 0.5) * tam - cam.z;
            return ax * ax + ay * ay + az * az < bx * bx + by * by + bz * bz;
        });
        auto dibujar = [&](bool agua) {
            for (size_t vi = 0; vi < visibles.size(); vi++) {
                Chunk* c = visibles[agua ? visibles.size() - 1 - vi : vi];
                MallaGL& g = agua ? c->gAg : c->gOp;
                if (!g.n) continue;
                glUniform3f(uRel, (float)(c->cx * tam - cam.x), (float)(c->cy * tam - cam.y), (float)(c->cz * tam - cam.z));
                glUniform3f(uAbs, (float)(c->cx * tam), (float)(c->cy * tam), (float)(c->cz * tam));
                glBindTexture(GL_TEXTURE_3D, c->gLuz.tex);
                glBindVertexArray(g.vao);
                glDrawElements(GL_TRIANGLES, g.n, GL_UNSIGNED_INT, (const void*)0);
                triDib += g.n / 3;
            }
        };
        dibujar(false);
        const float fogAv = (aguaFogA > 0 && !camAgua) ? aguaFogA : (float)(g_lodOn ? LOD_FOG_MAX * 0.55 / 0.60 : VIEW_BLOCKS) * 0.60f, fogBv = (aguaFogB > 0 && !camAgua) ? aguaFogB : (float)(g_lodOn ? LOD_FOG_MAX : VIEW_BLOCKS) * 0.97f;
        glDisable(GL_CULL_FACE);
        vehDibujar(vp, cam, solV, solF, skyMul, gfx ? 1.0f : 0.0f, cielo, fogAv, fogBv, false);
        ckDibujar(vp, cam, solV, solF, skyMul, gfx ? 1.0f : 0.0f, cielo, fogAv, fogBv, false);
        jugDibujar(vp, cam.x, cam.y, cam.z, solV, solF, skyMul, cielo, fogAv, fogBv);
        if (g_modoLuna) jefeDibujar(vp, cam.x, cam.y, cam.z, solV, solF, jefeSkyMul(skyMul), cielo, fogAv, fogBv, ahora);
        else mobsDibujar(vp, cam.x, cam.y, cam.z, solV, solF, skyMul, cielo, fogAv, fogBv, ahora);
        g_etiquetas.clear();
        for (auto& kv : g_red.rem) if (kv.second.visto) {       // nombres sobre la cabeza
            const Remoto& m = kv.second;
            float px = (float)(m.sx - cam.x), py = (float)(m.sy + 2.05 - cam.y), pz = (float)(m.sz - cam.z);
            float cx_ = vp[0] * px + vp[4] * py + vp[8] * pz + vp[12], cy_ = vp[1] * px + vp[5] * py + vp[9] * pz + vp[13], cw = vp[3] * px + vp[7] * py + vp[11] * pz + vp[15];
            if (cw < 0.1f || px * px + py * py + pz * pz > 80.0f * 80.0f) continue;
            g_etiquetas.push_back({(cx_ / cw * 0.5f + 0.5f) * fbw, (1.0f - (cy_ / cw * 0.5f + 0.5f)) * fbh, m.nombre});
        }
        glUseProgram(progMundo);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        dibujar(true);
        vehDibujar(vp, cam, solV, solF, skyMul, gfx ? 1.0f : 0.0f, cielo, fogAv, fogBv, true);
        ckDibujar(vp, cam, solV, solF, skyMul, gfx ? 1.0f : 0.0f, cielo, fogAv, fogBv, true);

        // ---- nubes: cajas de 16 bloques a ~110 de altura; la malla se rehace al cruzar de celda, el viento solo la desplaza
        if (g_modoLuna) {} else if (g_nubes == 2 && g_graf) {   // graficos mejorados: nubes volumetricas (rayo por la capa, luz del sol y sombra propia)
            const double NUBE_Y2 = 110.0 + OFFY; const float ESP = 28.0f, RQ = g_lodOn ? 1300.0f : 650.0f;
            float cn[3] = {0.10f + 0.90f * dia, 0.11f + 0.89f * dia, 0.17f + 0.83f * dia};
            { float osc = 1.0f - 0.50f * std::max(0.6f * g_clN, g_clT); cn[0] *= osc; cn[1] *= osc; cn[2] *= osc; }
            float oc2 = std::min(1.0f, ocaso * 1.7f) * (1.0f - clK);       // atardecer: nubes naranjas y rosadas (no verdosas)
            cn[0] += (1.00f - cn[0]) * oc2 * 0.85f; cn[1] += (0.52f - cn[1]) * oc2; cn[2] += (0.40f - cn[2]) * oc2;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE);
            glUseProgram(progNube2);
            glUniformMatrix4fv(glGetUniformLocation(progNube2, "uVP"), 1, GL_FALSE, vp);
            glUniform1f(glGetUniformLocation(progNube2, "uR"), RQ);
            glUniform1f(glGetUniformLocation(progNube2, "uYm"), (float)(NUBE_Y2 + ESP * 0.5 - cam.y));
            glUniform1f(glGetUniformLocation(progNube2, "uY0"), (float)(NUBE_Y2 - cam.y));
            glUniform1f(glGetUniformLocation(progNube2, "uTh"), ESP);
            glUniform3f(glGetUniformLocation(progNube2, "uCamAbs"), (float)std::fmod(cam.x, 20000.0), (float)cam.y, (float)std::fmod(cam.z, 20000.0));
            glUniform2f(glGetUniformLocation(progNube2, "uWind"), (float)std::fmod(ahora * 1.6, 20000.0), (float)std::fmod(ahora * 0.45, 20000.0));
            glUniform3f(glGetUniformLocation(progNube2, "uSun"), solV[0], solV[1], solV[2]);
            glUniform3f(glGetUniformLocation(progNube2, "uCol"), cn[0], cn[1], cn[2]);
            glUniform3f(glGetUniformLocation(progNube2, "uSkyF"), cielo[0], cielo[1], cielo[2]);
            glUniform2f(glGetUniformLocation(progNube2, "uFog"), RQ * 0.30f, RQ * 0.95f);
            glUniform1f(glGetUniformLocation(progNube2, "uCover"), 0.50f - 0.42f * g_clN);
            glBindVertexArray(vaoVacio);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDepthMask(GL_TRUE);
        } else if (g_nubes >= 1) {
            const double CN = 16.0, NUBE_Y = 110.0 + OFFY, NUBE_ALTO = 5.0;
            const int RN = 20;
            double vx = ahora * 1.6, vz = ahora * 0.45;
            int cix = (int)std::floor((cam.x - vx) / CN), ciz = (int)std::floor((cam.z - vz) / CN);
            if (cix != nubeCix || ciz != nubeCiz) {
                nubeCix = cix; nubeCiz = ciz;
                std::vector<float> nv;
                auto hay = [](int ix, int iz) {
                    float v = 0.65f * vnoise(ix * 0.17f, iz * 0.17f, 801u) + 0.35f * vnoise(ix * 0.45f, iz * 0.45f, 805u);
                    return v > 0.54f;
                };
                auto quad = [&](float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3, float sh) {
                    float q[6][3] = {{x0, y0, z0}, {x1, y1, z1}, {x2, y2, z2}, {x0, y0, z0}, {x2, y2, z2}, {x3, y3, z3}};
                    for (auto& p : q) { nv.push_back(p[0]); nv.push_back(p[1]); nv.push_back(p[2]); nv.push_back(sh); }
                };
                for (int iz = -RN; iz <= RN; iz++) for (int ix = -RN; ix <= RN; ix++) {
                    int gx = cix + ix, gz = ciz + iz;
                    if (!hay(gx, gz)) continue;
                    float x0 = (float)((ix + RN) * CN), x1 = x0 + (float)CN, z0 = (float)((iz + RN) * CN), z1 = z0 + (float)CN, yb = 0.0f, yt = (float)NUBE_ALTO;
                    quad(x0, yt, z0, x1, yt, z0, x1, yt, z1, x0, yt, z1, 1.0f);       // arriba
                    quad(x0, yb, z0, x1, yb, z0, x1, yb, z1, x0, yb, z1, 0.62f);      // abajo
                    if (!hay(gx - 1, gz)) quad(x0, yb, z0, x0, yb, z1, x0, yt, z1, x0, yt, z0, 0.78f);
                    if (!hay(gx + 1, gz)) quad(x1, yb, z0, x1, yb, z1, x1, yt, z1, x1, yt, z0, 0.78f);
                    if (!hay(gx, gz - 1)) quad(x0, yb, z0, x1, yb, z0, x1, yt, z0, x0, yt, z0, 0.88f);
                    if (!hay(gx, gz + 1)) quad(x0, yb, z1, x1, yb, z1, x1, yt, z1, x0, yt, z1, 0.88f);
                }
                nubeVerts = (int)(nv.size() / 4);
                glBindVertexArray(vaoN);
                glBindBuffer(GL_ARRAY_BUFFER, vboN);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv.size() * sizeof(float)), nv.data(), GL_DYNAMIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, (const void*)0);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 16, (const void*)12);
            }
            if (nubeVerts) {
                // color: blanco de dia, gris azulado de noche, anaranjado al amanecer/atardecer
                float cn[3] = {0.10f + 0.90f * dia, 0.11f + 0.89f * dia, 0.17f + 0.83f * dia};
                float oc2 = ocaso * 1.3f;
                cn[0] += (1.0f - cn[0]) * oc2 * 0.2f; cn[1] += (0.66f - cn[1]) * oc2; cn[2] += (0.48f - cn[2]) * oc2;
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_CULL_FACE);
                glUseProgram(progNube);
                glUniformMatrix4fv(glGetUniformLocation(progNube, "uVP"), 1, GL_FALSE, vp);
                glUniform3f(glGetUniformLocation(progNube, "uOff"),
                            (float)((cix - RN) * CN + vx - cam.x), (float)(NUBE_Y - cam.y), (float)((ciz - RN) * CN + vz - cam.z));
                glUniform3f(glGetUniformLocation(progNube, "uCol"), cn[0], cn[1], cn[2]);
                glUniform3f(glGetUniformLocation(progNube, "uSkyF"), cielo[0], cielo[1], cielo[2]);
                glUniform2f(glGetUniformLocation(progNube, "uFog"), 170.0f, 330.0f);
                glUniform1f(glGetUniformLocation(progNube, "uA"), 1.0f);
                glBindVertexArray(vaoN);
                glDrawArrays(GL_TRIANGLES, 0, nubeVerts);
            }
        }
        // ---- lluvia, nieve y rayos
        g_explos.erase(std::remove_if(g_explos.begin(), g_explos.end(), [&](const Explosion& e) { return ahora - e.t0 > 1.8; }), g_explos.end());
        if ((!camAgua && (g_clP * (1.0f - g_clCub) * climaAlt > 0.01f || rayoF > 0.001f || (ahora - g_rayoT) < 0.6)) || !g_explos.empty()) {
            const V3 fw_ = norm(mira), rg_ = norm(cruz(fw_, camUp)), up_ = cruz(rg_, fw_);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE);
            glUseProgram(progClima);
            glUniformMatrix4fv(glGetUniformLocation(progClima, "uVP"), 1, GL_FALSE, vp);
            const GLint wCol = glGetUniformLocation(progClima, "uColor");
            auto subirW = [&](float r, float g, float b, float a) {
                if (climaV.empty()) return;
                glUniform4f(wCol, r, g, b, a);
                glBindVertexArray(vaoW);
                glBindBuffer(GL_ARRAY_BUFFER, vboW);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(climaV.size() * sizeof(float)), climaV.data(), GL_DYNAMIC_DRAW);
                glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, (const void*)0);
                glEnableVertexAttribArray(1); glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 16, (const void*)12);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(climaV.size() / 4));
                climaV.clear();
            };
            auto quadW = [&](const float* a, const float* b, const float* c, const float* d, float aa, float ab, float ac, float ad) {
                const float* q[6] = {a, b, c, a, c, d}; const float al[6] = {aa, ab, ac, aa, ac, ad};
                for (int i = 0; i < 6; i++) { climaV.push_back(q[i][0]); climaV.push_back(q[i][1]); climaV.push_back(q[i][2]); climaV.push_back(al[i]); }
            };
            const float pI = camAgua ? 0.0f : g_clP * (1.0f - g_clCub) * climaAlt;
            const float luzP = 0.30f + 0.70f * dia;
            const double tp = std::fmod(ahora, 3000.0);
            if (pI > 0.01f && g_clNieve < 0.99f) {        // lluvia: rayitas que caen rapido, algo inclinadas por el viento
                const float R = 18.0f, H = 22.0f;
                const int n = (int)(2400 * pI * (1.0f - g_clNieve));
                for (int i = 0; i < n; i++) {
                    float dx = fmodPos(hf(i * 4u + 1) * 2 * R - cam.x, 2 * R) - R, dz = fmodPos(hf(i * 4u + 2) * 2 * R - cam.z, 2 * R) - R;
                    float dy = fmodPos(hf(i * 4u + 3) * H - tp * 26.0 - cam.y, H) - 8.0f;
                    float fade = 1.0f - (dx * dx + dz * dz) / (R * R);
                    if (fade <= 0) continue;
                    fade *= std::min(1.0f, std::max(0.0f, (std::sqrt(dx * dx + dz * dz + dy * dy) - 0.6f) / 2.0f));
                    float p0[3] = {dx, dy, dz}, p1[3] = {dx + 0.10f, dy + 1.0f, dz + 0.04f};
                    float w = 0.014f;
                    float a[3] = {p0[0] - (float)rg_.x * w, p0[1] - (float)rg_.y * w, p0[2] - (float)rg_.z * w}, b[3] = {p0[0] + (float)rg_.x * w, p0[1] + (float)rg_.y * w, p0[2] + (float)rg_.z * w};
                    float c[3] = {p1[0] + (float)rg_.x * w, p1[1] + (float)rg_.y * w, p1[2] + (float)rg_.z * w}, d[3] = {p1[0] - (float)rg_.x * w, p1[1] - (float)rg_.y * w, p1[2] - (float)rg_.z * w};
                    quadW(a, b, c, d, fade, fade, 0.0f, 0.0f);
                }
                subirW(0.72f * luzP + 0.08f, 0.80f * luzP + 0.08f, 0.92f * luzP + 0.08f, 0.55f * (1.0f - g_clNieve));
            }
            if (pI > 0.01f && g_clNieve > 0.01f) {        // nieve: copos que oscilan al caer despacio
                const float R = 22.0f, H = 16.0f;
                const int n = (int)(2200 * pI * g_clNieve);
                for (int i = 0; i < n; i++) {
                    float h = hf(i * 5u + 11);
                    float dx = fmodPos(hf(i * 5u + 1) * 2 * R - cam.x, 2 * R) - R + std::sin((float)tp * 0.9f + h * 40.0f) * 0.40f;
                    float dz = fmodPos(hf(i * 5u + 2) * 2 * R - cam.z, 2 * R) - R + std::cos((float)tp * 0.7f + h * 31.0f) * 0.40f;
                    float dy = fmodPos(hf(i * 5u + 3) * H - tp * (1.5 + 1.0 * h) - cam.y, H) - 6.0f;
                    float fade = 1.0f - (dx * dx + dz * dz) / (R * R);
                    if (fade <= 0) continue;
                    fade *= std::min(1.0f, std::max(0.0f, (std::sqrt(dx * dx + dz * dz + dy * dy) - 0.8f) / 2.5f));
                    float sz = 0.040f + 0.040f * hf(i * 5u + 4);
                    float rx = (float)rg_.x * sz, ry = (float)rg_.y * sz, rz = (float)rg_.z * sz, ux = (float)up_.x * sz, uy = (float)up_.y * sz, uz = (float)up_.z * sz;
                    float a[3] = {dx - rx - ux, dy - ry - uy, dz - rz - uz}, b[3] = {dx + rx - ux, dy + ry - uy, dz + rz - uz};
                    float c[3] = {dx + rx + ux, dy + ry + uy, dz + rz + uz}, d[3] = {dx - rx + ux, dy - ry + uy, dz - rz + uz};
                    quadW(a, b, c, d, fade, fade, fade, fade);
                }
                subirW(0.93f * luzP + 0.07f, 0.95f * luzP + 0.07f, 1.0f * luzP + 0.07f, 0.95f);
            }
            double dr = ahora - g_rayoT;
            if (!camAgua && dr >= 0 && dr < 0.45 && g_rayoPts.size() >= 6 && rayoF > 0.02f) {    // rayo: cinta quebrada de las nubes al suelo
                const size_t np = g_rayoPts.size() / 3;
                for (size_t i = 0; i + 1 < np; i++) {
                    float a0[3] = {g_rayoPts[i * 3] - (float)cam.x, g_rayoPts[i * 3 + 1] - (float)cam.y, g_rayoPts[i * 3 + 2] - (float)cam.z};
                    float a1[3] = {g_rayoPts[i * 3 + 3] - (float)cam.x, g_rayoPts[i * 3 + 4] - (float)cam.y, g_rayoPts[i * 3 + 5] - (float)cam.z};
                    float dx = a1[0] - a0[0], dy = a1[1] - a0[1], dz = a1[2] - a0[2];
                    float mx = (a0[0] + a1[0]) * 0.5f, my = (a0[1] + a1[1]) * 0.5f, mz = (a0[2] + a1[2]) * 0.5f;
                    float sx = dy * mz - dz * my, sy = dz * mx - dx * mz, sz = dx * my - dy * mx;      // lado = segmento x vista
                    float sl = std::sqrt(sx * sx + sy * sy + sz * sz) + 1e-6f, w = 0.45f + 0.0025f * std::sqrt(mx * mx + mz * mz);
                    sx = sx / sl * w; sy = sy / sl * w; sz = sz / sl * w;
                    float a[3] = {a0[0] - sx, a0[1] - sy, a0[2] - sz}, b[3] = {a0[0] + sx, a0[1] + sy, a0[2] + sz}, c[3] = {a1[0] + sx, a1[1] + sy, a1[2] + sz}, d[3] = {a1[0] - sx, a1[1] - sy, a1[2] - sz};
                    quadW(a, b, c, d, 1.0f, 1.0f, 1.0f, 1.0f);
                }
                subirW(0.90f, 0.93f, 1.0f, std::min(1.0f, rayoF * 1.3f));
            }
            for (int pase = 0; pase < 2 && !g_explos.empty(); pase++) {      // explosiones: humo (pase 0) y fuego (pase 1)
                for (const Explosion& e : g_explos) {
                    const float age = (float)(ahora - e.t0);
                    const float R = (float)e.R, exp_ = 1.0f - std::exp(-age * 4.0f);
                    const int n = pase == 0 ? 150 : 110;
                    for (int i = 0; i < n; i++) {
                        uint32_t sd = (uint32_t)(i * 7 + pase * 1000 + (int)(e.x * 13 + e.z * 29));
                        float u1 = hf(sd * 3 + 1) * 2.0f - 1.0f, a2 = hf(sd * 3 + 2) * 6.2831853f, rr = std::sqrt(std::max(0.0f, 1.0f - u1 * u1));
                        float dirx = rr * std::cos(a2), diry = u1, dirz = rr * std::sin(a2);
                        float dist = R * (0.15f + 0.85f * hf(sd * 3 + 3)) * exp_ * (pase == 0 ? 1.0f : 0.8f);
                        float cx = (float)(e.x - cam.x) + dirx * dist, cy = (float)(e.y - cam.y) + diry * dist + age * (pase == 0 ? 2.2f : 0.8f), cz = (float)(e.z - cam.z) + dirz * dist;
                        float sz, al;
                        if (pase == 0) { sz = (0.55f + 1.1f * hf(sd + 9)) * (0.35f + R * 0.10f) * (0.5f + 0.7f * age); al = std::max(0.0f, 1.0f - age / 1.8f) * 0.75f; }
                        else { if (age > 0.75f) break; sz = (0.45f + 0.8f * hf(sd + 9)) * (0.35f + R * 0.09f) * (1.0f - age / 0.75f * 0.6f); al = std::max(0.0f, 1.0f - age / 0.75f); }
                        float rx = (float)rg_.x * sz, ry = (float)rg_.y * sz, rz = (float)rg_.z * sz, ux = (float)up_.x * sz, uy = (float)up_.y * sz, uz = (float)up_.z * sz;
                        float a[3] = {cx - rx - ux, cy - ry - uy, cz - rz - uz}, b[3] = {cx + rx - ux, cy + ry - uy, cz + rz - uz};
                        float c[3] = {cx + rx + ux, cy + ry + uy, cz + rz + uz}, d[3] = {cx - rx + ux, cy - ry + uy, cz - rz + uz};
                        quadW(a, b, c, d, al, al, al, al);
                    }
                }
                if (pase == 0) subirW(0.20f * luzP + 0.05f, 0.19f * luzP + 0.05f, 0.18f * luzP + 0.05f, 0.85f);
                else subirW(1.0f, 0.55f, 0.12f, 1.0f);
            }
            glDepthMask(GL_TRUE);
        }
        glDisable(GL_BLEND);

        // ---- caja de seleccion y mira
        glDisable(GL_CULL_FACE);
        glUseProgram(progLinea);
        std::vector<float> ln;
        if (golpe.hay) {
            int x0, y0, z0, n;
            if (g_pincelBloque) { x0 = divPiso(golpe.x, SUB) * SUB; y0 = divPiso(golpe.y, SUB) * SUB; z0 = divPiso(golpe.z, SUB) * SUB; n = SUB; }
            else { x0 = golpe.x; y0 = golpe.y; z0 = golpe.z; n = 1; }
            double e = 0.004;
            double a[3] = {x0 * VS - e - cam.x, y0 * VS - e - cam.y, z0 * VS - e - cam.z};
            double b[3] = {(x0 + n) * VS + e - cam.x, (y0 + n) * VS + e - cam.y, (z0 + n) * VS + e - cam.z};
            int ar[12][2][3] = {};
            auto P = [&](int i, int j, int k, int idx, int arista) {
                ar[arista][idx][0] = i; ar[arista][idx][1] = j; ar[arista][idx][2] = k;
            };
            int a_ = 0;
            for (int j = 0; j < 2; j++) for (int k = 0; k < 2; k++) { P(0, j, k, 0, a_); P(1, j, k, 1, a_); a_++; }
            for (int i = 0; i < 2; i++) for (int k = 0; k < 2; k++) { P(i, 0, k, 0, a_); P(i, 1, k, 1, a_); a_++; }
            for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) { P(i, j, 0, 0, a_); P(i, j, 1, 1, a_); a_++; }
            for (int q = 0; q < 12; q++)
                for (int idx = 0; idx < 2; idx++) {
                    ln.push_back((float)(ar[q][idx][0] ? b[0] : a[0]));
                    ln.push_back((float)(ar[q][idx][1] ? b[1] : a[1]));
                    ln.push_back((float)(ar[q][idx][2] ? b[2] : a[2]));
                }
        }
        glBindVertexArray(vaoL);
        glBindBuffer(GL_ARRAY_BUFFER, vboL);
        if (!ln.empty()) {
            glUniformMatrix4fv(lVP, 1, GL_FALSE, vp);
            if (g_destruir) glUniform4f(lColor, 0.9f, 0.1f, 0.1f, 1.0f); else glUniform4f(lColor, 0.0f, 0.0f, 0.0f, 1.0f);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(ln.size() * sizeof(float)), ln.data(), GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (const void*)0);
            glDrawArrays(GL_LINES, 0, (GLsizei)(ln.size() / 3));
        }
        if (gfx) {   // oclusion ambiental + bloom + FXAA + tonemapping, y a la pantalla
            float tY = std::tan(1.22f * 0.5f), tX = tY * (float)fbw / fbh;
            ppComponer(pp, vaoVacio, fbw, fbh, tX, tY, 0.05f, g_lodOn ? 1500.0f : 800.0f, true);
        }
        if (!pil && !cargando && !g_menu) jugDibujarMano(vp, cam.x, cam.y, cam.z, g_j.yaw, g_j.pitch, objetoEnMano(), g_golpea, dt, solV, solF, skyMul, cielo);
        // ---- HUD (texto, barra de materiales, menu, mira)
        {
            g_hud.clear();
            hdibujar(fbw, fbh, fps, cargando, ahora - tMensaje < 3.0, g_j.p.x, g_j.p.y, g_j.p.z);
            suenoDibujar(fbw, fbh, ahora);
            fuegoDibujar(fbw, fbh, ahora);
            if (g_pausa) pausaDibujar(fbw, fbh);
            subirHud(fbw, fbh);
        }

        glfwSwapBuffers(ventana);

        // ---- informacion en el titulo de la ventana
        if (ahora - tFps >= 0.5) {
            fps = (int)(fpsCuenta / (ahora - tFps));
            fpsCuenta = 0; tFps = ahora;
            char t[320];
            std::snprintf(t, sizeof t, "Juego Voxel [%s] | %d FPS | %s | lejano %s %zu | chunks %zu (dibujados %d, tapados %d) | %dk triangulos | pos %.0f %.0f %.0f | pincel: %s | material: %s | %s",
                          g_nombreMundo.c_str(), fps, cargando ? "CARGANDO..." : "listo", g_lodOn ? "ON" : "OFF", g_lod.size(), g_mapa.size(), mallasDib, g_ocluidosN, triDib / 1000,
                          g_j.p.x, g_j.p.y, g_j.p.z, g_pincelBloque ? "BLOQUE" : "cubito", nombreSlot(g_material),
                          ahora - tMensaje < 3.0 ? "GUARDADO" : (g_j.volar ? "VOLANDO" : "caminando"));
            glfwSetWindowTitle(ventana, t);
        }
    }
    g_red.cerrar();
    int nFin = guardarTodosLosSucios();
    guardarJugador();
    std::printf("Al salir: %d chunks guardados | cargados desde disco en esta sesion: %d\n", nFin, g_cargadosDisco.load());
    g_pool.detener();
    glfwTerminate();
    return g_volverMenu ? 2 : 0;
}

// "guardar y salir": se vuelve a abrir el juego (sin argumentos) y arranca en el menu de inicio
#ifndef ANDROID_GAME
static void reiniciarEnMenu(const char* exe, bool otro = false) {
    std::fflush(stdout);
    const std::string dest = otro ? otroExe() : std::string(exe);
#ifdef _WIN32
    _putenv("MUNDO="); _putenv("RED_UNIR="); _putenv("RED_HOST=");
    _execl(dest.c_str(), dest.c_str(), (char*)nullptr);
#else
    unsetenv("MUNDO"); unsetenv("RED_UNIR"); unsetenv("RED_HOST");
    if (otro) execl(dest.c_str(), dest.c_str(), (char*)nullptr);
    else execl("/proc/self/exe", exe, (char*)nullptr);
#endif
}
#endif

#ifdef ANDROID_GAME
static void androidCorrer(struct android_app* app) {
    A_app = app;
    if (app->activity && app->activity->internalDataPath) { if (chdir(app->activity->internalDataPath) != 0) A_log("chdir fallo"); }   // aqui se guardan mundos y opciones
    int r = juego_main();
    if (r == 2 || r == 3) A_relanzar();       // "guardar y salir" / cambiar de SUB: se vuelve a abrir la app en el menu de inicio
    ANativeActivity_finish(app->activity);
    if (r == 2 || r == 3) { usleep(500000); _exit(0); }   // el proceso nuevo arranca con todo limpio
}
// el APK trae DOS librerias: libjuego.so (SUB 2) y libjuego_sub1.so (SUB 1). Android siempre abre libjuego.so, que lee sub.txt y,
// si toca SUB 1, entrega el control a libjuego_sub1.so (misma ventana, otro mundo).
extern "C" __attribute__((visibility("default"))) void juego_android_main(struct android_app* app) { androidCorrer(app); }
void android_main(struct android_app* app) {       // punto de entrada en Android (NativeActivity)
#if SUB_VAL == 2
    if (app->activity && app->activity->internalDataPath) { if (chdir(app->activity->internalDataPath) != 0) A_log("chdir fallo"); }
    int sub = 2;
    if (FILE* f = std::fopen("sub.txt", "r")) { if (std::fscanf(f, "%d", &sub) != 1) sub = 2; std::fclose(f); }
    if (sub == 1) {
        void* h = dlopen("libjuego_sub1.so", RTLD_NOW | RTLD_LOCAL);
        void (*fn)(struct android_app*) = h ? (void (*)(struct android_app*))dlsym(h, "juego_android_main") : nullptr;
        if (fn) { fn(app); return; }
        A_log("no pude abrir libjuego_sub1.so: se abre SUB 2");
    }
#endif
    androidCorrer(app);
}
#else
int main(int argc, char** argv) {
    leerArgs(argc, argv);
    g_exe0 = argv[0];
    int r = juego_main();
    if (r == 2) reiniciarEnMenu(argv[0]);
    if (r == 3) { reiniciarEnMenu(argv[0], true); return 0; }
    return r == 2 ? 0 : r;
}
#endif

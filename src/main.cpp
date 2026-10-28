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

#include <unordered_map>
#include <unordered_set>
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
struct Pool {
    std::vector<std::thread> hilos;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void()>> cola;
    bool parar = false;
    void iniciar(int n) {
        for (int i = 0; i < n; i++)
            hilos.emplace_back([this] {
                for (;;) {
                    std::function<void()> f;
                    {
                        std::unique_lock<std::mutex> lk(m);
                        cv.wait(lk, [this] { return parar || !cola.empty(); });
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
    construirMalla(s, op, ag);
    std::vector<uint8_t> luz;
    if (!op.i.empty() || !ag.i.empty()) {
        auto sn = std::make_unique<SnapColumnas>();
        juntarColumnas(*c, *sn);
        luz.resize((size_t)LN * LN * LN * 4);
        calcularLuz(*sn, c->cy, luz.data());
    }
    c->version++;
    std::lock_guard<std::mutex> lk(c->mtx);
    c->gOp.subir(op);
    c->gAg.subir(ag);
    c->gLuz.subir(luz);
    c->state.store(ST_MESHED);
}

// Tras editar, la luz de los chunks cercanos puede cambiar (hasta LMAX cubitos a los lados y toda la columna hacia abajo).
// Se marcan para rehacerse en los hilos de trabajo; mientras tanto se sigue viendo la malla anterior.
static void marcarLuzSucia(int xmin, int xmax, int ymin, int ymax, int zmin, int zmax, const std::vector<ChunkP>& excluir) {
    (void)ymin;
    int cx0 = divPiso(xmin - LMAX, CS), cx1 = divPiso(xmax + LMAX, CS);
    int cz0 = divPiso(zmin - LMAX, CS), cz1 = divPiso(zmax + LMAX, CS);
    int cy1 = std::min(HYC - 1, divPiso(ymax + LMAX, CS));
    for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) for (int cy = 0; cy <= cy1; cy++) {
        ChunkP c = buscar(cx, cy, cz);
        if (!c || c->state.load() != ST_MESHED) continue;
        if (!c->gOp.n && !c->gAg.n) continue;
        if (std::find(excluir.begin(), excluir.end(), c) != excluir.end()) continue;
        c->version++;
        c->state.store(ST_READY);
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
static std::unordered_set<int64_t> g_editCols;
static int g_hilos = 4;

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
static bool g_anclaOn = true;          // mantener dibujadas las zonas editadas lejanas
static const int LOD_N = 32, LOD_NIV = 3;
static const int LOD_CELDA[LOD_NIV] = {2, 4, 8};
static const double LOD_ROUT[LOD_NIV] = {256.0, 640.0, 1280.0};
static const double LOD_RIN0 = 100.0;
static const double LOD_FOG_MAX = 1280.0;

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
    struct Arbol { double x, z, base; };
    std::vector<Arbol> arboles;
    if (t.niv <= 1) {
        const int ext = N * cel;
        for (int tz = bz0; tz < bz0 + ext; tz++)
            for (int tx = bx0; tx < bx0 + ext; tx++) {
                if (hash2(tx, tz, 77) % 70 != 0) continue;
                double ha;
                if (!hayArbol(tx, tz, &ha)) continue;
                if (enMascara(tx + 0.5, tz + 0.5)) continue;
                arboles.push_back({tx + 0.5, tz + 0.5, (std::floor(ha * SUB) + 1 - 2) / SUB});
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
            bool arenoso = y < WATER_LEVEL + 2.4f || desierto(bx, bz) > 0.5;
            float cr, cg, cb;
            if (arenoso) {
                cr = 0.86f; cg = 0.80f; cb = 0.55f;
                if (y < WATER_LEVEL - 0.6f && vnoise((float)(bx * 0.07), (float)(bz * 0.07), 53u) > 0.52f) { cr = 0.62f; cg = 0.64f; cb = 0.72f; }
            } else {
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
            if (enMascara(bx0 + (i + 0.5) * cel, bz0 + (j + 0.5) * cel)) continue;      // ahi dibujan los chunks reales (con tus cambios)
            uint16_t a = (uint16_t)(j * (N + 1) + i), b = (uint16_t)(a + 1), d = (uint16_t)(a + N + 1), c = (uint16_t)(d + 1);
            float ha = h(i, j), hb = h(i + 1, j), hc = h(i + 1, j + 1), hd = h(i, j + 1);
            if (std::fabs(ha - hc) < std::fabs(hb - hd)) { uint16_t q[6] = {a, d, c, a, c, b}; I.insert(I.end(), q, q + 6); }
            else { uint16_t q[6] = {a, d, b, d, c, b}; I.insert(I.end(), q, q + 6); }
            if (std::min(std::min(ha, hb), std::min(hc, hd)) < WATER_LEVEL - 0.01f) {
                uint16_t base = (uint16_t)(W.size() / 6);
                float xs[4] = {(float)(i * cel), (float)(i * cel), (float)((i + 1) * cel), (float)((i + 1) * cel)};
                float zs[4] = {(float)(j * cel), (float)((j + 1) * cel), (float)((j + 1) * cel), (float)(j * cel)};
                for (int k = 0; k < 4; k++) {
                    W.push_back(xs[k]); W.push_back((float)WATER_LEVEL); W.push_back(zs[k]);
                    W.push_back(0.18f); W.push_back(0.38f); W.push_back(0.80f);
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
        if (t.niv == 0) caja(a.x - 0.5, a.base, a.z - 0.5, a.x + 0.5, a.base + 4.0, a.z + 0.5, 0.40f, 0.28f, 0.15f);
        caja(a.x - 2.5, a.base + 2.9, a.z - 2.5, a.x + 2.5, a.base + 7.1, a.z + 2.5, 0.20f, 0.52f, 0.16f);
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
            if (g_genPend.load() >= g_hilos * 6) break;
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
            if (g_meshPend.load() < g_hilos * 4) pedirMalla(c, 0);
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
static void guardarOpciones() {
    FILE* f = std::fopen("opciones.txt", "w");
    if (!f) return;
    std::fprintf(f, "mundo_lejano=%d\ncambios_lejanos=%d\ngraficos=%d\n", g_lodOn ? 1 : 0, g_anclaOn ? 1 : 0, g_graf ? 1 : 0);
    std::fclose(f);
}
static void cargarOpciones() {
    FILE* f = std::fopen("opciones.txt", "r");
    if (!f) return;
    int v = 1, w = 1, g = 1;
    if (std::fscanf(f, "mundo_lejano=%d cambios_lejanos=%d graficos=%d", &v, &w, &g) >= 1) { g_lodOn = v != 0; g_anclaOn = w != 0; g_graf = g != 0; }
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
static std::string g_nombreMundo = "MUNDO";
static int g_material = PASTO;
static bool g_pincelBloque = false;
static bool g_destruir = false;      // herramienta actual: destruir (en vez de poner un material)
static bool g_menu = false;          // menu de materiales abierto (tecla E)
static double g_hora = 0.12;         // hora del dia (0..1)
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
                                           "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA", "CARROCERIA"};
static const char* nombreSlot(int t) { return t == ID_SPRAY ? "SPRAY" : (t >= 0 && t < NTIPOS) ? NOMBRES_SLOT[t] : ""; }
static int g_barra[9] = {0, PASTO, TIERRA, PIEDRA, ARENA, MADERA, HOJAS, CARBON, ANTORCHA};   // barra de 9 casillas
static int g_sel = 1;                                                                        // casilla elegida
static void aplicarSel() { g_material = g_barra[g_sel]; g_destruir = g_material == 0; }
static void guardarJugador() {
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    FILE* f = std::fopen(archivoJugador().c_str(), "w");
    if (!f) return;
    std::fprintf(f, "%.4f %.4f %.4f %.5f %.5f %d %d %d\n", g_j.p.x, g_j.p.y, g_j.p.z, g_j.yaw, g_j.pitch,
                 g_j.volar ? 1 : 0, g_material, g_pincelBloque ? 1 : 0);
    std::fclose(f);
}
static bool cargarJugador() {
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
const vec3 NRM[6] = vec3[6](vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1));
float hash(vec3 p){ p = fract(p * 0.1031); p += dot(p, p.yzx + 33.33); return fract((p.x + p.y) * p.z); }
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
    vec3 col; float alfa = 1.0;
    if (tipo == 1) col = (cara == 2) ? vec3(0.34,0.64,0.21) : vec3(0.45,0.32,0.20);
    else if (tipo == 2) col = vec3(0.45,0.32,0.20);
    else if (tipo == 3) col = vec3(0.50,0.50,0.52);
    else if (tipo == 4) col = vec3(0.86,0.80,0.55);
    else if (tipo == 5) { col = vec3(0.18,0.38,0.80); alfa = 0.65; }
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
    col *= 1.0 + r;
    if (tipo >= 32 && tipo < 44) {
        col *= mix(1.1, 1.9, uHDR);       // los LED brillan por si solos
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
        float br = 0.045 + 0.955 * exp(-3.35 * (1.0 - nivel));
        float cara_s = (cara == 2) ? 1.0 : ((cara == 3) ? 0.5 : ((cara < 2) ? 0.62 : 0.80));
        col *= br * cara_s;
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
    float f = clamp((vDist - uFogA) / (uFogB - uFogA), 0.0, 1.0);
    frag = vec4(mix(col, uSky, f), alfa);
})";

static const char* VLOD_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aCol;
uniform mat4 uVP;
uniform vec3 uRel;
out vec3 vCol;
out float vDist;
void main(){
    vec3 rel = uRel + aPos;
    vCol = aCol;
    vDist = length(rel);
    gl_Position = uVP * vec4(rel, 1.0);
})";
static const char* FLOD_SRC = R"(#version 330 core
in vec3 vCol;
in float vDist;
out vec4 frag;
uniform vec3 uSky;
uniform float uFogA;
uniform float uFogB;
uniform float uSkyMul;
uniform float uAlpha;
void main(){
    float br = 0.045 + 0.955 * exp(-3.35 * (1.0 - uSkyMul));
    vec3 col = vCol * br;
    float f = clamp((vDist - uFogA) / (uFogB - uFogA), 0.0, 1.0);
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
float hash(vec3 p){ p = fract(p * 0.1031); p += dot(p, p.yzx + 33.33); return fract((p.x + p.y) * p.z); }
void main(){
    vec2 nd = gl_FragCoord.xy / uRes * 2.0 - 1.0;
    vec3 d = normalize(uFwd + nd.x * uTan.x * uRight + nd.y * uTan.y * uUp);
    vec3 col = d.y > 0.0 ? mix(uHor, uZen, pow(d.y, 0.55)) : uHor;
    if (uNoche > 0.01 && d.y > 0.02) {                       // estrellas
        vec3 c = floor(d * 150.0);
        float s = hash(c);
        if (s > 0.9965) col += vec3(1.0) * uNoche * (0.45 + 0.55 * hash(c + 3.0));
    }
    float ds = dot(d, uSun);                                 // sol: cuadrado como en Minecraft + brillo
    if (ds > 0.0) {
        vec3 u = normalize(cross(vec3(0.0, 0.0, 1.0), uSun));
        vec3 v = cross(uSun, u);
        vec2 ab = vec2(dot(d, u), dot(d, v)) / ds;
        if (abs(ab.x) < 0.12 && abs(ab.y) < 0.12) col = vec3(1.0, 0.96, 0.72);
        col += vec3(1.0, 0.80, 0.45) * pow(ds, 90.0) * 0.55;
    }
    float dm = dot(d, -uSun);                                // luna con craterls
    if (dm > 0.0) {
        vec3 u = normalize(cross(vec3(0.0, 0.0, 1.0), -uSun));
        vec3 v = cross(-uSun, u);
        vec2 ab = vec2(dot(d, u), dot(d, v)) / dm;
        if (abs(ab.x) < 0.09 && abs(ab.y) < 0.09) {
            vec2 cel = floor(ab / 0.03);
            float m = 0.80 + 0.14 * hash(vec3(cel, 5.0)) - (hash(vec3(cel, 9.0)) > 0.8 ? 0.16 : 0.0);
            col = vec3(0.86, 0.90, 1.0) * m;
        }
        col += vec3(0.35, 0.42, 0.65) * pow(dm, 120.0) * 0.30;
    }
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


// ---------------------------------------------------------------- mundos (menu que aparece al iniciar)
struct InfoMundo { std::string carpeta, nombre; uint32_t semilla = 0; int chunks = 0; long long t = 0; };
static const char* RAIZ_MUNDOS = "mundos";
static std::vector<InfoMundo> g_mundos;
static int g_pantalla = 1;                 // 0 = menu de mundos, 1 = juego
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
static MRect mRNuevo(const MBase& b) { return {b.x0, b.yNuevo, b.w, b.hFila}; }
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

static void alMouse(GLFWwindow*, double x, double y) {
    if (!g_capturado) { g_mx = x; g_my = y; return; }
    if (g_primerMouse) { g_mx = x; g_my = y; g_primerMouse = false; }
    g_j.yaw += (x - g_mx) * 0.0025;
    g_j.pitch -= (y - g_my) * 0.0025;
    g_j.pitch = std::max(-1.55, std::min(1.55, g_j.pitch));
    g_mx = x; g_my = y;
}
static void abrirMenu(GLFWwindow* w) {
    g_menu = true; g_capturado = false;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}
static void cerrarMenu(GLFWwindow* w) {
    g_menu = false; g_capturado = true; g_primerMouse = true;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}
static void abrirPausa(GLFWwindow* w) { g_pausa = true; g_capturado = false; glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
static void reanudar(GLFWwindow* w) { g_pausa = false; g_capturado = true; g_primerMouse = true; glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
// botones de la pausa: 0 continuar, 1 mundo lejano, 2 cambios lejanos, 3 graficos, 4 guardar y salir
static MRect pausaRect(int fbw, int fbh, int i) {
    float k = std::max(0.8f, fbh / 720.0f), w = 420 * k, h = 58 * k, gap = 16 * k;
    return {(fbw - w) / 2.0f, fbh * 0.30f + i * (h + gap), w, h};
}
static int pausaHit(double mx, double my) {
    double px = mx * g_fbW / std::max(1, g_winW), py = my * g_fbH / std::max(1, g_winH);
    for (int i = 0; i < 5; i++) if (mDentro(pausaRect(g_fbW, g_fbH, i), px, py)) return i;
    return -1;
}
static void alRueda(GLFWwindow*, double, double dy) {
    if (g_pantalla == 0) { g_scrollM += dy > 0 ? -1 : 1; mundosClamp(); return; }
    if (g_menu || g_pausa) return;
    g_sel = (g_sel + (dy > 0 ? 1 : -1) + 9) % 9;
    aplicarSel();
}
// geometria del menu de materiales (compartida por el dibujo y el clic)
static float g_k = 1.0f;
static void menuGeom(int fbw, int fbh, int i, float& x, float& y, float& s) {
    float k = std::max(0.8f, fbh / 720.0f);
    s = 58.0f * k;
    float gap = 22.0f * k;
    float total = 9 * s + 8 * gap;
    float x0 = (fbw - total) / 2.0f, y0 = fbh * 0.13f;
    x = x0 + (i % 9) * (s + gap);
    y = y0 + (i / 9) * (s + gap + 22.0f * k);
}
static int menuTileEn(double mx, double my) {
    double px = mx * g_fbW / std::max(1, g_winW), py = my * g_fbH / std::max(1, g_winH);
    for (int i = 0; i < NTIPOS_MENU; i++) {
        float x, y, sz;
        menuGeom(g_fbW, g_fbH, i, x, y, sz);
        if (px >= x && px <= x + sz && py >= y && py <= y + sz) return menuTipo(i);
    }
    return -1;
}
static void alTecla(GLFWwindow* w, int tecla, int, int accion, int) {
    if (accion != GLFW_PRESS) return;
    if (g_pantalla == 0) {
        if (tecla == GLFW_KEY_ESCAPE) { if (g_confBorrar >= 0) g_confBorrar = -1; else glfwSetWindowShouldClose(w, 1); }
        else if (tecla == GLFW_KEY_N) g_crearNuevo = true;
        else if ((tecla == GLFW_KEY_ENTER || tecla == GLFW_KEY_KP_ENTER) && !g_mundos.empty() && g_confBorrar < 0) g_mundoElegido = 0;
        return;
    }
    if (tecla == GLFW_KEY_ESCAPE) {
        if (g_menu) cerrarMenu(w);
        else if (g_pausa) reanudar(w);
        else abrirPausa(w);
        return;
    }
    if (g_pausa) return;
    if (g_veh.activo && g_veh.pilotando) {      // mientras vuelas solo valen estas teclas
        if (tecla == GLFW_KEY_G) g_pedirG = true;
        else if (tecla == GLFW_KEY_TAB) g_veh.cabina = !g_veh.cabina;
        else if (tecla == GLFW_KEY_F5) g_guardarYa = true;
        else if (tecla == GLFW_KEY_T) g_hora = std::fmod(std::floor(g_hora * 4.0 + 1e-6) * 0.25 + 0.25, 1.0);
        else if (tecla == GLFW_KEY_V) { g_vsync = !g_vsync; glfwSwapInterval(g_vsync ? 1 : 0); }
        return;
    }
    if (tecla == GLFW_KEY_E) { if (g_menu) cerrarMenu(w); else abrirMenu(w); }
    if (g_menu) return;
    if (tecla == GLFW_KEY_G) g_pedirG = true;
    if (tecla == GLFW_KEY_H) g_pedirH = true;
    if (tecla == GLFW_KEY_Z || tecla == GLFW_KEY_C) {      // color del spray / tamano de la rueda
        int d = tecla == GLFW_KEY_C ? 1 : -1;
        if (g_material == ID_SPRAY) g_sprayColor = (g_sprayColor + d + 12) % 12;
        else if (g_material == RUEDA) g_ruedaD = std::max(2, std::min(24, g_ruedaD + d));
    }
    if (tecla == GLFW_KEY_F) { g_j.volar = !g_j.volar; g_j.v = {0, 0, 0}; }
    if (tecla == GLFW_KEY_SPACE) {
        double t = glfwGetTime();
        if (t - g_ultEspacio < 0.30) { g_j.volar = !g_j.volar; g_j.v = {0, 0, 0}; g_ultEspacio = -10; }
        else g_ultEspacio = t;
    }
    if (tecla == GLFW_KEY_T) g_hora = std::fmod(std::floor(g_hora * 4.0 + 1e-6) * 0.25 + 0.25, 1.0);   // saltar a la siguiente fase del dia
    if (tecla == GLFW_KEY_F5) g_guardarYa = true;
    if (tecla == GLFW_KEY_B || tecla == GLFW_KEY_LEFT_CONTROL) g_pincelBloque = !g_pincelBloque;
    if (tecla == GLFW_KEY_V) { g_vsync = !g_vsync; glfwSwapInterval(g_vsync ? 1 : 0); }
    if (tecla == GLFW_KEY_X || tecla == GLFW_KEY_0) { g_barra[g_sel] = 0; aplicarSel(); }
    if (tecla >= GLFW_KEY_1 && tecla <= GLFW_KEY_9) { g_sel = tecla - GLFW_KEY_1; aplicarSel(); }
}
static void alClic(GLFWwindow* w, int boton, int accion, int) {
    if (accion != GLFW_PRESS) return;
    if (g_pantalla == 0) {
        if (boton != GLFW_MOUSE_BUTTON_LEFT) return;
        int tipo, idx;
        mundosHit(g_mx, g_my, tipo, idx);
        if (tipo == 0) g_crearNuevo = true;
        else if (tipo == 1 && g_confBorrar < 0) g_mundoElegido = idx;
        else if (tipo == 2) g_confBorrar = idx;
        else if (tipo == 3) { borrarMundo(g_mundos[idx]); g_mundos = listarMundos(); g_confBorrar = -1; mundosClamp(); }
        else g_confBorrar = -1;
        return;
    }
    if (g_pausa) {
        if (boton != GLFW_MOUSE_BUTTON_LEFT) return;
        int b = pausaHit(g_mx, g_my);
        if (b == 0) reanudar(w);
        else if (b == 1) { g_lodOn = !g_lodOn; guardarOpciones(); if (!g_lodOn) lodLiberarTodo(); }
        else if (b == 2) { g_anclaOn = !g_anclaOn; guardarOpciones(); }
        else if (b == 3) { g_graf = !g_graf; guardarOpciones(); }
        else if (b == 4) glfwSetWindowShouldClose(w, 1);
        return;
    }
    if (g_menu) {
        if (boton == GLFW_MOUSE_BUTTON_LEFT) {
            int t = menuTileEn(g_mx, g_my);
            if (t >= 0) { g_barra[g_sel] = t; aplicarSel(); cerrarMenu(w); }
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
// icono de un material (o de la herramienta "destruir" en el hueco 0)
static void hIcono(int slot, float x, float y, float s) {
    static const float C[NTIPOS][3] = {{0.45f, 0.10f, 0.10f}, {0.34f, 0.64f, 0.21f}, {0.45f, 0.32f, 0.20f}, {0.50f, 0.50f, 0.52f},
                                       {0.86f, 0.80f, 0.55f}, {0.18f, 0.38f, 0.80f}, {0.40f, 0.28f, 0.15f}, {0.20f, 0.52f, 0.16f},
                                       {0.07f, 0.07f, 0.08f}, {0.80f, 0.60f, 0.47f}, {0.82f, 0.47f, 0.22f}, {0.96f, 0.80f, 0.18f},
                                       {0.30f, 0.92f, 0.95f}, {0.95f, 0.94f, 0.92f}, {0.62f, 0.64f, 0.72f},
                                       {0.66f, 0.62f, 0.55f}, {0.70f, 0.33f, 0.26f}, {0.72f, 0.78f, 0.88f}, {0.30f, 0.38f, 0.55f},
                                       {0.78f, 0.62f, 0.92f}, {0.45f, 0.95f, 0.18f}, {1.0f, 0.72f, 0.2f},
                                       {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}, {0.90f, 0.92f, 0.28f}, {0.93f, 0.82f, 0.80f},
                                       {1.0f, 0.10f, 0.10f}, {1.0f, 0.48f, 0.08f}, {1.0f, 0.90f, 0.15f}, {0.60f, 1.0f, 0.12f}, {0.10f, 1.0f, 0.20f}, {0.08f, 1.0f, 0.80f},
                                       {0.15f, 0.70f, 1.0f}, {0.15f, 0.20f, 1.0f}, {0.55f, 0.18f, 1.0f}, {1.0f, 0.15f, 0.80f}, {1.0f, 0.50f, 0.72f}, {1.0f, 1.0f, 1.0f}};
    const int N = 6;
    float c = s / N;
    if (slot == ID_SPRAY) {   // lata de spray con la tapa del color elegido
        const float* k = COLOR_CARR[g_sprayColor];
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.10f, 0.10f, 0.12f);
        for (int j = 1; j < N; j++) for (int i = 2; i < 5; i++) hRect(x + i * c, y + j * c, c + 0.5f, c + 0.5f, 0.62f, 0.64f, 0.70f);
        for (int i = 2; i < 5; i++) hRect(x + i * c, y + 0 * c, c + 0.5f, c + 0.5f, k[0], k[1], k[2]);
        hRect(x + 3 * c, y + 0 * c - c * 0.5f, c, c * 0.6f, 0.9f, 0.9f, 0.9f);
        return;
    }
    if (slot >= VIDRIO) {   // piezas de vehiculo
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
    bool mineral = esMineral((uint8_t)slot);
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        const float* col = C[slot];
        float r = col[0], g = col[1], b = col[2];
        if (slot == 1 && j >= 2) { r = 0.45f; g = 0.32f; b = 0.20f; }      // pasto: costado de tierra
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
static void hdibujar(int fbw, int fbh, int fps, bool cargando, bool guardado, double px_, double py_, double pz_) {
    float k = std::max(0.8f, fbh / 720.0f);
    float t = std::max(2.0f, std::floor(2.0f * k + 0.5f));           // tamano de pixel de letra
    char b[128];
    // ---- panel de informacion (arriba izquierda)
    float x = 12 * k, y = 10 * k, lh = 11.0f * t;
    std::snprintf(b, sizeof b, "FPS %d", fps); hTexto(b, x, y, t, 1, 1, 1); y += lh;
    std::snprintf(b, sizeof b, "X %d  Y %d  Z %d", (int)std::floor(px_), (int)std::floor(py_), (int)std::floor(pz_)); hTexto(b, x, y, t, 0.9f, 0.9f, 0.9f); y += lh;
    hTexto(g_pincelBloque ? "BLOQUE 2X2 (CTRL)" : "CUBITO (CTRL)", x, y, t, 1.0f, 0.85f, 0.3f); y += lh;
    hTexto((g_veh.activo && g_veh.pilotando) ? "PILOTANDO" : g_j.volar ? "VOLANDO (F)" : "CAMINANDO (F)", x, y, t, 0.6f, 0.9f, 1.0f); y += lh;
    {
        double hh_ = std::fmod(6.0 + g_hora * 24.0, 24.0);
        std::snprintf(b, sizeof b, "HORA %02d:%02d (T)", (int)hh_, (int)((hh_ - (int)hh_) * 60.0));
        hTexto(b, x, y, t, 1.0f, 0.95f, 0.7f); y += lh;
    }
    if (cargando) { hTexto("CARGANDO...", x, y, t, 1, 0.6f, 0.4f); y += lh; }
    if (guardado) { hTexto("GUARDADO", x, y, t, 0.5f, 1, 0.5f); y += lh; }
#ifndef MOVIL
    {
        const char* a = "E: MATERIALES";
        hTexto(a, fbw - hAncho(a, t) - 12 * k, 10 * k, t, 0.85f, 0.85f, 0.85f);
    }
#endif
    // ---- barra de acceso rapido
    float S = std::floor(48.0f * k), gap = std::floor(5.0f * k);
    float total = 9 * S + 8 * gap, x0 = (fbw - total) / 2.0f, y0 = fbh - S - 16 * k;
    int sel = g_sel;
    for (int i = 0; i < 9 && !(g_veh.activo && g_veh.pilotando); i++) {
        float sx = x0 + i * (S + gap);
        hRect(sx, y0, S, S, 0, 0, 0, 0.55f);
        float in = 5 * k;
        hIcono(g_barra[i], sx + in, y0 + in, S - 2 * in);
        if (i == sel) hMarco(sx - 2 * k, y0 - 2 * k, S + 4 * k, S + 4 * k, std::max(2.0f, 3 * k), 1, 1, 1);
        else hMarco(sx, y0, S, S, std::max(1.0f, k), 0.45f, 0.45f, 0.45f);
        std::snprintf(b, sizeof b, "%d", i + 1);
        hTexto(b, sx + 4 * k, y0 + 3 * k, std::max(2.0f, t - 1), 1, 1, 1);
    }
    if (!g_menu && !(g_veh.activo && g_veh.pilotando)) {
        char nm[64];
        if (g_destruir) std::snprintf(nm, sizeof nm, "DESTRUIR");
        else if (g_material == ID_SPRAY) std::snprintf(nm, sizeof nm, "SPRAY %s (Z/C COLOR) CLIC DER PINTA", NOMBRE_COLOR[g_sprayColor]);
        else if (g_material == RUEDA) std::snprintf(nm, sizeof nm, "PONER: RUEDA DIAMETRO %d (Z/C TAMANO)", g_ruedaD);
        else std::snprintf(nm, sizeof nm, "PONER: %s", nombreSlot(g_material));
        hTexto(nm, (fbw - hAncho(nm, t + 1)) / 2.0f, y0 - 12 * k - 7 * (t + 1), t + 1, 1, 1, 1);
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
    if (glfwGetTime() - g_avisoT < 4.0 && !g_aviso.empty()) {
        float ta = t + 1;
        while (ta > 1 && hAncho(g_aviso.c_str(), ta) > fbw - 24 * k) ta -= 1;
        hTexto(g_aviso.c_str(), (fbw - hAncho(g_aviso.c_str(), ta)) / 2.0f, fbh * 0.22f, ta, 1.0f, 0.9f, 0.4f);
    }
    // ---- menu de materiales
    if (g_menu) {
        hRect(0, 0, (float)fbw, (float)fbh, 0, 0, 0, 0.62f);
        float tt = t + 3;
        const char* ti = "MATERIALES";
        float xs, ys, ss;
        menuGeom(fbw, fbh, 0, xs, ys, ss);
        hTexto(ti, (fbw - hAncho(ti, tt)) / 2.0f, ys - 78 * k, tt, 1, 1, 1);
        char h1[96];
        std::snprintf(h1, sizeof h1, "CLIC: PONER EN LA CASILLA %d    E / ESC: CERRAR", g_sel + 1);
        hTexto(h1, (fbw - hAncho(h1, t)) / 2.0f, ys - 24 * k, t, 0.85f, 0.85f, 0.85f);
        int hov = menuTileEn(g_mx, g_my);
        for (int i = 0; i < NTIPOS_MENU; i++) {
            float sx, sy, sz;
            menuGeom(fbw, fbh, i, sx, sy, sz);
            hRect(sx - 6 * k, sy - 6 * k, sz + 12 * k, sz + 12 * k, 0.1f, 0.1f, 0.1f, 0.9f);
            const int ti2 = menuTipo(i);
            hIcono(ti2, sx, sy, sz);
            if (ti2 == hov) hMarco(sx - 6 * k, sy - 6 * k, sz + 12 * k, sz + 12 * k, std::max(3.0f, 4 * k), 1.0f, 0.85f, 0.2f);
            else if (ti2 == g_barra[g_sel]) hMarco(sx - 6 * k, sy - 6 * k, sz + 12 * k, sz + 12 * k, std::max(2.0f, 3 * k), 1, 1, 1);
            float np = t;
            while (np > 1 && hAncho(nombreSlot(ti2), np) > sz + 24 * k) np -= 1;
            hTexto(nombreSlot(ti2), sx + (sz - hAncho(nombreSlot(ti2), np)) / 2.0f, sy + sz + 10 * k, np, 1, 1, 1);
        }
    } else {
        // mira
        float cx = fbw / 2.0f, cy = fbh / 2.0f, L = 9 * k, e = std::max(2.0f, 2 * k);
        hRect(cx - L, cy - e / 2, 2 * L, e, 1, 1, 1, 0.9f);
        hRect(cx - e / 2, cy - L, e, 2 * L, 1, 1, 1, 0.9f);
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
    const char* ay = "CLIC: JUGAR    N: NUEVO    ENTER: ULTIMO MUNDO    ESC: SALIR";
    hTexto(ay, (fbw - hAncho(ay, t)) / 2.0f, fbh - 36 * k - 12 * t, t, 1.0f, 1.0f, 1.0f);
}

static void pausaDibujar(int fbw, int fbh) {
    float k = std::max(0.8f, fbh / 720.0f), t = std::max(2.0f, std::floor(2.0f * k + 0.5f));
    hRect(0, 0, (float)fbw, (float)fbh, 0, 0, 0, 0.58f);
    const char* ti = "PAUSA";
    float tp = std::max(4.0f, std::floor(6.0f * k));
    hTexto(ti, (fbw - hAncho(ti, tp)) / 2.0f, fbh * 0.30f - 11 * tp, tp, 1.0f, 0.88f, 0.35f);
    int hov = pausaHit(g_mx, g_my);
    hBoton(pausaRect(fbw, fbh, 0), k, 0.16f, 0.50f, 0.22f, hov == 0, "CONTINUAR", t * 1.5f);
    hBoton(pausaRect(fbw, fbh, 1), k, g_lodOn ? 0.16f : 0.45f, g_lodOn ? 0.36f : 0.20f, g_lodOn ? 0.60f : 0.20f, hov == 1,
           g_lodOn ? "MUNDO LEJANO: ON" : "MUNDO LEJANO: OFF", t * 1.25f);
    hBoton(pausaRect(fbw, fbh, 2), k, g_anclaOn ? 0.16f : 0.45f, g_anclaOn ? 0.36f : 0.20f, g_anclaOn ? 0.60f : 0.20f, hov == 2,
           g_anclaOn ? "CAMBIOS LEJANOS: ON" : "CAMBIOS LEJANOS: OFF", t * 1.25f);
    hBoton(pausaRect(fbw, fbh, 3), k, g_graf ? 0.16f : 0.45f, g_graf ? 0.36f : 0.20f, g_graf ? 0.60f : 0.20f, hov == 3,
           g_graf ? "GRAFICOS: ON" : "GRAFICOS: OFF", t * 1.25f);
    hBoton(pausaRect(fbw, fbh, 4), k, 0.45f, 0.14f, 0.14f, hov == 4, "GUARDAR Y SALIR", t * 1.5f);
    const char* ay = "ESC: CONTINUAR";
    hTexto(ay, (fbw - hAncho(ay, t)) / 2.0f, pausaRect(fbw, fbh, 5).y, t, 0.8f, 0.8f, 0.85f);
}

// ---------------------------------------------------------------- principal
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
    glfwSetMouseButtonCallback(ventana, alClic);
    migrarMundoViejo();
    cargarOpciones();
    if (const char* em = std::getenv("MUNDO")) {      // (pruebas) saltar el menu: MUNDO=<carpeta> [SEMILLA=n]
        g_dir = std::string(RAIZ_MUNDOS) + "/" + em;
        std::error_code ec0;
        std::filesystem::create_directories(g_dir, ec0);
        if (const char* es = std::getenv("SEMILLA")) g_semilla = (uint32_t)std::strtoul(es, nullptr, 10);
        g_nombreMundo = em;
        g_pantalla = 1;
    } else g_pantalla = 0;
    if (g_pantalla == 0) {
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

    // ---- menu de mundos (siempre al iniciar)
    if (g_pantalla == 0) {
        g_mundos = listarMundos();
        while (!glfwWindowShouldClose(ventana) && g_mundoElegido < 0 && !g_crearNuevo) {
            g_tac.ui = true; g_tac.uiJuego = false;
            glfwPollEvents();
            int fbw, fbh;
            glfwGetFramebufferSize(ventana, &fbw, &fbh);
            g_fbW = fbw; g_fbH = fbh;
            glfwGetWindowSize(ventana, &g_winW, &g_winH);
            if (fbw < 1 || fbh < 1) { glfwSwapBuffers(ventana); continue; }
            mundosClamp();
            glViewport(0, 0, fbw, fbh);
            glClearColor(0.42f, 0.62f, 0.90f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            g_hud.clear();
            mundosDibujar(fbw, fbh);
            subirHud(fbw, fbh);
            glfwSwapBuffers(ventana);
        }
        if (glfwWindowShouldClose(ventana)) { glfwTerminate(); return 0; }
        InfoMundo m = g_crearNuevo ? crearMundo() : g_mundos[g_mundoElegido];
        g_dir = m.carpeta; g_semilla = m.semilla; g_nombreMundo = m.nombre;
        g_capturado = true; g_primerMouse = true; g_pantalla = 1;
        glfwSetInputMode(ventana, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(ventana, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    int hilos = (int)std::thread::hardware_concurrency() - 1;
    hilos = std::max(2, std::min(8, hilos));
#ifdef MOVIL
    hilos = std::min(hilos, 3);          // celular: deja libres los nucleos grandes para dibujar y no calientes de mas
#endif
    g_hilos = hilos;
    g_pool.iniciar(hilos);

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
    vehCargar();
    if (const char* eh = std::getenv("HORA")) g_hora = std::atof(eh);   // (pruebas)
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
        g_tac.pil = g_veh.activo && g_veh.pilotando;
        g_tac.extra = g_material == ID_SPRAY || g_material == RUEDA;
        g_tac.volando = g_j.volar;
        glfwPollEvents();
#ifdef ANDROID_GAME
        if (A_pausado) { A_pausado = false; g_guardarYa = true; }      // al pasar a segundo plano se guarda
#endif
        double ahora = glfwGetTime();
        double dt = std::min(0.05, ahora - tAnterior);
        tAnterior = ahora;
        frame++; fpsCuenta++;

        // ---- carga y descarga de chunks
        int pcx = divPiso((int)std::floor(g_j.p.x * SUB), CS), pcz = divPiso((int)std::floor(g_j.p.z * SUB), CS);
        if (frame % 2 == 0) {
            for (const Off& o : offsets) {
                for (int cy = 0; cy < HYC; cy++) {
                    int cx = pcx + o.dx, cz = pcz + o.dz;
                    ChunkP c = buscar(cx, cy, cz);
                    if (!c) {
                        if (g_genPend.load() >= hilos * 6) continue;
                        c = std::make_shared<Chunk>(cx, cy, cz);
                        g_mapa[clave(cx, cy, cz)] = c;
                        pedirGenerar(c);
                    } else if (c->state.load(std::memory_order_acquire) == ST_READY && o.d2 <= RC * RC) {
                        if (!c->dat.d && c->dat.uni == AIRE) { c->state.store(ST_MESHED); continue; }   // aire: nada que dibujar
                        if (g_meshPend.load() < hilos * 4) pedirMalla(c, o.d2);
                    }
                }
            }
        }
        if (frame % 30 == 0) {
            anclarActualizar(g_j.p.x, g_j.p.z);
            for (auto it = g_mapa.begin(); it != g_mapa.end();) {
                int dx = it->second->cx - pcx, dz = it->second->cz - pcz;
                if (dx * dx + dz * dz > (RC + 3) * (RC + 3) && !g_requeridos.count(colKey(it->second->cx, it->second->cz))) {
                    it->second->viva.store(false);
                    if (it->second->sucioDisco.load() && it->second->state.load() >= ST_READY) guardarChunk(*it->second);
                    std::lock_guard<std::mutex> lk(it->second->mtx);
                    it->second->gOp.liberar();
                    it->second->gAg.liberar();
                    it->second->gLuz.liberar();
                    it = g_mapa.erase(it);
                } else ++it;
            }
        }
#ifdef MOVIL
        recogerMallas(4);                // celular: pocas mallas nuevas por cuadro (evita tirones)
#else
        recogerMallas(12);
#endif

        // ---- guardado (automatico cada 30 s, con F5, y al salir)
        if (g_guardarYa || ahora - tGuardado > 30.0) {
            int n = guardarTodosLosSucios();
            guardarJugador();
            vehGuardar();
            tGuardado = ahora; tMensaje = ahora; g_guardarYa = false;
            if (n) std::printf("Guardado: %d chunks\n", n);
        }

        // ---- fisica
        bool conocido = true;
        leerCubito((int)std::floor(g_j.p.x * SUB), (int)std::floor((g_j.p.y - 0.2) * SUB), (int)std::floor(g_j.p.z * SUB), &conocido);
        if (cargando && conocido) cargando = false;
        const bool pil = g_veh.activo && g_veh.pilotando;
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
                double vel = correr ? 7.0 : 4.3;
                if (enAgua) vel *= 0.6;
                g_j.v.x = mov.x * vel + g_j.inercia.x;
                g_j.v.z = mov.z * vel + g_j.inercia.z;
                g_j.v.y -= (enAgua ? 8.0 : 28.0) * dt;
                if (g_j.v.y < (enAgua ? -3.0 : -50.0)) g_j.v.y = enAgua ? -3.0 : -50.0;
                if (salto && enAgua) g_j.v.y = 4.0;
                else if (salto && g_j.suelo) g_j.v.y = 9.0;
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
                if (sobre) vehEntrar();                       // pisaste el vehiculo: pasas a caminar en su sistema de referencia
                else if (g_j.suelo) g_j.inercia = {0, 0, 0};
                else g_j.inercia = g_j.inercia * std::exp(-0.6 * dt);
            }
        }

        // ---- camara
        V3 mira = {std::cos(g_j.pitch) * std::sin(g_j.yaw), std::sin(g_j.pitch), -std::cos(g_j.pitch) * std::cos(g_j.yaw)};
        V3 cam = {g_j.p.x, g_j.p.y + g_j.ojo, g_j.p.z};
        V3 camUp = {0, 1, 0};
        if (pil) vehCamara(mira, cam, camUp);
        int fbw, fbh;
        glfwGetFramebufferSize(ventana, &fbw, &fbh);
        g_fbW = fbw; g_fbH = fbh;
        glfwGetWindowSize(ventana, &g_winW, &g_winH);
        if (fbw < 1 || fbh < 1) { glfwSwapBuffers(ventana); continue; }
        float proy[16], vista[16], vp[16];
        matPerspectiva(proy, 1.22f, (float)fbw / fbh, 0.05f, g_lodOn ? 1500.0f : 800.0f);
        matVista(vista, mira, camUp);
        matMul(vp, proy, vista);

        // ---- romper / poner
        Golpe golpe;
        if (!cargando && !pil) golpe = lanzarRayo(cam, norm(mira), 8.0);
        bool izq = !pil && g_capturado && glfwGetMouseButton(ventana, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool der = !pil && g_capturado && glfwGetMouseButton(ventana, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (g_pedirG) { g_pedirG = false; if (pil) vehSubirBajar(); else if (g_veh.activo) vehSubirBajar(); else if (golpe.hay) vehEnsamblar(golpe.x, golpe.y, golpe.z); else aviso("MIRA UNA PIEZA DEL VEHICULO Y PULSA G"); if (g_veh.activo) g_guardarYa = true; }
        if (g_pedirH) { g_pedirH = false; if (g_veh.activo && !g_veh.pilotando) { vehDesarmar(); g_guardarYa = true; } }
        if (golpe.hay && (izq || der) && ahora - tAccion > 0.18) {
            tAccion = ahora;
            std::vector<ChunkP> sucios;
            auto celda = [&](int x, int y, int z, int& x0, int& y0, int& z0, int& n) {
                if (g_pincelBloque) { x0 = divPiso(x, SUB) * SUB; y0 = divPiso(y, SUB) * SUB; z0 = divPiso(z, SUB) * SUB; n = SUB; }
                else { x0 = x; y0 = y; z0 = z; n = 1; }
            };
            if (izq || (der && g_destruir)) {
                int x0, y0, z0, n;
                celda(golpe.x, golpe.y, golpe.z, x0, y0, z0, n);
                for (int y = y0; y < y0 + n; y++) for (int z = z0; z < z0 + n; z++) for (int x = x0; x < x0 + n; x++)
                    if (esRompible(leerCubito(x, y, z))) escribirCubito(x, y, z, AIRE, sucios);
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
                        escribirCubito(x, y, z, RUEDA, sucios);
                    }
                }
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
                    escribirCubito(x, y, z, (uint8_t)g_material, sucios);
                }
            }
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
            g_j.p = g_veh.p + V3{3.0, 0.5, 0}; g_pedirG = true;
            std::printf("AVION celdas=%d largo=%d enverg=%d sust=%.2f hel=%d asientos=%d timon=%d puntos=%d\n", (int)g_veh.celdas.size(), g_veh.largo, g_veh.enverg, g_veh.sust, g_veh.nHel, g_veh.nAsiento, g_veh.nTimon, (int)g_veh.pts.size());
        }
        if (std::getenv("PRUEBA_AVION_TECHO") && frame == 560) g_j.p.y += 6.0;      // (pruebas) saltar al techo del avion en pleno vuelo
        if (std::getenv("PRUEBA_AVION") && frame % 20 == 0 && g_veh.activo)
            std::printf("f=%d pos=%.1f %.1f %.1f v=%.1f %.1f %.1f pitch=%.2f gas=%.2f suelo=%d pil=%d | jug=%.1f %.1f %.1f sobre=%d\n", frame, g_veh.p.x, g_veh.p.y, g_veh.p.z, g_veh.v.x, g_veh.v.y, g_veh.v.z, g_veh.pitch, g_veh.gas, g_veh.suelo ? 1 : 0, g_veh.pilotando ? 1 : 0, g_j.p.x, g_j.p.y, g_j.p.z, g_sobreVeh ? 1 : 0);
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

        // ---- dibujo
        glViewport(0, 0, fbw, fbh);
        // hora del dia: 0 amanecer, 0.25 mediodia, 0.5 atardecer, 0.75 medianoche
        g_hora = std::fmod(g_hora + dt / 600.0, 1.0);
        float sh = (float)std::sin(g_hora * 6.2831853);
        float dia = std::max(0.0f, std::min(1.0f, (sh + 0.25f) / 0.60f));
        dia = dia * dia * (3.0f - 2.0f * dia);
        float skyMul = 0.14f + 0.86f * dia;
        float cielo[3] = {0.015f + 0.535f * dia, 0.02f + 0.73f * dia, 0.06f + 0.89f * dia};
        float ocaso = std::exp(-(sh / 0.28f) * (sh / 0.28f)) * 0.45f;
        cielo[0] += (0.95f - cielo[0]) * ocaso; cielo[1] += (0.50f - cielo[1]) * ocaso; cielo[2] += (0.30f - cielo[2]) * ocaso;
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
                for (auto& kv : g_mapa) {
                    Chunk* c = kv.second.get();
                    if (c->state.load(std::memory_order_acquire) == ST_GEN || !c->gOp.n) continue;
                    double ddx = (c->cx + 0.5) * tamS - cam.x, ddz = (c->cz + 0.5) * tamS - cam.z;
                    if (std::fabs(ddx) > SOMB_R + tamS || std::fabs(ddz) > SOMB_R + tamS) continue;
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
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glUseProgram(progCielo);
            glUniform2f(glGetUniformLocation(progCielo, "uRes"), (float)fbw, (float)fbh);
            glUniform3f(glGetUniformLocation(progCielo, "uFwd"), (float)fw.x, (float)fw.y, (float)fw.z);
            glUniform3f(glGetUniformLocation(progCielo, "uRight"), (float)rg.x, (float)rg.y, (float)rg.z);
            glUniform3f(glGetUniformLocation(progCielo, "uUp"), (float)up.x, (float)up.y, (float)up.z);
            glUniform2f(glGetUniformLocation(progCielo, "uTan"), tanX, tanY);
            glUniform3f(glGetUniformLocation(progCielo, "uHor"), cielo[0], cielo[1], cielo[2]);
            glUniform3f(glGetUniformLocation(progCielo, "uZen"), zen[0], zen[1], zen[2]);
            glUniform3f(glGetUniformLocation(progCielo, "uSun"), sx / sl, sy / sl, sz / sl);
            glUniform1f(glGetUniformLocation(progCielo, "uNoche"), 1.0f - dia);
            glBindVertexArray(vaoVacio);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glEnable(GL_MULTISAMPLE);

        if (g_lodOn) {   // ---- mundo lejano: se dibuja primero con su propia proyeccion y luego se borra la profundidad
            for (auto& kv : g_lod) if (kv.second->estado.load(std::memory_order_acquire) == 1) { lodSubir(*kv.second); break; }
            lodReemplazar();
            if (frame % 6 == 0) { lodProcesarRegen(); lodActualizar(cam.x, cam.z); }
            float projL[16], vpL[16], plL[6][4];
            matPerspectiva(projL, 1.22f, (float)fbw / fbh, 1.5f, 6000.0f);
            matMul(vpL, projL, vista);
            planosFrustum(vpL, plL);
            glUseProgram(progLod);
            glUniformMatrix4fv(glGetUniformLocation(progLod, "uVP"), 1, GL_FALSE, vpL);
            glUniform3f(glGetUniformLocation(progLod, "uSky"), cielo[0], cielo[1], cielo[2]);
            glUniform1f(glGetUniformLocation(progLod, "uFogA"), (float)(LOD_FOG_MAX * 0.55));
            glUniform1f(glGetUniformLocation(progLod, "uFogB"), (float)(LOD_FOG_MAX * 0.97));
            glUniform1f(glGetUniformLocation(progLod, "uSkyMul"), skyMul);
            GLint lRel = glGetUniformLocation(progLod, "uRel"), lAlfa = glGetUniformLocation(progLod, "uAlpha");
            std::vector<LodTile*> vis;
            for (auto& kv : g_lod) {
                LodTile* t = kv.second.get();
                if (t->estado.load(std::memory_order_acquire) != 2) continue;
                if (!lodNecesario(t->niv, t->tx, t->tz, cam.x, cam.z, 0.0)) continue;
                double ts = (double)LOD_N * LOD_CELDA[t->niv];
                double mnx = t->tx * ts - cam.x, mny = -cam.y, mnz = t->tz * ts - cam.z, hy = 100.0;
                bool fuera = false;
                for (int k = 0; k < 6 && !fuera; k++) {
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
        glActiveTexture(GL_TEXTURE0);
        glUniform1f(uFogB, (float)(g_lodOn ? LOD_FOG_MAX : VIEW_BLOCKS) * 0.97f);
        glUniform1f(uFogA, (float)(g_lodOn ? LOD_FOG_MAX * 0.55 / 0.60 : VIEW_BLOCKS) * 0.60f);
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

        std::vector<Chunk*> visibles;
        std::unordered_map<int64_t, bool> colOk;
        std::unordered_map<int64_t, double> colMinH;
        const double tam = CS * VS;
        for (auto& kv : g_mapa) {
            Chunk* c = kv.second.get();
            if (c->state.load(std::memory_order_acquire) == ST_GEN) continue;     // se sigue dibujando la malla anterior mientras se rehace
            if (!c->gOp.n && !c->gAg.n) continue;
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
            if (!fuera) visibles.push_back(c);
        }
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
        const float fogAv = (float)(g_lodOn ? LOD_FOG_MAX * 0.55 / 0.60 : VIEW_BLOCKS) * 0.60f, fogBv = (float)(g_lodOn ? LOD_FOG_MAX : VIEW_BLOCKS) * 0.97f;
        glDisable(GL_CULL_FACE);
        vehDibujar(vp, cam, solV, solF, skyMul, gfx ? 1.0f : 0.0f, cielo, fogAv, fogBv, false);
        glUseProgram(progMundo);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        dibujar(true);
        vehDibujar(vp, cam, solV, solF, skyMul, gfx ? 1.0f : 0.0f, cielo, fogAv, fogBv, true);

        // ---- nubes: cajas de 16 bloques a ~110 de altura; la malla se rehace al cruzar de celda, el viento solo la desplaza
        {
            const double CN = 16.0, NUBE_Y = 110.0, NUBE_ALTO = 5.0;
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
        // ---- HUD (texto, barra de materiales, menu, mira)
        {
            g_hud.clear();
            hdibujar(fbw, fbh, fps, cargando, ahora - tMensaje < 3.0, g_j.p.x, g_j.p.y, g_j.p.z);
            if (g_pausa) pausaDibujar(fbw, fbh);
            subirHud(fbw, fbh);
        }

        glfwSwapBuffers(ventana);

        // ---- informacion en el titulo de la ventana
        if (ahora - tFps >= 0.5) {
            fps = (int)(fpsCuenta / (ahora - tFps));
            fpsCuenta = 0; tFps = ahora;
            char t[320];
            std::snprintf(t, sizeof t, "Juego Voxel [%s] | %d FPS | %s | lejano %s %zu | chunks %zu (dibujados %d) | %dk triangulos | pos %.0f %.0f %.0f | pincel: %s | material: %s | %s",
                          g_nombreMundo.c_str(), fps, cargando ? "CARGANDO..." : "listo", g_lodOn ? "ON" : "OFF", g_lod.size(), g_mapa.size(), mallasDib, triDib / 1000,
                          g_j.p.x, g_j.p.y, g_j.p.z, g_pincelBloque ? "BLOQUE" : "cubito", nombreSlot(g_material),
                          ahora - tMensaje < 3.0 ? "GUARDADO" : (g_j.volar ? "VOLANDO" : "caminando"));
            glfwSetWindowTitle(ventana, t);
        }
    }
    int nFin = guardarTodosLosSucios();
    guardarJugador();
    std::printf("Al salir: %d chunks guardados | cargados desde disco en esta sesion: %d\n", nFin, g_cargadosDisco.load());
    g_pool.detener();
    glfwTerminate();
    return 0;
}

#ifdef ANDROID_GAME
void android_main(struct android_app* app) {       // punto de entrada en Android (NativeActivity)
    A_app = app;
    if (app->activity && app->activity->internalDataPath) { if (chdir(app->activity->internalDataPath) != 0) A_log("chdir fallo"); }   // aqui se guardan mundos y opciones
    juego_main();
    ANativeActivity_finish(app->activity);
}
#else
int main() { return juego_main(); }
#endif

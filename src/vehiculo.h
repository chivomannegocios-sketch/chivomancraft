// Vehiculos (v18): una avioneta hecha de cubitos.
//  - Se construye con bloques normales (CONTROL, CARROCERIA, VIDRIO, ASIENTO, TIMON, ALA, RUEDA, HELICE).
//  - G mirando una pieza la "ensambla": todas las piezas conectadas se sacan del mundo y pasan a ser un cuerpo rigido que se dibuja
//    con su propia malla (las helices giran). G cerca de la avioneta ensamblada = subir / bajar. H = desarmar (vuelve a ser bloques).
//  - Fisica sencilla tipo arcade: empuje de las helices, sustentacion de las alas (solo hay sustentacion si la envergadura es al menos la
//    mitad del largo), resistencia, gravedad y contacto con el terreno por puntos de la superficie (las ruedas absorben el aterrizaje).
// Este archivo se incluye desde main.cpp (usa leerCubito, escribirCubito, remallarYa, marcarLuzSucia, programa, g_dir, V3...).
#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static const float COLOR_CARR[12][3] = {{0.80f, 0.12f, 0.12f}, {0.95f, 0.45f, 0.08f}, {0.95f, 0.85f, 0.15f}, {0.55f, 0.80f, 0.15f},
                                        {0.12f, 0.62f, 0.22f}, {0.10f, 0.65f, 0.62f}, {0.18f, 0.55f, 0.90f}, {0.15f, 0.22f, 0.75f},
                                        {0.45f, 0.20f, 0.78f}, {0.80f, 0.18f, 0.62f}, {0.95f, 0.55f, 0.70f}, {0.97f, 0.97f, 0.97f}};
static const char* NOMBRE_COLOR[12] = {"ROJO", "NARANJA", "AMARILLO", "LIMA", "VERDE", "TURQUESA", "CELESTE", "AZUL", "VIOLETA", "MAGENTA", "ROSA", "BLANCO"};
static int g_sprayColor = 0;      // color actual del spray (0..11)
static int g_ruedaD = 6;          // diametro de la rueda en cubitos (Z / C cambian el tamano)

static bool esEmisivoBase(uint8_t t) { return t == PLASMA_FRIO || t == PLASMA_VERDE || t == PLASMA_CELESTE || t == PLASMA_ROJO || t == PLASMA_AMBAR || t == BASE_PANTALLA || t == BASE_CONSOLA || t == BASE_BOTON; }
static void colorPieza(uint8_t t, float* c) {
    float r = 0.5f, g = 0.5f, b = 0.5f;
    switch (t) {
        case VIDRIO: r = 0.62f; g = 0.80f; b = 0.90f; break;
        case CARROCERIA: r = 0.85f; g = 0.86f; b = 0.88f; break;
        case ASIENTO: r = 0.55f; g = 0.12f; b = 0.12f; break;
        case TIMON: r = 0.15f; g = 0.15f; b = 0.18f; break;
        case ALA: r = 0.92f; g = 0.92f; b = 0.95f; break;
        case RUEDA: r = 0.07f; g = 0.07f; b = 0.08f; break;
        case HELICE: r = 0.55f; g = 0.38f; b = 0.20f; break;
        case CONTROL: r = 0.95f; g = 0.65f; b = 0.10f; break;
        default:
            if (esLed(t)) { const float* k = LED_RGB[t - LED_ROJO]; r = k[0]; g = k[1]; b = k[2]; }
            else if (t >= CARR_0 && t <= CARR_11) { const float* k = COLOR_CARR[t - CARR_0]; r = k[0]; g = k[1]; b = k[2]; }
            else switch (t) {      // bloques normales (el cohete)
                case NIEVE: r = 0.93f; g = 0.94f; b = 0.96f; break;
                case PIEDRA: r = 0.52f; g = 0.53f; b = 0.56f; break;
                case CARBON: r = 0.09f; g = 0.09f; b = 0.11f; break;
                case COBRE: r = 0.80f; g = 0.45f; b = 0.24f; break;
                case ARCILLA: r = 0.58f; g = 0.44f; b = 0.38f; break;
                case TITANIO: r = 0.70f; g = 0.72f; b = 0.77f; break;
                case ALUMINIO: r = 0.80f; g = 0.82f; b = 0.86f; break;
                case HIERRO: r = 0.60f; g = 0.60f; b = 0.64f; break;
                // v71: piezas del cohete moderno de la base lunar
                case BASE_ACERO: r = 0.50f; g = 0.53f; b = 0.58f; break;
                case BASE_MURO: r = 0.48f; g = 0.48f; b = 0.50f; break;
                case BASE_METAL: r = 0.72f; g = 0.75f; b = 0.80f; break;
                case BASE_PLACA: r = 0.93f; g = 0.94f; b = 0.96f; break;
                case BASE_CABLE: r = 0.80f; g = 0.12f; b = 0.10f; break;
                case BASE_PISO: r = 0.34f; g = 0.35f; b = 0.40f; break;
                case BASE_FRANJA: r = 0.95f; g = 0.75f; b = 0.10f; break;
                case BASE_AVISO: r = 0.90f; g = 0.50f; b = 0.10f; break;
                case BASE_CONCRETO: r = 0.60f; g = 0.60f; b = 0.60f; break;
                case BASE_TUBERIA: case BASE_REJILLA: r = 0.60f; g = 0.60f; b = 0.65f; break;
                case PLASMA_FRIO: r = 0.60f; g = 0.90f; b = 1.00f; break;
                case PLASMA_VERDE: r = 0.30f; g = 1.00f; b = 0.50f; break;
                case PLASMA_CELESTE: r = 0.40f; g = 0.90f; b = 1.00f; break;
                case PLASMA_ROJO: r = 1.00f; g = 0.20f; b = 0.15f; break;
                case PLASMA_AMBAR: r = 1.00f; g = 0.70f; b = 0.20f; break;
                case BASE_PANTALLA: r = 0.30f; g = 0.70f; b = 1.00f; break;
                case BASE_CONSOLA: r = 0.25f; g = 0.45f; b = 0.65f; break;
                case BASE_BOTON: r = 1.00f; g = 0.20f; b = 0.20f; break;
                default: break;
            }
    }
    c[0] = r; c[1] = g; c[2] = b;
}

static bool esParteVeh(uint8_t t) { return esPiezaVehiculo(t) || t == VIDRIO || esLed(t); }

static std::string g_aviso;
static double g_avisoT = -10;
static void aviso(const char* s) { g_aviso = s; g_avisoT = glfwGetTime(); }

struct VMalla {
    GLuint vao = 0, vbo = 0;
    int n = 0;      // vertices
    void liberar() {
        if (vao) { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); }
        vao = vbo = 0; n = 0;
    }
    void subir(const std::vector<float>& v) {
        liberar();
        if (v.empty()) return;
        n = (int)(v.size() / 10);
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, (const void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, (const void*)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 40, (const void*)24);
    }
};

struct VCelda { int x, y, z; uint8_t t; };      // posicion local (en cubitos) respecto al bloque CONTROL
struct VPunto { int x, y, z; bool rueda; };
struct VHelice { V3 pivote; VMalla malla; };    // pivote en bloques (local)

struct Veh {
    bool activo = false, pilotando = false, cabina = false;
    std::vector<VCelda> celdas;
    std::vector<VPunto> pts;                 // puntos de contacto (superficie)
    std::vector<uint8_t> grid; int gx0 = 0, gy0 = 0, gz0 = 0, gnx = 0, gny = 0, gnz = 0;   // celdas (para que camines encima)
    V3 p{0, 0, 0}, v{0, 0, 0};               // posicion del bloque CONTROL (centro) y velocidad, en bloques
    double yaw = 0, pitch = 0, roll = 0, gas = 0, angHel = 0;
    double hy = 0, hp = 0;                   // mirada alrededor (cabina) u orbita (camara externa)
    bool suelo = false;
    // medidas
    int largo = 1, enverg = 0, nHel = 0, nAsiento = 0, nTimon = 0;
    double sust = 0;                         // factor de sustentacion (0 = las alas no alcanzan)
    V3 ojo{0, 0, 0};                         // ojo del piloto (bloques, local)
    double radio = 4;
    VMalla cuerpo, vidrio;
    bool libre = false; V3 bf{0, 0, -1}, br{1, 0, 0}, bu{0, 1, 0};   // actitud libre (cohete en el espacio): base propia en vez de yaw/pitch/roll
    std::vector<VHelice> helices;
    int pilotoRed = -1;                      // red: id del otro jugador que lo pilota (-1 = nadie)
    bool objValido = false;                  // red: hay una pose objetivo recibida
    V3 objP{0, 0, 0}, objV{0, 0, 0}; double objYaw = 0, objPitch = 0, objRoll = 0, objGas = 0; bool objSuelo = false;
};
static Veh g_veh;
static bool g_pedirG = false, g_pedirH = false;
static bool g_sobreVeh = false;        // el jugador camina dentro del sistema de referencia del vehiculo (viaja con el)
static V3 g_L{0, 0, 0}, g_vL{0, 0, 0}; // posicion y velocidad locales (bloques) mientras g_sobreVeh
static double g_aireVeh = 0;
static bool g_saltoVeh = false;
static void vehEntrar();
static std::function<void()> g_vehHookSet, g_vehHookClear;   // red: avisan al ensamblar / desarmar
static int g_avionOrg[3] = {0, 0, 0};   // (pruebas)

// base del vehiculo: adelante, derecha, arriba
static void vehBase(const Veh& v, V3& f, V3& r, V3& u) {
    if (v.libre) { f = v.bf; r = v.br; u = v.bu; return; }
    double cp = std::cos(v.pitch), sp = std::sin(v.pitch);
    V3 r0 = {std::cos(v.yaw), 0, std::sin(v.yaw)};
    V3 f0 = {std::sin(v.yaw) * cp, sp, -std::cos(v.yaw) * cp};
    V3 u0 = cruz(r0, f0);
    double cr = std::cos(v.roll), sr = std::sin(v.roll);
    r = r0 * cr - u0 * sr;
    u = u0 * cr + r0 * sr;
    f = f0;
}

// ---------------------------------------------------------------- malla y medidas
static void vehReconstruir(Veh& v) {
    if (v.celdas.empty()) return;
    int x0 = 1 << 30, y0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30), z1 = -(1 << 30);
    for (auto& c : v.celdas) { x0 = std::min(x0, c.x); x1 = std::max(x1, c.x); y0 = std::min(y0, c.y); y1 = std::max(y1, c.y); z0 = std::min(z0, c.z); z1 = std::max(z1, c.z); }
    const int nx = x1 - x0 + 1, ny = y1 - y0 + 1, nz = z1 - z0 + 1;
    std::vector<uint8_t> grid((size_t)nx * ny * nz, 0);
    auto gi = [&](int x, int y, int z) { return ((size_t)(y - y0) * nz + (z - z0)) * nx + (x - x0); };
    auto tipoEn = [&](int x, int y, int z) -> uint8_t {
        if (x < x0 || x > x1 || y < y0 || y > y1 || z < z0 || z > z1) return 0;
        return grid[gi(x, y, z)];
    };
    for (auto& c : v.celdas) grid[gi(c.x, c.y, c.z)] = c.t;
    v.grid = grid; v.gx0 = x0; v.gy0 = y0; v.gz0 = z0; v.gnx = nx; v.gny = ny; v.gnz = nz;

    // medidas
    int mz0 = 1 << 30, mz1 = -(1 << 30), ax0 = 1 << 30, ax1 = -(1 << 30);
    v.nHel = v.nAsiento = v.nTimon = 0;
    double sx = 0, sy = 0, sz = 0; int ns = 0, ymaxAs = -(1 << 30);
    for (auto& c : v.celdas) {
        mz0 = std::min(mz0, c.z); mz1 = std::max(mz1, c.z);
        if (c.t == ALA) { ax0 = std::min(ax0, c.x); ax1 = std::max(ax1, c.x); }
        if (c.t == HELICE) v.nHel++;
        if (c.t == TIMON) v.nTimon++;
        if (c.t == ASIENTO) { v.nAsiento++; sx += c.x; sz += c.z; ns++; ymaxAs = std::max(ymaxAs, c.y); }
    }
    v.largo = mz1 - mz0 + 1;
    v.enverg = ax1 >= ax0 ? ax1 - ax0 + 1 : 0;
    // las alas deben medir al menos la mitad del largo del avion
    v.sust = (v.enverg > 0 && v.enverg * 2 >= v.largo) ? std::min(1.35, 0.8 + 0.7 * v.enverg / (double)v.largo) : 0.0;
    if (ns) v.ojo = {sx / ns * VS, (ymaxAs + 0.5) * VS + 0.75, sz / ns * VS};
    else v.ojo = {0, 0.9, 0};
    v.radio = std::max({(double)v.largo, (double)std::max(v.enverg, nx), (double)ny}) * VS;

    // puntos de contacto: celdas de la superficie (un subconjunto, para no gastar de mas)
    std::vector<VPunto> sup;
    static const int D6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (auto& c : v.celdas) {
        bool borde = false;
        for (auto& d : D6) if (!tipoEn(c.x + d[0], c.y + d[1], c.z + d[2])) { borde = true; break; }
        if (borde) sup.push_back({c.x, c.y, c.z, c.t == RUEDA});
    }
    int mod = std::max(1, (int)((sup.size() + 3999) / 4000));
    v.pts.clear();
    for (auto& q : sup) if (((q.x * 73 + q.y * 31 + q.z * 17) % mod + mod) % mod == 0) v.pts.push_back(q);

    // mallas (caras expuestas)
    std::vector<float> mo, mv;
    auto quad = [&](std::vector<float>& out, int x, int y, int z, int k, const float* col, float a, float sh) {
        int ax = k / 2, u = (ax + 1) % 3, w = (ax + 2) % 3;
        float s = (k & 1) ? -0.5f : 0.5f;
        float c[4][3];
        for (int j = 0; j < 4; j++) {
            c[j][ax] = s;
            c[j][u] = (j == 1 || j == 2) ? 0.5f : -0.5f;
            c[j][w] = (j >= 2) ? 0.5f : -0.5f;
        }
        float nrm[3] = {0, 0, 0}; nrm[ax] = (k & 1) ? -1.0f : 1.0f;
        int id[6] = {0, 1, 2, 0, 2, 3};
        for (int q = 0; q < 6; q++) {
            const float* cc = c[id[q]];
            out.push_back((x + cc[0]) * (float)VS); out.push_back((y + cc[1]) * (float)VS); out.push_back((z + cc[2]) * (float)VS);
            out.push_back(nrm[0]); out.push_back(nrm[1]); out.push_back(nrm[2]);
            out.push_back(col[0] * sh); out.push_back(col[1] * sh); out.push_back(col[2] * sh); out.push_back(a);
        }
    };
    auto hsh = [](int x, int y, int z) {
        unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)z * 2147483647u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float)(h & 0xffff) / 65535.0f;
    };
    // helices: cada grupo conectado (26 vecinos) gira alrededor de su centro
    std::vector<char> visto(v.celdas.size(), 0);
    std::unordered_map<uint64_t, int> idxHel;
    auto key = [](int x, int y, int z) { return ((uint64_t)(x + (1 << 20)) << 42) | ((uint64_t)(y + (1 << 20)) << 21) | (uint64_t)(z + (1 << 20)); };
    for (size_t i = 0; i < v.celdas.size(); i++) if (v.celdas[i].t == HELICE) idxHel[key(v.celdas[i].x, v.celdas[i].y, v.celdas[i].z)] = (int)i;
    for (auto& h : v.helices) h.malla.liberar();
    v.helices.clear();
    for (size_t i = 0; i < v.celdas.size(); i++) {
        if (v.celdas[i].t != HELICE || visto[i]) continue;
        std::vector<int> grupo; grupo.push_back((int)i); visto[i] = 1;
        for (size_t h = 0; h < grupo.size(); h++) {
            VCelda c = v.celdas[grupo[h]];
            for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) {
                auto it = idxHel.find(key(c.x + dx, c.y + dy, c.z + dz));
                if (it != idxHel.end() && !visto[it->second]) { visto[it->second] = 1; grupo.push_back(it->second); }
            }
        }
        double cx = 0, cy = 0, cz = 0;
        for (int gI : grupo) { cx += v.celdas[gI].x; cy += v.celdas[gI].y; cz += v.celdas[gI].z; }
        cx /= grupo.size(); cy /= grupo.size(); cz /= grupo.size();
        VHelice H; H.pivote = {cx * VS, cy * VS, cz * VS};
        std::vector<float> mh;
        float col[3]; colorPieza(HELICE, col);
        for (int gI : grupo) {
            const VCelda& c = v.celdas[gI];
            for (int k = 0; k < 6; k++) {
                uint8_t o = tipoEn(c.x + D6[k][0], c.y + D6[k][1], c.z + D6[k][2]);
                if (o) continue;
                float va = 1.0f + (hsh(c.x, c.y, c.z) - 0.5f) * 0.10f;
                quad(mh, c.x, c.y, c.z, k, col, 1.0f, va);
            }
        }
        H.malla.subir(mh);
        v.helices.push_back(std::move(H));
    }
    for (auto& c : v.celdas) {
        if (c.t == HELICE) continue;
        float col[3]; colorPieza(c.t, col);
        for (int k = 0; k < 6; k++) {
            uint8_t o = tipoEn(c.x + D6[k][0], c.y + D6[k][1], c.z + D6[k][2]);
            float va = 1.0f + (hsh(c.x, c.y, c.z) - 0.5f) * 0.08f;
            if (c.t == VIDRIO) { if (!o) quad(mv, c.x, c.y, c.z, k, col, 0.32f, 1.0f); }
            else if (esLed(c.t) || esEmisivoBase(c.t)) { if (!o || o == VIDRIO) quad(mo, c.x, c.y, c.z, k, col, 2.0f, 1.0f); }
            else if (!o || o == VIDRIO) quad(mo, c.x, c.y, c.z, k, col, 1.0f, va);
        }
    }
    v.cuerpo.subir(mo);
    v.vidrio.subir(mv);
}

// ---------------------------------------------------------------- shader
static const char* VV_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aN;
layout(location=2) in vec4 aCol;
uniform mat4 uVP;
uniform vec3 uPos, uR, uU, uF, uPiv;
uniform float uAng;
out vec3 vN;
out vec4 vCol;
out float vDist;
void main(){
    vec3 l = aPos - uPiv;
    float c = cos(uAng), s = sin(uAng);
    vec3 q = vec3(l.x * c - l.y * s, l.x * s + l.y * c, l.z) + uPiv;
    vec3 nl = vec3(aN.x * c - aN.y * s, aN.x * s + aN.y * c, aN.z);
    vec3 w = uPos + uR * q.x + uU * q.y + uF * q.z;
    vN = uR * nl.x + uU * nl.y + uF * nl.z;
    vCol = aCol;
    vDist = length(w);
    gl_Position = uVP * vec4(w, 1.0);
})";
static const char* FV_SRC = R"(#version 330 core
in vec3 vN;
in vec4 vCol;
in float vDist;
out vec4 frag;
uniform vec3 uSun, uSky;
uniform float uSolF, uSkyMul, uHDR, uFogA, uFogB;
void main(){
    vec3 n = normalize(vN);
    float ndl = max(dot(n, uSun), 0.0);
    float lum = (0.40 + 0.22 * (n.y * 0.5 + 0.5) + 0.50 * ndl * uSolF) * uSkyMul + 0.05;
    vec3 col = vCol.rgb * lum * (1.0 + 0.2 * uHDR * ndl * uSolF);
    if (vCol.a > 1.5) { float f0 = clamp((vDist - uFogA) / (uFogB - uFogA), 0.0, 1.0); frag = vec4(mix(vCol.rgb * mix(1.5, 2.2, uHDR), uSky, f0), 1.0); return; }   // LED: brilla solo
    col *= mix(vec3(0.88, 0.92, 1.0), vec3(1.03, 1.0, 0.94), ndl * uSolF);
    float f = clamp((vDist - uFogA) / (uFogB - uFogA), 0.0, 1.0);
    frag = vec4(mix(col, uSky, f), vCol.a);
})";
static GLuint g_progVeh = 0;

static void vehDibujarUno(Veh& v, const float* vp, V3 cam, const float* sol, float solF, float skyMul, float hdr, const float* cielo, float fogA, float fogB, bool vidrio) {
    if (!v.activo || v.celdas.empty()) return;
    V3 rel = v.p - cam;
    if (rel.x * rel.x + rel.y * rel.y + rel.z * rel.z > 700.0 * 700.0) return;
    if (!g_progVeh) g_progVeh = programa(VV_SRC, FV_SRC);
    V3 f, r, u; vehBase(v, f, r, u);
    glUseProgram(g_progVeh);
    auto U = [&](const char* n) { return glGetUniformLocation(g_progVeh, n); };
    glUniformMatrix4fv(U("uVP"), 1, GL_FALSE, vp);
    glUniform3f(U("uPos"), (float)rel.x, (float)rel.y, (float)rel.z);
    glUniform3f(U("uR"), (float)r.x, (float)r.y, (float)r.z);
    glUniform3f(U("uU"), (float)u.x, (float)u.y, (float)u.z);
    glUniform3f(U("uF"), (float)f.x, (float)f.y, (float)f.z);
    glUniform3f(U("uSun"), sol[0], sol[1], sol[2]);
    glUniform3f(U("uSky"), cielo[0], cielo[1], cielo[2]);
    glUniform1f(U("uSolF"), solF); glUniform1f(U("uSkyMul"), skyMul); glUniform1f(U("uHDR"), hdr);
    glUniform1f(U("uFogA"), fogA); glUniform1f(U("uFogB"), fogB);
    glUniform3f(U("uPiv"), 0, 0, 0); glUniform1f(U("uAng"), 0.0f);
    if (!vidrio) {
        if (v.cuerpo.n) { glBindVertexArray(v.cuerpo.vao); glDrawArrays(GL_TRIANGLES, 0, v.cuerpo.n); }
        for (auto& h : v.helices) if (h.malla.n) {
            glUniform3f(U("uPiv"), (float)h.pivote.x, (float)h.pivote.y, (float)h.pivote.z);
            glUniform1f(U("uAng"), (float)v.angHel);
            glBindVertexArray(h.malla.vao); glDrawArrays(GL_TRIANGLES, 0, h.malla.n);
        }
    } else if (v.vidrio.n) {
        glBindVertexArray(v.vidrio.vao); glDrawArrays(GL_TRIANGLES, 0, v.vidrio.n);
    }
}

static void vehDibujar(const float* vp, V3 cam, const float* sol, float solF, float skyMul, float hdr, const float* cielo, float fogA, float fogB, bool vidrio) {
    vehDibujarUno(g_veh, vp, cam, sol, solF, skyMul, hdr, cielo, fogA, fogB, vidrio);
}

// ---------------------------------------------------------------- guardado
static std::string archivoVeh() { return g_dir + "/vehiculo.txt"; }
static void vehGuardar() {
    if (!g_veh.activo) { std::remove(archivoVeh().c_str()); return; }
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    FILE* f = std::fopen(archivoVeh().c_str(), "w");
    if (!f) return;
    std::fprintf(f, "V1 %.4f %.4f %.4f %.5f %.5f %.5f %d\n", g_veh.p.x, g_veh.p.y, g_veh.p.z, g_veh.yaw, g_veh.pitch, g_veh.roll, (int)g_veh.celdas.size());
    for (auto& c : g_veh.celdas) std::fprintf(f, "%d %d %d %d\n", c.x, c.y, c.z, (int)c.t);
    std::fclose(f);
}
static void vehCargar() {
    FILE* f = std::fopen(archivoVeh().c_str(), "r");
    if (!f) return;
    char tag[8]; int n = 0; double x, y, z, yw, pt, rl;
    if (std::fscanf(f, "%7s %lf %lf %lf %lf %lf %lf %d", tag, &x, &y, &z, &yw, &pt, &rl, &n) != 8 || n <= 0) { std::fclose(f); return; }
    Veh& v = g_veh;
    v.celdas.clear();
    for (int i = 0; i < n; i++) {
        int a, b, c, t;
        if (std::fscanf(f, "%d %d %d %d", &a, &b, &c, &t) != 4) break;
        v.celdas.push_back({a, b, c, (uint8_t)t});
    }
    std::fclose(f);
    if (v.celdas.empty()) return;
    v.p = {x, y, z}; v.yaw = yw; v.pitch = pt; v.roll = rl; v.v = {0, 0, 0};
    v.activo = true; v.pilotando = false; v.gas = 0;
    vehReconstruir(v);
}

// ---------------------------------------------------------------- ensamblar / desarmar
static void vehEnsamblar(int sx, int sy, int sz) {
    Veh& v = g_veh;
    if (v.activo) { aviso("YA HAY UNA AVIONETA ENSAMBLADA (H LA DESARMA)"); return; }
    if (!esParteVeh(leerCubito(sx, sy, sz))) { aviso("MIRA UNA PIEZA DEL VEHICULO Y PULSA G"); return; }
    auto key = [](int x, int y, int z) { return ((uint64_t)(x + (1 << 20)) << 42) | ((uint64_t)(y + (1 << 20)) << 21) | (uint64_t)(z + (1 << 20)); };
    std::vector<std::array<int, 3>> cs;
    std::unordered_set<uint64_t> vis;
    cs.push_back({sx, sy, sz}); vis.insert(key(sx, sy, sz));
    static const int D6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (size_t i = 0; i < cs.size(); i++) {
        if (cs.size() > 80000) { aviso("DEMASIADAS PIEZAS (MAX 80000 CUBITOS)"); return; }
        auto c = cs[i];
        for (auto& d : D6) {
            int x = c[0] + d[0], y = c[1] + d[1], z = c[2] + d[2];
            if (vis.count(key(x, y, z))) continue;
            if (!esParteVeh(leerCubito(x, y, z))) continue;
            vis.insert(key(x, y, z)); cs.push_back({x, y, z});
        }
    }
    // bloque CONTROL (el mas cercano al que miras)
    int best = -1; double bd = 1e18;
    for (size_t i = 0; i < cs.size(); i++) if (leerCubito(cs[i][0], cs[i][1], cs[i][2]) == CONTROL) {
        double d = std::pow(cs[i][0] - sx, 2) + std::pow(cs[i][1] - sy, 2) + std::pow(cs[i][2] - sz, 2);
        if (d < bd) { bd = d; best = (int)i; }
    }
    if (best < 0) { aviso("FALTA EL BLOQUE CONTROL EN EL VEHICULO"); return; }
    int cx = cs[best][0], cy = cs[best][1], cz = cs[best][2];
    // direccion "adelante": del cuerpo hacia la helice (la helice va en la nariz)
    double hx = 0, hz = 0, bx = 0, bz = 0; int nh = 0, nb = 0;
    std::vector<uint8_t> tp(cs.size());
    for (size_t i = 0; i < cs.size(); i++) {
        tp[i] = leerCubito(cs[i][0], cs[i][1], cs[i][2]);
        if (tp[i] == HELICE) { hx += cs[i][0]; hz += cs[i][2]; nh++; } else { bx += cs[i][0]; bz += cs[i][2]; nb++; }
    }
    int fx = 0, fz = -1;
    double dx = 0, dz = 0;
    if (nh && nb) { dx = hx / nh - bx / nb; dz = hz / nh - bz / nb; }
    if (std::fabs(dx) < 0.5 && std::fabs(dz) < 0.5) { dx = std::sin(g_j.yaw); dz = -std::cos(g_j.yaw); }
    if (std::fabs(dx) >= std::fabs(dz)) { fx = dx > 0 ? 1 : -1; fz = 0; } else { fx = 0; fz = dz > 0 ? 1 : -1; }
    int rx = -fz, rz = fx;                       // derecha = (-fz, 0, fx)
    v.celdas.clear();
    for (size_t i = 0; i < cs.size(); i++) {
        int ddx = cs[i][0] - cx, ddy = cs[i][1] - cy, ddz = cs[i][2] - cz;
        v.celdas.push_back({ddx * rx + ddz * rz, ddy, ddx * fx + ddz * fz, tp[i]});
    }
    // sacarlas del mundo
    std::vector<ChunkP> su;
    int ex0 = 1 << 30, ex1 = -(1 << 30), ey0 = 1 << 30, ey1 = -(1 << 30), ez0 = 1 << 30, ez1 = -(1 << 30);
    for (auto& c : cs) {
        escribirCubito(c[0], c[1], c[2], AIRE, su);
        ex0 = std::min(ex0, c[0]); ex1 = std::max(ex1, c[0]); ey0 = std::min(ey0, c[1]); ey1 = std::max(ey1, c[1]); ez0 = std::min(ez0, c[2]); ez1 = std::max(ez1, c[2]);
    }
    for (auto& c : su) remallarYa(c);
    marcarLuzSucia(ex0 - 1, ex1 + 1, ey0 - 1, ey1 + 1, ez0 - 1, ez1 + 1, su);
    v.p = {(cx + 0.5) * VS, (cy + 0.5) * VS, (cz + 0.5) * VS};
    v.yaw = std::atan2((double)fx, (double)-fz); v.pitch = 0; v.roll = 0; v.v = {0, 0, 0}; v.gas = 0;
    v.activo = true; v.pilotando = false; v.suelo = true;
    vehReconstruir(v);
    char b[160];
    std::snprintf(b, sizeof b, "ENSAMBLADA: LARGO %d ALAS %d BLOQUES%s - G PARA SUBIR", (int)std::lround(v.largo / (double)SUB), (int)std::lround(v.enverg / (double)SUB),
                  v.sust > 0 ? "" : " (ALAS CORTAS: NO VUELA)");
    aviso(b);
    vehGuardar();
    if (g_vehHookSet) g_vehHookSet();
}

static void vehDesarmar() {
    Veh& v = g_veh;
    if (!v.activo || v.pilotando) return;
    if (v.pilotoRed >= 0) { aviso("OTRO JUGADOR LA ESTA PILOTANDO"); return; }
    double yw = v.yaw;
    int k = (int)std::lround(yw / (3.14159265358979 / 2));
    int fx = 0, fz = -1;
    switch (((k % 4) + 4) % 4) { case 0: fx = 0; fz = -1; break; case 1: fx = 1; fz = 0; break; case 2: fx = 0; fz = 1; break; default: fx = -1; fz = 0; }
    int rx = -fz, rz = fx;
    int cx = (int)std::floor(v.p.x * SUB), cy = (int)std::floor(v.p.y * SUB), cz = (int)std::floor(v.p.z * SUB);
    std::vector<ChunkP> su;
    int perdidas = 0;
    int ex0 = 1 << 30, ex1 = -(1 << 30), ey0 = 1 << 30, ey1 = -(1 << 30), ez0 = 1 << 30, ez1 = -(1 << 30);
    for (auto& c : v.celdas) {
        int x = cx + c.x * rx + c.z * fx, y = cy + c.y, z = cz + c.x * rz + c.z * fz;
        uint8_t a = leerCubito(x, y, z);
        if (a != AIRE && !esAgua(a)) { perdidas++; continue; }
        escribirCubito(x, y, z, c.t, su);
        ex0 = std::min(ex0, x); ex1 = std::max(ex1, x); ey0 = std::min(ey0, y); ey1 = std::max(ey1, y); ez0 = std::min(ez0, z); ez1 = std::max(ez1, z);
    }
    for (auto& c : su) remallarYa(c);
    if (ex1 >= ex0) marcarLuzSucia(ex0 - 1, ex1 + 1, ey0 - 1, ey1 + 1, ez0 - 1, ez1 + 1, su);
    v.cuerpo.liberar(); v.vidrio.liberar();
    for (auto& h : v.helices) h.malla.liberar();
    v.helices.clear(); v.celdas.clear(); v.pts.clear();
    v.activo = false;
    char b[96];
    if (perdidas) std::snprintf(b, sizeof b, "DESARMADA (%d CUBITOS NO CUPIERON)", perdidas); else std::snprintf(b, sizeof b, "DESARMADA");
    aviso(b);
    vehGuardar();
    v.objValido = false; v.pilotoRed = -1;
    if (g_vehHookClear) g_vehHookClear();
}

// ---------------------------------------------------------------- subir / bajar
static void vehSubirBajar() {
    Veh& v = g_veh;
    if (v.pilotando) {
        // te quedas parado encima del asiento; la avioneta sigue sola (con el gas y el rumbo que llevaba)
        V3 f, r, u; vehBase(v, f, r, u);
        V3 np = v.p + (r * v.ojo.x + u * (v.ojo.y - 0.75 + 0.05) + f * v.ojo.z);
        g_j.p = np; g_j.v = {0, 0, 0}; g_j.volar = false; g_j.inercia = {0, 0, 0};
        g_j.yaw = v.yaw + v.hy; g_j.pitch = 0;
        v.pilotando = false;
        vehEntrar();
        aviso("BAJASTE: LA AVIONETA SIGUE SOLA (G CERCA = SUBIR, H = DESARMAR)");
        return;
    }
    if (!v.activo) return;
    if (v.pilotoRed >= 0) { aviso("OTRO JUGADOR LA ESTA PILOTANDO"); return; }
    V3 d = {g_j.p.x - v.p.x, g_j.p.y - v.p.y, g_j.p.z - v.p.z};
    if (std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) > std::max(14.0, v.radio + 8.0)) { aviso("ACERCATE A LA AVIONETA"); return; }
    if (!v.nAsiento) { aviso("FALTA UN ASIENTO"); return; }
    if (!v.nTimon) { aviso("FALTA EL TIMON"); return; }
    g_sobreVeh = false;
    v.pilotando = true; v.hy = v.hp = 0; g_j.yaw = 0; g_j.pitch = 0; g_j.v = {0, 0, 0};
    if (!v.nHel) aviso("SIN HELICE: NO VUELA");
    else if (v.sust <= 0) aviso("ALAS MUY CORTAS: DEBEN MEDIR LA MITAD DEL LARGO");
    else aviso("SHIFT = ACELERAR - DESPEGA A UNOS 60 KM/H TIRANDO DE S");
}

// ---------------------------------------------------------------- jugador sobre el vehiculo
static V3 vehAlocal(V3 w) {      // mundo -> coordenadas del vehiculo (en bloques: derecha, arriba, adelante)
    V3 f, r, u; vehBase(g_veh, f, r, u);
    V3 d = w - g_veh.p;
    return {d.x * r.x + d.y * r.y + d.z * r.z, d.x * u.x + d.y * u.y + d.z * u.z, d.x * f.x + d.y * f.y + d.z * f.z};
}
static V3 vehAmundo(V3 l) {
    V3 f, r, u; vehBase(g_veh, f, r, u);
    return g_veh.p + r * l.x + u * l.y + f * l.z;
}
static bool chocaVeh(double px, double py, double pz) {
    const Veh& v = g_veh;
    if (!v.activo || v.grid.empty()) return false;
    V3 f, r, u; vehBase(v, f, r, u);
    static const double YS[6] = {0.02, 0.4, 0.8, 1.2, 1.6, 1.78};
    static const double OS[3] = {-0.3, 0.0, 0.3};
    double dx0 = px - v.p.x, dz0 = pz - v.p.z;
    if (std::fabs(dx0) > v.radio + 3 || std::fabs(dz0) > v.radio + 3 || std::fabs(py - v.p.y) > v.radio + 4) return false;
    for (double yy : YS) for (double ox : OS) for (double oz : OS) {
        V3 d = {dx0 + ox, py + yy - v.p.y, dz0 + oz};
        int ix = (int)std::floor((d.x * r.x + d.y * r.y + d.z * r.z) * SUB + 0.5);
        int iy = (int)std::floor((d.x * u.x + d.y * u.y + d.z * u.z) * SUB + 0.5);
        int iz = (int)std::floor((d.x * f.x + d.y * f.y + d.z * f.z) * SUB + 0.5);
        if (ix < v.gx0 || ix >= v.gx0 + v.gnx || iy < v.gy0 || iy >= v.gy0 + v.gny || iz < v.gz0 || iz >= v.gz0 + v.gnz) continue;
        if (v.grid[((size_t)(iy - v.gy0) * v.gnz + (iz - v.gz0)) * v.gnx + (ix - v.gx0)]) return true;
    }
    return false;
}

// camina en coordenadas locales del vehiculo: el piso inclinado no te empuja; si se inclina mas de 50 grados o sales de las piezas, caes
static bool chocaLoc(const V3& L) {
    const Veh& v = g_veh;
    int x0 = (int)std::floor((L.x - 0.3) * SUB + 0.5), x1 = (int)std::floor((L.x + 0.3) * SUB + 0.5 - 1e-6);
    int y0 = (int)std::floor(L.y * SUB + 0.5), y1 = (int)std::floor((L.y + 1.8) * SUB + 0.5 - 1e-6);
    int z0 = (int)std::floor((L.z - 0.3) * SUB + 0.5), z1 = (int)std::floor((L.z + 0.3) * SUB + 0.5 - 1e-6);
    for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        if (x < v.gx0 || x >= v.gx0 + v.gnx || y < v.gy0 || y >= v.gy0 + v.gny || z < v.gz0 || z >= v.gz0 + v.gnz) continue;
        if (v.grid[((size_t)(y - v.gy0) * v.gnz + (z - v.gz0)) * v.gnx + (x - v.gx0)]) return true;
    }
    return false;
}
static void vehEntrar() {
    g_L = vehAlocal(g_j.p);
    V3 f, r, u; vehBase(g_veh, f, r, u);
    V3 dv = g_j.v - g_veh.v;      // velocidad relativa -> local
    g_vL = {dv.x * r.x + dv.y * r.y + dv.z * r.z, 0, dv.x * f.x + dv.y * f.y + dv.z * f.z};
    g_vL.y = std::min(0.0, dv.x * u.x + dv.y * u.y + dv.z * u.z);
    g_aireVeh = 0; g_saltoVeh = false;
    g_sobreVeh = true;
    g_j.inercia = {0, 0, 0};
}
static void vehSalir() {
    V3 f, r, u; vehBase(g_veh, f, r, u);
    V3 w = g_veh.v + r * g_vL.x + u * g_vL.y + f * g_vL.z;
    g_j.p = vehAmundo(g_L);
    g_j.v = {0, w.y, 0};
    g_j.inercia = {w.x, 0, w.z};
    g_j.suelo = false;
    g_sobreVeh = false;
}
static void vehCaminar(V3 mov, bool salto, bool correr, double dt) {
    V3 f, r, u; vehBase(g_veh, f, r, u);
    if (u.y < 0.64) { vehSalir(); return; }                       // inclinada mas de 50 grados: ya no vas pegado
    V3 ml = {mov.x * r.x + mov.y * r.y + mov.z * r.z, 0, mov.x * f.x + mov.y * f.y + mov.z * f.z};
    double nl = std::sqrt(ml.x * ml.x + ml.z * ml.z);
    if (nl > 1e-6) ml = ml * (1.0 / nl);
    double vel = correr ? 7.0 : 4.3;
    g_vL.x = ml.x * vel; g_vL.z = ml.z * vel;
    g_vL.y -= 28.0 * dt;
    if (g_vL.y < -50.0) g_vL.y = -50.0;
    bool suelo = false;
    if (salto && g_aireVeh < 0.05) { g_vL.y = 9.0; g_saltoVeh = true; }
    double* P[3] = {&g_L.x, &g_L.y, &g_L.z};
    double* Vv[3] = {&g_vL.x, &g_vL.y, &g_vL.z};
    bool pisaba = g_aireVeh < 0.05;
    for (int eje = 0; eje < 3; eje++) {
        int e = eje == 0 ? 0 : (eje == 1 ? 2 : 1);               // x, z, y
        double delta = *Vv[e] * dt;
        while (std::fabs(delta) > 1e-9) {
            double paso = std::max(-0.1, std::min(0.1, delta));
            delta -= paso;
            *P[e] += paso;
            if (!chocaLoc(g_L)) continue;
            if (e != 1 && pisaba) {
                bool subio = false;
                for (double sx = VS; sx <= 0.62; sx += VS) { V3 t = g_L; t.y += sx; if (!chocaLoc(t)) { g_L.y += sx; subio = true; break; } }
                if (subio) continue;
            }
            *P[e] -= paso;
            if (e == 1) { if (g_vL.y < 0) suelo = true; g_vL.y = 0; }
            else *Vv[e] = 0;
            break;
        }
    }
    if (suelo) { g_aireVeh = 0; g_saltoVeh = false; }
    else g_aireVeh += dt;
    V3 lim = g_L;
    bool lejos = std::fabs(lim.x) > g_veh.radio + 6 || std::fabs(lim.y) > g_veh.radio + 6 || std::fabs(lim.z) > g_veh.radio + 6;
    if (g_aireVeh > (g_saltoVeh ? 0.7 : 0.15) || lejos) { vehSalir(); return; }
    g_j.p = vehAmundo(g_L);
    g_j.v = {0, 0, 0};
    g_j.suelo = suelo;
}

// ---------------------------------------------------------------- fisica
static bool solidoEn(int x, int y, int z) {
    bool c = true;
    uint8_t t = leerCubito(x, y, z, &c);
    return c && esSolido(t);
}

// red: el que no la simula (no es el piloto ni el anfitrion libre) solo sigue la pose que llega
static void vehSeguir(double dt) {
    Veh& v = g_veh;
    if (!v.objValido) return;
    double k = std::min(1.0, dt * 14.0);
    V3 d = v.objP - v.p;
    if (d.x * d.x + d.y * d.y + d.z * d.z > 100.0) v.p = v.objP; else v.p = v.p + d * k;
    auto ang = [&](double a, double b) { double e = b - a; while (e > 3.14159265) e -= 6.2831853; while (e < -3.14159265) e += 6.2831853; return a + e * k; };
    v.yaw = ang(v.yaw, v.objYaw); v.pitch += (v.objPitch - v.pitch) * k; v.roll += (v.objRoll - v.roll) * k;
    v.v = v.objV; v.gas = v.objGas; v.suelo = v.objSuelo;
    v.angHel += (3.0 + v.gas * 90.0) * dt;
    if (v.angHel > 6.2831853 * 50) v.angHel -= 6.2831853 * 50;
}
static bool g_vehCinematico = false;     // la pone el juego: true si en esta maquina la avioneta solo sigue a la red
static void vehPaso(GLFWwindow* w, double dt) {
    Veh& v = g_veh;
    if (g_vehCinematico && !v.pilotando) { vehSeguir(dt); return; }
    auto K = [&](int k) { return glfwGetKey(w, k) == GLFW_PRESS; };
    const double G = 11.0, VR = 13.0, KD = 0.0045, EMPUJE = 7.0;
    const bool piloto = v.pilotando;
    if (!piloto) {      // sin piloto: si el terreno de abajo no esta cargado se queda quieto
        bool conocido = true;
        leerCubito((int)std::floor(v.p.x * SUB), (int)std::floor((v.p.y - 1.0) * SUB), (int)std::floor(v.p.z * SUB), &conocido);
        if (!conocido) return;
    }
    double pitchIn = piloto ? (K(GLFW_KEY_S) ? 1 : 0) - (K(GLFW_KEY_W) ? 1 : 0) : 0;
    double rollIn = piloto ? (K(GLFW_KEY_D) ? 1 : 0) - (K(GLFW_KEY_A) ? 1 : 0) : 0;
    double yawIn = piloto ? (K(GLFW_KEY_E) ? 1 : 0) - (K(GLFW_KEY_Q) ? 1 : 0) : 0;
    bool freno = piloto ? K(GLFW_KEY_SPACE) : true;
    if (piloto) {
        if (K(GLFW_KEY_LEFT_SHIFT)) v.gas = std::min(1.0, v.gas + 0.6 * dt);
        if (K(GLFW_KEY_LEFT_CONTROL)) v.gas = std::max(0.0, v.gas - 0.6 * dt);
        if (K(GLFW_KEY_X)) v.gas = 0;
    } else v.gas = std::max(0.0, v.gas - 0.012 * dt);      // sin piloto el gas baja poco a poco y la avioneta termina cayendo
    v.angHel += (3.0 + v.gas * 90.0) * dt;
    if (v.angHel > 6.2831853 * 50) v.angHel -= 6.2831853 * 50;
    const double propF = v.nHel ? std::min(1.6, 0.7 + 0.3 * v.nHel) : 0.0;
    const int pasos = std::max(1, (int)std::ceil(dt / 0.02));
    const double h = dt / pasos;
    for (int s = 0; s < pasos; s++) {
        V3 f, r, u; vehBase(v, f, r, u);
        double vf = v.v.x * f.x + v.v.y * f.y + v.v.z * f.z;
        double vr = v.v.x * r.x + v.v.y * r.y + v.v.z * r.z;
        double vu = v.v.x * u.x + v.v.y * u.y + v.v.z * u.z;
        double vel = std::sqrt(v.v.x * v.v.x + v.v.y * v.v.y + v.v.z * v.v.z);
        double auth = std::max(0.2, std::min(1.2, vel / VR));
        // sustentacion
        double alpha = std::atan2(-vu, std::max(vf, 0.1));
        double cl = 0.40 + 4.5 * alpha;
        if (alpha > 0.33) cl = std::max(0.2, 1.5 - (alpha - 0.33) * 6.0);
        cl = std::max(-0.6, std::min(1.5, cl));
        double lift = vf > 0 ? G * v.sust * (vf * vf) / (VR * VR) * cl / 0.8 : 0.0;
        V3 a = f * (v.gas * propF * EMPUJE) + u * lift + V3{0, -G, 0};
        a = a - v.v * (KD * vel);
        a = a - r * (vr * 1.2) - u * (vu * std::fabs(vu) * 0.04);
        if (v.suelo) {
            a = a - f * (vf * (freno ? 3.0 : 0.25));
            a = a - r * (vr * 12.0);
        }
        v.v = v.v + a * h;
        // actitud
        double velH = std::sqrt(v.v.x * v.v.x + v.v.z * v.v.z);
        if (v.suelo) {
            double yr = (rollIn + yawIn) * 0.9 * std::max(-1.0, std::min(1.0, vf / 5.0));
            v.yaw += yr * h;
            v.roll -= v.roll * std::min(1.0, 6.0 * h);
            bool rota = pitchIn > 0 && vf > 9.0;
            if (rota) v.pitch += pitchIn * 0.9 * h;
            else v.pitch += (0.0 - v.pitch) * std::min(1.0, 2.0 * h);
            v.pitch = std::max(-0.1, std::min(0.6, v.pitch));
        } else {
            v.pitch += pitchIn * 1.0 * auth * h;
            double gamma = vel > 1.0 ? std::asin(std::max(-1.0, std::min(1.0, v.v.y / vel))) : 0.0;
            if (pitchIn == 0) v.pitch += (gamma - v.pitch) * 0.5 * std::min(1.5, vf / VR) * h;
            v.pitch = std::max(-1.3, std::min(1.3, v.pitch));
            v.roll += rollIn * 1.5 * auth * h;
            if (rollIn == 0) v.roll -= v.roll * 0.5 * h;
            v.roll = std::max(-1.2, std::min(1.2, v.roll));
            double yr = G * std::tan(v.roll) / std::max(velH, 8.0) + yawIn * 0.45 * auth;
            v.yaw += yr * h;
        }
        // mover
        V3 antes = v.p;
        v.p = v.p + v.v * h;
        // contacto con el terreno
        V3 f2, r2, u2; vehBase(v, f2, r2, u2);
        double prof = 0; bool pared = false, rueda = false;
        for (auto& q : v.pts) {
            V3 P = v.p + (r2 * q.x + u2 * q.y + f2 * q.z) * VS;
            double qy = P.y - VS * 0.5 + 1e-4;
            int cx = (int)std::floor(P.x * SUB), cy = (int)std::floor(qy * SUB), cz = (int)std::floor(P.z * SUB);
            if (!solidoEn(cx, cy, cz)) continue;
            int k = 0;
            while (k < 6 && solidoEn(cx, cy + 1 + k, cz)) k++;
            if (k >= 6) { pared = true; continue; }
            double d = (cy + 1 + k) * VS - qy;
            if (d > prof) { prof = d; rueda = q.rueda; }
        }
        bool crash = false;
        double impacto = -v.v.y;
        v.suelo = false;
        if (pared) {
            double hs = std::sqrt(v.v.x * v.v.x + v.v.z * v.v.z);
            v.p.x = antes.x; v.p.z = antes.z;
            if (hs > 14.0) crash = true;
            v.v.x = v.v.z = 0;
        }
        if (prof > 0) {
            v.p.y += std::min(prof, 0.4);
            if (v.v.y < 0) {
                if (impacto > (rueda ? 16.0 : 9.0)) crash = true;
                v.v.y = 0;
            }
            v.suelo = true;
        }
        if (crash) { v.v = {0, 0, 0}; v.gas = 0; aviso("CHOQUE!"); }
        if (v.p.y < 1.0) { v.p.y = 1.0; if (v.v.y < 0) v.v.y = 0; v.suelo = true; }
    }
    if (piloto) { g_j.p = v.p; g_j.v = {0, 0, 0}; }
}

// ---------------------------------------------------------------- camara
// devuelve mira (direccion), camara (posicion) y vector arriba
static void vehCamara(V3& mira, V3& cam, V3& arriba) {
    Veh& v = g_veh;
    V3 f, r, u; vehBase(v, f, r, u);
    v.hy = g_j.yaw; v.hp = std::max(-1.4, std::min(1.4, g_j.pitch));
    if (v.cabina) {
        cam = v.p + (r * v.ojo.x + u * v.ojo.y + f * v.ojo.z);
        double ch = std::cos(v.hp), sh = std::sin(v.hp), cy = std::cos(v.hy), sy = std::sin(v.hy);
        mira = norm(f * (ch * cy) + r * (ch * sy) + u * sh);
        arriba = u;
    } else {
        double a = v.yaw + v.hy, dist = v.radio * 0.6 + 4.0;
        double el = std::max(-0.2, std::min(1.4, 0.30 - v.hp));
        V3 fh = {std::sin(a), 0, -std::cos(a)};
        cam = v.p + fh * (-std::cos(el) * dist) + V3{0, std::sin(el) * dist + 0.8, 0};
        mira = norm((v.p + V3{0, 0.6, 0}) - cam);
        arriba = {0, 1, 0};
    }
}

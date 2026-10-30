// Cohete de dos etapas (v56/v57).
//  - cohGeom(): la forma del cohete en cubitos (pura, sin tocar el mundo). x = derecha, y = altura (0 = base), z = adelante (la puerta esta en z negativo).
//  - /cohete (chat) lo construye en el mundo con su torre de escaleras (ver construirCohete en main.cpp).
//  - L dentro de la cabina: cuenta atras, despegue de la 1a etapa, separacion y orbita con gravedad 0.
//    Al lanzar, las celdas del cohete salen del mundo y pasan a ser dos cuerpos rigidos (etapa 1 y etapa 2 con la cabina) que se dibujan con la
//    malla de los vehiculos (vehiculo.h).
// Este archivo se incluye desde main.cpp justo despues de vehiculo.h.
#pragma once
#include <cstdio>
#include <unordered_map>
#include <vector>

struct CCel { int x, y, z; uint8_t t; };
static const int CK_H = 110;        // alto total en cubitos
static const int CK_FLOOR = 77;     // piso de la cabina (altura en cubitos)
static const int CK_CEIL = 89;      // techo de la cabina
static const int CK_SPLIT = 74;     // desde aqui es la etapa 2
static const int CK_O1 = 37, CK_O2 = 92;   // origen local (altura) de cada etapa, cerca de su centro

static double cohR(int h) {         // radio del casco a cada altura
    if (h < 6) return 0.0;
    if (h < 70) return 4.5;
    if (h < 74) return 3.8;
    if (h < 94) return 4.0;
    double t = (h - 94) / 16.0;
    return 4.0 * std::pow(std::max(0.0, 1.0 - std::pow(t, 1.7)), 0.65);
}

static void cohGeom(std::vector<CCel>& out) {
    std::unordered_map<int, uint8_t> m;
    auto K = [](int x, int y, int z) { return (y * 64 + (z + 32)) * 64 + (x + 32); };
    auto put = [&](int x, int y, int z, uint8_t t) { m[K(x, y, z)] = t; };
    const double GROSOR = 1.35;
    static const int EN[5][2] = {{0, 0}, {3, 0}, {-3, 0}, {0, 3}, {0, -3}};
    for (int dz = -11; dz <= 11; dz++) for (int dx = -11; dx <= 11; dx++) {
        const double r = std::sqrt((double)(dx * dx + dz * dz));
        // toberas
        for (int h = 0; h < 6; h++) {
            bool dentro = false;
            for (auto& e : EN) {
                double rr = std::sqrt((double)((dx - e[0]) * (dx - e[0]) + (dz - e[1]) * (dz - e[1])));
                if (rr <= 1.2 + (5 - h) * 0.18) dentro = true;
            }
            if (dentro) put(dx, h, dz, h >= 4 ? PIEDRA : TITANIO);
        }
        // aletas (en las diagonales)
        if (std::abs(std::abs(dx) - std::abs(dz)) <= 1 && r > 4.0 && r <= 11.0) {
            int hmin = (int)std::floor((r - 4.5) * 0.8), hmax = (int)std::floor(34.0 - (r - 4.5) * 3.4);
            for (int h = std::max(0, hmin); h <= hmax; h++) put(dx, h, dz, PIEDRA);
        }
        // casco
        for (int h = 6; h < CK_H; h++) {
            const double Rh = cohR(h);
            if (Rh <= 0 || r > Rh) continue;
            const bool banda = Rh < 2.0 || r > Rh - GROSOR || h <= 7;
            if (!banda) {
                if (h == CK_FLOOR) put(dx, h, dz, ARCILLA);
                else if (h == CK_CEIL) put(dx, h, dz, PIEDRA);
                continue;
            }
            // puerta (hueco) hacia z negativo
            if (h >= CK_FLOOR + 1 && h <= CK_FLOOR + 7 && dz < 0 && std::abs(dx) <= 1 && h > 7) continue;
            uint8_t t = NIEVE;
            if (h < 10) t = CARBON;
            else if (h >= 14 && h <= 15) t = PIEDRA;
            else if (h >= 40 && h <= 41) t = PIEDRA;
            else if (h >= 52 && h <= 56) t = COBRE;
            else if (h >= 66 && h <= 73) t = CARBON;
            else if (h >= 76 && h <= 77) t = COBRE;
            else if (h >= 108) t = CARBON;
            else if (h >= 104) t = PIEDRA;
            // ventanas de la cabina: atras y a los lados
            if (h >= CK_FLOOR + 4 && h <= CK_FLOOR + 7) {
                if (dz > 0 && std::abs(dx) <= 1) t = VIDRIO;
                if (std::abs(dz) <= 1 && std::abs(dx) >= 2) t = VIDRIO;
            }
            put(dx, h, dz, t);
        }
    }
    // luces del techo y panel de mandos (atras)
    static const int L[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
    for (auto& l : L) put(l[0], CK_CEIL - 1, l[1], LED_BLANCO);
    static const uint8_t LC[3] = {LED_VERDE, LED_CELESTE, LED_NARANJA};
    for (int x = -1; x <= 1; x++) { put(x, CK_FLOOR + 1, 2, PIEDRA); put(x, CK_FLOOR + 2, 2, LC[x + 1]); }
    out.clear();
    out.reserve(m.size());
    for (auto& kv : m) {
        int k = kv.first;
        int x = k % 64 - 32; k /= 64;
        int z = k % 64 - 32; k /= 64;
        out.push_back({x, k, z, kv.second});
    }
}

// ---------------------------------------------------------------- donde esta el cohete construido
struct CohInfo { bool ok = false; int cxm = 0, czm = 0, gy = 0, ax = 0, az = 1; };   // (ax, az) = direccion de la puerta (hacia ti al construirlo)
static CohInfo g_cohInfo;
static std::string archivoCoh() { return g_dir + "/cohete.txt"; }
static void ckGuardarInfo() {
    if (!g_cohInfo.ok) { std::remove(archivoCoh().c_str()); return; }
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    FILE* f = std::fopen(archivoCoh().c_str(), "w");
    if (!f) return;
    std::fprintf(f, "C1 %d %d %d %d %d\n", g_cohInfo.cxm, g_cohInfo.czm, g_cohInfo.gy, g_cohInfo.ax, g_cohInfo.az);
    std::fclose(f);
}
static void ckCargarInfo() {
    g_cohInfo = CohInfo();
    FILE* f = std::fopen(archivoCoh().c_str(), "r");
    if (!f) return;
    char tag[8]; int a, b, c, d, e;
    if (std::fscanf(f, "%7s %d %d %d %d %d", tag, &a, &b, &c, &d, &e) == 6) g_cohInfo = {true, a, b, c, d, e};
    std::fclose(f);
}

// ---------------------------------------------------------------- el vuelo
struct Humo { V3 p, v; double t0, vida; };
struct Cohete {
    bool activo = false;
    int fase = 0;                    // 0 cuenta atras, 1 etapa 1 encendida, 2 orbita (gravedad 0)
    double t = 0, tv = 0, sp = 0, tSep = -1, y0 = 0, tReloj = 0;
    V3 P{0, 0, 0}, P0{0, 0, 0};      // origen local de la etapa 1 / punto del suelo bajo el cohete
    V3 V{0, 0, 0};                   // velocidad de la etapa 2 (la nave) en orbita
    double yaw = 0, pitch = 0;       // rumbo y cabeceo del despegue
    Veh e1, e2;                      // etapa 1 y etapa 2 (con la cabina)
    bool e1vivo = false;
    V3 e1V{0, 0, 0}, w1{0, 0, 0};    // la etapa 1 suelta: velocidad y giro
    V3 w{0, 0, 0};                   // giro que pide el piloto (alrededor de r, f, u) en rad/s
    bool cab = false;                // camara dentro de la cabina
    std::vector<Humo> humo;
    double sobra = 0;                // tiempo para generar humo
    VMalla flama, fx;
    int nHumo = 0;
    double fuel = 100.0;             // combustible de la etapa 2 (%)
    bool quema = false, llegada = false;
    bool desc = false, esperaLuna = false, enLuna = false;   // v59: descenso, esperando cargar la Luna, ya en el mundo lunar
    double tLleg = -1;
    std::vector<std::array<int, 3>> muestras;                 // celdas de la nave para detectar el choque
};
static Cohete g_ck;
static bool g_pedirLanzar = false;
static bool g_lunaPend = false;      // v59: pide cambiar al mundo de la Luna (lo hace el bucle principal)
static const double CK_LUNA_G = 1.62;          // gravedad lunar (bloques/s^2): ~1/6 de la Tierra
static double g_ckMult = 1.0;       // (pruebas) CK_MULT: acelera el tiempo del despegue

// ---- la Tierra y la Luna vistas desde el espacio (v58). La Tierra es una esfera de radio CK_RP con su centro bajo el suelo (y = -CK_RP).
// Normalmente su centro va justo debajo de la camara (el cielo la pinta asi); al encender el motor se fija en el sitio donde estabas,
// y desde ahi la Tierra se aleja y la Luna se acerca de verdad.
static const double CK_RP = 6000.0;
static const double CK_LUNA_R = 3200.0;        // radio de la Luna (bloques)
static const double CK_LUNA_D = 52000.0;       // distancia entre los centros de la Tierra y la Luna
static bool g_terraFija = false;
static double g_terraX = 0, g_terraZ = 0;
static V3 ckLunaE() { return norm(V3{0.58, 0.55, 0.60}) * CK_LUNA_D; }      // centro de la Luna respecto al centro de la Tierra
static void ckTierraXZ(const V3& cam, double& ex, double& ez) { if (g_terraFija || g_modoLuna) { ex = g_terraX; ez = g_terraZ; } else { ex = cam.x; ez = cam.z; } }
static V3 ckLunaRel(const V3& cam) {           // centro de la Luna respecto a una posicion
    double ex, ez; ckTierraXZ(cam, ex, ez);
    V3 m = ckLunaE();
    return {ex + m.x - cam.x, -CK_RP + m.y - cam.y, ez + m.z - cam.z};
}
static V3 g_solLuna = norm(V3{0.809, 0.514, 0.288});            // v60: el sol visto desde el suelo lunar (se calcula al descender)
static V3 g_tierraM = {-0.771 * 48500.0, 0.574 * 48500.0, -0.274 * 48500.0};   // centro de la Tierra respecto a la nave, en el mundo lunar
// el sol en el espacio es fijo: la Luna se ve a medias (fase) desde la Tierra, asi que un lado esta de dia y el otro de noche
static V3 g_solHora = {0, 1, 0};                 // el sol del cielo segun la hora del juego (lo actualiza el bucle principal)
static V3 ckSolEsp() { return norm(g_solHora); }      // la Luna se ilumina con el MISMO sol del juego: segun la hora, su lado visible puede estar oscuro
static double ckLuzSuelo(const V3& pos) {       // 1 = sol vertical sobre ese punto de la Luna, 0 = amanecer, negativo = noche
    V3 mr = ckLunaRel(pos); double d = std::sqrt(mr.x * mr.x + mr.y * mr.y + mr.z * mr.z);
    V3 up = mr * (-1.0 / std::max(1.0, d)); V3 S = ckSolEsp();
    return up.x * S.x + up.y * S.y + up.z * S.z;
}
struct CkMarca { bool ok = false; float lx = 0, ly = 0, px = 0, py = 0, sx = 0, sy = 0; bool lDentro = false, pDentro = false, sDentro = false; double ang = 0, dist = 0; };
static CkMarca g_ckM;                          // marcadores de la pantalla (Luna y proa de la nave)

static uint32_t ckH(uint32_t a) { a ^= a >> 16; a *= 0x7feb352dU; a ^= a >> 15; a *= 0x846ca68bU; a ^= a >> 16; return a; }
static double ckR(uint32_t a) { return (ckH(a) & 0xffffff) / 16777215.0; }

static void ckCaja(std::vector<float>& o, float cx, float cy, float cz, float hx, float hy, float hz, float r, float g, float b, float a) {
    static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const float SH[6] = {0.86f, 0.80f, 1.0f, 0.62f, 0.92f, 0.74f};
    for (int k = 0; k < 6; k++) {
        const int ax = k / 2, u = (ax + 1) % 3, w = (ax + 2) % 3;
        const float s = (k & 1) ? -1.0f : 1.0f;
        const float h[3] = {hx, hy, hz};
        float c[4][3];
        for (int j = 0; j < 4; j++) {
            c[j][ax] = s * h[ax];
            c[j][u] = ((j == 1 || j == 2) ? 1.0f : -1.0f) * h[u];
            c[j][w] = ((j >= 2) ? 1.0f : -1.0f) * h[w];
        }
        static const int id[6] = {0, 1, 2, 0, 2, 3};
        for (int q = 0; q < 6; q++) {
            const float* cc = c[id[q]];
            o.push_back(cx + cc[0]); o.push_back(cy + cc[1]); o.push_back(cz + cc[2]);
            o.push_back((float)N[k][0]); o.push_back((float)N[k][1]); o.push_back((float)N[k][2]);
            o.push_back(r * SH[k]); o.push_back(g * SH[k]); o.push_back(b * SH[k]); o.push_back(a);
        }
    }
}

static V3 ckRot(V3 v, V3 k, double a) {      // gira v alrededor del eje k (unitario) un angulo a
    double c = std::cos(a), s = std::sin(a), d = v.x * k.x + v.y * k.y + v.z * k.z;
    V3 kv = cruz(k, v);
    return {v.x * c + kv.x * s + k.x * d * (1 - c), v.y * c + kv.y * s + k.y * d * (1 - c), v.z * c + kv.z * s + k.z * d * (1 - c)};
}
static void ckOrto(Veh& v) {               // deja f, r, u perpendiculares y unitarios (u = r x f)
    V3 u = norm(v.bu);
    V3 f = v.bf - u * (v.bf.x * u.x + v.bf.y * u.y + v.bf.z * u.z);
    f = norm(f);
    v.bu = u; v.bf = f; v.br = norm(cruz(f, u));
}
static void ckLibre(Veh& v) {              // pasa a actitud libre conservando la actual
    V3 f, r, u; vehBase(v, f, r, u);
    v.bf = f; v.br = r; v.bu = u; v.libre = true;
}

static void ckLiberar() {
    Cohete& k = g_ck;
    for (Veh* v : {&k.e1, &k.e2}) {
        v->cuerpo.liberar(); v->vidrio.liberar();
        v->celdas.clear(); v->pts.clear(); v->grid.clear(); v->activo = false; v->libre = false;
    }
    k.flama.liberar(); k.fx.liberar();
    k.humo.clear();
    k.activo = false; k.e1vivo = false; k.quema = false; g_terraFija = false; g_ckM.ok = false;
}

// mundo <-> celdas del cohete construido
static void ckMapa(const CohInfo& I, int& fx, int& fz, int& rx, int& rz) { fx = -I.ax; fz = -I.az; rx = -fz; rz = fx; }

static bool ckLanzar() {
    Cohete& k = g_ck;
    if (k.activo) return false;
    const CohInfo I = g_cohInfo;
    if (!I.ok) { aviso("NO HAY COHETE: ESCRIBE /COHETE EN EL CHAT"); return false; }
    const double cx = (I.cxm + 0.5) * VS, cz = (I.czm + 0.5) * VS, cy = (I.gy + 1 + CK_FLOOR + 1) * VS;
    const double dx = g_j.p.x - cx, dz = g_j.p.z - cz;
    if (dx * dx + dz * dz > 3.2 * 3.2 || g_j.p.y < cy - 0.8 || g_j.p.y > cy + 3.0) { aviso("ENTRA A LA CABINA (SUBE POR LA TORRE) Y PULSA L PARA LANZAR"); return false; }
    std::vector<CCel> cel;
    cohGeom(cel);
    int fx, fz, rx, rz; ckMapa(I, fx, fz, rx, rz);
    size_t presentes = 0;
    for (auto& c : cel) {
        int x = I.cxm + c.x * rx + c.z * fx, z = I.czm + c.x * rz + c.z * fz, y = I.gy + 1 + c.y;
        bool cono = true;
        uint8_t a = leerCubito(x, y, z, &cono);
        if (!cono) { aviso("ESPERA UN MOMENTO: EL COHETE AUN NO CARGO"); return false; }
        if (a == c.t) presentes++;
    }
    if (presentes * 10 < cel.size() * 6) { aviso("EL COHETE ESTA ROTO O NO ESTA AQUI"); return false; }
    std::vector<ChunkP> su;
    const bool prev = g_redEnviar; g_redEnviar = false;
    int ex0 = 1 << 30, ex1 = -(1 << 30), ey0 = 1 << 30, ey1 = -(1 << 30), ez0 = 1 << 30, ez1 = -(1 << 30);
    k.e1.celdas.clear(); k.e2.celdas.clear();
    for (auto& c : cel) {
        int x = I.cxm + c.x * rx + c.z * fx, z = I.czm + c.x * rz + c.z * fz, y = I.gy + 1 + c.y;
        if (leerCubito(x, y, z) != c.t) continue;
        escribirCubito(x, y, z, AIRE, su);
        ex0 = std::min(ex0, x); ex1 = std::max(ex1, x); ey0 = std::min(ey0, y); ey1 = std::max(ey1, y); ez0 = std::min(ez0, z); ez1 = std::max(ez1, z);
        if (c.y < CK_SPLIT) k.e1.celdas.push_back({c.x, c.y - CK_O1, c.z, c.t});
        else k.e2.celdas.push_back({c.x, c.y - CK_O2, c.z, c.t});
    }
    g_redEnviar = prev;
    for (auto& c : su) remallarYa(c);
    marcarLuzSucia(ex0 - 1, ex1 + 1, ey0 - 1, ey1 + 1, ez0 - 1, ez1 + 1, su);
    k.yaw = std::atan2((double)fx, (double)-fz);
    k.pitch = 0;
    k.P0 = {cx, (I.gy + 1) * VS, cz};
    k.P = {cx, (I.gy + 1 + CK_O1 + 0.5) * VS, cz};
    k.y0 = k.P0.y;
    for (Veh* v : {&k.e1, &k.e2}) {
        v->libre = false; v->yaw = k.yaw; v->pitch = 0; v->roll = 0; v->v = {0, 0, 0}; v->activo = true; v->pilotando = false;
        v->p = k.P;
        vehReconstruir(*v);
    }
    k.e1vivo = true;
    k.fase = 0; k.t = 0; k.tv = 0; k.sp = 0; k.tSep = -1; k.V = {0, 0, 0}; k.w = {0, 0, 0}; k.cab = false; k.sobra = 0;
    k.fuel = 100.0; k.quema = false; k.llegada = false; k.desc = false; k.esperaLuna = false; k.enLuna = false; k.tLleg = -1; k.muestras.clear(); g_terraFija = false;
    k.humo.clear();
    k.activo = true;
    g_jResp[0] = g_j.p.x; g_jResp[1] = g_j.p.y; g_jResp[2] = g_j.p.z; g_jRespOk = true;      // si sales del juego en pleno viaje, vuelves aqui
    g_j.volar = true; g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0}; g_sobreVeh = false;
    g_j.yaw = k.yaw + 1.25; g_j.pitch = 0.18;      // la camara mira de lado (la torre no tapa el cohete)
    aviso("CUENTA ATRAS...");
    return true;
}

static void ckSalir() {       // vuelves a la plataforma; el cohete se pierde
    Cohete& k = g_ck;
    if (!k.activo) return;
    if (k.llegada || g_modoLuna) { aviso("NO PUEDES SALIR EN PLENO VUELO: ATERRIZA"); return; }
    const CohInfo I = g_cohInfo;
    ckLiberar();
    g_jRespOk = false;
    int fx, fz, rx, rz; ckMapa(I, fx, fz, rx, rz);
    // sobre la plataforma de la torre (u = 6, v = 2), a la altura de la puerta... no: al pie de la torre, en el suelo de la plataforma de lanzamiento
    const int u = 8, v = 0;
    const int x = I.cxm + I.ax * u - I.az * v, z = I.czm + I.az * u + I.ax * v;
    g_j.p = {(x + 0.5) * VS, (I.gy + 1) * VS + 0.1, (z + 0.5) * VS};
    g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0}; g_j.volar = false; g_j.pitch = 0; g_j.yaw = std::atan2((double)(-I.ax), (double)I.az) + 3.14159265;
    g_cohInfo.ok = false; ckGuardarInfo();
    aviso("VOLVISTE A LA TIERRA (EL COHETE SE PERDIO: /COHETE PARA OTRO)");
}

static void ckSeparar() {
    Cohete& k = g_ck;
    V3 f, r, u; vehBase(k.e1, f, r, u);
    k.V = u * k.sp;
    ckLibre(k.e1); ckLibre(k.e2);
    // la etapa 2 sale hacia adelante, la 1 se queda atras con un giro lento
    k.e1V = k.V - u * 4.0 + r * 0.6;
    k.V = k.V + u * 2.5;
    k.w1 = {0.12, 0.05, 0.33};
    k.fase = 2; k.tSep = 0; k.w = {0, 0, 0};
    aviso("SEPARACION DE LA ETAPA 1 - ORBITA: GRAVEDAD 0 (W/S A/D Q/E GIRAN LA NAVE)");
}

static void ckEmite(const V3& p, const V3& v, double t, double vida) {
    Cohete& k = g_ck;
    if (k.humo.size() >= 520) k.humo.erase(k.humo.begin(), k.humo.begin() + 40);
    k.humo.push_back({p, v, t, vida});
}


// ---------------------------------------------------------------- v59: aterrizaje en la Luna
static double ckDot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// el bucle principal ya cambio al mundo lunar: pasa la nave a ese mundo (arriba = +Y) conservando su actitud y su velocidad
static void ckLunaTeleport() {
    Cohete& k = g_ck; Veh& e = k.e2;
    V3 mr = ckLunaRel(e.p);
    const double dist = std::sqrt(ckDot(mr, mr));
    V3 up = mr * (-1.0 / std::max(1.0, dist));
    V3 ref = std::fabs(up.y) > 0.97 ? V3{1, 0, 0} : V3{0, 1, 0};
    V3 t1 = norm(cruz(ref, up)), t2 = cruz(t1, up);
    auto M = [&](V3 v) { return V3{ckDot(v, t1), ckDot(v, up), ckDot(v, t2)}; };
    g_tierraM = M(V3{g_terraX - e.p.x, -CK_RP - e.p.y, g_terraZ - e.p.z});
    g_solLuna = M(ckSolEsp());
    e.bf = M(e.bf); e.br = M(e.br); e.bu = M(e.bu); ckOrto(e);
    k.V = M(k.V);
    const double alt = std::max(40.0, std::min(95.0, dist - CK_LUNA_R));
    e.p = {0.5, alturaLuna(0.0, 0.0) + alt, 0.5};
    k.muestras.clear();
    const size_t N = e.celdas.size(), paso = std::max<size_t>(1, N / 2500);
    for (size_t i = 0; i < N; i += paso) k.muestras.push_back({e.celdas[i].x, e.celdas[i].y, e.celdas[i].z});
    k.enLuna = true; k.esperaLuna = false;
    g_j.p = e.p;
}

static bool ckChoque(Cohete& k) {
    Veh& e = k.e2; V3 f, r, u; vehBase(e, f, r, u);
    for (auto& m : k.muestras) {
        V3 w = e.p + r * ((m[0] + 0.5) * VS) + u * ((m[1] + 0.5) * VS) + f * ((m[2] + 0.5) * VS);
        bool kn = true;
        uint8_t t = leerCubito((int)std::floor(w.x * SUB), (int)std::floor(w.y * SUB), (int)std::floor(w.z * SUB), &kn);
        if (kn && esSolido(t)) return true;
    }
    return false;
}

// choque: la nave se queda tumbada en el suelo, convertida en bloques (como al desarmar la avioneta); unos pocos bloques salen disparados
static void ckAterrizaje() {
    Cohete& k = g_ck; Veh& e = k.e2;
    const double spd = std::sqrt(ckDot(k.V, k.V));
    V3 f, r, u; vehBase(e, f, r, u);
    // orientacion mas cercana alineada con los ejes (asi los cubitos encajan sin huecos)
    auto domin = [&](V3 v, int excl, int& sg) { double a[3] = {v.x, v.y, v.z}; int ax = -1; for (int i = 0; i < 3; i++) { if (i == excl) continue; if (ax < 0 || std::fabs(a[i]) > std::fabs(a[ax])) ax = i; } sg = a[ax] >= 0 ? 1 : -1; return ax; };
    int sU, sR; const int aU = domin(u, -1, sU), aR = domin(r, aU, sR);
    V3 Uv{0, 0, 0}, Rv{0, 0, 0};
    (&Uv.x)[aU] = sU; (&Rv.x)[aR] = sR;
    V3 Fv = cruz(Rv, Uv) * (ckDot(cruz(r, u), f) >= 0 ? 1.0 : -1.0);
    const int P0[3] = {(int)std::floor(e.p.x * SUB), (int)std::floor(e.p.y * SUB), (int)std::floor(e.p.z * SUB)};
    const int topY = HYC * CS - 1;
    std::unordered_map<uint64_t, int> gcache;
    auto gtop = [&](int X, int Z) {
        uint64_t key = ((uint64_t)(uint32_t)(X + (1 << 20)) << 32) | (uint32_t)(Z + (1 << 20));
        auto it = gcache.find(key); if (it != gcache.end()) return it->second;
        int y = topY; bool kn = true;
        for (; y > 0; y--) { uint8_t t = leerCubito(X, y, Z, &kn); if (!kn) { y = (int)std::floor(alturaLuna((X + 0.5) / SUB, (Z + 0.5) / SUB) * SUB); break; } if (esSolido(t)) break; }
        gcache[key] = y; return y;
    };
    struct P { int x, y, z; uint8_t t; };
    std::vector<P> cs; cs.reserve(e.celdas.size());
    int dy = -(1 << 20);
    for (auto& c : e.celdas) {
        int X = P0[0] + (int)std::lround(Rv.x * c.x + Uv.x * c.y + Fv.x * c.z), Y = P0[1] + (int)std::lround(Rv.y * c.x + Uv.y * c.y + Fv.y * c.z), Z = P0[2] + (int)std::lround(Rv.z * c.x + Uv.z * c.y + Fv.z * c.z);
        cs.push_back({X, Y, Z, (uint8_t)c.t});
        dy = std::max(dy, gtop(X, Z) + 1 - Y);
    }
    dy -= 2;                                              // se entierra un poco
    std::vector<ChunkP> su;
    const bool prev = g_redEnviar; g_redEnviar = false;
    int ex0 = 1 << 30, ex1 = -(1 << 30), ey0 = 1 << 30, ey1 = -(1 << 30), ez0 = 1 << 30, ez1 = -(1 << 30);
    std::unordered_map<uint64_t, int> pila;
    uint32_t sem = 12345u;
    for (size_t i = 0; i < cs.size(); i++) {
        P p = cs[i];
        sem = ckH(sem + (uint32_t)i * 2654435761u);
        if (spd > 6.0 && sem % 100 < 4) {                 // cascote suelto
            int dx = (int)(ckR(sem + 1) * 56.0) - 28, dz = (int)(ckR(sem + 2) * 56.0) - 28;
            p.x += dx; p.z += dz;
            uint64_t key = ((uint64_t)(uint32_t)(p.x + (1 << 20)) << 32) | (uint32_t)(p.z + (1 << 20));
            p.y = gtop(p.x, p.z) + 1 + pila[key]++;
        } else p.y += dy;
        escribirCubito(p.x, p.y, p.z, p.t, su);
        ex0 = std::min(ex0, p.x); ex1 = std::max(ex1, p.x); ey0 = std::min(ey0, p.y); ey1 = std::max(ey1, p.y); ez0 = std::min(ez0, p.z); ez1 = std::max(ez1, p.z);
    }
    g_redEnviar = prev;
    for (auto& c : su) remallarYa(c);
    marcarLuzSucia(ex0 - 1, ex1 + 1, ey0 - 1, ey1 + 1, ez0 - 1, ez1 + 1, su);
    // el jugador queda de pie junto a los restos, mirandolos
    int mx = 0, mz = 0; { long sx = 0, sz = 0; for (auto& p : cs) { sx += p.x; sz += p.z; } mx = (int)(sx / (long)cs.size()); mz = (int)(sz / (long)cs.size()); }
    const int medio = std::max(ex1 - ex0, ez1 - ez0) / 2;
    ckLiberar();
    const int px = mx + medio + 12 * SUB, pz = mz;
    g_j.p = {(px + 0.5) * VS, (gtop(px, pz) + 1) * VS + 0.05, (pz + 0.5) * VS};
    g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0}; g_j.volar = false; g_j.pitch = 0.05; g_j.yaw = -1.5708; g_sobreVeh = false;
    char b[160]; std::snprintf(b, sizeof b, "ATERRIZAJE DE EMERGENCIA! IMPACTO A %d KM/H - LA NAVE SE DESARMO EN BLOQUES", (int)(spd * 3.6));
    aviso(b);
}

// un paso de simulacion (dt <= 0.05)
static void ckPaso1(GLFWwindow* w, double dt) {
    Cohete& k = g_ck;
    auto Kd = [&](int c) { return glfwGetKey(w, c) == GLFW_PRESS; };
    k.tReloj += dt;
    uint32_t sem = (uint32_t)(k.tReloj * 1000.0);
    if (k.fase == 0) {
        k.t += dt;
        if (k.t > 2.5) {          // humo en la plataforma
            k.sobra += dt * 38.0;
            while (k.sobra >= 1.0) {
                k.sobra -= 1.0; sem = ckH(sem + 17);
                double a = ckR(sem) * 6.2831853, rr = 1.5 + ckR(sem + 3) * 4.0;
                V3 p = {k.P0.x + std::cos(a) * rr, k.P0.y + 0.6, k.P0.z + std::sin(a) * rr};
                ckEmite(p, V3{std::cos(a) * (3.0 + ckR(sem + 5) * 4.0), 0.4 + ckR(sem + 7) * 1.2, std::sin(a) * (3.0 + ckR(sem + 5) * 4.0)}, k.tReloj, 3.5 + ckR(sem + 9) * 2.0);
            }
        }
        if (k.t >= 5.0) { k.fase = 1; k.tv = 0; k.sp = 0; aviso("DESPEGUE!"); }
    }
    if (k.fase == 1) {
        k.tv += dt;
        k.sp += (3.0 + 0.2 * k.tv) * dt;
        const double d = k.P.y - k.y0;
        const double s = std::max(0.0, std::min(1.0, (d - 120.0) / 1280.0));
        k.pitch = -(s * s * (3.0 - 2.0 * s)) * 1.15;        // el cohete se inclina poco a poco (giro gravitatorio)
        V3 f, r, u;
        k.e1.yaw = k.yaw; k.e1.pitch = k.pitch; vehBase(k.e1, f, r, u);
        k.P = k.P + u * (k.sp * dt);
        // humo y estela
        if (d < 950.0) {
            k.sobra += dt * 30.0;
            while (k.sobra >= 1.0) {
                k.sobra -= 1.0; sem = ckH(sem + 29);
                double a = ckR(sem) * 6.2831853, rr = ckR(sem + 3) * 2.5;
                V3 base = k.P - u * ((CK_O1 + 3) * VS);
                V3 p = {base.x + std::cos(a) * rr, base.y, base.z + std::sin(a) * rr};
                V3 vel = {0, 0, 0};
                if (d < 25.0) { double sp2 = 4.0 + ckR(sem + 5) * 6.0; vel = {std::cos(a) * sp2, 0.5, std::sin(a) * sp2}; }
                else vel = u * (-k.sp * 0.15) + V3{std::cos(a) * 1.2, 0, std::sin(a) * 1.2};
                ckEmite(p, vel, k.tReloj, 4.0 + ckR(sem + 9) * 2.0);
            }
        }
        if (d >= 1500.0) ckSeparar();
    }
    if (k.fase == 2) {
        k.tSep += dt;
        // giro pedido por el piloto: W/S (cabeceo), A/D (guinada), Q/E (alabeo)
        const double VEL = 0.55;
        V3 pedido = {((Kd(GLFW_KEY_W) ? 1 : 0) - (Kd(GLFW_KEY_S) ? 1 : 0)) * VEL, ((Kd(GLFW_KEY_D) ? 1 : 0) - (Kd(GLFW_KEY_A) ? 1 : 0)) * VEL,
                     ((Kd(GLFW_KEY_E) ? 1 : 0) - (Kd(GLFW_KEY_Q) ? 1 : 0)) * VEL};
        if (const char* tg = std::getenv("CK_GIRO")) { double a_, b_, c_; if (std::sscanf(tg, "%lf,%lf,%lf", &a_, &b_, &c_) == 3) pedido = {a_ * VEL, b_ * VEL, c_ * VEL}; }   // (pruebas)
        double q = std::min(1.0, 3.0 * dt);
        k.w = k.w + (pedido - k.w) * q;
        Veh& e = k.e2;
        e.bf = ckRot(e.bf, e.br, -k.w.x * dt); e.bu = ckRot(e.bu, e.br, -k.w.x * dt);
        e.br = ckRot(e.br, e.bf, k.w.y * dt); e.bu = ckRot(e.bu, e.bf, k.w.y * dt);
        e.br = ckRot(e.br, e.bu, k.w.z * dt); e.bf = ckRot(e.bf, e.bu, k.w.z * dt);
        ckOrto(e);
        // motor de la etapa 2 (manten la tecla 2): empuja hacia la proa; la velocidad lateral se va corrigiendo (control facil)
        {
            const bool motor = Kd(GLFW_KEY_2) || (std::getenv("CK_MOTOR") && k.tSep > 3.0);
            if (std::getenv("CK_ALINEAR") && k.tSep > 2.0 && !g_terraFija) {     // (pruebas) apuntar solo a la Luna
                V3 dm = norm(ckLunaRel(e.p));
                V3 f0 = norm(V3{0, 1, 0} - dm * dm.y);
                e.bu = dm; e.bf = f0; e.br = cruz(f0, dm); ckOrto(e);
            }
            k.quema = motor && k.fuel > 0.0 && !k.esperaLuna;
            if (k.quema) {
                if (!g_terraFija) { g_terraFija = true; g_terraX = e.p.x; g_terraZ = e.p.z; }
                V3 u = e.bu;
                double vu = k.V.x * u.x + k.V.y * u.y + k.V.z * u.z;
                V3 perp = k.V - u * vu;
                k.V = u * (vu + 60.0 * dt) + perp * std::exp(-0.6 * dt);
                k.fuel = std::max(0.0, k.fuel - 1.5 * dt);
            }
            if (k.esperaLuna) { /* esperando a que cargue la Luna */ }
            else if (g_modoLuna) {                   // ---- ya en el mundo lunar: gravedad hacia abajo y choque con el suelo
                k.V.y -= CK_LUNA_G * dt;
            } else {
            if (g_terraFija && !k.llegada) {            // la Luna atrae un poco y al llegar la nave se frena
                V3 mr = ckLunaRel(e.p);
                double dist = std::sqrt(mr.x * mr.x + mr.y * mr.y + mr.z * mr.z);
                if (dist < 12.0 * CK_LUNA_R) {
                    double GM = 15.0 * 9.0 * CK_LUNA_R * CK_LUNA_R;
                    k.V = k.V + mr * (GM / (dist * dist * dist) * dt);
                }
                const double vel = std::sqrt(k.V.x * k.V.x + k.V.y * k.V.y + k.V.z * k.V.z);
                if (dist - CK_LUNA_R < 700.0 + vel * vel / 600.0) { k.llegada = true; aviso("LLEGASTE A LA LUNA! BUSCA SU LADO ILUMINADO (MARCA SOL), VUELA HACIA EL CON 2 Y PULSA 3 PARA DESCENDER"); }
            }
            if (k.llegada && !k.desc) {            // estabilizador: mantiene unos 300 bloques sobre la superficie; puedes volar de lado con 2
                V3 mr = ckLunaRel(e.p);
                double dist = std::sqrt(mr.x * mr.x + mr.y * mr.y + mr.z * mr.z);
                V3 dir = mr * (1.0 / std::max(1.0, dist));
                const double alt = dist - CK_LUNA_R;
                double rad = ckDot(k.V, dir);
                const double lim = std::sqrt(2.0 * 300.0 * std::max(0.0, alt - 300.0));
                const double obj = std::min(lim, std::max(-40.0, (alt - 300.0) * 0.4));
                double dv = std::max(-500.0 * dt, std::min(500.0 * dt, obj - rad));
                k.V = k.V + dir * dv;
                if (!k.quema) { V3 lat = k.V - dir * ckDot(k.V, dir); k.V = k.V - lat * (1.0 - std::exp(-0.8 * dt)); }
                const double luz = ckLuzSuelo(e.p), vv = std::sqrt(ckDot(k.V, k.V));
                if (std::getenv("CK_LADO") && luz < 0.55 && k.tReloj > 0) {    // (pruebas) vuela solo al lado iluminado
                    V3 S = ckSolEsp(); V3 up = dir * -1.0;
                    V3 tang = norm(S - up * ckDot(S, up));
                    e.bu = tang; e.bf = up; e.br = cruz(e.bf, e.bu); ckOrto(e);
                    if (k.fuel > 0) { V3 u = e.bu; double vu = ckDot(k.V, u); k.V = k.V + u * (60.0 * dt); (void)vu; k.fuel = std::max(0.0, k.fuel - 1.5 * dt); k.quema = true; }
                }
                if (Kd(GLFW_KEY_3) || (std::getenv("CK_DESC") && (luz >= 0.55 || k.fuel <= 0.5))) {
                    if (k.tLleg < 0) k.tLleg = k.tReloj;
                    const bool pideTecla = Kd(GLFW_KEY_3);
                    static double tAv = -10;
                    if (luz < 0.30 && k.fuel > 0.5) { if (k.tReloj - tAv > 2.5) { tAv = k.tReloj; aviso("ES EL LADO OSCURO: SIN LUZ NO SE PUEDE ATERRIZAR. VUELA AL LADO ILUMINADO (MARCA SOL)"); } }
                    else if (vv > 8.0 && pideTecla && k.fuel > 0.5) { if (k.tReloj - tAv > 2.5) { tAv = k.tReloj; aviso("FRENA ANTES DE DESCENDER (SUELTA 2 Y ESPERA)"); } }
                    else if (vv <= 8.0 && (pideTecla || k.tReloj - k.tLleg > 2.0)) { k.desc = true; aviso("DESCENDIENDO! GIRA LA NAVE (W/S A/D Q/E): EL ATERRIZAJE SERA DE EMERGENCIA"); }
                }
            } else if (k.llegada && k.desc) {         // descenso: la gravedad de la Luna te jala hacia su centro
                V3 mr = ckLunaRel(e.p);
                double dist = std::sqrt(mr.x * mr.x + mr.y * mr.y + mr.z * mr.z);
                V3 dir = mr * (1.0 / std::max(1.0, dist));
                k.V = k.V + dir * (CK_LUNA_G * dt);
                if (dist - CK_LUNA_R < 90.0 && !g_pedirLanzar && !g_lunaPend) { k.esperaLuna = true; g_lunaPend = true; k.V = k.V * 1.0; }
            }
            }
        }
        e.p = e.p + k.V * dt;
        if (k.enLuna && g_modoLuna && !k.esperaLuna && ckChoque(k)) { ckAterrizaje(); return; }
        if (k.e1vivo) {
            Veh& a = k.e1;
            a.bf = ckRot(a.bf, a.br, -k.w1.x * dt); a.bu = ckRot(a.bu, a.br, -k.w1.x * dt);
            a.br = ckRot(a.br, a.bf, k.w1.y * dt); a.bu = ckRot(a.bu, a.bf, k.w1.y * dt);
            a.br = ckRot(a.br, a.bu, k.w1.z * dt); a.bf = ckRot(a.bf, a.bu, k.w1.z * dt);
            ckOrto(a);
            a.p = a.p + k.e1V * dt;
            V3 dd = a.p - e.p;
            if (k.tSep > 40.0 || dd.x * dd.x + dd.y * dd.y + dd.z * dd.z > 900.0 * 900.0) { k.e1vivo = false; a.activo = false; a.cuerpo.liberar(); a.vidrio.liberar(); }
        }
    } else {
        V3 f, r, u;
        k.e1.libre = false; k.e2.libre = false;
        k.e1.yaw = k.e2.yaw = k.yaw; k.e1.pitch = k.e2.pitch = k.pitch;
        vehBase(k.e1, f, r, u);
        k.e1.p = k.P;
        k.e2.p = k.P + u * ((CK_O2 - CK_O1) * VS);
    }
    // humo: mover y envejecer
    for (size_t i = 0; i < k.humo.size();) {
        Humo& h = k.humo[i];
        if (k.tReloj - h.t0 > h.vida) { h = k.humo.back(); k.humo.pop_back(); continue; }
        h.p = h.p + h.v * dt;
        h.v = h.v * std::exp(-0.9 * dt);
        i++;
    }
}

static void ckPaso(GLFWwindow* w, double dt) {
    Cohete& k = g_ck;
    if (!k.activo) return;
    int n = std::max(1, (int)std::lround(g_ckMult));
    for (int i = 0; i < n && k.activo; i++) ckPaso1(w, dt);
    if (!k.activo) return;
    g_j.p = k.e2.p; g_j.v = {0, 0, 0}; g_j.inercia = {0, 0, 0};
}

static void ckCamara(V3& mira, V3& cam, V3& arriba) {
    Cohete& k = g_ck;
    V3 f, r, u; vehBase(k.e2, f, r, u);
    if (k.cab) {
        double hp = std::max(-1.4, std::min(1.4, g_j.pitch)), hy = g_j.yaw;
        cam = k.e2.p + u * ((CK_FLOOR + 3 - CK_O2) * VS);
        double ch = std::cos(hp), sh = std::sin(hp), cy = std::cos(hy), sy = std::sin(hy);
        mira = norm(f * (ch * cy) + r * (ch * sy) + u * sh);
        arriba = u;
    } else {
        V3 tgt = k.fase < 2 ? k.P + u * ((55 - CK_O1) * VS) : k.e2.p - u * 4.0;
        double d = k.fase < 2 ? k.P.y - k.y0 : 0;
        double dist = k.fase < 2 ? 50.0 + std::min(70.0, d * 0.05) : 42.0;
        mira = {std::cos(g_j.pitch) * std::sin(g_j.yaw), std::sin(g_j.pitch), -std::cos(g_j.pitch) * std::cos(g_j.yaw)};
        if (k.fase == 2 && std::getenv("CK_CAM_LUNA")) mira = norm(ckLunaRel(tgt));                 // (pruebas) mirar hacia la Luna
        if (k.fase == 2 && std::getenv("CK_CAM_TIERRA")) { double ex, ez; ckTierraXZ(tgt, ex, ez); mira = norm(V3{ex - tgt.x, -CK_RP - tgt.y, ez - tgt.z}); }   // (pruebas) mirar hacia la Tierra
        cam = tgt - mira * dist;
        arriba = {0, 1, 0};
    }
    // temblor al encender
    double sh = 0;
    if (k.fase == 0 && k.t > 3.0) sh = 0.04 + 0.05 * (k.t - 3.0);
    else if (k.fase == 1 && k.tv < 8.0) sh = 0.30 * (1.0 - k.tv / 8.0);
    if (sh > 0) {
        uint32_t s = (uint32_t)(k.tReloj * 60.0);
        cam = cam + V3{(ckR(s) - 0.5) * sh, (ckR(s + 7) - 0.5) * sh, (ckR(s + 13) - 0.5) * sh};
    }
}

static void ckDibujar(const float* vp, V3 cam, const float* sol, float solF, float skyMul, float hdr, const float* cielo, float fogA, float fogB, bool vidrio) {
    Cohete& k = g_ck;
    if (!k.activo) return;
    // temblor del propio cohete
    V3 off = {0, 0, 0};
    if (k.fase == 0 && k.t > 3.0) {
        double sh = 0.03 + 0.04 * (k.t - 3.0);
        uint32_t s = (uint32_t)(k.tReloj * 45.0);
        off = {(ckR(s + 21) - 0.5) * sh, (ckR(s + 23) - 0.5) * sh * 0.5, (ckR(s + 29) - 0.5) * sh};
    } else if (k.fase == 1 && k.tv < 6.0) {
        double sh = 0.10 * (1.0 - k.tv / 6.0);
        uint32_t s = (uint32_t)(k.tReloj * 45.0);
        off = {(ckR(s + 21) - 0.5) * sh, 0, (ckR(s + 29) - 0.5) * sh};
    }
    V3 p1 = k.e1.p, p2 = k.e2.p;
    k.e1.p = p1 + off; k.e2.p = p2 + off;
    if (k.e1vivo) vehDibujarUno(k.e1, vp, cam, sol, solF, skyMul, hdr, cielo, fogA, fogB, vidrio);
    vehDibujarUno(k.e2, vp, cam, sol, solF, skyMul, hdr, cielo, fogA, fogB, vidrio);
    k.e1.p = p1; k.e2.p = p2;
    if (!g_progVeh) return;
    auto U = [&](const char* n) { return glGetUniformLocation(g_progVeh, n); };
    glUseProgram(g_progVeh);
    glUniformMatrix4fv(U("uVP"), 1, GL_FALSE, vp);
    glUniform3f(U("uSun"), sol[0], sol[1], sol[2]);
    glUniform3f(U("uSky"), cielo[0], cielo[1], cielo[2]);
    glUniform1f(U("uSolF"), solF); glUniform1f(U("uSkyMul"), skyMul); glUniform1f(U("uHDR"), hdr);
    glUniform1f(U("uFogA"), fogA); glUniform1f(U("uFogB"), fogB);
    glUniform3f(U("uPiv"), 0, 0, 0); glUniform1f(U("uAng"), 0.0f);
    if (!vidrio && k.quema) {      // llama del motor de la etapa 2
        std::vector<float> o;
        uint32_t sem = (uint32_t)(k.tReloj * 30.0);
        const int L = 14;
        for (int s = 0; s < L; s++) {
            double t = s / (double)L;
            double fl = 0.75 + 0.5 * ckR(sem * 7 + s * 3);
            float hx = (float)(1.9 * (1.0 - 0.65 * t) * fl);
            float cy = (float)(CK_SPLIT - CK_O2 - 1.5 - s);
            ckCaja(o, (float)((ckR(sem + s) - 0.5) * 0.3 * VS), (float)(cy * VS), (float)((ckR(sem + 9 + s) - 0.5) * 0.3 * VS), hx * (float)VS, 0.5f * (float)VS, hx * (float)VS,
                   0.75f, (float)(0.90 - 0.60 * t), (float)(1.0 - 0.9 * t), 2.0f);
        }
        k.flama.subir(o);
        V3 f, r, u; vehBase(k.e2, f, r, u);
        V3 rel = k.e2.p - cam;
        glUniform3f(U("uPos"), (float)rel.x, (float)rel.y, (float)rel.z);
        glUniform3f(U("uR"), (float)r.x, (float)r.y, (float)r.z);
        glUniform3f(U("uU"), (float)u.x, (float)u.y, (float)u.z);
        glUniform3f(U("uF"), (float)f.x, (float)f.y, (float)f.z);
        if (k.flama.n) { glBindVertexArray(k.flama.vao); glDrawArrays(GL_TRIANGLES, 0, k.flama.n); }
    }
    if (!vidrio) {
        // llamas de las 5 toberas (solo con la etapa 1 encendida)
        double inten = 0;
        if (k.fase == 0 && k.t > 3.0) inten = 0.25 + 0.22 * (k.t - 3.0);
        else if (k.fase == 1) inten = 1.0;
        if (inten > 0) {
            std::vector<float> o;
            static const int EN[5][2] = {{0, 0}, {3, 0}, {-3, 0}, {0, 3}, {0, -3}};
            uint32_t sem = (uint32_t)(k.tReloj * 30.0);
            const int L = (int)(7 + 13 * inten);
            for (int e = 0; e < 5; e++) {
                for (int s = 0; s < L; s++) {
                    double t = s / (double)L;
                    double fl = 0.75 + 0.5 * ckR(sem * 7 + e * 31 + s * 3);
                    float hx = (float)((e == 0 ? 1.45 : 1.25) * (1.0 - 0.65 * t) * fl);
                    float cy = (float)(-CK_O1 - 0.5 - s);
                    float rr = 1.0f, gg = (float)(0.96 - 0.62 * t), bb = (float)(0.70 - 0.62 * t);
                    ckCaja(o, (float)((EN[e][0] + (ckR(sem + e * 5 + s) - 0.5) * 0.4) * VS), (float)(cy * VS), (float)((EN[e][1] + (ckR(sem + e * 9 + s) - 0.5) * 0.4) * VS),
                           hx * (float)VS, 0.5f * (float)VS, hx * (float)VS, rr, gg, bb, 2.0f);
                }
            }
            k.flama.subir(o);
            V3 f, r, u; vehBase(k.e1, f, r, u);
            V3 rel = k.e1.p + off - cam;
            glUniform3f(U("uPos"), (float)rel.x, (float)rel.y, (float)rel.z);
            glUniform3f(U("uR"), (float)r.x, (float)r.y, (float)r.z);
            glUniform3f(U("uU"), (float)u.x, (float)u.y, (float)u.z);
            glUniform3f(U("uF"), (float)f.x, (float)f.y, (float)f.z);
            if (k.flama.n) { glBindVertexArray(k.flama.vao); glDrawArrays(GL_TRIANGLES, 0, k.flama.n); }
        }
    } else if (!k.humo.empty()) {
        std::vector<float> o;
        o.reserve(k.humo.size() * 360);
        for (auto& h : k.humo) {
            double edad = (k.tReloj - h.t0), x = edad / h.vida;
            float a = (float)(0.50 * std::pow(std::max(0.0, 1.0 - x), 1.5) * std::min(1.0, edad * 6.0));
            float tam = (float)std::min(8.0, 0.8 + edad * 1.7);
            float tint = (float)std::max(0.0, 1.0 - edad / 0.8);
            ckCaja(o, (float)(h.p.x - k.P0.x), (float)(h.p.y - k.P0.y), (float)(h.p.z - k.P0.z), tam, tam, tam,
                   0.84f + 0.16f * tint, 0.84f - 0.04f * tint, 0.86f - 0.20f * tint, a);
        }
        k.fx.subir(o);
        V3 rel = k.P0 - cam;
        glUniform3f(U("uPos"), (float)rel.x, (float)rel.y, (float)rel.z);
        glUniform3f(U("uR"), 1, 0, 0); glUniform3f(U("uU"), 0, 1, 0); glUniform3f(U("uF"), 0, 0, 1);
        glDepthMask(GL_FALSE);
        if (k.fx.n) { glBindVertexArray(k.fx.vao); glDrawArrays(GL_TRIANGLES, 0, k.fx.n); }
        glDepthMask(GL_TRUE);
    }
}

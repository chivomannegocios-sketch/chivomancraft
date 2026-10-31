// Editor de avatar 3D (v90, fase 1): el avatar es una lista de cubitos de color, cada uno de una parte del cuerpo.
// Se edita desde el menu principal (AVATAR 3D). Se guarda en avatar.txt. (Se incluye desde main.cpp: usa sus funciones h* de HUD.)
#pragma once

struct AvCubo { int parte; int x, y, z, sx, sy, sz; float r, g, b; };      // esquina minima y tamano en "unidades" (1 unidad = 1/32 de la altura del avatar)
static std::vector<AvCubo> g_avCubos;
static int g_avSel = -1, g_avParte = 0, g_avColor = 8;
static float g_avYaw = 3.14159f + 0.55f, g_avPitch = 0.22f, g_avDist = 4.4f;
static bool g_avOrbita = false, g_avSolo = false;
static double g_avPX = 0, g_avPY = 0;
static const char* AV_PARTES[6] = {"CABEZA", "TORSO", "BRAZO IZQ", "BRAZO DER", "PIERNA IZQ", "PIERNA DER"};
// centro de giro de cada parte (para animar en la fase 3)
static const int AV_PIVOTE[6][3] = {{0, 24, 0}, {0, 12, 0}, {6, 24, 0}, {-6, 24, 0}, {2, 12, 0}, {-2, 12, 0}};   // izquierda = +x (como el modelo del juego)
static const int AV_MAXC = 3000;
static const int AV_LIM[3][2] = {{-11, 11}, {-3, 35}, {-7, 7}};          // caja limite: el cuerpo normal (16x32x8) mas 3 cubitos por lado
static const float AV_US = 1.0f / 16.0f;                                 // unidades -> metros del editor
static std::string avArchivo() { return "avatar.txt"; }

static void avHsv(float h, float s, float v, float* o) {
    h = h - std::floor(h); float c = v * s, x = c * (1 - std::fabs(std::fmod(h * 6, 2.0f) - 1)), m = v - c, r = 0, g = 0, b = 0;
    int i = (int)(h * 6);
    switch (i % 6) { case 0: r = c; g = x; break; case 1: r = x; g = c; break; case 2: g = c; b = x; break; case 3: g = x; b = c; break; case 4: r = x; b = c; break; default: r = c; b = x; }
    o[0] = r + m; o[1] = g + m; o[2] = b + m;
}
static void avColorPaleta(int i, float* o) {         // 32 colores: 8 grises + 3 filas de 8 tonos
    if (i < 8) { float v = 1.0f - i / 7.0f * 0.95f; o[0] = o[1] = o[2] = v; return; }
    int f = (i - 8) / 8, c = (i - 8) % 8;
    static const float SV[3][2] = {{0.45f, 1.0f}, {0.85f, 0.95f}, {0.9f, 0.55f}};
    avHsv(c / 8.0f, SV[f][0], SV[f][1], o);
}
static void avLimitar(AvCubo& c) {
    int* p[3] = {&c.x, &c.y, &c.z}; int* s[3] = {&c.sx, &c.sy, &c.sz};
    for (int i = 0; i < 3; i++) {
        *s[i] = std::max(1, std::min(*s[i], AV_LIM[i][1] - AV_LIM[i][0]));
        *p[i] = std::max(AV_LIM[i][0], std::min(*p[i], AV_LIM[i][1] - *s[i]));
    }
}
static void avGuardar() {
    FILE* f = std::fopen(avArchivo().c_str(), "w");
    if (!f) return;
    std::fprintf(f, "AVATAR2\n");
    for (auto& c : g_avCubos) std::fprintf(f, "%d %d %d %d %d %d %d %.3f %.3f %.3f\n", c.parte, c.x, c.y, c.z, c.sx, c.sy, c.sz, c.r, c.g, c.b);
    std::fclose(f);
}
static void avBase() {       // modelo inicial: como el jugador normal (cabeza 8, torso 8x12x4, brazos y piernas 4x12x4)
    g_avCubos.clear();
    auto A = [&](int p, int x, int y, int z, int sx, int sy, int sz, float r, float g, float b) { g_avCubos.push_back({p, x, y, z, sx, sy, sz, r, g, b}); };
    const float P[3] = {0.93f, 0.74f, 0.60f}, C[3] = {0.20f, 0.62f, 0.64f}, J[3] = {0.25f, 0.28f, 0.62f}, H[3] = {0.30f, 0.20f, 0.12f}, Z[3] = {0.18f, 0.18f, 0.20f};
    A(0, -4, 24, -4, 8, 8, 8, P[0], P[1], P[2]);
    A(0, -4, 32, -4, 8, 2, 8, H[0], H[1], H[2]);                    // pelo arriba
    A(0, -4, 26, -5, 8, 6, 1, H[0], H[1], H[2]);                    // pelo atras
    A(0, -3, 27, 4, 2, 1, 1, 0.1f, 0.1f, 0.15f); A(0, 1, 27, 4, 2, 1, 1, 0.1f, 0.1f, 0.15f);   // ojos (miran hacia +z)
    A(1, -4, 12, -2, 8, 12, 4, C[0], C[1], C[2]);
    A(3, -8, 12, -2, 4, 6, 4, P[0], P[1], P[2]); A(3, -8, 18, -2, 4, 6, 4, C[0], C[1], C[2]);
    A(2, 4, 12, -2, 4, 6, 4, P[0], P[1], P[2]); A(2, 4, 18, -2, 4, 6, 4, C[0], C[1], C[2]);
    A(5, -4, 2, -2, 4, 10, 4, J[0], J[1], J[2]); A(5, -4, 0, -2, 4, 2, 4, Z[0], Z[1], Z[2]);
    A(4, 0, 2, -2, 4, 10, 4, J[0], J[1], J[2]); A(4, 0, 0, -2, 4, 2, 4, Z[0], Z[1], Z[2]);
}
static void avCargar() {
    g_avCubos.clear();
    FILE* f = std::fopen(avArchivo().c_str(), "r");
    if (f) {
        char cab[16] = {0};
        const bool ok1 = std::fscanf(f, "%15s", cab) == 1;
        if (ok1 && (std::strcmp(cab, "AVATAR1") == 0 || std::strcmp(cab, "AVATAR2") == 0)) {
            const bool viejo = std::strcmp(cab, "AVATAR1") == 0;      // v90 tenia izquierda/derecha al reves: se intercambian las partes
            AvCubo c;
            while (std::fscanf(f, "%d %d %d %d %d %d %d %f %f %f", &c.parte, &c.x, &c.y, &c.z, &c.sx, &c.sy, &c.sz, &c.r, &c.g, &c.b) == 10 && (int)g_avCubos.size() < AV_MAXC) {
                c.parte = std::max(0, std::min(5, c.parte)); if (viejo && c.parte >= 2) c.parte ^= 1; avLimitar(c); g_avCubos.push_back(c);
            }
        }
        std::fclose(f);
    }
    if (g_avCubos.empty()) avBase();
    g_avSel = -1;
}

// ---- camara del editor (orbita alrededor del centro del avatar)
struct AvCam { V3 pos, fwd, rg, up; float proy[16], vp[16]; float w, h; };
static float avPanelW(int fbw, int fbh) { float k = std::max(0.8f, fbh / 720.0f); return std::min(330.0f * k, fbw * 0.46f); }
static AvCam avCamara(int fbw, int fbh) {
    AvCam c;
    c.w = std::max(1.0f, fbw - avPanelW(fbw, fbh)); c.h = (float)fbh;
    V3 t = {0, 16 * AV_US, 0};
    c.pos = {t.x + g_avDist * std::sin(g_avYaw) * std::cos(g_avPitch), t.y + g_avDist * std::sin(g_avPitch), t.z - g_avDist * std::cos(g_avYaw) * std::cos(g_avPitch)};
    c.fwd = norm(V3{t.x - c.pos.x, t.y - c.pos.y, t.z - c.pos.z});
    c.rg = norm(cruz(c.fwd, V3{0, 1, 0})); c.up = cruz(c.rg, c.fwd);
    float vista[16];
    matPerspectiva(c.proy, 0.9f, c.w / c.h, 0.05f, 60.0f);
    matVista(vista, c.fwd, V3{0, 1, 0});
    matMul(c.vp, c.proy, vista);
    return c;
}
static void avCaja(std::vector<float>& v, const AvCam& cm, float x0, float y0, float z0, float x1, float y1, float z1, const float* col) {
    // coordenadas del editor en unidades; el modelo mira hacia +z local = -z del mundo (como jfCaja)
    JfXf T; T.ox = 0; T.oy = 0; T.oz = 0; T.cy = 1; T.sy = 0; T.cam[0] = cm.pos.x; T.cam[1] = cm.pos.y; T.cam[2] = cm.pos.z;
    jfCaja(v, T, x0 * AV_US, x1 * AV_US, y0 * AV_US, y1 * AV_US, -z1 * AV_US, -z0 * AV_US, col);   // z del editor = z del mundo (jfCaja usa +z local = -z mundo)
}
static void avMarco(std::vector<float>& v, const AvCam& cm, float x0, float y0, float z0, float x1, float y1, float z1, float e, const float* col) {
    for (int i = 0; i < 4; i++) {
        float xa = (i & 1) ? x1 : x0, za = (i & 2) ? z1 : z0;
        avCaja(v, cm, xa - e, y0 - e, za - e, xa + e, y1 + e, za + e, col);            // verticales
    }
    for (int j = 0; j < 2; j++) {
        float ya = j ? y1 : y0;
        avCaja(v, cm, x0 - e, ya - e, z0 - e, x1 + e, ya + e, z0 + e, col); avCaja(v, cm, x0 - e, ya - e, z1 - e, x1 + e, ya + e, z1 + e, col);
        avCaja(v, cm, x0 - e, ya - e, z0 - e, x0 + e, ya + e, z1 + e, col); avCaja(v, cm, x1 - e, ya - e, z0 - e, x1 + e, ya + e, z1 + e, col);
    }
}
static bool avVisible(const AvCubo& c) { return !g_avSolo || c.parte == g_avParte; }
static void avDibujar3D(int fbw, int fbh) {
    AvCam cm = avCamara(fbw, fbh);
    glClearColor(0.15f, 0.16f, 0.19f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, (GLsizei)cm.w, (GLsizei)cm.h);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    std::vector<float> v;
    const float suelo[3] = {0.30f, 0.32f, 0.36f}, lim[3] = {0.95f, 0.80f, 0.25f}, ejeX[3] = {0.9f, 0.25f, 0.25f}, ejeZ[3] = {0.25f, 0.45f, 0.95f};
    avCaja(v, cm, -14, -0.6f, -10, 14, 0, 10, suelo);
    avMarco(v, cm, (float)AV_LIM[0][0], (float)AV_LIM[1][0], (float)AV_LIM[2][0], (float)AV_LIM[0][1], (float)AV_LIM[1][1], (float)AV_LIM[2][1], 0.07f, lim);   // caja limite
    avCaja(v, cm, 0, 0, -0.15f, 13, 0.15f, 0.15f, ejeX);          // eje X (rojo)
    avCaja(v, cm, -0.15f, 0, 0, 0.15f, 0.15f, 9, ejeZ);          // eje Z (azul): hacia donde mira el avatar (adelante = azul)
    for (size_t i = 0; i < g_avCubos.size(); i++) {
        const AvCubo& c = g_avCubos[i];
        if (!avVisible(c)) continue;
        float col[3] = {c.r, c.g, c.b};
        avCaja(v, cm, (float)c.x, (float)c.y, (float)c.z, (float)(c.x + c.sx), (float)(c.y + c.sy), (float)(c.z + c.sz), col);
    }
    const float sol[3] = {0.35f, 0.85f, 0.40f}, cielo[3] = {0.5f, 0.55f, 0.65f};
    itemDibujar(cm.vp, sol, 1.0f, 1.0f, cielo, 1e7f, 2e7f, v);
    v.clear();
    glDisable(GL_DEPTH_TEST);        // el contorno del elegido se ve aunque quede tapado por otros cubitos
    if (g_avSel >= 0 && g_avSel < (int)g_avCubos.size() && avVisible(g_avCubos[g_avSel])) {
        const AvCubo& c = g_avCubos[g_avSel];
        const float p = 0.6f + 0.4f * std::sin((float)glfwGetTime() * 6.0f);
        const float col[3] = {1.0f, 1.0f * p, 0.2f};
        avMarco(v, cm, (float)c.x, (float)c.y, (float)c.z, (float)(c.x + c.sx), (float)(c.y + c.sy), (float)(c.z + c.sz), 0.09f, col);
    }
    if (!v.empty()) itemDibujar(cm.vp, sol, 1.0f, 1.0f, cielo, 1e7f, 2e7f, v);
    glViewport(0, 0, fbw, fbh);
}
// ---- elegir un cubito con el raton: rayo contra las cajas
static int avElegir(double mx, double my, int fbw, int fbh) {
    AvCam cm = avCamara(fbw, fbh);
    if (mx < 0 || mx >= cm.w) return -1;
    const double nx = (mx / cm.w) * 2.0 - 1.0, ny = 1.0 - (my / cm.h) * 2.0;
    const double ty = std::tan(0.45), tx = ty * cm.w / cm.h;
    V3 d = norm(cm.fwd + cm.rg * (nx * tx) + cm.up * (ny * ty));
    int mejor = -1; double bt = 1e9;
    for (size_t i = 0; i < g_avCubos.size(); i++) {
        const AvCubo& c = g_avCubos[i];
        if (!avVisible(c)) continue;
        const double lo[3] = {c.x * (double)AV_US, c.y * (double)AV_US, c.z * (double)AV_US}, hi[3] = {(c.x + c.sx) * (double)AV_US, (c.y + c.sy) * (double)AV_US, (c.z + c.sz) * (double)AV_US};
        const double o[3] = {cm.pos.x, cm.pos.y, cm.pos.z}, dd[3] = {d.x, d.y, d.z};
        double t0 = 0, t1 = 1e9; bool ok = true;
        for (int a = 0; a < 3 && ok; a++) {
            if (std::fabs(dd[a]) < 1e-9) { if (o[a] < lo[a] || o[a] > hi[a]) ok = false; continue; }
            double ta = (lo[a] - o[a]) / dd[a], tb = (hi[a] - o[a]) / dd[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) ok = false;
        }
        if (ok && t0 < bt) { bt = t0; mejor = (int)i; }
    }
    return mejor;
}

// ---- panel de botones (derecha)
struct AvBtn { MRect r; int id; std::string txt; float cr, cg, cb; };
static std::vector<AvBtn> avBotones(int fbw, int fbh) {
    std::vector<AvBtn> b;
    const float k = std::max(0.8f, fbh / 720.0f), pw = avPanelW(fbw, fbh), x0 = fbw - pw + 8 * k, W = pw - 16 * k;
    float y = 34 * k;
    auto B = [&](float x, float yy, float w, float h, int id, const char* t, float r, float g, float bl) { b.push_back({{x, yy, w, h}, id, t, r, g, bl}); };
    B(x0, 6 * k, W * 0.5f - 3 * k, 24 * k, 26, "VOLVER", 0.45f, 0.14f, 0.14f);
    B(x0 + W * 0.5f + 3 * k, 6 * k, W * 0.5f - 3 * k, 24 * k, 24, "MODELO BASE", 0.30f, 0.30f, 0.34f);
    // partes
    for (int i = 0; i < 6; i++) B(x0 + (i % 2) * (W * 0.5f + 3 * k), y + (i / 2) * 27 * k, W * 0.5f - 3 * k, 24 * k, 100 + i, AV_PARTES[i], i == g_avParte ? 0.20f : 0.12f, i == g_avParte ? 0.55f : 0.22f, i == g_avParte ? 0.30f : 0.28f);
    y += 3 * 27 * k + 4 * k;
    B(x0, y, W, 22 * k, 25, g_avSolo ? "VER: SOLO ESTA PARTE" : "VER: TODAS LAS PARTES", 0.22f, 0.25f, 0.40f);
    y += 26 * k;
    // paleta (se dibuja aparte, aqui solo los huecos)
    const float sw = W / 8.0f;
    for (int i = 0; i < 32; i++) B(x0 + (i % 8) * sw, y + (i / 8) * sw, sw - 2 * k, sw - 2 * k, 200 + i, "", 0, 0, 0);
    y += 4 * sw + 6 * k;
    // posicion y tamano del cubo elegido
    static const char* NOM[6] = {"X", "Y", "Z", "ANCHO", "ALTO", "FONDO"};
    for (int i = 0; i < 6; i++) {
        float yy = y + i * 25 * k;
        B(x0 + W - 60 * k, yy, 28 * k, 22 * k, 1 + i * 2, "-", 0.28f, 0.20f, 0.20f);
        B(x0 + W - 28 * k, yy, 28 * k, 22 * k, 2 + i * 2, "+", 0.20f, 0.28f, 0.20f);
        (void)NOM;
    }
    y += 6 * 25 * k + 4 * k;
    const float bw = W / 3.0f - 3 * k;
    B(x0, y, bw, 24 * k, 20, "NUEVO", 0.18f, 0.45f, 0.22f); B(x0 + bw + 4.5f * k, y, bw, 24 * k, 21, "COPIAR", 0.20f, 0.32f, 0.50f); B(x0 + 2 * (bw + 4.5f * k), y, bw, 24 * k, 22, "ESPEJAR", 0.35f, 0.22f, 0.50f);
    y += 28 * k;
    B(x0, y, W, 24 * k, 23, "BORRAR CUBITO", 0.55f, 0.16f, 0.16f);
    y += 30 * k;
    // vista
    const float bv = W / 4.0f - 3 * k;
    B(x0, y, bv, 24 * k, 30, "IZQ", 0.2f, 0.2f, 0.26f); B(x0 + (bv + 4 * k), y, bv, 24 * k, 31, "DER", 0.2f, 0.2f, 0.26f);
    B(x0 + 2 * (bv + 4 * k), y, bv, 24 * k, 32, "ARR", 0.2f, 0.2f, 0.26f); B(x0 + 3 * (bv + 4 * k), y, bv, 24 * k, 33, "ABA", 0.2f, 0.2f, 0.26f);
    y += 28 * k;
    B(x0, y, W * 0.5f - 3 * k, 24 * k, 34, "ZOOM +", 0.2f, 0.2f, 0.26f); B(x0 + W * 0.5f + 3 * k, y, W * 0.5f - 3 * k, 24 * k, 35, "ZOOM -", 0.2f, 0.2f, 0.26f);
    return b;
}
static void avAbrir() { avCargar(); g_pantalla = 4; g_avOrbita = false; }
static void avTocado() { avGuardar(); }
static void avCambiar(int eje, int signo, bool tam) {      // mueve (o cambia el tamano de) el cubito elegido
    if (g_avSel < 0 || g_avSel >= (int)g_avCubos.size()) return;
    AvCubo& c = g_avCubos[g_avSel];
    int* p[3] = {&c.x, &c.y, &c.z}; int* s[3] = {&c.sx, &c.sy, &c.sz};
    if (tam) *s[eje] += signo; else *p[eje] += signo;
    avLimitar(c); avTocado();
}
static void avAccion(int id) {
    AvCubo* sel = (g_avSel >= 0 && g_avSel < (int)g_avCubos.size()) ? &g_avCubos[g_avSel] : nullptr;
    if (id >= 1 && id <= 12) { int i = (id - 1) / 2, sg = ((id - 1) % 2) ? 1 : -1; avCambiar(i % 3, sg, i >= 3); return; }
    if (id >= 100 && id < 106) { g_avParte = id - 100; if (sel) { sel->parte = g_avParte; avTocado(); } return; }
    if (id >= 200 && id < 232) { g_avColor = id - 200; if (sel) { float o[3]; avColorPaleta(g_avColor, o); sel->r = o[0]; sel->g = o[1]; sel->b = o[2]; avTocado(); } return; }
    switch (id) {
    case 20: { if ((int)g_avCubos.size() >= AV_MAXC) break; AvCubo c; c.parte = g_avParte; c.x = -1; c.y = AV_PIVOTE[g_avParte][1]; c.z = -1; c.sx = c.sy = c.sz = 2; float o[3]; avColorPaleta(g_avColor, o); c.r = o[0]; c.g = o[1]; c.b = o[2]; avLimitar(c); g_avCubos.push_back(c); g_avSel = (int)g_avCubos.size() - 1; avTocado(); break; }
    case 21: if (sel && (int)g_avCubos.size() < AV_MAXC) { AvCubo c = *sel; c.x += 1; avLimitar(c); g_avCubos.push_back(c); g_avSel = (int)g_avCubos.size() - 1; avTocado(); } break;
    case 22: if (sel && (int)g_avCubos.size() < AV_MAXC) { AvCubo c = *sel; c.x = -(sel->x + sel->sx); if (c.parte == 2) c.parte = 3; else if (c.parte == 3) c.parte = 2; else if (c.parte == 4) c.parte = 5; else if (c.parte == 5) c.parte = 4; avLimitar(c); g_avCubos.push_back(c); g_avSel = (int)g_avCubos.size() - 1; avTocado(); } break;
    case 23: if (sel) { g_avCubos.erase(g_avCubos.begin() + g_avSel); g_avSel = -1; avTocado(); } break;
    case 24: avBase(); g_avSel = -1; avTocado(); break;
    case 25: g_avSolo = !g_avSolo; break;
    case 26: avGuardar(); g_pantalla = 2; g_avOrbita = false; break;
    case 30: g_avYaw -= 0.25f; break;
    case 31: g_avYaw += 0.25f; break;
    case 32: g_avPitch = std::min(1.4f, g_avPitch + 0.15f); break;
    case 33: g_avPitch = std::max(-1.2f, g_avPitch - 0.15f); break;
    case 34: g_avDist = std::max(1.5f, g_avDist * 0.85f); break;
    case 35: g_avDist = std::min(12.0f, g_avDist * 1.18f); break;
    }
}
static int avHit(double mx, double my) {
    const double px = mx * g_fbW / std::max(1, g_winW), py = my * g_fbH / std::max(1, g_winH);
    for (auto& b : avBotones(g_fbW, g_fbH)) if (mDentro(b.r, px, py)) return b.id;
    return -1;
}
static void avClic(int boton, int accion) {
    const double px = g_mx * g_fbW / std::max(1, g_winW), py = g_my * g_fbH / std::max(1, g_winH);
    if (boton == GLFW_MOUSE_BUTTON_RIGHT || boton == 2) { g_avOrbita = accion == GLFW_PRESS; g_avPX = px; g_avPY = py; return; }
    if (boton != GLFW_MOUSE_BUTTON_LEFT || accion != GLFW_PRESS) return;
    int h = avHit(g_mx, g_my);
    if (h >= 0) { avAccion(h); return; }
    if (px < g_fbW - avPanelW(g_fbW, g_fbH)) g_avSel = avElegir(px, py, g_fbW, g_fbH);
}
static void avMouse(double x, double y) {
    const double px = x * g_fbW / std::max(1, g_winW), py = y * g_fbH / std::max(1, g_winH);
    if (g_avOrbita) { g_avYaw += (float)(px - g_avPX) * 0.01f; g_avPitch = std::max(-1.2f, std::min(1.4f, g_avPitch + (float)(py - g_avPY) * 0.01f)); }
    g_avPX = px; g_avPY = py;
}
static void avRueda(double dy) { g_avDist = std::max(1.5f, std::min(12.0f, g_avDist * (dy > 0 ? 0.88f : 1.14f))); }
static void avTecla(GLFWwindow* w, int tecla, int accion) {
    if (accion != GLFW_PRESS && accion != GLFW_REPEAT) return;
    const bool may = glfwGetKey(w, 340) == GLFW_PRESS || glfwGetKey(w, 344) == GLFW_PRESS;
    // izquierda/derecha y adelante/atras segun hacia donde miras la camara
    const double fx = std::sin(g_avYaw), fz = -std::cos(g_avYaw);           // posicion de la camara respecto al centro (en el mundo)
    // la camara mira hacia -(fx,fz); su derecha en el mundo: cruz(adelante, arriba)
    const double ax = -fx, az = -fz;                                        // adelante (hacia el centro)
    const double rx = -az, rz = az * 0.0 + ax;                              // derecha = (adelante x arriba) en el plano xz
    auto eje = [&](double vx, double vz, int& e, int& sg) {                 // eje del editor (x o z local) mas cercano a un vector del mundo
        if (std::fabs(vx) >= std::fabs(vz)) { e = 0; sg = vx >= 0 ? 1 : -1; } else { e = 2; sg = vz >= 0 ? 1 : -1; }   // z local = -z mundo
    };
    int e, sg;
    switch (tecla) {
    case 256: avAccion(26); break;
    case 262: eje(rx, rz, e, sg); avCambiar(e, sg, may); break;
    case 263: eje(rx, rz, e, sg); avCambiar(e, -sg, may); break;
    case 265: eje(ax, az, e, sg); avCambiar(e, sg, may); break;
    case 264: eje(ax, az, e, sg); avCambiar(e, -sg, may); break;
    case 266: avCambiar(1, 1, may); break;
    case 267: avCambiar(1, -1, may); break;
    case 261: case 259: avAccion(23); break;
    case 78: avAccion(20); break;
    case 67: avAccion(21); break;
    case 77: avAccion(22); break;
    case 258: if (!g_avCubos.empty()) { g_avSel = (g_avSel + 1) % (int)g_avCubos.size(); } break;
    }
}
static void avDibujarUI(int fbw, int fbh) {
    const float k = std::max(0.8f, fbh / 720.0f), t = std::max(1.0f, std::floor(1.5f * k + 0.5f)), pw = avPanelW(fbw, fbh);
    const double px = g_mx * fbw / std::max(1, g_winW), py = g_my * fbh / std::max(1, g_winH);
    hRect(fbw - pw, 0, pw, (float)fbh, 0.09f, 0.10f, 0.13f, 1.0f);
    const float x0 = fbw - pw + 8 * k, W = pw - 16 * k;
    // ayuda abajo a la izquierda
    char b[200];
    std::snprintf(b, sizeof b, "AVATAR 3D   CUBITOS: %d / %d", (int)g_avCubos.size(), AV_MAXC);
    hTexto(b, 10 * k, 10 * k, t * 1.4f, 1.0f, 0.9f, 0.4f);
    if (g_avSel >= 0 && g_avSel < (int)g_avCubos.size()) {
        const AvCubo& c = g_avCubos[g_avSel];
        std::snprintf(b, sizeof b, "ELEGIDO: %s  POS %d %d %d  TAMANO %d %d %d", AV_PARTES[c.parte], c.x, c.y, c.z, c.sx, c.sy, c.sz);
        hTexto(b, 10 * k, 10 * k + 14 * t, t, 0.9f, 0.95f, 1.0f);
    } else hTexto("ELIGE UN CUBITO CON EL CLIC, O PULSA NUEVO", 10 * k, 10 * k + 14 * t, t, 0.7f, 0.75f, 0.8f);
    const char* ayuda[5] = {"CLIC: ELEGIR   CLIC DERECHO + ARRASTRAR: GIRAR   RUEDA: ZOOM", "FLECHAS: MOVER (SEGUN LA CAMARA)   RE PAG / AV PAG: SUBIR / BAJAR", "MAYUS + FLECHAS: CAMBIAR TAMANO   N: NUEVO   C: COPIAR   M: ESPEJAR", "SUPR: BORRAR   TAB: SIGUIENTE CUBITO   ESC: VOLVER (SE GUARDA SOLO)", "CAJA AMARILLA = LIMITE DE TAMANO   EJE AZUL = HACIA DONDE MIRA"};
    for (int i = 0; i < 5; i++) hTexto(ayuda[i], 10 * k, fbh - (5 - i) * 13.0f * t - 8 * k, t, 0.7f, 0.74f, 0.8f);
    // botones
    static const char* NOM[6] = {"X", "Y", "Z", "ANCHO", "ALTO", "FONDO"};
    const int cs = g_avSel >= 0 && g_avSel < (int)g_avCubos.size() ? 1 : 0;
    for (auto& bt : avBotones(fbw, fbh)) {
        const bool hov = mDentro(bt.r, px, py);
        if (bt.id >= 200 && bt.id < 232) {
            float o[3]; avColorPaleta(bt.id - 200, o);
            hRect(bt.r.x, bt.r.y, bt.r.w, bt.r.h, o[0], o[1], o[2], 1.0f);
            if (bt.id - 200 == g_avColor) hMarco(bt.r.x - 1, bt.r.y - 1, bt.r.w + 2, bt.r.h + 2, std::max(2.0f, 2 * k), 1, 1, 1, 1.0f);
            else if (hov) hMarco(bt.r.x, bt.r.y, bt.r.w, bt.r.h, std::max(1.0f, k), 1, 1, 1, 0.7f);
            continue;
        }
        hBoton(bt.r, k, bt.cr, bt.cg, bt.cb, hov, bt.txt.c_str(), t * 1.2f);
    }
    // etiquetas de posicion / tamano (a la izquierda de los - y +)
    for (auto& bt : avBotones(fbw, fbh)) if (bt.id >= 1 && bt.id <= 12 && (bt.id % 2) == 1) {
        const int i = (bt.id - 1) / 2; int val = 0;
        if (cs) { const AvCubo& c = g_avCubos[g_avSel]; const int vv[6] = {c.x, c.y, c.z, c.sx, c.sy, c.sz}; val = vv[i]; }
        std::snprintf(b, sizeof b, cs ? "%s: %d" : "%s", NOM[i], val);
        hTexto(b, x0, bt.r.y + 5 * k, t * 1.2f, 0.9f, 0.92f, 1.0f);
    }
    (void)W;
}

// ======================= fase 2: el avatar dentro del juego =======================
// Dibuja el avatar (cubitos de color) con la misma animacion del modelo normal: cabeza sigue el cabeceo, brazos y piernas caminan,
// el brazo derecho golpea. 'solo' = codigo de animacion de la parte (2 = brazo derecho) para la mano en primera persona.
static const int AV_ANIM[6] = {1, 0, 3, 2, 5, 4};      // cabeza, torso, brazo izq, brazo der, pierna izq, pierna der -> codigos de J_PARTES
static void avGenerar(const Remoto& m, double camx, double camy, double camz, std::vector<float>& out, std::vector<float>* items, int solo, float angPrimera) {
    const float S = 1.8f / 32.0f;
    const float fx = std::sin(m.syaw), fz = -std::cos(m.syaw);
    const float Xx = fz, Xz = -fx;
    const float amp = 0.85f * std::min(1.0f, m.vel / 3.5f), sw = std::sin(m.fase) * amp;
    const bool sentado = (m.flags & 2) != 0, primera = angPrimera < 50.0f, tiene = m.held != 255;
    const float swAdd = m.swing > 0 ? -1.9f * std::sin(m.swing * 3.14159265f) : 0.0f;
    float angR = sentado ? -1.2f : (tiene ? -0.55f - sw * 0.4f : -sw);
    angR += swAdd;
    if (primera) angR = angPrimera + swAdd * 0.9f;
    const float ox = (float)(m.sx - camx), oy = (float)(m.sy - camy), oz = (float)(m.sz - camz);
    struct Cara { float q[4][3]; float n[3]; float sh; };
    for (const AvCubo& c : g_avCubos) {
        const int anim = AV_ANIM[c.parte];
        if (solo >= 0 && anim != solo) continue;
        float ang = 0;
        switch (anim) {
            case 1: ang = -std::max(-1.2f, std::min(1.2f, m.pitch)); break;
            case 2: ang = angR; break;
            case 3: ang = sentado ? -1.2f : sw; break;
            case 4: ang = sentado ? -1.5708f : sw; break;
            case 5: ang = sentado ? -1.5708f : -sw; break;
        }
        const float ca = std::cos(ang), sa = std::sin(ang), py = (float)AV_PIVOTE[c.parte][1], pz = (float)AV_PIVOTE[c.parte][2];
        auto T = [&](float lx, float ly, float lz, float* o) {
            float y = ly, z = lz;
            if (anim) { const float yy = ly - py, zz = lz - pz; y = yy * ca - zz * sa + py; z = yy * sa + zz * ca + pz; }
            o[0] = ox + Xx * lx * S + fx * z * S; o[1] = oy + y * S; o[2] = oz + Xz * lx * S + fz * z * S;
        };
        auto N = [&](float nx, float ny, float nz, float* o) {
            float y = ny, z = nz;
            if (anim) { y = ny * ca - nz * sa; z = ny * sa + nz * ca; }
            o[0] = Xx * nx + fx * z; o[1] = y; o[2] = Xz * nx + fz * z;
        };
        const float x0 = (float)c.x, x1 = (float)(c.x + c.sx), y0 = (float)c.y, y1 = (float)(c.y + c.sy), z0 = (float)c.z, z1 = (float)(c.z + c.sz);
        const Cara cs[6] = {
            {{{x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}}, {-1, 0, 0}, 0.80f},
            {{{x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}}, {1, 0, 0}, 0.80f},
            {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, {0, -1, 0}, 0.55f},
            {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, {0, 1, 0}, 1.00f},
            {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}, {0, 0, -1}, 0.70f},
            {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, {0, 0, 1}, 0.90f}};
        static const int I[6] = {0, 1, 2, 0, 2, 3};
        for (const Cara& f : cs) {
            float Q[4][3], Nn[3];
            for (int i = 0; i < 4; i++) T(f.q[i][0], f.q[i][1], f.q[i][2], Q[i]);
            N(f.n[0], f.n[1], f.n[2], Nn);
            for (int q : I) { out.push_back(Q[q][0]); out.push_back(Q[q][1]); out.push_back(Q[q][2]); out.push_back(c.r * f.sh); out.push_back(c.g * f.sh); out.push_back(c.b * f.sh); out.push_back(Nn[0]); out.push_back(Nn[1]); out.push_back(Nn[2]); }
        }
    }
    if (items && (solo < 0 || solo == 2)) jugItem(m, camx, camy, camz, angR, primera ? 1.0f : 0.55f, *items);
}
// estado de animacion del jugador local (tercera persona)
static Remoto g_avYo;
static void avYoActualizar(double dt, double px, double py, double pz, double yaw, double pitch, bool golpea, bool volar, uint8_t held) {
    static double ax = 0, az = 0; static bool ini = false;
    Remoto& m = g_avYo;
    if (!ini) { ax = px; az = pz; ini = true; }
    const double v = dt > 1e-4 ? std::sqrt((px - ax) * (px - ax) + (pz - az) * (pz - az)) / dt : 0.0;
    ax = px; az = pz;
    m.vel += (float)((std::min(v, 12.0) - m.vel) * std::min(1.0, dt * 8.0));
    m.sx = (float)px; m.sy = (float)py; m.sz = (float)pz; m.syaw = (float)yaw; m.pitch = (float)pitch; m.held = held;
    m.flags = (uint8_t)((volar ? 1 : 0) | (golpea ? 4 : 0));
    m.fase += (float)dt * (2.0f + m.vel * 1.7f);
    if (golpea) { m.swing += (float)dt / 0.36f; if (m.swing >= 1.0f) m.swing -= 1.0f; if (m.swing <= 0) m.swing = 0.001f; }
    else if (m.swing > 0) { m.swing += (float)dt / 0.36f; if (m.swing >= 1.0f) m.swing = 0; }
}
static void avDibujarYo(const float* vp, double camx, double camy, double camz, const float* sol, float solF, float skyMul, const float* cielo, float fogA, float fogB) {
    std::vector<float> v, items;
    avGenerar(g_avYo, camx, camy, camz, v, &items, -1, 100.0f);
    v.insert(v.end(), items.begin(), items.end());
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    itemDibujar(vp, sol, solF, skyMul, cielo, fogA, fogB, v);
}

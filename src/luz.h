// Iluminacion estilo Minecraft, pero calculada por chunk en los hilos de trabajo y aplicada en la GPU:
//  - luz del cielo: baja en vertical sin perder fuerza y se reparte hacia los lados perdiendo 1 nivel por cubito
//  - luz de bloque: antorchas (nivel LTORCH) que se reparten igual
//  - hojas y agua dejan pasar la luz pero la apagan un poco; lo demas la bloquea
// Resultado: una textura 3D de (CS+2)^3 celdas con 4 bytes por celda (cielo, rojo, verde, azul de bloque) que el shader lee con
// filtrado lineal (luz suave sin tocar la malla, asi se mantiene el fusionado de caras).
#pragma once
#include "mundo.h"
#include <cstring>
#include <vector>

constexpr int LMAX = 15 * SUB;        // 15 niveles por bloque, igual que Minecraft
constexpr int LTORCH = 14 * SUB;      // la antorcha ilumina ~14 bloques
constexpr int LLED = 15 * SUB;        // los bloques LED iluminan ~15 bloques, cada uno con su color
static const float TORCH_RGB[3] = {1.0f, 0.84f, 0.66f};   // color de la antorcha
constexpr int LM = LMAX + 1;          // margen de region alrededor del chunk
constexpr int LRX = CS + 2 * LM;      // lado (x y z) de la region de calculo
constexpr int LRY = HYC * CS;         // la region abarca toda la altura del mundo
constexpr int LN = CS + 2;            // lado de la textura de luz (celdas -1 .. CS)
constexpr size_t LRN = (size_t)LRX * LRX * LRY;

static inline int luzCoste(uint8_t t) {      // 255 = bloquea la luz
    if (t >= AGUA_F1 && t <= AGUA_F8) return 1;
    switch (t) {
        case AIRE: case ANTORCHA: case VIDRIO: case LIANA: case FLOR_ROJA: case FLOR_AMARILLA: case FLOR_AZUL: case FLOR_ROSA: return 0;
        case HOJA_SELVA: case HOJA_NIEVE: case ALGA:
        case AGUA: case HOJAS: case HOJA_ROJA: case HOJA_NARANJA: case HOJA_AMARILLA: case HOJA_ROSA: case HOJA_PINO: return 1;
        default: return 255;
    }
}

struct SnapColumnas {                 // datos de las 3x3 columnas de chunks alrededor del objetivo
    DatosChunk c[3][HYC][3];          // [x][cy][z], x y z = -1..1
    bool completo = true;
};

static void luzBFS(uint8_t* L, const uint8_t* T, const uint8_t* coste, std::vector<uint32_t>& q, int WH) {
    const uint32_t SY = (uint32_t)LRX * LRX;
    size_t head = 0;
    while (head < q.size()) {
        uint32_t i = q[head++];
        int l = L[i];
        if (l <= 1) continue;
        int x = (int)(i % LRX), z = (int)((i / LRX) % LRX), y = (int)(i / SY);
        auto vecino = [&](uint32_t j) {
            int c = coste[T[j]];
            if (c == 255) return;
            int nl = l - 1 - c;
            if (nl > L[j]) { L[j] = (uint8_t)nl; q.push_back(j); }
        };
        if (x > 0) vecino(i - 1);
        if (x < LRX - 1) vecino(i + 1);
        if (z > 0) vecino(i - LRX);
        if (z < LRX - 1) vecino(i + LRX);
        if (y > 0) vecino(i - SY);
        if (y < WH - 1) vecino(i + SY);
    }
}

// out: LN*LN*LN*4 bytes (indice ((yy*LN+zz)*LN+xx)*4 + {0 cielo, 1 rojo, 2 verde, 3 azul}); celdas -1..CS del chunk cyObj
// Solo se calcula una VENTANA vertical de CS + 2*LM celdas alrededor del chunk (la luz no llega mas lejos que LMAX);
// la luz del cielo de cada columna se obtiene recorriendo los chunks desde arriba hasta el primer bloque opaco.
constexpr int LRYW = CS + 2 * LM;            // alto maximo de la ventana
constexpr size_t LRNW = (size_t)LRX * LRX * LRYW;
static void calcularLuz(const SnapColumnas& sn, int cyObj, uint8_t* out) {
    static thread_local std::vector<uint8_t> T, S, BR, BG, BB;
    static thread_local std::vector<uint32_t> q;
    static thread_local uint8_t coste[256];
    static thread_local bool iniCoste = false;
    if (!iniCoste) { for (int i = 0; i < 256; i++) coste[i] = (uint8_t)luzCoste((uint8_t)i); iniCoste = true; }
    const int wy0 = std::max(0, cyObj * CS - LM), wy1 = std::min(LRY, cyObj * CS + CS + LM), WH = wy1 - wy0;
    const size_t NC = (size_t)LRX * LRX * WH;
    if (T.size() != LRNW) { T.assign(LRNW, AIRE); S.assign(LRNW, 0); BR.assign(LRNW, 0); BG.assign(LRNW, 0); BB.assign(LRNW, 0); }
    std::memset(S.data(), 0, NC); std::memset(BR.data(), 0, NC); std::memset(BG.data(), 0, NC); std::memset(BB.data(), 0, NC);
    auto idx = [&](int x, int y, int z) { return ((size_t)(y - wy0) * LRX + z) * LRX + x; };       // y = altura absoluta en cubitos
    const int cyLow = wy0 / CS, cyHigh = (wy1 - 1) / CS;

    // 1) tipos de cubito de la ventana
    for (int ix = 0; ix < 3; ix++) for (int iz = 0; iz < 3; iz++) for (int cy = cyLow; cy <= cyHigh; cy++) {
        const DatosChunk& d = sn.c[ix][cy][iz];
        int x0 = (ix - 1) * CS, z0 = (iz - 1) * CS;
        int xs = std::max(x0, -LM), xe = std::min(x0 + CS, CS + LM);
        int zs = std::max(z0, -LM), ze = std::min(z0 + CS, CS + LM);
        if (xs >= xe || zs >= ze) continue;
        int n = xe - xs;
        for (int ly = 0; ly < CS; ly++) {
            int y = cy * CS + ly;
            if (y < wy0 || y >= wy1) continue;
            for (int z = zs; z < ze; z++) {
                uint8_t* dst = &T[idx(xs + LM, y, z + LM)];
                if (d.d) std::memcpy(dst, d.d->data() + IDX(xs - x0, ly, z - z0), (size_t)n);
                else if (d.uni == 255) std::memset(dst, cy > cyObj ? (int)AIRE : (int)PIEDRA, (size_t)n);   // chunk que falta
                else std::memset(dst, d.uni, (size_t)n);
            }
        }
    }

    // 2) luz del cielo: cada columna recibe luz completa desde arriba hasta el primer bloque opaco (se recorren los chunks de arriba abajo)
    static thread_local std::vector<int16_t> lc;        // luz restante de cada columna (0 = ya termino)
    lc.assign((size_t)LRX * LRX, (int16_t)LMAX);
    int yTop = 0;
    for (int cy = HYC - 1; cy >= cyLow; cy--) {
        for (int ix = 0; ix < 3; ix++) for (int iz = 0; iz < 3; iz++) {
            const DatosChunk& d = sn.c[ix][cy][iz];
            int x0 = (ix - 1) * CS, z0 = (iz - 1) * CS;
            int xs = std::max(x0, -LM), xe = std::min(x0 + CS, CS + LM);
            int zs = std::max(z0, -LM), ze = std::min(z0 + CS, CS + LM);
            if (xs >= xe || zs >= ze) continue;
            const uint8_t uni = d.d ? (uint8_t)AIRE : (d.uni == 255 ? (cy > cyObj ? (uint8_t)AIRE : (uint8_t)PIEDRA) : d.uni);
            const bool unif = !d.d;
            const int cu = unif ? coste[uni] : 0;
            if (unif && cu == 0 && uni == AIRE) {                         // aire uniforme: filas completas con luz maxima se copian de golpe
                for (int z = zs; z < ze; z++) {
                    int16_t* lr = &lc[(size_t)(z + LM) * LRX + (xs + LM)];
                    bool todas = true;
                    for (int x = 0; x < xe - xs; x++) if (lr[x] != LMAX) { todas = false; break; }
                    if (todas) {
                        for (int ly = 0; ly < CS; ly++) { int y = cy * CS + ly; if (y >= wy0 && y < wy1) std::memset(&S[idx(xs + LM, y, z + LM)], LMAX, (size_t)(xe - xs)); }
                    } else {
                        for (int x = xs; x < xe; x++) {
                            int l = lc[(size_t)(z + LM) * LRX + (x + LM)];
                            if (l <= 0) continue;
                            for (int ly = 0; ly < CS; ly++) { int y = cy * CS + ly; if (y >= wy0 && y < wy1) S[idx(x + LM, y, z + LM)] = (uint8_t)l; }
                        }
                    }
                }
                continue;
            }
            for (int z = zs; z < ze; z++) for (int x = xs; x < xe; x++) {
                int16_t& l = lc[(size_t)(z + LM) * LRX + (x + LM)];
                if (l <= 0) continue;
                if (unif && cu == 0) {                                   // aire uniforme: solo copiar la luz a la ventana
                    if (uni != AIRE && cy * CS + CS > yTop) yTop = cy * CS + CS;
                    for (int ly = CS - 1; ly >= 0; ly--) { int y = cy * CS + ly; if (y >= wy0 && y < wy1) S[idx(x + LM, y, z + LM)] = (uint8_t)l; }
                    continue;
                }
                if (unif) {                                              // uniforme no vacio
                    if (uni != AIRE && cy * CS + CS > yTop) yTop = cy * CS + CS;
                    if (cu == 255) { l = 0; continue; }
                    for (int ly = CS - 1; ly >= 0; ly--) {
                        l = (int16_t)(l - cu); if (l <= 0) break;
                        int y = cy * CS + ly; if (y >= wy0 && y < wy1) S[idx(x + LM, y, z + LM)] = (uint8_t)l;
                    }
                    continue;
                }
                const uint8_t* dd = d.d->data();
                for (int ly = CS - 1; ly >= 0; ly--) {
                    int y = cy * CS + ly;
                    uint8_t t = dd[IDX(x - x0, ly, z - z0)];
                    if (t != AIRE && y + 1 > yTop) yTop = y + 1;
                    int c = coste[t];
                    if (c == 255) { l = 0; break; }
                    l = (int16_t)(l - c);
                    if (l <= 0) break;
                    if (y >= wy0 && y < wy1) S[idx(x + LM, y, z + LM)] = (uint8_t)l;
                }
            }
        }
    }
    int ylo = std::max(0, cyObj * CS - 1 - LM), yhi = std::min(LRY - 1, cyObj * CS + CS + LM);
    yhi = std::min(yhi, yTop + 1);
    q.clear();
    const size_t SY = (size_t)LRX * LRX;
    for (int y = std::max(ylo, wy0); y <= std::min(yhi, wy1 - 1); y++) for (int z = 1; z < LRX - 1; z++) for (int x = 1; x < LRX - 1; x++) {
        size_t i = idx(x, y, z);
        int s = S[i];
        if (s <= 1) continue;
        size_t v[6] = {i - 1, i + 1, i - LRX, i + LRX, i - SY, i + SY};
        for (int k = 0; k < 6; k++) {
            if (k == 4 && y == wy0) continue;
            if (k == 5 && y == wy1 - 1) continue;
            if (S[v[k]] + 1 >= s) continue;                       // (barato) ya recibe casi tanta luz como yo
            int c = coste[T[v[k]]];
            if (c != 255 && S[v[k]] + 1 + c < s) { q.push_back((uint32_t)(i)); break; }
        }
    }
    luzBFS(S.data(), T.data(), coste, q, WH);

    // 3) luz de bloque con color: antorchas y bloques LED. Se reparte aparte en rojo, verde y azul (se suman, como la luz real)
    {
        const uint8_t* base = T.data();
        const uint8_t* fin = base + NC;
        std::vector<uint32_t> qr, qg, qb;
        auto fuente = [&](uint8_t tipo, int nivel, const float* rgb) {
            const uint8_t* p = base;
            while (p < fin) {
                const uint8_t* h = (const uint8_t*)std::memchr(p, tipo, (size_t)(fin - p));
                if (!h) break;
                size_t i = (size_t)(h - base);
                int vr = (int)(nivel * rgb[0] + 0.5f), vg = (int)(nivel * rgb[1] + 0.5f), vb = (int)(nivel * rgb[2] + 0.5f);
                if (vr > BR[i]) { BR[i] = (uint8_t)vr; qr.push_back((uint32_t)i); }
                if (vg > BG[i]) { BG[i] = (uint8_t)vg; qg.push_back((uint32_t)i); }
                if (vb > BB[i]) { BB[i] = (uint8_t)vb; qb.push_back((uint32_t)i); }
                p = h + 1;
            }
        };
        fuente(ANTORCHA, LTORCH, TORCH_RGB);
        { static const float LAVA_RGB[3] = {1.0f, 0.45f, 0.12f}; fuente(LAVA, LTORCH - 2 * SUB, LAVA_RGB); }       // la lava ilumina
        for (int k = 0; k < 12; k++) fuente((uint8_t)(LED_ROJO + k), LLED, LED_RGB[k]);
        luzBFS(BR.data(), T.data(), coste, qr, WH);
        luzBFS(BG.data(), T.data(), coste, qg, WH);
        luzBFS(BB.data(), T.data(), coste, qb, WH);
    }

    // 4) recorte al chunk (con borde de 1 celda)
    for (int yy = 0; yy < LN; yy++) {
        int y = cyObj * CS - 1 + yy;
        for (int zz = 0; zz < LN; zz++) for (int xx = 0; xx < LN; xx++) {
            uint8_t sk, br, bg, bb;
            if (y < 0) { sk = 0; br = bg = bb = 0; }
            else if (y >= LRY) { sk = LMAX; br = bg = bb = 0; }
            else { size_t i = idx(xx - 1 + LM, y, zz - 1 + LM); sk = S[i]; br = BR[i]; bg = BG[i]; bb = BB[i]; }
            size_t o = (((size_t)yy * LN + zz) * LN + xx) * 4;
            out[o] = sk; out[o + 1] = br; out[o + 2] = bg; out[o + 3] = bb;
        }
    }
}

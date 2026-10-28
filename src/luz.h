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
        case AIRE: case ANTORCHA: case VIDRIO: return 0;
        case AGUA: case HOJAS: return 1;
        default: return 255;
    }
}

struct SnapColumnas {                 // datos de las 3x3 columnas de chunks alrededor del objetivo
    DatosChunk c[3][HYC][3];          // [x][cy][z], x y z = -1..1
    bool completo = true;
};

static void luzBFS(uint8_t* L, const uint8_t* T, const uint8_t* coste, std::vector<uint32_t>& q) {
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
        if (y < LRY - 1) vecino(i + SY);
    }
}

// out: LN*LN*LN*4 bytes (indice ((yy*LN+zz)*LN+xx)*4 + {0 cielo, 1 rojo, 2 verde, 3 azul}); celdas -1..CS del chunk cyObj
static void calcularLuz(const SnapColumnas& sn, int cyObj, uint8_t* out) {
    static thread_local std::vector<uint8_t> T, S, BR, BG, BB;
    static thread_local std::vector<uint32_t> q;
    static thread_local uint8_t coste[256];
    static thread_local bool iniCoste = false;
    if (!iniCoste) { for (int i = 0; i < 256; i++) coste[i] = (uint8_t)luzCoste((uint8_t)i); iniCoste = true; }
    if (T.size() != LRN) { T.assign(LRN, AIRE); S.assign(LRN, 0); BR.assign(LRN, 0); BG.assign(LRN, 0); BB.assign(LRN, 0); }
    else { std::memset(S.data(), 0, LRN); std::memset(BR.data(), 0, LRN); std::memset(BG.data(), 0, LRN); std::memset(BB.data(), 0, LRN); }
    auto idx = [](int x, int y, int z) { return ((size_t)y * LRX + z) * LRX + x; };

    // 1) tipos de cubito de la region
    for (int ix = 0; ix < 3; ix++) for (int iz = 0; iz < 3; iz++) for (int cy = 0; cy < HYC; cy++) {
        const DatosChunk& d = sn.c[ix][cy][iz];
        int x0 = (ix - 1) * CS, z0 = (iz - 1) * CS;
        int xs = std::max(x0, -LM), xe = std::min(x0 + CS, CS + LM);
        int zs = std::max(z0, -LM), ze = std::min(z0 + CS, CS + LM);
        if (xs >= xe || zs >= ze) continue;
        int n = xe - xs;
        for (int ly = 0; ly < CS; ly++) for (int z = zs; z < ze; z++) {
            uint8_t* dst = &T[idx(xs + LM, cy * CS + ly, z + LM)];
            if (d.d) std::memcpy(dst, d.d->data() + IDX(xs - x0, ly, z - z0), (size_t)n);
            else if (d.uni == 255) std::memset(dst, cy > cyObj ? (int)AIRE : (int)PIEDRA, (size_t)n);   // chunk que falta
            else std::memset(dst, d.uni, (size_t)n);
        }
    }

    // 2) luz del cielo: cada columna recibe luz completa desde arriba hasta el primer bloque opaco
    int yTop = 0;
    for (int z = 0; z < LRX; z++) for (int x = 0; x < LRX; x++) {
        int l = LMAX;
        for (int y = LRY - 1; y >= 0; y--) {
            size_t i = idx(x, y, z);
            uint8_t t = T[i];
            if (t != AIRE && y + 1 > yTop) yTop = y + 1;
            int c = coste[t];
            if (c == 255) break;
            l -= c;
            if (l <= 0) break;
            S[i] = (uint8_t)l;
        }
    }
    int ylo = std::max(0, cyObj * CS - 1 - LM), yhi = std::min(LRY - 1, cyObj * CS + CS + LM);
    yhi = std::min(yhi, yTop + 1);
    q.clear();
    const size_t SY = (size_t)LRX * LRX;
    for (int y = ylo; y <= yhi; y++) for (int z = 1; z < LRX - 1; z++) for (int x = 1; x < LRX - 1; x++) {
        size_t i = idx(x, y, z);
        int s = S[i];
        if (s <= 1) continue;
        size_t v[6] = {i - 1, i + 1, i - LRX, i + LRX, i - SY, i + SY};
        for (int k = 0; k < 6; k++) {
            if (k == 4 && y == 0) continue;
            if (k == 5 && y == LRY - 1) continue;
            int c = coste[T[v[k]]];
            if (c != 255 && S[v[k]] + 1 + c < s) { q.push_back((uint32_t)i); break; }
        }
    }
    luzBFS(S.data(), T.data(), coste, q);

    // 3) luz de bloque con color: antorchas y bloques LED. Se reparte aparte en rojo, verde y azul (se suman, como la luz real)
    {
        const uint8_t* base = T.data();
        const uint8_t* fin = base + LRN;
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
        for (int k = 0; k < 12; k++) fuente((uint8_t)(LED_ROJO + k), LLED, LED_RGB[k]);
        luzBFS(BR.data(), T.data(), coste, qr);
        luzBFS(BG.data(), T.data(), coste, qg);
        luzBFS(BB.data(), T.data(), coste, qb);
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

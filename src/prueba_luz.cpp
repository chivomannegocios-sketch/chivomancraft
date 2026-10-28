// Prueba sin ventana del calculo de luz: tiempos y comprobaciones basicas.
#include "luz.h"
#include <chrono>
#include <cstdio>
int main() {
    int cx0 = 0, cz0 = 0;
    SnapColumnas sn;
    for (int ix = 0; ix < 3; ix++) for (int iz = 0; iz < 3; iz++) for (int cy = 0; cy < HYC; cy++)
        sn.c[ix][cy][iz] = generarChunk(cx0 + ix - 1, cy, cz0 + iz - 1);
    std::vector<uint8_t> out((size_t)LN * LN * LN * 4);
    for (int cy = 0; cy < HYC; cy++) {
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 5; k++) calcularLuz(sn, cy, out.data());
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 5;
        long cielo = 0, bloque = 0, total = 0; int maxS = 0;
        for (size_t i = 0; i < (size_t)LN * LN * LN; i++) { if (out[i * 4]) cielo++; if (out[i * 4 + 1] || out[i * 4 + 2] || out[i * 4 + 3]) bloque++; maxS = std::max<int>(maxS, out[i * 4]); total++; }
        std::printf("cy=%d  %.2f ms  celdas con cielo %.1f%%  con luz de bloque %ld  max cielo %d (LMAX %d)\n", cy, ms, 100.0 * cielo / total, bloque, maxS, LMAX);
    }
    // antorcha en una cueva: poner una en el centro de un chunk bajo tierra
    {
        auto& d = sn.c[1][1][1];
        if (!d.d) { d.d = std::make_shared<std::vector<uint8_t>>((size_t)CS * CS * CS, d.uni); }
        for (auto& v : *d.d) v = PIEDRA;
        for (int z = 10; z < 22; z++) for (int y = 10; y < 22; y++) for (int x = 10; x < 22; x++) (*d.d)[IDX(x, y, z)] = AIRE;
        (*d.d)[IDX(16, 12, 16)] = ANTORCHA;
        calcularLuz(sn, 1, out.data());
        auto L = [&](int x, int y, int z) { return out[((((size_t)(y + 1) * LN) + (z + 1)) * LN + (x + 1)) * 2 + 1]; };
        std::printf("antorcha: en la antorcha %d, a 1 cubito %d, a 6 cubitos %d, en la pared a 10 %d, fuera de la sala %d\n",
                    L(16, 12, 16), L(17, 12, 16), L(22 - 1, 12, 16), L(21, 12, 16), L(25, 12, 16));
    }
}

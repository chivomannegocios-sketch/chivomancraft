// Prueba del nucleo del mundo SIN ventana: genera chunks, los convierte en mallas y mide tiempos.
#include "mundo.h"
#include <cstdio>
#include <chrono>
#include <map>
#include <tuple>
int main() {
    using clk = std::chrono::steady_clock;
    const int R = 4;
    std::map<std::tuple<int,int,int>, DatosChunk> mapa;
    auto t0 = clk::now();
    int noUni = 0, total = 0;
    for (int cz = -R - 1; cz <= R + 1; cz++) for (int cx = -R - 1; cx <= R + 1; cx++) for (int cy = 0; cy < HYC; cy++) {
        mapa[{cx, cy, cz}] = generarChunk(cx, cy, cz);
        total++;
        if (mapa[{cx, cy, cz}].d) noUni++;
    }
    double tg = std::chrono::duration<double>(clk::now() - t0).count();
    auto t1 = clk::now();
    size_t verts = 0, tris = 0, aguaTris = 0;
    int mallas = 0;
    const int dirs[6][3] = {{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    for (int cz = -R; cz <= R; cz++) for (int cx = -R; cx <= R; cx++) for (int cy = 0; cy < HYC; cy++) {
        DatosChunk s[7];
        s[0] = mapa[{cx, cy, cz}];
        if (!s[0].d && s[0].uni == AIRE) continue;
        for (int k = 0; k < 6; k++) {
            int nx = cx + dirs[k][0], ny = cy + dirs[k][1], nz = cz + dirs[k][2];
            if (ny < 0) { s[k + 1].uni = PIEDRA; continue; }
            if (ny >= HYC) { s[k + 1].uni = AIRE; continue; }
            s[k + 1] = mapa[{nx, ny, nz}];
        }
        DatosMalla op, ag;
        construirMalla(s, op, ag);
        verts += op.v.size() + ag.v.size(); tris += op.i.size() / 3; aguaTris += ag.i.size() / 3; mallas++;
    }
    double tm = std::chrono::duration<double>(clk::now() - t1).count();
    printf("SUB=%d CS=%d HYC=%d RC=%d\n", SUB, CS, HYC, RC);
    printf("Chunks generados: %d (con datos: %d) en %.2f s\n", total, noUni, tg);
    printf("Mallas: %d | vertices %zu | triangulos %zu (agua %zu) en %.2f s\n", mallas, verts, tris, aguaTris, tm);
    printf("Altura en (0,0): %.1f bloques\n", alturaTerreno(0.5, 0.5));
    return tris > 0 ? 0 : 1;
}

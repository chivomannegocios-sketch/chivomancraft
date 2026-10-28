// Herramienta: dibuja un mapa aereo del terreno (PPM) para ajustar rios, lagunas y desiertos.
#include "mundo.h"
#include <cstdio>
int main(int argc, char** argv) {
    const int N = 1024;
    double cx = argc > 2 ? atof(argv[1]) : 0, cz = argc > 2 ? atof(argv[2]) : 0;
    FILE* f = std::fopen(argc > 3 ? argv[3] : "mapa.ppm", "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", N, N);
    long agua = 0, arena = 0, total = 0, rio = 0;
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        double bx = cx + x - N / 2, bz = cz + z - N / 2;
        double h = alturaTerreno(bx, bz);
        double d = desierto(bx, bz);
        unsigned char c[3];
        total++;
        if (h < WATER_LEVEL) { agua++; double p = std::min(1.0, (WATER_LEVEL - h) / 6.0); c[0] = 40 - 20 * p; c[1] = 90 - 40 * p; c[2] = 200 - 60 * p; }
        else if (h < WATER_LEVEL + 2.4 || d > 0.5) { arena++; c[0] = 225; c[1] = 205; c[2] = 140; }
        else if (h > 62) { c[0] = c[1] = c[2] = 140 + (h - 62) * 3; }
        else { double t = (h - 20) / 40.0; c[0] = 60 + 40 * t; c[1] = 160 - 40 * t; c[2] = 50; }
        std::fwrite(c, 1, 3, f);
    }
    std::fclose(f);
    std::printf("agua %.1f%%  arena %.1f%%\n", 100.0 * agua / total, 100.0 * arena / total);
}

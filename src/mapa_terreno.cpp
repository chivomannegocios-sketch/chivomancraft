// Herramienta: dibuja un mapa aereo del terreno (PPM) con los biomas: ./mapa cx cz salida.ppm [paso]
#include "mundo.h"
#include <cstdio>
int main(int argc, char** argv) {
    const int N = 1000;
    double cx = argc > 2 ? atof(argv[1]) : 0, cz = argc > 2 ? atof(argv[2]) : 0;
    int paso = argc > 4 ? atoi(argv[4]) : 3;
    FILE* f = std::fopen(argc > 3 ? argv[3] : "mapa.ppm", "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", N, N);
    long cuenta[BIO_N] = {0}, agua = 0, roca = 0, total = 0;
    static const unsigned char COL[BIO_N][3] = {{150, 205, 90}, {50, 140, 50}, {20, 90, 35}, {235, 215, 130}, {200, 120, 70}, {90, 110, 70}, {235, 240, 250}, {190, 170, 80}, {210, 110, 30}, {200, 215, 230}, {240, 160, 190}, {180, 70, 40}, {0, 110, 30}, {120, 160, 150}, {255, 120, 220}, {150, 220, 255}, {245, 235, 160}, {30, 70, 150}};
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        double bx = cx + (x - N / 2) * paso, bz = cz + (z - N / 2) * paso;
        int dom; double h = terrenoBase(bx, bz, &dom);
        unsigned char c[3]; total++;
        if (h < WATER_LEVEL) { agua++; double p = std::min(1.0, (WATER_LEVEL - h) / 6.0); c[0] = 40 - 20 * p; c[1] = 90 - 40 * p; c[2] = 200 - 60 * p; }
        else {
            cuenta[dom]++;
            for (int k = 0; k < 3; k++) c[k] = COL[dom][k];
            if (tipoSuperficie(dom, h, bx, bz, false) == PIEDRA) { roca++; c[0] = c[1] = c[2] = 130 + std::min(100.0, (h - 52) * 4); }
            if (tipoSuperficie(dom, h, bx, bz, false) == NIEVE) { c[0] = 240; c[1] = 245; c[2] = 255; }
            double sh = 0.75 + 0.25 * std::min(1.0, std::max(0.0, (h - 15) / 50.0));
            for (int k = 0; k < 3; k++) c[k] = (unsigned char)(c[k] * sh);
        }
        std::fwrite(c, 1, 3, f);
    }
    std::fclose(f);
    static const char* NB[BIO_N] = {"pradera", "bosque", "bosque denso", "desierto", "meseta", "pantano", "tundra", "sabana", "otono", "picos", "cerezo", "canon", "selva", "taiga", "flores", "glaciar", "playa", "oceano"};
    std::printf("agua %.1f%%  roca %.1f%%\n", 100.0 * agua / total, 100.0 * roca / total);
    for (int b = 0; b < BIO_N; b++) std::printf("  %-13s %.1f%%\n", NB[b], 100.0 * cuenta[b] / total);
}

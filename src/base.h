// base.h (v63): la base subterranea de la Luna, generada con codigo (no se guarda en archivos: se calcula al crear cada chunk).
// Coordenadas locales: x = lateral, d = profundidad hacia dentro de la colina (hacia -Z del mundo), y = altura sobre el suelo (0 = primer bloque de aire).
// Todo se diseña en BLOQUES; algunas piezas (escaleras) en cubitos.
#pragma once

constexpr int BASE_X = 520, BASE_Z = -380;                 // centro de la puerta (bloques del mundo lunar)
constexpr double LUNA_Y0 = 106.0;                         // altura media del suelo lunar (bloques)
static inline int baseCX(int lx) { return BASE_X * SUB + lx; }
static inline int baseCZ(int ld) { return BASE_Z * SUB - ld; }
static inline int baseCY(int ly) { return (int)(LUNA_Y0 * SUB) + ly; }

struct BBox { int x0, x1, d0, d1, y0, y1; uint8_t t; int k = 0, ri = 0, p0 = 0, p1 = 0, p2 = 0, p3 = 0, pat = 0, ro = 0, hs = 0; };   // k: 0 caja, 1 cilindro/anillo vertical (ri = radio interior), 2 banda recta (ri = semiancho)    // en cubitos, relativos a la puerta
struct BaseDis { std::vector<BBox> sh, ca, dc; int x0 = 1 << 30, x1 = -(1 << 30), d0 = 1 << 30, d1 = -(1 << 30), y0 = 1 << 30, y1 = -(1 << 30); };

// lamparas del almacen (bloques): f(x, d, y, tipo_encendido). El diseno las pone APAGADAS; el juego las enciende al caer el mecha (v69)
template <class F> static void almacenLamparas(F f) {
    const int W0 = 201, W1 = 298;
    for (int x = -48; x <= 48; x += 12) for (int d = W0 + 6; d <= W1 - 4; d += 10) f(x, d, -19, ((x / 12 + d / 10) & 1) ? PLASMA_CELESTE : PLASMA_FRIO);
    for (int x = -42; x <= 42; x += 12) for (int d = W0 + 11; d <= W1 - 4; d += 20) f(x, d, -19, PLASMA_ROJO);
    for (int x = -42; x <= 42; x += 12) for (int d = W0 + 21; d <= W1 - 4; d += 20) f(x, d, -19, PLASMA_CELESTE);                       // techo, mas densidad
    for (int d = W0 + 4; d <= W1 - 2; d += 8) { f(-50, d, -34, PLASMA_CELESTE); f(50, d, -34, PLASMA_CELESTE); f(-50, d, -27, PLASMA_FRIO); f(50, d, -27, PLASMA_FRIO); }   // paredes laterales
    for (int x = -40; x <= 40; x += 8) { f(x, W0, -34, PLASMA_CELESTE); f(x, W1, -34, PLASMA_CELESTE); f(x, W0, -27, PLASMA_FRIO); f(x, W1, -27, PLASMA_FRIO); }       // paredes del fondo
    for (int x : {-36, -12, 12, 36}) for (int d = W0 + 20; d <= W1 - 4; d += 24) f(x, d, -48, PLASMA_FRIO);                            // balizas en el suelo
    for (int cx : {-36, -18, 18, 36}) for (int cd : {222, 250, 278}) for (int y : {-30, -29}) { f(cx - 2, cd, y, PLASMA_ROJO); f(cx + 2, cd, y, PLASMA_ROJO); f(cx, cd - 2, y, PLASMA_ROJO); f(cx, cd + 2, y, PLASMA_ROJO); }
}
static BaseDis baseConstruir() {
    BaseDis D;
    // bloques -> cubitos (inclusivo)
    auto B = [&](std::vector<BBox>& v, int x0, int x1, int d0, int d1, int y0, int y1, uint8_t t) {
        v.push_back({x0 * SUB, (x1 + 1) * SUB - 1, d0 * SUB, (d1 + 1) * SUB - 1, y0 * SUB, (y1 + 1) * SUB - 1, t});
    };
    auto C = [&](std::vector<BBox>& v, int x0, int x1, int d0, int d1, int y0, int y1, uint8_t t) { v.push_back({x0, x1, d0, d1, y0, y1, t}); };
    auto& sh = D.sh; auto& ca = D.ca; auto& dc = D.dc;
    const uint8_t CON = BASE_CONCRETO, MET = BASE_METAL, REJ = BASE_REJILLA, TUB = BASE_TUBERIA, PUE = BASE_PUERTA, TEC = BASE_TECLADO,
                  PAN = BASE_PANTALLA, FRA = BASE_FRANJA, PIS = BASE_PISO, PLA = BASE_PLACA, AIR = AIRE;

    // ================= fachada de concreto con alas inclinadas (como un bunker enterrado en la colina)
    B(sh, -10, 10, 0, 3, -1, 7, CON);
    for (int dd = 1; dd <= 10; dd++) {
        const int top = 6 - (dd - 1) * 6 / 10;
        B(sh, -10, -9, -dd, -dd, -1, top, CON);
        B(sh, 9, 10, -dd, -dd, -1, top, CON);
    }
    B(dc, -8, 8, -10, -1, -1, -1, CON);                     // plaza de concreto delante de la puerta
    B(ca, -3, 2, 0, 3, 0, 4, AIR);                           // hueco de la puerta
    B(ca, -6, -4, 1, 1, 0, 4, AIR);                          // huecos donde se esconden las dos hojas
    B(ca, 3, 5, 1, 1, 0, 4, AIR);
    B(dc, -3, 2, 0, 0, 5, 5, FRA);                           // franja de peligro sobre la puerta
    B(dc, -4, -4, 0, 0, 0, 4, FRA); B(dc, 3, 3, 0, 0, 0, 4, FRA);   // y a los lados
    B(dc, -3, -1, 1, 1, 0, 4, PUE);                          // hoja izquierda
    B(dc, 0, 2, 1, 1, 0, 4, PUE);                            // hoja derecha
    B(dc, 4, 4, 0, 0, 1, 1, TEC);                            // teclado a la derecha de la puerta
    B(dc, 4, 4, 0, 0, 2, 2, PAN);
    B(dc, 4, 4, 0, 0, 3, 3, PLASMA_CELESTE);
    B(dc, -6, -6, 0, 0, 1, 3, TUB); B(dc, 6, 6, 0, 0, 1, 3, TUB);      // tuberias en la fachada
    B(dc, -8, -8, -1, -1, 0, 0, PLA);
    // ================= esclusa (sala de entrada)
    B(sh, -6, 6, 3, 13, -1, 5, MET);
    B(ca, -5, 5, 4, 12, 0, 4, AIR);
    B(dc, -5, 5, 4, 12, -1, -1, PIS);
    B(dc, -3, 2, 4, 4, -1, -1, FRA);
    for (int d : {5, 8, 11}) { B(dc, -4, -3, d, d, 4, 4, PLASMA_FRIO); B(dc, 3, 4, d, d, 4, 4, PLASMA_FRIO); }
    B(dc, 0, 0, 12, 12, 4, 4, PLASMA_CELESTE);
    B(dc, -5, -5, 4, 12, 3, 3, TUB); B(dc, 5, 5, 4, 12, 3, 3, TUB); B(dc, -5, -5, 4, 12, 4, 4, TUB); B(dc, 5, 5, 4, 12, 4, 4, TUB);
    B(dc, 5, 5, 6, 7, 1, 2, PAN); B(dc, -5, -5, 8, 9, 1, 2, PAN);
    B(dc, -5, -5, 11, 12, 0, 1, PLA); B(dc, 5, 5, 11, 12, 0, 1, PLA);
    // ================= escalera larga hacia abajo (cada escalon sube/baja medio bloque: se camina sin saltar)
    const int nEsc = 16 * SUB;
    for (int k = 0; k < nEsc; k++) {
        const int dcu = 13 * SUB + k;
        C(sh, -4 * SUB, 4 * SUB + SUB - 1, dcu, dcu, -k - SUB, -k + 5 * SUB, MET);
        C(ca, -3 * SUB, 3 * SUB + SUB - 1, dcu, dcu, -k, -k + 4 * SUB + SUB - 1, AIR);
        C(dc, -3 * SUB, 3 * SUB + SUB - 1, dcu, dcu, -k - 1, -k - 1, PIS);
        if (k % 6 == 3) { C(dc, -4 * SUB + SUB - 1, -4 * SUB + SUB - 1, dcu, dcu, -k + 4, -k + 4, PLASMA_ROJO); C(dc, 4 * SUB, 4 * SUB, dcu, dcu, -k + 4, -k + 4, PLASMA_ROJO); }
        if (k % 12 == 0) { C(dc, -4 * SUB + SUB - 1, -4 * SUB + SUB - 1, dcu, dcu, -k + 1, -k + 1, TUB); C(dc, 4 * SUB, 4 * SUB, dcu, dcu, -k + 1, -k + 1, TUB); }
    }
    // descanso plano antes del hangar
    B(sh, -4, 4, 29, 35, -17, -11, MET);
    B(ca, -3, 3, 29, 35, -16, -12, AIR);
    B(dc, -3, 3, 29, 35, -17, -17, PIS);
    B(dc, -3, 3, 29, 29, -17, -17, FRA);
    for (int d : {31, 34}) { B(dc, -1, 0, d, d, -12, -12, PLASMA_FRIO); }
    B(dc, -3, -3, 29, 35, -13, -13, TUB); B(dc, 3, 3, 29, 35, -13, -13, TUB);

    // ================= hangar grande (pasarelas de rejilla, columnas, luces en el techo)
    B(sh, -17, 17, 35, 76, -18, -4, CON);
    B(ca, -16, 16, 36, 75, -16, -5, AIR);
    B(ca, -3, 3, 35, 36, -16, -12, AIR);                     // entrada desde el descanso
    B(dc, -16, 16, 36, 75, -17, -17, PIS);
    B(dc, -5, -5, 36, 75, -17, -17, FRA); B(dc, 5, 5, 36, 75, -17, -17, FRA);
    B(dc, -1, 0, 36, 75, -17, -17, PLA);
    for (int d : {44, 54, 64, 73}) for (int x : {-9, 8}) {
        B(dc, x, x + 1, d, d + 1, -16, -5, CON);
        B(dc, x - 1, x + 2, d - 1, d + 2, -16, -16, PLA);
    }
    // pasarelas de rejilla a los dos lados, con barandas y escaleras
    B(dc, -16, -13, 45, 74, -10, -10, REJ); B(dc, 13, 16, 45, 74, -10, -10, REJ);
    B(dc, -13, -13, 45, 74, -9, -9, TUB); B(dc, 13, 13, 45, 74, -9, -9, TUB);
    B(dc, -16, -13, 74, 75, -9, -9, TUB); B(dc, 13, 16, 74, 75, -9, -9, TUB);
    for (int k = 0; k < 7 * SUB; k++) {              // escalera a la pasarela (7 bloques de subida)
        C(dc, -16 * SUB, -14 * SUB + SUB - 1, 37 * SUB + k, 37 * SUB + k, -16 * SUB - 2, -16 * SUB + k, PLA);
        C(dc, 14 * SUB, 16 * SUB + SUB - 1, 37 * SUB + k, 37 * SUB + k, -16 * SUB - 2, -16 * SUB + k, PLA);
    }
    for (int x : {-12, 11}) for (int d : {41, 51, 61, 70}) B(dc, x, x + 1, d, d + 1, -5, -5, PLASMA_FRIO);
    for (int x : {-4, 3}) for (int d : {41, 51, 61, 70}) B(dc, x, x + 1, d, d + 1, -5, -5, PLASMA_CELESTE);
    B(dc, -15, -15, 36, 75, -6, -6, TUB); B(dc, 15, 15, 36, 75, -6, -6, TUB); B(dc, -8, -8, 36, 75, -5, -5, TUB); B(dc, 7, 7, 36, 75, -5, -5, TUB);
    B(dc, -16, -16, 52, 58, -8, -7, PAN); B(dc, 16, 16, 52, 58, -8, -7, PAN);          // pantallas en las paredes
    for (int d : {60, 66}) { B(dc, -15, -13, d, d + 1, -16, -15, PLA); B(dc, 13, 15, d - 2, d - 1, -16, -15, PLA); }   // cajas
    B(dc, -4, -4, 75, 75, -16, -12, FRA); B(dc, 3, 3, 75, 75, -16, -12, FRA); B(dc, -4, 3, 75, 75, -11, -11, FRA);   // marco de la salida del fondo
    // ================= pasillo largo hacia las salas
    B(sh, -3, 2, 75, 111, -17, -11, MET);
    B(ca, -2, 1, 75, 110, -16, -12, AIR);
    B(dc, -2, 1, 75, 110, -17, -17, PIS);
    for (int d = 79; d <= 109; d += 4) B(dc, -1, 0, d, d, -12, -12, PLASMA_FRIO);
    B(dc, -2, -2, 76, 110, -13, -13, TUB); B(dc, 1, 1, 76, 110, -13, -13, TUB);
    B(dc, -2, -2, 100, 101, -16, -15, PAN); B(dc, 1, 1, 104, 105, -16, -15, PAN);
    for (int d : {85, 97}) { B(dc, -2, 1, d, d, -17, -17, FRA); }
    // sala de control (izquierda)
    B(sh, -16, -4, 80, 98, -17, -10, MET);
    B(ca, -15, -5, 81, 97, -16, -11, AIR);
    B(ca, -5, -3, 86, 89, -16, -13, AIR);
    B(dc, -15, -5, 81, 97, -17, -17, PIS);
    B(dc, -15, -15, 83, 95, -15, -12, PAN);
    for (int d : {83, 88, 93}) { B(dc, -13, -8, d, d, -16, -16, PLA); B(dc, -13, -8, d, d, -15, -15, PAN); }
    for (int d : {83, 89, 95}) B(dc, -10, -10, d, d, -11, -11, PLASMA_CELESTE);
    B(dc, -6, -6, 81, 97, -11, -11, PLASMA_CELESTE);
    B(dc, -5, -5, 81, 97, -13, -13, TUB);
    // laboratorio (derecha)
    B(sh, 3, 15, 80, 98, -17, -10, MET);
    B(ca, 4, 14, 81, 97, -16, -11, AIR);
    B(ca, 2, 4, 86, 89, -16, -13, AIR);
    B(dc, 4, 14, 81, 97, -17, -17, PIS);
    for (int d : {84, 92}) { B(dc, 7, 9, d, d + 2, -16, -12, VIDRIO); B(dc, 8, 8, d + 1, d + 1, -16, -13, PLASMA_VERDE); B(dc, 7, 9, d, d + 2, -11, -11, PLA); }
    B(dc, 12, 14, 83, 95, -16, -15, PLA);
    for (int d : {83, 89, 95}) B(dc, 10, 10, d, d, -11, -11, PLASMA_VERDE);
    B(dc, 6, 6, 81, 97, -11, -11, PLASMA_VERDE);
    B(dc, 14, 14, 85, 93, -14, -12, PAN);
    // ================= camara central de la IA
    B(sh, -16, 15, 109, 139, -18, -5, MET);
    B(ca, -15, 14, 110, 138, -16, -6, AIR);
    B(dc, -15, 14, 110, 138, -17, -17, PIS);
    B(dc, -1, 0, 110, 118, -17, -17, FRA);
    B(dc, -5, 4, 118, 129, -16, -16, PLA);                   // plataforma central
    B(dc, -4, 3, 119, 128, -15, -15, PIS);
    B(dc, -3, 2, 120, 127, -14, -14, PLA);
    B(dc, -1, 0, 123, 124, -13, -6, PLASMA_CELESTE);            // el nucleo de la IA
    for (int y : {-13, -10, -7}) B(dc, -2, 1, 122, 125, y, y, PLA);
    B(dc, -3, 2, 121, 126, -6, -6, PLA);
    for (int x : {-13, 12}) for (int d : {113, 134}) { B(dc, x, x + 1, d, d + 1, -16, -7, PLA); B(dc, x, x + 1, d, d + 1, -12, -9, PLASMA_ROJO); }
    B(dc, -14, 13, 138, 138, -14, -8, PAN);
    for (int d : {113, 120, 127, 134}) { B(dc, -8, -7, d, d, -6, -6, PLASMA_ROJO); B(dc, 6, 7, d, d, -6, -6, PLASMA_ROJO); }
    B(dc, -15, -15, 110, 138, -9, -9, TUB); B(dc, 14, 14, 110, 138, -9, -9, TUB);


    // ================= SILO VERTICAL (v64): pozo circular con torre roja central, pasarelas en X y pasillo anular
    {
        const int SD = 170, R = 26, RI = 22;
        const uint8_t ACE = BASE_ACERO, MUR = BASE_MURO, CAB = BASE_CABLE, AVI = BASE_AVISO, ARM = BASE_ARMARIO, AMB = PLASMA_AMBAR,
                      PFR = PLASMA_FRIO, PRO = PLASMA_ROJO, RRD = BASE_REJ_R;
        auto CIL = [&](std::vector<BBox>& v, int cx, int cd, int rOut, int rIn, int y0, int y1, uint8_t t) {      // en bloques
            BBox b{}; const int Rc = rOut * SUB;
            b.x0 = cx * SUB - Rc; b.x1 = cx * SUB + Rc - 1; b.d0 = cd * SUB - Rc; b.d1 = cd * SUB + Rc - 1;
            b.y0 = y0 * SUB; b.y1 = (y1 + 1) * SUB - 1; b.t = t; b.k = 1; b.ri = rIn * SUB; b.p0 = cx * SUB; b.p1 = cd * SUB;
            v.push_back(b);
        };
        auto BAND = [&](std::vector<BBox>& v, int ax, int ad, int bx, int bd, int hwC, int y0, int y1, uint8_t t) {   // extremos en bloques, semiancho en cubitos
            BBox b{}; b.x0 = std::min(ax, bx) * SUB - hwC; b.x1 = std::max(ax, bx) * SUB + hwC; b.d0 = std::min(ad, bd) * SUB - hwC; b.d1 = std::max(ad, bd) * SUB + hwC;
            b.y0 = y0 * SUB; b.y1 = (y1 + 1) * SUB - 1; b.t = t; b.k = 2; b.ri = hwC; b.p0 = ax * SUB; b.p1 = ad * SUB; b.p2 = bx * SUB; b.p3 = bd * SUB;
            v.push_back(b);
        };
        CIL(sh, 0, SD, R, 0, -72, -3, MUR);                      // cilindro macizo
        CIL(ca, 0, SD, RI, 0, -70, -6, AIR);                     // el pozo
        // pasillo desde la camara de la IA
        B(sh, -4, 4, 139, 148, -18, -10, MET);
        B(ca, -2, 2, 139, 150, -16, -12, AIR);
        B(dc, -2, 2, 139, 148, -17, -17, PIS);
        B(dc, -2, 2, 139, 148, -17, -17, PIS);
        B(dc, -1, 1, 140, 148, -17, -17, FRA);
        for (int d : {141, 145}) { B(dc, -2, -2, d, d, -12, -12, PFR); B(dc, 2, 2, d, d, -12, -12, PFR); }
        // la puerta de la camara hacia el pasillo (se repinta despues de las pantallas de la pared)
        B(dc, -3, -3, 138, 139, -16, -11, FRA); B(dc, 3, 3, 138, 139, -16, -11, FRA); B(dc, -3, 3, 138, 139, -11, -11, FRA);
        B(dc, -2, 2, 138, 139, -16, -12, AIR);
        B(dc, -3, 3, 137, 137, -10, -10, PRO);
        // ---- helpers en cubitos (rejillas y barandas finas de 1 cubito)
        auto CILC = [&](std::vector<BBox>& v, int rOut, int rIn, int y0, int y1, uint8_t t, int pat) {      // centrado en el eje del silo; todo en cubitos
            BBox bb{}; const int cx = 0, cd = SD * SUB;
            bb.x0 = cx - rOut; bb.x1 = cx + rOut - 1; bb.d0 = cd - rOut; bb.d1 = cd + rOut - 1; bb.y0 = y0; bb.y1 = y1; bb.t = t;
            bb.k = 1; bb.ri = rIn; bb.ro = rOut; bb.p0 = cx; bb.p1 = cd; bb.pat = pat; v.push_back(bb);
        };
        auto BANDC = [&](std::vector<BBox>& v, int ax, int ad, int bx, int bd, int hw, int y0, int y1, uint8_t t, int pat) {   // extremos y semiancho en cubitos
            BBox bb{}; bb.x0 = std::min(ax, bx) - hw - 1; bb.x1 = std::max(ax, bx) + hw + 1; bb.d0 = std::min(ad, bd) - hw - 1; bb.d1 = std::max(ad, bd) + hw + 1;
            bb.y0 = y0; bb.y1 = y1; bb.t = t; bb.k = 2; bb.ri = hw; bb.p0 = ax; bb.p1 = ad; bb.p2 = bx; bb.p3 = bd; bb.pat = pat; v.push_back(bb);
        };
        const int YP = -17 * SUB + 1;                              // cubito de arriba del suelo (la rejilla es de 1 cubito: se ve el fondo por los huecos)
        const int YR0 = -16 * SUB, YR1 = -16 * SUB + 3;           // baranda: 4 cubitos de alto
        const int C0 = SD * SUB;
        // anillo de paseo y baranda
        CIL(dc, 0, SD, 22, 16, -17, -17, PIS);
        CILC(dc, 17 * SUB, 16 * SUB - 1, -17 * SUB + 1, -17 * SUB + 1, FRA, 0);
        CILC(dc, 16 * SUB, 16 * SUB - 1, YR0, YR1, TUB, 1);
        // plataforma central (rejilla de cubitos) con baranda y cabina de ascensor
        CIL(dc, 0, SD, 7, 0, -17, -17, PLA);
        CILC(dc, 7 * SUB, 0, YP, YP, REJ, 5);
        CILC(dc, 7 * SUB, 7 * SUB - 1, YR0, YR1, TUB, 1);
        // pasarelas en cruz de cubitos hacia la entrada (sur) y las tres puertas
        const int dg[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (auto& g : dg) {
            const int r0 = 7 * SUB, r1 = 16 * SUB;
            const int ax = g[0] * r0, ad = C0 + g[1] * r0, bx = g[0] * r1, bd = C0 + g[1] * r1;
            const int lx = g[1] != 0 ? 1 : 0, ld = g[1] != 0 ? 0 : 1;            // lado de la pasarela
            BANDC(dc, ax, ad, bx, bd, 6, YR0, YR1 + 4, AIR, 0);                   // hueco en las barandas
            BANDC(dc, ax, ad, bx, bd, 4, YP, YP, REJ, g[1] != 0 ? 2 : 3);       // rejilla (se ve el fondo)
            BANDC(dc, ax, ad, bx, bd, 4, -17 * SUB, -17 * SUB, AIR, 0);          // y por debajo, nada
            const int r1b = 16 * SUB - 4;
            for (int s : {-1, 1}) {                                               // barandas laterales de 1 cubito (a 5.5 cubitos del centro)
                const int ox = lx * s * 11, od = ld * s * 11, rx = g[0] * r1b, rd = g[1] * r1b;
                BBox bb{}; bb.x0 = std::min(ax, rx) - 7; bb.x1 = std::max(ax, rx) + 7; bb.d0 = std::min(ad, C0 + rd) - 7; bb.d1 = std::max(ad, C0 + rd) + 7;
                bb.y0 = YR0; bb.y1 = YR1; bb.t = TUB; bb.k = 2; bb.ri = 0; bb.hs = 1; bb.pat = 4;
                bb.p0 = ax * 2 + ox; bb.p1 = ad * 2 + od; bb.p2 = rx * 2 + ox; bb.p3 = (C0 + rd) * 2 + od;
                dc.push_back(bb);
            }
        }
        // cabina del ascensor del centro (metal con rojo), puerta hacia la entrada
        {
            const int H = 5;                                       // medio lado en cubitos (cabina de 10x10 cubitos)
            const int yA = -16 * SUB, yB = -8 * SUB;
            C(dc, -H, H - 1, C0 - H, C0 + H - 1, -17 * SUB, -17 * SUB + 1, PLA);                  // piso de la cabina
            C(dc, -H, H - 1, C0 - H, C0 - H + 1, yA, yB, MET); C(dc, -H, H - 1, C0 + H - 2, C0 + H - 1, yA, yB, MET);
            C(dc, -H, -H + 1, C0 - H, C0 + H - 1, yA, yB, MET); C(dc, H - 2, H - 1, C0 - H, C0 + H - 1, yA, yB, MET);
            C(dc, -H, H - 1, C0 - H, C0 + H - 1, yB + 1, yB + 2, MET);                            // techo
            for (int sx : {-H, H - 2}) for (int sd : {C0 - H, C0 + H - 2}) C(dc, sx, sx + 1, sd, sd + 1, -17 * SUB, yB + 2, ACE);   // postes rojos
            C(dc, -H, H - 1, C0 - H, C0 - H + 1, yA + 7, yA + 7, ACE); C(dc, -H, H - 1, C0 - H, C0 - H + 1, yA, yA, ACE);          // franjas rojas
            // puerta: dos hojas de acero rojo con marco metalico (cara sur)
            C(dc, -3, 2, C0 - H, C0 - H + 1, yA, yA + 6, ACE);
            C(dc, -1, 0, C0 - H, C0 - H + 1, yA, yA + 6, MET);                                      // juntura central
            C(dc, -4, -4, C0 - H, C0 - H + 1, yA, yA + 7, FRA); C(dc, 3, 3, C0 - H, C0 - H + 1, yA, yA + 7, FRA); C(dc, -4, 3, C0 - H, C0 - H + 1, yA + 7, yA + 7, FRA);
            C(dc, -1, 0, C0 - H - 1, C0 - H - 1, yA + 9, yA + 9, PRO); C(dc, 3, 3, C0 - H - 1, C0 - H - 1, yA + 3, yA + 4, PAN);
            C(dc, -H, -H + 1, C0 - H - 1, C0 - H - 1, yB + 3, yB + 3, PRO); C(dc, H - 2, H - 1, C0 - H - 1, C0 - H - 1, yB + 3, yB + 3, PRO);
        }
        // armazon de la torre por debajo de la plataforma: 4 postes rojos, anillos y diagonales, y cables
        {
            const int H = 5;
            for (int sx : {-H, H - 2}) for (int sd : {C0 - H, C0 + H - 2}) C(dc, sx, sx + 1, sd, sd + 1, -72 * SUB, -17 * SUB - 1, ACE);
            for (int y = -70; y <= -22; y += 6) {
                const int ya = y * SUB;
                C(dc, -H, H - 1, C0 - H, C0 - H, ya, ya + 1, ACE); C(dc, -H, H - 1, C0 + H - 1, C0 + H - 1, ya, ya + 1, ACE);
                C(dc, -H, -H, C0 - H, C0 + H - 1, ya, ya + 1, ACE); C(dc, H - 1, H - 1, C0 - H, C0 + H - 1, ya, ya + 1, ACE);
                for (int t = 0; t < 2 * H; t++) {
                    const int y1 = ya + t * (6 * SUB) / (2 * H), y2 = ya + (2 * H - 1 - t) * (6 * SUB) / (2 * H);
                    const int xx = -H + t, zz = C0 - H + t;
                    C(dc, xx, xx, C0 - H, C0 - H, y1, y1 + 1, ACE); C(dc, xx, xx, C0 - H, C0 - H, y2, y2 + 1, ACE);
                    C(dc, xx, xx, C0 + H - 1, C0 + H - 1, y1, y1 + 1, ACE); C(dc, xx, xx, C0 + H - 1, C0 + H - 1, y2, y2 + 1, ACE);
                    C(dc, -H, -H, zz, zz, y1, y1 + 1, ACE); C(dc, -H, -H, zz, zz, y2, y2 + 1, ACE);
                    C(dc, H - 1, H - 1, zz, zz, y1, y1 + 1, ACE); C(dc, H - 1, H - 1, zz, zz, y2, y2 + 1, ACE);
                }
            }
            for (int i = 0; i < 12; i++) {                         // cables que cuelgan de la plataforma hasta el fondo
                const double a = i * 6.2831853 / 12.0;
                const int cx = (int)std::lround(std::cos(a) * 8.0), cz = C0 + (int)std::lround(std::sin(a) * 8.0);
                C(dc, cx, cx, cz, cz, -70 * SUB, -17 * SUB - 1, CAB);
            }
        }
        // costillas rojas de la pared del pozo con lamparas ambar
        for (int y = -26; y >= -68; y -= 6) {
            CIL(dc, 0, SD, 22, 21, y, y, ACE);
            for (int a = 0; a < 4; a++) { const int px = (a == 0 ? 20 : a == 1 ? -20 : 0), pz = (a == 2 ? 20 : a == 3 ? -20 : 0); B(dc, px, px, SD + pz, SD + pz, y + 1, y + 1, AMB); }
        }
        for (int a = 0; a < 8; a++) {                            // columnas de acero en la pared
            const double an = a * 6.2831853 / 8.0 + 0.3927;
            const int px = (int)std::lround(std::cos(an) * 21.0), pz = (int)std::lround(std::sin(an) * 21.0);
            B(dc, px, px, SD + pz, SD + pz, -69, -18, ACE);
        }
        // pared alta: tuberia anular, luces, puertas, avisos y armarios
        CIL(dc, 0, SD, 22, 21, -8, -8, TUB);
        for (int a = 0; a < 8; a++) {
            const double an = a * 6.2831853 / 8.0;
            const int px = (int)std::lround(std::cos(an) * 20.0), pz = (int)std::lround(std::sin(an) * 20.0);
            B(dc, px, px, SD + pz, SD + pz, -7, -7, (a % 2) ? PFR : AMB);
        }
        struct P { int x, d, dx, dd; };                           // puertas en el este, el oeste y el fondo (dx,dd = direccion a lo largo de la pared)
        for (P p : {P{-22, SD, 0, 1}, P{21, SD, 0, 1}, P{0, SD + 22, 1, 0}}) {
            for (int w = -3; w <= 3; w++) {
                const int xx = p.x + p.dx * w, dd = p.d + p.dd * w;
                B(dc, xx, xx, dd, dd, -16, -10, FRA);
                if (w >= -2 && w <= 2) B(dc, xx, xx, dd, dd, -16, -11, PUE);
                B(dc, xx, xx, dd, dd, -9, -9, PRO);
            }
            const int sx = p.x + p.dx * 6, sd = p.d + p.dd * 6;
            B(dc, sx, sx, sd, sd, -15, -14, ARM); B(dc, sx + p.dx, sx + p.dx, sd + p.dd, sd + p.dd, -15, -14, ARM);
            const int ax_ = p.x - p.dx * 6, ad_ = p.d - p.dd * 6;
            B(dc, ax_, ax_ + p.dx, ad_, ad_ + p.dd, -13, -12, AVI);
            B(dc, p.x + p.dx * 4, p.x + p.dx * 4, p.d + p.dd * 4, p.d + p.dd * 4, -12, -12, AMB);
        }
        B(dc, 6, 7, SD - 21, SD - 21, -13, -12, AVI); B(dc, -7, -6, SD - 21, SD - 21, -13, -12, AVI);
    }

    // ================= SALA DE LA IA (v67): tunel desde la puerta norte del silo, sala amplia, pantalla azul, consolas y hangar del mecha
    {
        const uint8_t ACE = BASE_ACERO, MUR = BASE_MURO, AVI = BASE_AVISO, PRO = PLASMA_ROJO, PFR = PLASMA_FRIO, PCE = PLASMA_CELESTE, TUBx = BASE_TUBERIA,
                      CAB = BASE_CABLE, ARM = BASE_ARMARIO, CNS = BASE_CONSOLA, BTN = BASE_BOTON, IAO = BASE_IA_OFF;
        const int SD = 170, A0 = SD + 32, A1 = SD + 78, PW = SD + 79;           // A0..A1: suelo libre; PW: pared de la pantalla
        B(sh, -27, 27, SD + 23, PW + 2, -18, -3, MUR);
        B(sh, 26, 44, 217, 235, -18, -3, MUR);
        // tunel
        B(ca, -2, 2, SD + 23, A0 - 1, -16, -12, AIR);
        B(dc, -2, 2, SD + 23, A0 - 1, -17, -17, PIS); B(dc, -1, 1, SD + 23, A0 - 1, -17, -17, FRA);
        for (int d : {SD + 26, SD + 30}) { B(dc, -2, -2, d, d, -12, -12, PFR); B(dc, 2, 2, d, d, -12, -12, PFR); }
        B(dc, -3, -3, SD + 23, A0 - 1, -13, -13, TUBx); B(dc, 3, 3, SD + 23, A0 - 1, -13, -13, TUBx);
        // sala
        B(ca, -25, 25, A0, A1 + 1, -16, -5, AIR);
        B(dc, -25, 25, A0, A1 + 1, -17, -17, PIS);
        B(dc, -1, 1, A0, A1 - 6, -17, -17, FRA);                              // camino de franjas hasta el pedestal del boton
        for (int x : {-25, 25}) { B(dc, x, x, A0, A1 + 1, -6, -6, TUBx); B(dc, x, x, A0, A1 + 1, -15, -15, ACE); B(dc, x, x, A0, A1 + 1, -5, -5, ACE); }
        for (int d = A0 + 4; d <= A1; d += 10) { B(dc, -25, -25, d, d + 1, -13, -12, AVI); B(dc, 25, 25, d + 4, d + 4, -16, -14, ARM); B(dc, -25, -25, d + 5, d + 5, -16, -14, ARM); }
        for (int x : {-20, -10, 0, 10, 20}) for (int d : {SD + 38, SD + 52, SD + 66, SD + 75}) B(dc, x, x, d, d, -5, -5, PCE);   // plafones celestes
        // columnas de acero rojo
        for (int sx : {-12, 12}) for (int sd : {SD + 48, SD + 64}) { B(dc, sx - 1, sx + 1, sd - 1, sd + 1, -47, -6, ACE); B(dc, sx, sx, sd, sd, -5, -5, PCE); B(dc, sx - 1, sx + 1, sd - 1, sd + 1, -16, -16, FRA); }
        // pared de la pantalla de la IA: marco, pantalla (apagada hasta pulsar el boton), luces y consolas
        B(dc, -16, 16, PW, PW, -15, -4, MUR);
        B(dc, -15, 15, PW, PW, -15, -5, FRA);
        B(dc, -14, 14, PW, PW, -14, -6, IAO);
        for (int s : {-1, 1}) { B(dc, s * 17, s * 17, PW, PW, -14, -6, PCE); B(dc, s * 19, s * 19, PW, PW, -14, -6, ACE); }
        B(dc, -4, 4, PW, PW, -4, -4, AVI);
        for (int s : {-1, 1}) { B(dc, s * 4, s * 12, PW - 1, PW - 1, -16, -15, CNS); B(dc, s * 4, s * 12, PW - 2, PW - 2, -15, -15, CNS); B(dc, s * 13, s * 13, PW - 1, PW - 1, -16, -16, CAB); }
        for (int s : {-1, 1}) for (int d : {PW - 6, PW - 10}) B(dc, s * 22, s * 24, d, d + 1, -16, -15, CNS);
        // pedestal con el boton
        B(dc, -1, 1, PW - 8, PW - 7, -16, -15, CNS);
        B(dc, 0, 0, PW - 8, PW - 8, -14, -14, BTN);
        B(dc, -2, 2, PW - 10, PW - 10, -17, -17, FRA);
        // puerta grande del hangar (pared este) y hangar del mecha
        B(ca, 27, 43, 219, 233, -16, -6, AIR);
        B(dc, 27, 43, 219, 233, -17, -17, PIS);
        B(dc, 26, 26, 219, 219, -16, -5, FRA); B(dc, 26, 26, 233, 233, -16, -5, FRA); B(dc, 26, 26, 219, 233, -5, -5, FRA);
        B(dc, 26, 26, 220, 232, -16, -6, ACE); B(dc, 26, 26, 226, 226, -16, -6, MUR);
        B(dc, 26, 26, 220, 232, -11, -11, FRA); B(dc, 25, 25, 218, 218, -16, -14, AVI); B(dc, 25, 25, 234, 234, -16, -14, AVI);
        for (int d : {222, 226, 230}) { B(dc, 25, 25, d, d, -7, -7, PRO); }
        for (int x : {30, 36, 42}) for (int d : {219, 233}) B(dc, x, x, d, d, -7, -7, PRO);
        B(dc, 27, 43, 219, 219, -17, -17, FRA); B(dc, 27, 43, 233, 233, -17, -17, FRA);
    }

    // ================= ALMACEN SUBTERRANEO (v68): 4 veces el area de la sala de la IA, debajo de ella; el suelo de la sala se hunde cuando sale el mecha
    {
        const uint8_t ACE = BASE_ACERO, MUR = BASE_MURO, AVI = BASE_AVISO, PRO = PLASMA_ROJO, PFR = PLASMA_FRIO, PCE = PLASMA_CELESTE, TUBx = BASE_TUBERIA,
                      CAB = BASE_CABLE, ARM = BASE_ARMARIO, CNS = BASE_CONSOLA;
        const int W0 = 201, W1 = 298;                                           // profundidad (bloques): 98; ancho -50..50 (101)
        B(sh, -52, 52, W0 - 2, W1 + 2, -49, -18, MUR);
        B(ca, -50, 50, W0, W1, -47, -19, AIR);
        B(dc, -50, 50, W0, W1, -48, -48, PIS);
        for (int x = -48; x <= 48; x += 24) B(dc, x, x + 1, W0, W1, -48, -48, FRA);                            // carriles de franjas en el suelo
        for (int d = W0 + 8; d <= W1 - 8; d += 24) B(dc, -50, 50, d, d + 1, -48, -48, FRA);
        // columnas de acero
        for (int cx : {-36, -18, 18, 36}) for (int cd : {222, 250, 278}) {
            B(dc, cx - 1, cx + 1, cd - 1, cd + 1, -47, -19, ACE);
            B(dc, cx - 1, cx + 1, cd - 1, cd + 1, -47, -47, FRA); B(dc, cx - 1, cx + 1, cd - 1, cd + 1, -26, -26, FRA);
        }
        // luces del techo: celestes en cuadricula y rojas de emergencia entre medio
        almacenLamparas([&](int x, int d, int y, uint8_t) { B(dc, x, x, d, d, y, y, BASE_IA_OFF); });       // apagadas hasta que cae el mecha
        // tuberias, cables, avisos y armarios en las paredes
        for (int x : {-50, 50}) { B(dc, x, x, W0, W1, -22, -22, TUBx); B(dc, x, x, W0, W1, -24, -24, CAB); B(dc, x, x, W0, W1, -47, -46, ACE); }
        for (int d : {W0, W1}) { B(dc, -50, 50, d, d, -22, -22, TUBx); B(dc, -50, 50, d, d, -47, -46, ACE); }
        for (int d = W0 + 12; d <= W1 - 12; d += 16) { B(dc, -50, -50, d, d + 2, -40, -38, AVI); B(dc, 50, 50, d + 6, d + 8, -40, -38, AVI); }
        // estanterias a lo largo de las paredes este y oeste, con cajas
        for (int s : {-1, 1}) for (int d0 = W0 + 6; d0 + 8 <= W1 - 4; d0 += 12) {
            const int xa = s > 0 ? 46 : -49, xb = xa + 3;
            for (int y : {-44, -40, -36, -32}) B(dc, xa, xb, d0, d0 + 7, y, y, MET);
            for (int d : {d0, d0 + 7}) for (int x : {xa, xb}) B(dc, x, x, d, d, -47, -32, ACE);
            for (int k = 0; k < 4; k++) {
                const unsigned h = (unsigned)(d0 * 31 + k * 17 + (s + 2) * 7) * 2654435761u;
                const uint8_t t = (h >> 12) % 3 == 0 ? CON : ((h >> 12) % 3 == 1 ? PLA : ARM);
                const int yy = -43 + 4 * k, dd = d0 + 1 + (int)((h >> 5) % 4);
                B(dc, xa + 1, xa + 2, dd, dd + 1, yy, yy + 1, t);
            }
        }
        // pilas de cajas grandes en las esquinas del fondo y de la entrada
        for (int s : {-1, 1}) for (int d : {W0 + 2, W1 - 6}) { B(dc, s * 40, s * 44, d, d + 3, -47, -44, CON); B(dc, s * 41, s * 43, d, d + 3, -43, -41, PLA); }
        // consolas de mantenimiento junto a la escalera
        B(dc, -34, -30, W0 + 1, W0 + 2, -47, -46, CNS);
        // ESCALERA: del suelo del almacen hasta el suelo de la sala (pegada al borde oeste, bajo la cornisa). Su salida esta tapada hasta que se derrota al mecha
        for (int k = 0; k < 62; k++) C(dc, -48, -41, 414 + k, 414 + k, -95, -33 - k, PLA);
    }

    // ================= SALA DEL COHETE (v70): detras de la puerta oeste del silo. Tunel, pozo circular con un cohete moderno y pasarela a su escotilla
    {
        const uint8_t ACE = BASE_ACERO, MUR = BASE_MURO, AVI = BASE_AVISO, PFR = PLASMA_FRIO, PCE = PLASMA_CELESTE, PRO = PLASMA_ROJO, TUBx = BASE_TUBERIA,
                      CAB = BASE_CABLE, CNS = BASE_CONSOLA, BTN = BASE_BOTON, PAN2 = BASE_PANTALLA, MET2 = BASE_METAL;
        const int SD = 170, RX = -62, RD = SD;                              // centro del pozo (bloques)
        const int CXc = RX * SUB, CDc = RD * SUB;                           // centro en cubitos
        auto CILb = [&](std::vector<BBox>& v, int cx, int cd, int rOut, int rIn, int y0, int y1, uint8_t t) {      // en bloques
            BBox bb{}; const int Rc = rOut * SUB;
            bb.x0 = cx * SUB - Rc; bb.x1 = cx * SUB + Rc - 1; bb.d0 = cd * SUB - Rc; bb.d1 = cd * SUB + Rc - 1;
            bb.y0 = y0 * SUB; bb.y1 = (y1 + 1) * SUB - 1; bb.t = t; bb.k = 1; bb.ri = rIn * SUB; bb.p0 = cx * SUB; bb.p1 = cd * SUB; v.push_back(bb);
        };
        auto CILG = [&](std::vector<BBox>& v, int cx, int cd, int rOut, int rIn, int y0, int y1, uint8_t t) {      // todo en cubitos
            BBox bb{}; bb.x0 = cx - rOut; bb.x1 = cx + rOut - 1; bb.d0 = cd - rOut; bb.d1 = cd + rOut - 1; bb.y0 = y0; bb.y1 = y1; bb.t = t;
            bb.k = 1; bb.ri = rIn; bb.ro = rOut; bb.p0 = cx; bb.p1 = cd; v.push_back(bb);
        };
        auto BANDG = [&](std::vector<BBox>& v, int ax, int ad, int bx, int bd, int hw, int y0, int y1, uint8_t t, int pat) {   // cubitos
            BBox bb{}; bb.x0 = std::min(ax, bx) - hw - 1; bb.x1 = std::max(ax, bx) + hw + 1; bb.d0 = std::min(ad, bd) - hw - 1; bb.d1 = std::max(ad, bd) + hw + 1;
            bb.y0 = y0; bb.y1 = y1; bb.t = t; bb.k = 2; bb.ri = hw; bb.p0 = ax; bb.p1 = ad; bb.p2 = bx; bb.p3 = bd; bb.pat = pat; v.push_back(bb);
        };
        // tunel desde la puerta oeste
        B(sh, -50, -23, SD - 4, SD + 4, -18, -9, MUR);
        B(ca, -47, -23, SD - 2, SD + 2, -16, -11, AIR);
        B(dc, -47, -23, SD - 2, SD + 2, -17, -17, PIS); B(dc, -47, -23, SD - 1, SD + 1, -17, -17, FRA);
        for (int x : {-27, -32, -37, -42}) { B(dc, x, x, SD - 2, SD - 2, -12, -12, PFR); B(dc, x, x, SD + 2, SD + 2, -12, -12, PFR); }
        B(dc, -47, -23, SD - 3, SD - 3, -13, -13, TUBx);
        B(dc, -40, -39, SD - 3, SD - 3, -15, -14, AVI);
        // pozo
        CILb(sh, RX, RD, 19, 0, -58, -1, MUR);
        CILb(ca, RX, RD, 16, 0, -56, -3, AIR);
        CILb(dc, RX, RD, 16, 0, -57, -57, PIS);
        CILb(dc, RX, RD, 8, 0, -56, -56, PLA);                                  // plataforma de lanzamiento
        CILG(dc, CXc, CDc, 6, 0, -114, -111, AIR);                              // foso de gases bajo los motores
        for (int y = -50; y <= -8; y += 6) {                                     // costillas rojas y lamparas frias
            CILb(dc, RX, RD, 16, 15, y, y, ACE);
            for (auto p : {std::pair<int, int>{15, 0}, {-15, 0}, {0, 15}, {0, -15}}) B(dc, RX + p.first, RX + p.first, RD + p.second, RD + p.second, y + 1, y + 1, PFR);
        }
        for (int a = 0; a < 8; a++) { const double an = a * 6.2831853 / 8.0 + 0.3927; const int px = RX + (int)std::lround(std::cos(an) * 15.0), pz = RD + (int)std::lround(std::sin(an) * 15.0); B(dc, px, px, pz, pz, -55, -4, a % 2 ? TUBx : ACE); }
        for (int y = -54; y <= -4; y += 10) for (int a = 0; a < 4; a++) { const int px = RX + (a == 0 ? 15 : a == 1 ? -15 : 0), pz = RD + (a == 2 ? 15 : a == 3 ? -15 : 0); B(dc, px, px, pz, pz, y, y, AVI); }
        // tapa del techo (a ras del suelo): se abre al subir al cohete
        CILG(dc, CXc, CDc, 14, 0, -4, 0, ACE);
        CILG(dc, CXc, CDc, 14, 12, -4, 0, FRA);
        // ---- el cohete (cubitos). Los motores quedan 8 bloques sobre la plataforma
        for (int sx : {-1, 1}) for (int sd : {-1, 1}) CILG(dc, CXc + sx * 10, CDc + sd * 10, 2, 0, -110, -95, ACE);       // 4 torres de sujecion
        CILG(dc, CXc, CDc, 4, 0, -94, -91, MET2);                               // toberas: la central y cuatro alrededor
        CILG(dc, CXc, CDc, 3, 0, -90, -87, MET2);
        for (int s : {-1, 1}) { CILG(dc, CXc + s * 5, CDc, 3, 0, -94, -88, MET2); CILG(dc, CXc, CDc + s * 5, 3, 0, -94, -88, MET2); }
        CILG(dc, CXc, CDc, 9, 0, -86, -80, MET2);                               // faldon de motores
        CILG(dc, CXc, CDc, 9, 7, -80, -80, ACE);
        for (int y = -79; y <= -35; y++) {                                       // tanque principal (blanco con bandas)
            const int rel = (y + 79) % 14;
            CILG(dc, CXc, CDc, 8, 0, y, y, rel < 2 ? CAB : (rel == 7 ? FRA : PLA));
        }
        for (int y = -34; y <= -17; y++) CILG(dc, CXc, CDc, 8, 6, y, y, (y == -34 || y == -17) ? ACE : PLA);       // cabina (hueca)
        CILG(dc, CXc, CDc, 6, 0, -35, -33, PIS);                                 // suelo de la cabina
        CILG(dc, CXc, CDc, 7, 0, -18, -17, PLA);                                 // techo de la cabina
        for (int k = 0; k < 8; k++) CILG(dc, CXc, CDc, 8 - k, 0, -16 + k, -16 + k, k < 2 ? ACE : PLA);                  // ojiva
        // aletas traseras
        for (int y = -86; y <= -67; y++) {
            const int L = 9 - (y + 86) * 7 / 20;
            for (int s : {-1, 1}) {
                C(dc, CXc + (s > 0 ? 8 : -8 - L), CXc + (s > 0 ? 8 + L : -8) - (s > 0 ? 0 : 1), CDc - 1, CDc, y, y, ACE);
                C(dc, CXc - 1, CXc, CDc + (s > 0 ? 8 : -8 - L), CDc + (s > 0 ? 8 + L : -8) - (s > 0 ? 0 : 1), y, y, ACE);
            }
        }
        // propulsores laterales
        for (int s : {-1, 1}) {
            const int bd = CDc + s * 11;
            CILG(dc, CXc, bd, 3, 0, -88, -85, MET2);
            for (int y = -84; y <= -52; y++) CILG(dc, CXc, bd, 3, 0, y, y, ((y + 84) % 12 < 2) ? CAB : PLA);
            for (int k = 0; k < 5; k++) CILG(dc, CXc, bd, 3 - (k + 1) / 2, 0, -51 + k, -51 + k, k < 2 ? ACE : PLA);
        }
        // escotilla (cara este, hacia la pasarela) y cabina por dentro
        C(dc, CXc + 5, CXc + 9, CDc - 3, CDc + 2, -32, -22, AIR);                // paso de la escotilla
        C(dc, CXc + 5, CXc + 9, CDc - 4, CDc - 4, -32, -21, FRA); C(dc, CXc + 5, CXc + 9, CDc + 3, CDc + 3, -32, -21, FRA); C(dc, CXc + 5, CXc + 9, CDc - 4, CDc + 3, -21, -21, FRA);
        C(dc, CXc + 8, CXc + 8, CDc - 2, CDc + 1, -19, -19, PRO);               // luz roja sobre la escotilla
        B(dc, RX + 3, RX + 3, RD + 2, RD + 2, -15, -15, BTN);                    // boton de embarque
        for (int k = 0; k < 5; k++) C(dc, CXc - 7, CXc - 6, CDc - 4 + k * 2 - 1, CDc - 4 + k * 2, -28, -24, PAN2);      // ventanas del frente
        for (int s : {-1, 1}) { C(dc, CXc - 3, CXc - 2, CDc + s * 3 - 1, CDc + s * 3, -32, -30, CNS); C(dc, CXc + 1, CXc + 2, CDc + s * 4 - 1, CDc + s * 4, -32, -31, CNS); }
        C(dc, CXc - 1, CXc, CDc - 1, CDc, -20, -20, PCE); C(dc, CXc + 3, CXc + 4, CDc + 2, CDc + 3, -20, -20, PCE);
        // pasarela de cubitos desde la pared del pozo hasta la escotilla
        {
            const int ax = (RX + 16) * SUB, bx = CXc + 9, ad = CDc, bd = CDc;
            const int YP = -17 * SUB + 1, YR0 = -16 * SUB, YR1 = -16 * SUB + 3;
            BANDG(dc, ax, ad, bx, bd, 4, YP, YP, REJ, 3);
            for (int s : {-1, 1}) {
                BBox bb{}; bb.x0 = std::min(ax, bx) - 7; bb.x1 = std::max(ax, bx) + 7; bb.d0 = CDc - 8; bb.d1 = CDc + 8;
                bb.y0 = YR0; bb.y1 = YR1; bb.t = TUB; bb.k = 2; bb.ri = 0; bb.hs = 1; bb.pat = 4;
                bb.p0 = ax * 2; bb.p1 = ad * 2 + s * 11; bb.p2 = bx * 2; bb.p3 = bd * 2 + s * 11;
                dc.push_back(bb);
            }
        }
        // luces de la pasarela
        for (int x : {RX + 18, RX + 21, RX + 24}) { B(dc, x, x, RD - 3, RD - 3, -15, -15, PFR); B(dc, x, x, RD + 3, RD + 3, -15, -15, PFR); }
    }

    // limites de toda la base
    for (auto* v : {&D.sh, &D.ca, &D.dc})
        for (auto& b : *v) {
            D.x0 = std::min(D.x0, b.x0); D.x1 = std::max(D.x1, b.x1); D.d0 = std::min(D.d0, b.d0); D.d1 = std::max(D.d1, b.d1);
            D.y0 = std::min(D.y0, b.y0); D.y1 = std::max(D.y1, b.y1);
        }
    return D;
}
static const BaseDis& baseDiseno() { static const BaseDis D = baseConstruir(); return D; }

// ¿el chunk (origen ox,oy,oz en cubitos del mundo) toca la base?
static bool baseToca(int ox, int oy, int oz) {
    const BaseDis& D = baseDiseno();
    const int lx0 = ox - baseCX(0), lx1 = lx0 + CS - 1;
    const int ld1 = baseCZ(0) - oz, ld0 = ld1 - (CS - 1);
    const int ly0 = oy - baseCY(0), ly1 = ly0 + CS - 1;
    return !(lx1 < D.x0 || lx0 > D.x1 || ld1 < D.d0 || ld0 > D.d1 || ly1 < D.y0 || ly0 > D.y1);
}
// rellena un chunk con la base (primero las paredes, luego se vacian las salas, al final los detalles)
static void baseAplicar(uint8_t* dt, int ox, int oy, int oz) {
    const BaseDis& D = baseDiseno();
    auto pasar = [&](const std::vector<BBox>& v) {
        for (const BBox& b : v) {
            // a coordenadas del mundo: x = X0 + lx; z = Z0 - d; y = Y0 + ly
            const int wx0 = baseCX(b.x0), wx1 = baseCX(b.x1), wz0 = baseCZ(b.d1), wz1 = baseCZ(b.d0), wy0 = baseCY(b.y0), wy1 = baseCY(b.y1);
            const int x0 = std::max(wx0, ox), x1 = std::min(wx1, ox + CS - 1), z0 = std::max(wz0, oz), z1 = std::min(wz1, oz + CS - 1), y0 = std::max(wy0, oy), y1 = std::min(wy1, oy + CS - 1);
            if (x0 > x1 || y0 > y1 || z0 > z1) continue;
            if (b.k == 0) { for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) dt[IDX(x - ox, y - oy, z - oz)] = b.t; continue; }
            const int Ro = b.ro > 0 ? b.ro : (b.x1 - b.x0 + 1) / 2;
            const double ro2 = (double)Ro * Ro, ri2 = (double)b.ri * b.ri;
            for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
                const int lxi = x - baseCX(0), ldi = baseCZ(0) - z;                    // indices locales (cubitos)
                const double lx = lxi + 0.5, ld = ldi + 0.5;                           // centro de la celda
                bool dentro; double theta = 0;
                if (b.k == 1) { const double dx = lx - b.p0, dd = ld - b.p1, r2 = dx * dx + dd * dd; dentro = r2 < ro2 && r2 >= ri2; if (b.pat == 1) theta = std::atan2(dd, dx); }
                else {
                    const double sc = b.hs ? 0.5 : 1.0, P0 = b.p0 * sc, P1 = b.p1 * sc, P2 = b.p2 * sc, P3 = b.p3 * sc;
                    const double sx = P2 - P0, sd = P3 - P1, L2 = sx * sx + sd * sd;
                    double u = L2 > 0 ? ((lx - P0) * sx + (ld - P1) * sd) / L2 : 0.0; u = u < 0 ? 0 : (u > 1 ? 1 : u);
                    const double dx = lx - (P0 + u * sx), dd = ld - (P1 + u * sd);
                    dentro = dx * dx + dd * dd < (b.ri + 0.5) * (b.ri + 0.5) + 1e-9;
                }
                if (!dentro) continue;
                // patrones de rendija: 1 baranda circular, 2 rejilla a lo largo de d, 3 a lo largo de x, 4 baranda recta, 5 agujeros cuadrados
                bool hueco = false;
                if (b.pat == 2) hueco = (lxi & 1) && ((ldi & 3) != 0);
                else if (b.pat == 3) hueco = (ldi & 1) && ((lxi & 3) != 0);
                else if (b.pat == 5) hueco = (lxi & 1) && (ldi & 1);
                for (int y = y0; y <= y1; y++) {
                    bool h = hueco;
                    if (b.pat == 1 || b.pat == 4) {                                    // baranda: barra abajo y arriba, barras verticales finas entre medio
                        const int capa = y - wy0, n = wy1 - wy0 + 1;
                        const bool barra = b.pat == 1 ? (((int)std::floor(theta * Ro / 1.0)) & 3) == 0 : ((lxi + ldi) & 3) == 0;
                        h = !(capa == 0 || capa == n - 1 || barra);
                    }
                    if (!h) dt[IDX(x - ox, y - oy, z - oz)] = b.t;
                }
            }
        }
    };
    pasar(D.sh); pasar(D.ca); pasar(D.dc);
}

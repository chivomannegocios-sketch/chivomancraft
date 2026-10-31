// v84: mobs (ovejas) y camas. Modelos hechos con cajas (jfCaja de jefe.h). Las ovejas no se guardan: aparecen y desaparecen cerca del jugador.
#pragma once

// ---------------------------------------------------------------- utilidades
struct MbRayo { double ox, oy, oz, dx, dy, dz; };
// rayo contra caja alineada con los ejes; devuelve la distancia o -1
static double mbRayoCaja(const MbRayo& r, double x0, double x1, double y0, double y1, double z0, double z1) {
    double tmin = 0, tmax = 1e30;
    const double o[3] = {r.ox, r.oy, r.oz}, d[3] = {r.dx, r.dy, r.dz}, lo[3] = {x0, y0, z0}, hi[3] = {x1, y1, z1};
    for (int i = 0; i < 3; i++) {
        if (std::fabs(d[i]) < 1e-12) { if (o[i] < lo[i] || o[i] > hi[i]) return -1; continue; }
        double t1 = (lo[i] - o[i]) / d[i], t2 = (hi[i] - o[i]) / d[i];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
        if (tmin > tmax) return -1;
    }
    return tmin;
}
static uint32_t g_mbRng = 2463534242u;
static double mbRand() { g_mbRng ^= g_mbRng << 13; g_mbRng ^= g_mbRng >> 17; g_mbRng ^= g_mbRng << 5; return (g_mbRng & 0xFFFFFF) / 16777216.0; }
static bool mbSolido(double x, double y, double z) { return jfSolido(x, y, z); }

// ---------------------------------------------------------------- ovejas
struct Oveja {
    double x, y, z, vy = 0, rumbo = 0, yaw = 0, kx = 0, kz = 0;
    double vida = 8.0, tSig = 0, tHit = -10, tHuye = -10, tMuerte = -1, fase = 0, vel = 0;
    int tipo = 0;          // 0 = blanca de cabeza grande, 1 = con cuernos y patas cafes
    bool mueve = false, suelo = false;
};
static std::vector<Oveja> g_ovejas;
static double g_ovTSpawn = 0;

static bool mbCargado(double x, double y, double z) {
    bool k = true;
    leerCubito((int)std::floor(x * SUB), (int)std::floor(y * SUB), (int)std::floor(z * SUB), &k);
    return k;
}
static bool ovChoca(const Oveja& o, double nx, double ny, double nz) {
    static const double H[3] = {0.12, 0.7, 1.15};
    for (int cx = -1; cx <= 1; cx += 2) for (int cz = -1; cz <= 1; cz += 2) for (double h : H)
        if (mbSolido(nx + cx * 0.38, ny + h, nz + cz * 0.38)) return true;
    return false;
}
static void ovTick(Oveja& o, double dt, double ahora) {
    if (o.tMuerte >= 0) return;
    if (!mbCargado(o.x, o.y, o.z)) return;                     // el terreno de aqui todavia no esta: se queda quieta
    if (ahora > o.tSig) {                                      // cambia de idea: quieta o caminar hacia un rumbo
        o.mueve = mbRand() < 0.55;
        if (o.mueve) o.rumbo = mbRand() * 6.2831853;
        o.tSig = ahora + 1.5 + mbRand() * 4.0;
    }
    const bool huye = ahora - o.tHuye < 3.0;
    if (huye) o.mueve = true;
    const double sp = huye ? 3.4 : 1.0;
    double dx = 0, dz = 0;
    if (o.mueve) {
        // evita lagos y precipicios
        const double fx = o.x + std::sin(o.rumbo) * 0.9, fz = o.z - std::cos(o.rumbo) * 0.9;
        const bool agua = esAgua(leerCubito((int)std::floor(fx * SUB), (int)std::floor((o.y - 0.3) * SUB), (int)std::floor(fz * SUB)));
        const bool vacio = !mbSolido(fx, o.y - 0.3, fz) && !mbSolido(fx, o.y - 1.3, fz);
        if ((agua || vacio) && !huye) { o.rumbo += 2.0 + mbRand() * 2.0; o.tSig = ahora + 1.0; }
        else { dx = std::sin(o.rumbo) * sp; dz = -std::cos(o.rumbo) * sp; }
    }
    dx += o.kx; dz += o.kz;
    o.kx *= std::exp(-5.0 * dt); o.kz *= std::exp(-5.0 * dt);
    bool bloq = false;
    double nx = o.x + dx * dt;
    if (!ovChoca(o, nx, o.y, o.z)) o.x = nx; else bloq = true;
    double nz = o.z + dz * dt;
    if (!ovChoca(o, o.x, o.y, nz)) o.z = nz; else bloq = true;
    if (bloq && o.suelo && o.mueve) o.vy = 7.2;                // salta un escalon
    if (bloq && !o.suelo) { /* en el aire */ }
    if (bloq && o.suelo && !huye && mbRand() < 0.02) o.rumbo += 3.14159;
    if (std::fabs(dx) + std::fabs(dz) > 0.05) {                // gira suave hacia donde camina
        double ob = std::atan2(dx, -dz), d = ob - o.yaw;
        while (d > 3.14159265) d -= 6.2831853;
        while (d < -3.14159265) d += 6.2831853;
        o.yaw += d * std::min(1.0, dt * 8.0);
    }
    o.vel = std::sqrt(dx * dx + dz * dz);
    o.fase += dt * (2.0 + o.vel * 3.2);
    // vertical
    const bool agua = esAgua(leerCubito((int)std::floor(o.x * SUB), (int)std::floor((o.y + 0.3) * SUB), (int)std::floor(o.z * SUB)));
    o.vy -= 25.0 * dt;
    if (agua) o.vy = std::max(o.vy, 1.8);
    double ny = o.y + o.vy * dt;
    o.suelo = false;
    if (o.vy <= 0) {
        bool pisa = false;
        for (int cx = -1; cx <= 1; cx += 2) for (int cz = -1; cz <= 1; cz += 2) if (mbSolido(o.x + cx * 0.35, ny, o.z + cz * 0.35)) pisa = true;
        if (pisa) { ny = (std::floor(ny * SUB) + 1.0) / SUB; o.vy = 0; o.suelo = true; }
    } else {
        for (int cx = -1; cx <= 1; cx += 2) for (int cz = -1; cz <= 1; cz += 2) if (mbSolido(o.x + cx * 0.35, ny + 1.3, o.z + cz * 0.35)) { o.vy = 0; ny = o.y; }
    }
    o.y = ny;
    if (o.y < -200) o.vida = 0;
}
static void ovejasTick(double dt, double ahora, double px, double py, double pz, bool permitido) {
    dt = std::min(dt, 0.05);
    for (auto& o : g_ovejas) ovTick(o, dt, ahora);
    for (size_t i = 0; i < g_ovejas.size();) {                 // muertas (despues de caer) y lejanas desaparecen
        const Oveja& o = g_ovejas[i];
        const double ddx = o.x - px, ddz = o.z - pz;
        if ((o.tMuerte >= 0 && ahora - o.tMuerte > 0.9) || ddx * ddx + ddz * ddz > 130.0 * 130.0 || !permitido) g_ovejas.erase(g_ovejas.begin() + i);
        else i++;
    }
    if (!permitido || ahora - g_ovTSpawn < 2.0) return;
    g_ovTSpawn = ahora;
    int vivas = 0; for (auto& o : g_ovejas) if (o.tMuerte < 0) vivas++;
    if (vivas >= 10) return;
    const double ang = mbRand() * 6.2831853, dis = 25.0 + mbRand() * 55.0;
    const double sx = px + std::cos(ang) * dis, sz = pz + std::sin(ang) * dis;
    const double h = alturaTerreno(sx, sz);
    if (h < WATER_LEVEL + 1.5) return;
    if (!mbCargado(sx, h, sz)) return;
    if (leerCubito((int)std::floor(sx * SUB), (int)std::floor((h - 0.25) * SUB), (int)std::floor(sz * SUB)) != PASTO) return;
    if (mbSolido(sx, h + 0.25, sz) || mbSolido(sx, h + 1.2, sz)) return;
    const int n = 1 + (int)(mbRand() * 3.0);                   // grupitos de 1 a 3
    const int tipo = mbRand() < 0.5 ? 0 : 1;
    for (int i = 0; i < n; i++) {
        Oveja o; o.x = sx + (mbRand() - 0.5) * 3.0; o.z = sz + (mbRand() - 0.5) * 3.0;
        o.y = alturaTerreno(o.x, o.z) + 0.3; o.yaw = mbRand() * 6.2831853; o.rumbo = o.yaw; o.tipo = mbRand() < 0.8 ? tipo : 1 - tipo;
        o.tSig = ahora + mbRand() * 2.0; o.fase = mbRand() * 6.0;
        g_ovejas.push_back(o);
    }
}
// el jugador apunta a una oveja: devuelve el indice (o -1) y la distancia
static int ovejaApuntada(V3 cam, V3 dir, double alcance, double* dist) {
    MbRayo r{cam.x, cam.y, cam.z, dir.x, dir.y, dir.z};
    int mejor = -1; double bt = alcance;
    for (size_t i = 0; i < g_ovejas.size(); i++) {
        const Oveja& o = g_ovejas[i];
        if (o.tMuerte >= 0) continue;
        const double t = mbRayoCaja(r, o.x - 0.55, o.x + 0.55, o.y, o.y + 1.35, o.z - 0.55, o.z + 0.55);
        if (t >= 0 && t < bt) { bt = t; mejor = (int)i; }
    }
    if (dist) *dist = bt;
    return mejor;
}
// golpea a una oveja; devuelve true si murio (y cuantas piezas suelta)
static bool ovejaGolpear(int i, double dano, double px, double pz, double ahora, int* lana, int* carne) {
    Oveja& o = g_ovejas[i];
    o.vida -= dano; o.tHit = ahora; o.tHuye = ahora;
    double ax = o.x - px, az = o.z - pz; const double l = std::sqrt(ax * ax + az * az) + 1e-6;
    o.kx += ax / l * 5.0; o.kz += az / l * 5.0; if (o.suelo) o.vy = 4.5;
    o.rumbo = std::atan2(ax, -az);
    if (o.vida <= 0) {
        o.tMuerte = ahora; o.mueve = false;
        *lana = 1 + (int)(mbRand() * 3.0); *carne = 1 + (int)(mbRand() * 2.0);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- modelos
static void ovGenerar(std::vector<float>& v, const Oveja& o, double camx, double camy, double camz, double ahora) {
    JfXf T; T.ox = o.x; T.oy = o.y; T.oz = o.z; T.cy = std::cos(o.yaw); T.sy = std::sin(o.yaw); T.cam[0] = camx; T.cam[1] = camy; T.cam[2] = camz;
    const float S = 1.0f / 12.0f;
    const bool hit = ahora - o.tHit < 0.28;
    const bool muerta = o.tMuerte >= 0;
    const float da = muerta ? (float)std::min(1.0, (ahora - o.tMuerte) / 0.5) * 1.5708f : 0.0f;   // cae de lado hacia delante
    auto B = [&](float x0, float x1, float y0, float y1, float z0, float z1, float r, float g, float b, float ang = 0, float py = 0, float pz = 0) {
        float c[3] = {r, g, b};
        if (hit || muerta) { c[1] *= 0.45f; c[2] *= 0.45f; }
        if (muerta) { ang = da; py = 6; pz = 0; }
        jfCaja(v, T, x0 * S, x1 * S, y0 * S, y1 * S, z0 * S, z1 * S, c, ang, 0, py * S, pz * S);
    };
    const bool A = o.tipo == 0;
    // patas (en diagonal)
    const float sw = (muerta ? 0.0f : std::sin((float)o.fase) * 0.7f * (float)std::min(1.0, o.vel * 0.8));
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
        const float xc = a ? 3.5f : -3.5f, zc = b ? 5.0f : -5.0f, ang = ((a ^ b) ? sw : -sw);
        if (A) B(xc - 1.5f, xc + 1.5f, 0, 5, zc - 1.5f, zc + 1.5f, 0.91f, 0.91f, 0.93f, ang, 5, zc);
        else   B(xc - 1.5f, xc + 1.5f, 0, 5.2f, zc - 1.5f, zc + 1.5f, 0.55f, 0.46f, 0.34f, ang, 5, zc);
    }
    // cuerpo
    B(-6, 6, 5, 14, -8, 8, 0.94f, 0.94f, 0.95f);
    if (A) {
        B(6, 6.7f, 5.8f, 13, -7, 4.5f, 0.72f, 0.72f, 0.74f); B(-6.7f, -6, 5.8f, 13, -7, 4.5f, 0.72f, 0.72f, 0.74f);
        B(-4, 4, 14, 14.8f, -7, 3, 0.98f, 0.98f, 0.99f);
    } else {
        B(6, 6.6f, 9, 14, -7, -1, 0.86f, 0.86f, 0.88f); B(-6.6f, -6, 9, 14, -7, -1, 0.86f, 0.86f, 0.88f);
        B(6, 6.6f, 6, 10, 0, 5, 0.80f, 0.80f, 0.83f); B(-6.6f, -6, 6, 10, 0, 5, 0.80f, 0.80f, 0.83f);
        B(-5, 5, 14, 14.7f, -7, 0, 0.98f, 0.98f, 0.99f); B(-3, 3, 14.7f, 15.3f, -5, -1, 0.96f, 0.96f, 0.98f);
    }
    // cabeza (sube y baja al caminar)
    const float bob = muerta ? 0.0f : std::sin((float)o.fase * 2.0f) * 0.35f * (float)std::min(1.0, o.vel);
    const float hy = bob;
    B(-4, 4, 9 + hy, 15 + hy, 7.5f, 11.5f, 0.95f, 0.95f, 0.96f);                                   // lana de la cabeza
    if (A) {
        B(-4.6f, 4.6f, 14.2f + hy, 16.6f + hy, 6.5f, 12, 0.98f, 0.98f, 0.99f);                      // gorro de lana
        B(4, 5.6f, 11 + hy, 14 + hy, 8, 9.8f, 0.93f, 0.93f, 0.95f); B(-5.6f, -4, 11 + hy, 14 + hy, 8, 9.8f, 0.93f, 0.93f, 0.95f);   // orejas
    } else {
        B(-3.8f, 3.8f, 14.2f + hy, 15.8f + hy, 7.5f, 12, 0.98f, 0.98f, 0.99f);
        B(-3.2f, -1.4f, 15.8f + hy, 19.5f + hy, 8.5f, 10.2f, 0.76f, 0.66f, 0.50f); B(1.4f, 3.2f, 15.8f + hy, 19.5f + hy, 8.5f, 10.2f, 0.76f, 0.66f, 0.50f);   // cuernos
    }
    B(-3, 3, 9.2f + hy, 14.2f + hy, 11.5f, 12.2f, 0.83f, 0.73f, 0.56f);                              // cara
    B(-2, 2, 9 + hy, 12 + hy, 12.2f, 14.4f, 0.80f, 0.70f, 0.52f);                                   // hocico
    B(-2.4f, -1.2f, 12 + hy, 13.6f, 12.2f, 12.45f, 0.04f, 0.04f, 0.05f);                              // ojos
    B(1.2f, 2.4f, 12 + hy, 13.6f, 12.2f, 12.45f, 0.04f, 0.04f, 0.05f);
}

// ---------------------------------------------------------------- camas
struct Cama { double x, y, z; int dir; };
static std::vector<Cama> g_camas;
static void camasGuardar() {
    FILE* f = std::fopen((g_dir + "/camas.txt").c_str(), "w");
    if (!f) return;
    for (auto& c : g_camas) std::fprintf(f, "%.3f %.3f %.3f %d\n", c.x, c.y, c.z, c.dir);
    std::fclose(f);
}
static void camasCargar() {
    g_camas.clear();
    FILE* f = std::fopen((g_dir + "/camas.txt").c_str(), "r");
    if (!f) return;
    Cama c;
    while (std::fscanf(f, "%lf %lf %lf %d", &c.x, &c.y, &c.z, &c.dir) == 4) g_camas.push_back(c);
    std::fclose(f);
}
static void camaGenerar(std::vector<float>& v, const Cama& c, double camx, double camy, double camz) {
    const double yaw = c.dir * 1.5707963;
    JfXf T; T.ox = c.x; T.oy = c.y; T.oz = c.z; T.cy = std::cos(yaw); T.sy = std::sin(yaw); T.cam[0] = camx; T.cam[1] = camy; T.cam[2] = camz;
    const float S = 1.0f / 16.0f;
    auto B = [&](float x0, float x1, float y0, float y1, float z0, float z1, float r, float g, float b) {
        const float col[3] = {r, g, b};
        jfCaja(v, T, x0 * S, x1 * S, y0 * S, y1 * S, z0 * S, z1 * S, col);
    };
    const float M[3] = {0.74f, 0.53f, 0.30f}, D[3] = {0.62f, 0.43f, 0.24f};
    B(-8, 8, 0, 14, -16, -14, M[0], M[1], M[2]);            // cabecera alta
    B(-8, 8, 0, 8, 14, 16, M[0], M[1], M[2]);               // pie bajo
    B(-8, -7, 3, 6, -14, 14, D[0], D[1], D[2]); B(7, 8, 3, 6, -14, 14, D[0], D[1], D[2]);   // laterales
    B(-8, 8, 12.5f, 14.5f, -16.3f, -13.7f, 0.84f, 0.62f, 0.36f);   // remate de la cabecera
    B(-7, 7, 4, 8.5f, -14, 14, 0.86f, 0.86f, 0.89f);        // colchon
    B(-5.5f, 5.5f, 8.5f, 10.3f, -13.5f, -7.5f, 0.99f, 0.99f, 1.0f);    // almohada
    B(-7.3f, 7.3f, 4, 9, -3, 14, 0.95f, 0.95f, 0.97f);      // manta
    B(-7.5f, 7.5f, 4, 9.4f, -3, 0.5f, 0.99f, 0.99f, 1.0f);  // doblez de la manta
}
static int camaApuntada(V3 cam, V3 dir, double alcance) {
    int mejor = -1; double bt = alcance;
    for (size_t i = 0; i < g_camas.size(); i++) {
        const Cama& c = g_camas[i];
        const double yaw = c.dir * 1.5707963, cy = std::cos(yaw), sy = std::sin(yaw);
        const double rx = cam.x - c.x, rz = cam.z - c.z;
        MbRayo r{rx * cy + rz * sy, cam.y - c.y, rx * sy - rz * cy, dir.x * cy + dir.z * sy, dir.y, dir.x * sy - dir.z * cy};
        const double t = mbRayoCaja(r, -0.5, 0.5, 0.0, 0.9, -1.0, 1.0);
        if (t >= 0 && t < bt) { bt = t; mejor = (int)i; }
    }
    return mejor;
}

// ---------------------------------------------------------------- dibujo de todo
static void mobsDibujar(const float* vp, double camx, double camy, double camz, const float* sol, float solF, float skyMul, const float* cielo, float fogA, float fogB, double ahora) {
    if (g_ovejas.empty() && g_camas.empty()) return;
    std::vector<float> v;
    for (const Oveja& o : g_ovejas) {
        const double dx = o.x - camx, dz = o.z - camz;
        if (dx * dx + dz * dz < 160.0 * 160.0) ovGenerar(v, o, camx, camy, camz, ahora);
    }
    for (const Cama& c : g_camas) {
        const double dx = c.x - camx, dz = c.z - camz;
        if (dx * dx + dz * dz < 200.0 * 200.0) camaGenerar(v, c, camx, camy, camz);
    }
    if (v.empty()) return;
    glDisable(GL_CULL_FACE);
    itemDibujar(vp, sol, solF, skyMul, cielo, fogA, fogB, v);
}

// Dibujo de los otros jugadores (modelo estilo Minecraft con la skin 64x64 embebida).
#pragma once
#include "skin.h"
#include "red.h"

static const char* VJ_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aN;
uniform mat4 uVP;
out vec2 vUV; out vec3 vN; out float vDist;
void main(){ vUV = aUV; vN = aN; vDist = length(aPos); gl_Position = uVP * vec4(aPos, 1.0); })";
static const char* FJ_SRC = R"(#version 330 core
in vec2 vUV; in vec3 vN; in float vDist;
out vec4 frag;
uniform sampler2D uSkin;
uniform vec3 uSun, uSky;
uniform float uSolF, uSkyMul, uFogA, uFogB, uOver;
void main(){
    vec4 t = texture(uSkin, vUV);
    if (uOver > 0.5) { if (t.a < 0.5) discard; } else t.a = 1.0;
    vec3 n = normalize(vN);
    float ndl = max(dot(n, uSun), 0.0);
    float lum = (0.40 + 0.22 * (n.y * 0.5 + 0.5) + 0.50 * ndl * uSolF) * uSkyMul + 0.05;
    vec3 col = t.rgb * lum;
    float f = clamp((vDist - uFogA) / (uFogB - uFogA), 0.0, 1.0);
    frag = vec4(mix(col, uSky, f), 1.0);
})";

static GLuint g_progJug = 0, g_texSkin = 0, g_vaoJug = 0, g_vboJug = 0;

struct JParte { int u, v, w, h, d; float cx, cy, cz; float infl; float px, py, pz; int anim; bool capa; };
// anim: 0 fijo, 1 cabeza (cabeceo), 2 brazo der., 3 brazo izq., 4 pierna der., 5 pierna izq.
static const JParte J_PARTES[] = {
    {0, 0, 8, 8, 8, 0, 28, 0, 0, 0, 24, 0, 1, false},     {32, 0, 8, 8, 8, 0, 28, 0, 0.5f, 0, 24, 0, 1, true},
    {16, 16, 8, 12, 4, 0, 18, 0, 0, 0, 0, 0, 0, false},   {16, 32, 8, 12, 4, 0, 18, 0, 0.25f, 0, 0, 0, 0, true},
    {40, 16, 4, 12, 4, -6, 18, 0, 0, -6, 24, 0, 2, false}, {40, 32, 4, 12, 4, -6, 18, 0, 0.25f, -6, 24, 0, 2, true},
    {32, 48, 4, 12, 4, 6, 18, 0, 0, 6, 24, 0, 3, false},  {48, 48, 4, 12, 4, 6, 18, 0, 0.25f, 6, 24, 0, 3, true},
    {0, 16, 4, 12, 4, -2, 6, 0, 0, -2, 12, 0, 4, false},  {0, 32, 4, 12, 4, -2, 6, 0, 0.25f, -2, 12, 0, 4, true},
    {16, 48, 4, 12, 4, 2, 6, 0, 0, 2, 12, 0, 5, false},   {0, 48, 4, 12, 4, 2, 6, 0, 0.25f, 2, 12, 0, 5, true},
};

static void jugIniciar() {
    if (g_progJug) return;
    g_progJug = programa(VJ_SRC, FJ_SRC);
    glGenTextures(1, &g_texSkin);
    glBindTexture(GL_TEXTURE_2D, g_texSkin);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, SKIN_RGBA);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenVertexArrays(1, &g_vaoJug); glGenBuffers(1, &g_vboJug);
    glBindVertexArray(g_vaoJug);
    glBindBuffer(GL_ARRAY_BUFFER, g_vboJug);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (const void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (const void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 32, (const void*)20);
}

// genera los vertices (posiciones relativas a la camara) de un jugador; capa=false cuerpo, true capa exterior (sombrero, chaqueta...)
static void itemColor(int id, float* rgb);   // (la define main.cpp) color base de un objeto
static bool itemHerrSprite(int id, float col[8][8][3], bool fill[8][8]);
static void jugItem(const Remoto& m, double camx, double camy, double camz, float angR, float preRot, std::vector<float>& it);
static void jugGenerar(const Remoto& m, double camx, double camy, double camz, bool capa, std::vector<float>& out, std::vector<float>* items = nullptr, int solo = -1, float angPrimera = 100.0f) {
    const float S = 1.8f / 32.0f;
    float fx = std::sin(m.syaw), fz = -std::cos(m.syaw);
    float Xx = fz, Xz = -fx;                                       // X = Y x F
    float amp = 0.85f * std::min(1.0f, m.vel / 3.5f), sw = std::sin(m.fase) * amp;
    bool sentado = (m.flags & 2) != 0;
    const bool primera = angPrimera < 50.0f;
    const bool tiene = m.held != 255;
    float swAdd = m.swing > 0 ? -1.9f * std::sin(m.swing * 3.14159265f) : 0.0f;
    float angR = sentado ? -1.2f : (tiene ? -0.55f - sw * 0.4f : -sw);
    angR += swAdd;
    if (primera) angR = angPrimera + swAdd * 0.9f;
    float ox = (float)(m.sx - camx), oy = (float)(m.sy - camy), oz = (float)(m.sz - camz);
    for (const JParte& p : J_PARTES) {
        if (p.capa != capa) continue;
        if (solo >= 0 && p.anim != solo) continue;
        float ang = 0;
        switch (p.anim) {
            case 1: ang = -std::max(-1.2f, std::min(1.2f, m.pitch)); break;   // pitch>0 = mirar arriba
            case 2: ang = angR; break;
            case 3: ang = sentado ? -1.2f : sw; break;
            case 4: ang = sentado ? -1.5708f : sw; break;
            case 5: ang = sentado ? -1.5708f : -sw; break;
        }
        float ca = std::cos(ang), sa = std::sin(ang);
        auto T = [&](float lx, float ly, float lz, float nx, float ny, float nz, float* o, bool normal) {   // local del modelo -> mundo relativo
            float x = lx, y = ly, z = lz;
            if (p.anim) { float yy = y - p.py, zz = z - p.pz; y = yy * ca - zz * sa + p.py; z = yy * sa + zz * ca + p.pz; }
            (void)nx; (void)ny; (void)nz; (void)normal;
            o[0] = ox + (Xx * x) * S + fx * z * S; o[1] = oy + y * S; o[2] = oz + (Xz * x) * S + fz * z * S;
        };
        auto N = [&](float nx, float ny, float nz, float* o) {
            float y = ny, z = nz;
            if (p.anim) { float yy = ny, zz = nz; y = yy * ca - zz * sa; z = yy * sa + zz * ca; }
            o[0] = Xx * nx + fx * z; o[1] = y; o[2] = Xz * nx + fz * z;
        };
        float hx = p.w * 0.5f + p.infl, hy = p.h * 0.5f + p.infl, hz = p.d * 0.5f + p.infl;
        // cara: 4 esquinas (en local de la caja), normal, rectangulo UV (u0,v0,u1,v1 en pixeles)
        struct Cara { float c[4][3]; float n[3]; float uv[4]; };
        float U0, V0, U1, V1;
        Cara caras[6];
        int k = 0;
        // frente (+z)
        U0 = p.u + p.d; V0 = p.v + p.d; U1 = U0 + p.w; V1 = V0 + p.h;
        caras[k++] = {{{-hx, hy, hz}, {hx, hy, hz}, {hx, -hy, hz}, {-hx, -hy, hz}}, {0, 0, 1}, {U0, V0, U1, V1}};
        // atras (-z)
        U0 = p.u + 2 * p.d + p.w; U1 = U0 + p.w;
        caras[k++] = {{{hx, hy, -hz}, {-hx, hy, -hz}, {-hx, -hy, -hz}, {hx, -hy, -hz}}, {0, 0, -1}, {U0, V0, U1, V1}};
        // derecha del personaje (-x)
        U0 = p.u; U1 = U0 + p.d;
        caras[k++] = {{{-hx, hy, -hz}, {-hx, hy, hz}, {-hx, -hy, hz}, {-hx, -hy, -hz}}, {-1, 0, 0}, {U0, V0, U1, V1}};
        // izquierda del personaje (+x)
        U0 = p.u + p.d + p.w; U1 = U0 + p.d;
        caras[k++] = {{{hx, hy, hz}, {hx, hy, -hz}, {hx, -hy, -hz}, {hx, -hy, hz}}, {1, 0, 0}, {U0, V0, U1, V1}};
        // arriba (+y)
        U0 = p.u + p.d; V0 = p.v; U1 = U0 + p.w; V1 = V0 + p.d;
        caras[k++] = {{{-hx, hy, -hz}, {hx, hy, -hz}, {hx, hy, hz}, {-hx, hy, hz}}, {0, 1, 0}, {U0, V0, U1, V1}};
        // abajo (-y)
        U0 = p.u + p.d + p.w; U1 = U0 + p.w;
        caras[k++] = {{{-hx, -hy, hz}, {hx, -hy, hz}, {hx, -hy, -hz}, {-hx, -hy, -hz}}, {0, -1, 0}, {U0, V0, U1, V1}};
        for (auto& c : caras) {
            float P[4][3], Nn[3];
            for (int i = 0; i < 4; i++) T(p.cx + c.c[i][0], p.cy + c.c[i][1], p.cz + c.c[i][2], 0, 0, 0, P[i], false);
            N(c.n[0], c.n[1], c.n[2], Nn);
            float uu[4][2] = {{c.uv[0], c.uv[1]}, {c.uv[2], c.uv[1]}, {c.uv[2], c.uv[3]}, {c.uv[0], c.uv[3]}};
            static const int I[6] = {0, 1, 2, 0, 2, 3};
            for (int q : I) {
                out.push_back(P[q][0]); out.push_back(P[q][1]); out.push_back(P[q][2]);
                out.push_back(uu[q][0] / 64.0f); out.push_back(uu[q][1] / 64.0f);
                out.push_back(Nn[0]); out.push_back(Nn[1]); out.push_back(Nn[2]);
            }
        }
    }
    if (items && !capa && (solo < 0 || solo == 2)) jugItem(m, camx, camy, camz, angR, primera ? 1.0f : 0.55f, *items);
}

// ---- objeto en la mano: se extruye un "sprite" 8x8 (herramientas) o un cubito (bloques), con el mismo giro del brazo derecho
static void jugItem(const Remoto& m, double camx, double camy, double camz, float angR, float preRot, std::vector<float>& it) {
    if (m.held == 255) return;
    const float S = 1.8f / 32.0f;
    float fx = std::sin(m.syaw), fz = -std::cos(m.syaw);
    float Xx = fz, Xz = -fx;
    float ox = (float)(m.sx - camx), oy = (float)(m.sy - camy), oz = (float)(m.sz - camz);
    const float Hy = 11.0f, Hz = 1.0f, Hx = -6.0f;                // mano (en el modelo)
    float cp = std::cos(preRot), sp = std::sin(preRot), ca = std::cos(angR), sa = std::sin(angR);
    auto mundo = [&](float x, float y, float z, float* o) {       // modelo -> relativo a la camara, con giro previo del objeto y giro del brazo
        float dy = y - Hy, dz = z - Hz;
        float y1 = Hy + dy * cp - dz * sp, z1 = Hz + dy * sp + dz * cp;
        float yy = y1 - 24.0f, zz = z1;
        float y2 = yy * ca - zz * sa + 24.0f, z2 = yy * sa + zz * ca;
        o[0] = ox + Xx * x * S + fx * z2 * S; o[1] = oy + y2 * S; o[2] = oz + Xz * x * S + fz * z2 * S;
    };
    auto dir = [&](float nx, float ny, float nz, float* o) {
        float y1 = ny * cp - nz * sp, z1 = ny * sp + nz * cp;
        float y2 = y1 * ca - z1 * sa, z2 = y1 * sa + z1 * ca;
        o[0] = Xx * nx + fx * z2; o[1] = y2; o[2] = Xz * nx + fz * z2;
    };
    // una caja alineada con los ejes del modelo [x0,x1]x[y0,y1]x[z0,z1] con color
    auto caja = [&](float x0, float x1, float y0, float y1, float z0, float z1, const float* c, bool fxm, bool fxp, bool fym, bool fyp, bool fzm, bool fzp) {
        struct Cara { float v[4][3]; float n[3]; float sh; bool on; };
        Cara cs[6] = {
            {{{x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}}, {-1, 0, 0}, 0.80f, fxm},
            {{{x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}}, {1, 0, 0}, 0.80f, fxp},
            {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, {0, -1, 0}, 0.55f, fym},
            {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, {0, 1, 0}, 1.00f, fyp},
            {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}, {0, 0, -1}, 0.70f, fzm},
            {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, {0, 0, 1}, 0.90f, fzp}};
        static const int I[6] = {0, 1, 2, 0, 2, 3};
        for (auto& f : cs) {
            if (!f.on) continue;
            float P[4][3], N[3];
            for (int i = 0; i < 4; i++) mundo(f.v[i][0], f.v[i][1], f.v[i][2], P[i]);
            dir(f.n[0], f.n[1], f.n[2], N);
            for (int q : I) { it.push_back(P[q][0]); it.push_back(P[q][1]); it.push_back(P[q][2]); it.push_back(c[0] * f.sh); it.push_back(c[1] * f.sh); it.push_back(c[2] * f.sh); it.push_back(N[0]); it.push_back(N[1]); it.push_back(N[2]); }
        }
    };
    int id = m.held;
    float col[8][8][3]; bool fill[8][8];
    if (itemHerrSprite(id, col, fill)) {           // herramienta: sprite extruido
        const float u = 1.35f, th = 1.0f;
        for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) if (fill[r][c]) {
            float y0 = Hy + (7 - r) * u, z0 = Hz + c * u;
            bool vecino[4] = {c > 0 && fill[r][c - 1], c < 7 && fill[r][c + 1], r < 7 && fill[r + 1][c], r > 0 && fill[r - 1][c]};   // atras, adelante, abajo, arriba
            caja(Hx - th * u * 0.5f, Hx + th * u * 0.5f, y0, y0 + u, z0, z0 + u, col[r][c], true, true, !vecino[2], !vecino[3], !vecino[0], !vecino[1]);
        }
        return;
    }
    float base[3]; itemColor(id, base);
    if (id == 250) {   // spray: lata
        float g[3] = {0.62f, 0.64f, 0.70f}, t[3] = {0.9f, 0.2f, 0.2f};
        caja(Hx - 2, Hx + 2, Hy - 1, Hy + 6, Hz, Hz + 4, g, true, true, true, true, true, true);
        caja(Hx - 1, Hx + 1, Hy + 6, Hy + 8, Hz + 1, Hz + 3, t, true, true, true, true, true, true);
        return;
    }
    // bloque: cubito de 6x6x6 con 3x3 "texturas" por cara (ruido) a la altura de la mano
    const float s = 2.0f; bool mineral = id < 64 && esMineral((uint8_t)id);
    float cx0 = Hx - 3.0f, cy0 = Hy - 1.0f, cz0 = Hz;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) {
        bool borde = a == 0 || a == 2 || b == 0 || b == 2 || c == 0 || c == 2;
        if (!borde) continue;
        unsigned h = (unsigned)(a * 73 + b * 31 + c * 17 + id * 13) * 2654435761u; float v = 0.88f + 0.24f * ((h >> 8 & 255) / 255.0f);
        float cc[3] = {base[0] * v, base[1] * v, base[2] * v};
        if (mineral && ((h >> 4) & 3) != 0) { cc[0] = 0.50f * v; cc[1] = 0.50f * v; cc[2] = 0.52f * v; }
        if (id == PASTO && b == 0) { cc[0] = 0.45f * v; cc[1] = 0.32f * v; cc[2] = 0.20f * v; }
        caja(cx0 + a * s, cx0 + a * s + s, cy0 + b * s, cy0 + b * s + s, cz0 + c * s, cz0 + c * s + s, cc, a == 0, a == 2, b == 0, b == 2, c == 0, c == 2);
    }
}

static const char* VI_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aCol;
layout(location=2) in vec3 aN;
uniform mat4 uVP;
out vec3 vCol; out vec3 vN; out float vDist;
void main(){ vCol = aCol; vN = aN; vDist = length(aPos); gl_Position = uVP * vec4(aPos, 1.0); })";
static const char* FI_SRC = R"(#version 330 core
in vec3 vCol; in vec3 vN; in float vDist;
out vec4 frag;
uniform vec3 uSun, uSky;
uniform float uSolF, uSkyMul, uFogA, uFogB;
void main(){
    vec3 n = normalize(vN);
    float ndl = max(dot(n, uSun), 0.0);
    float lum = (0.40 + 0.22 * (n.y * 0.5 + 0.5) + 0.50 * ndl * uSolF) * uSkyMul + 0.05;
    vec3 col = vCol * lum;
    float f = clamp((vDist - uFogA) / (uFogB - uFogA), 0.0, 1.0);
    frag = vec4(mix(col, uSky, f), 1.0);
})";
static GLuint g_progItem = 0, g_vaoItem = 0, g_vboItem = 0;
static void itemDibujar(const float* vp, const float* sol, float solF, float skyMul, const float* cielo, float fogA, float fogB, const std::vector<float>& v) {
    if (v.empty()) return;
    if (!g_progItem) {
        g_progItem = programa(VI_SRC, FI_SRC);
        glGenVertexArrays(1, &g_vaoItem); glGenBuffers(1, &g_vboItem);
        glBindVertexArray(g_vaoItem);
        glBindBuffer(GL_ARRAY_BUFFER, g_vboItem);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, (const void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 36, (const void*)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 36, (const void*)24);
    }
    glUseProgram(g_progItem);
    auto U = [&](const char* n) { return glGetUniformLocation(g_progItem, n); };
    glUniformMatrix4fv(U("uVP"), 1, GL_FALSE, vp);
    glUniform3f(U("uSun"), sol[0], sol[1], sol[2]);
    glUniform3f(U("uSky"), cielo[0], cielo[1], cielo[2]);
    glUniform1f(U("uSolF"), solF); glUniform1f(U("uSkyMul"), skyMul);
    glUniform1f(U("uFogA"), fogA); glUniform1f(U("uFogB"), fogB);
    glBindVertexArray(g_vaoItem);
    glBindBuffer(GL_ARRAY_BUFFER, g_vboItem);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 9));
}

static void jugActualizar(double dt) {
    for (auto& kv : g_red.rem) {
        Remoto& m = kv.second;
        if (!m.visto) { m.sx = m.x; m.sy = m.y; m.sz = m.z; m.syaw = m.yaw; m.visto = true; }
        float dx = m.x - m.sx, dy = m.y - m.sy, dz = m.z - m.sz;
        if (dx * dx + dy * dy + dz * dz > 25.0f) { m.sx = m.x; m.sy = m.y; m.sz = m.z; }
        else { float k = (float)std::min(1.0, dt * 14.0); m.sx += dx * k; m.sy += dy * k; m.sz += dz * k; }
        float dyw = m.yaw - m.syaw;
        while (dyw > 3.14159265f) dyw -= 6.2831853f;
        while (dyw < -3.14159265f) dyw += 6.2831853f;
        m.syaw += dyw * (float)std::min(1.0, dt * 14.0);
        m.fase += (float)dt * (2.0f + m.vel * 1.7f);
        if (m.flags & 4) { m.swing += (float)dt / 0.36f; if (m.swing >= 1.0f) m.swing -= 1.0f; if (m.swing <= 0) m.swing = 0.001f; }
        else if (m.swing > 0) { m.swing += (float)dt / 0.36f; if (m.swing >= 1.0f) m.swing = 0; }
    }
}

static void jugDibujar(const float* vp, double camx, double camy, double camz, const float* sol, float solF, float skyMul, const float* cielo, float fogA, float fogB) {
    if (g_red.rem.empty()) return;
    jugIniciar();
    glUseProgram(g_progJug);
    auto U = [&](const char* n) { return glGetUniformLocation(g_progJug, n); };
    glUniformMatrix4fv(U("uVP"), 1, GL_FALSE, vp);
    glUniform3f(U("uSun"), sol[0], sol[1], sol[2]);
    glUniform3f(U("uSky"), cielo[0], cielo[1], cielo[2]);
    glUniform1f(U("uSolF"), solF); glUniform1f(U("uSkyMul"), skyMul);
    glUniform1f(U("uFogA"), fogA); glUniform1f(U("uFogB"), fogB);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_texSkin);
    glUniform1i(U("uSkin"), 0);
    glBindVertexArray(g_vaoJug);
    glBindBuffer(GL_ARRAY_BUFFER, g_vboJug);
    std::vector<float> v, items;
    for (auto& kv : g_red.rem) {
        const Remoto& m = kv.second;
        if (!m.visto) continue;
        double dx = m.sx - camx, dy = m.sy - camy, dz = m.sz - camz;
        if (dx * dx + dy * dy + dz * dz > 600.0 * 600.0) continue;
        for (int capa = 0; capa < 2; capa++) {
            v.clear();
            jugGenerar(m, camx, camy, camz, capa != 0, v, capa == 0 ? &items : nullptr);
            glUniform1f(U("uOver"), capa ? 1.0f : 0.0f);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 8));
        }
    }
    itemDibujar(vp, sol, solF, skyMul, cielo, fogA, fogB, items);
}

// mano en primera persona (brazo con la skin + objeto): se dibuja al final sobre todo lo demas
static float g_swingLocal = 0;
static void jugDibujarMano(const float* vp, double cx, double cy, double cz, double yaw, double pitch, uint8_t held, bool golpeando, double dt,
                           const float* sol, float solF, float skyMul, const float* cielo) {
    if (golpeando) { g_swingLocal += (float)dt / 0.36f; if (g_swingLocal >= 1.0f) g_swingLocal -= 1.0f; if (g_swingLocal <= 0) g_swingLocal = 0.001f; }
    else if (g_swingLocal > 0) { g_swingLocal += (float)dt / 0.36f; if (g_swingLocal >= 1.0f) g_swingLocal = 0; }
    jugIniciar();
    Remoto m; m.held = held; m.swing = g_swingLocal; m.vel = 0;
    float cyw = (float)std::cos(yaw), syw = (float)std::sin(yaw);
    float R[3] = {cyw, 0, syw};
    float F[3] = {(float)(std::cos(pitch) * std::sin(yaw)), (float)std::sin(pitch), (float)(-std::cos(pitch) * std::cos(yaw))};
    float Up[3] = {R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0]};
    const float S = 1.8f / 32.0f;
    m.syaw = (float)yaw - 0.30f;
    float fx = std::sin(m.syaw), fz = -std::cos(m.syaw);
    float Xx = fz, Xz = -fx;
    float want[3] = {R[0] * 0.36f - Up[0] * 0.02f + F[0] * 0.42f, R[1] * 0.36f - Up[1] * 0.02f + F[1] * 0.42f, R[2] * 0.36f - Up[2] * 0.02f + F[2] * 0.42f};
    // el hombro del modelo esta en (-6, 24, 0): se coloca el modelo para que caiga en 'want'
    m.sx = (float)cx + want[0] - Xx * (-6.0f) * S; m.sy = (float)cy + want[1] - 24.0f * S; m.sz = (float)cz + want[2] - Xz * (-6.0f) * S;
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glUseProgram(g_progJug);
    auto U = [&](const char* n) { return glGetUniformLocation(g_progJug, n); };
    glUniformMatrix4fv(U("uVP"), 1, GL_FALSE, vp);
    glUniform3f(U("uSun"), sol[0], sol[1], sol[2]);
    glUniform3f(U("uSky"), cielo[0], cielo[1], cielo[2]);
    glUniform1f(U("uSolF"), solF); glUniform1f(U("uSkyMul"), skyMul);
    glUniform1f(U("uFogA"), 1e7f); glUniform1f(U("uFogB"), 2e7f);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_texSkin); glUniform1i(U("uSkin"), 0);
    glBindVertexArray(g_vaoJug); glBindBuffer(GL_ARRAY_BUFFER, g_vboJug);
    std::vector<float> v, items;
    float angP = -(1.20f + (float)pitch * 0.9f);
    for (int capa = 0; capa < 2; capa++) {
        v.clear();
        jugGenerar(m, cx, cy, cz, capa != 0, v, capa == 0 ? &items : nullptr, 2, angP);
        glUniform1f(U("uOver"), capa ? 1.0f : 0.0f);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 8));
    }
    itemDibujar(vp, sol, solF, skyMul, cielo, 1e7f, 2e7f, items);
}

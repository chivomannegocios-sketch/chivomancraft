// Controles tactiles (celular). Logica independiente de la plataforma: la plataforma (Android, o el simulador de pruebas) solo avisa
// de los dedos (down / move / up) y lee las teclas y botones del raton "simulados". Asi main.cpp no cambia entre PC y celular.
//
//  Dentro del juego:
//    - dedo en la mitad izquierda: joystick flotante (W A S D)        - dedo en la derecha: mirar (arrastrar)
//    - botones: SALTO, ROMPER (mantener), PONER (mantener), VOLAR, MENU, PAUSA, PINCEL, BAJAR, G (subir/bajar de vehiculo), H (desarmar)
//    - tocar la barra de acceso rapido elige el material
//    - pilotando: GAS+ / GAS- / FRENO / timon Q E / camara / G, y el joystick es la palanca (W S = nariz, A D = inclinar)
//  En menus: tocar = clic, arrastrar = desplazar la lista.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

enum TId { T_SALTO, T_ROMPER, T_PONER, T_VOLAR, T_MENU, T_PAUSA, T_BAJAR, T_PINCEL, T_G, T_H, T_CAM, T_Q, T_E, T_GASM, T_GASL, T_FRENO, T_Z, T_C, T_CHAT, T_SOLTAR, T_HORA, T_GUARDAR, T_KB, T_ENVIAR };
// codigos de tecla (iguales a los de GLFW)
enum { TK_SPACE = 32, TK_ESC = 256, TK_SHIFT = 340, TK_CTRL = 341, TK_TAB = 258, TK_W = 87, TK_A = 65, TK_S = 83, TK_D = 68,
       TK_F = 70, TK_E = 69, TK_B = 66, TK_G = 71, TK_H = 72, TK_Q = 81, TK_Z = 90, TK_C = 67, TK_1 = 49 };

struct TBtn { int id; const char* txt; float cx, cy, r; int tecla; bool mantener; };
struct Dedo { int id = -1; float x = 0, y = 0, x0 = 0, y0 = 0; int rol = 0; int btn = -1; bool scroll = false; float acc = 0; };   // rol: 1 joystick, 2 mirar, 3 boton, 4 menu, 5 barra

struct Tactil {
    int fbw = 1280, fbh = 720;
    bool ui = false;        // hay un menu en pantalla (el dedo hace de raton)
    bool uiJuego = false;   // menu dentro del juego (materiales / pausa): se muestra un boton para volver
    bool pil = false;       // pilotando
    bool extra = false;     // herramienta con Z / C (spray, rueda)
    bool volando = false;
    bool chat = false;      // escribiendo en el chat (el juego pide teclado)
    bool texto = false;     // se necesita el teclado del telefono (chat o direccion IP)
    bool pedirKb = false;   // el jugador toco el boton TECLADO (la plataforma lo muestra de nuevo)
    bool activo = false;    // se uso la pantalla tactil alguna vez
    bool teclas[512];
    bool raton[2];
    std::vector<TBtn> btns;
    Dedo dedos[10];
    float jx0 = 0, jy0 = 0, jx = 0, jy = 0;
    double cx = 640, cy = 360;
    std::function<void(int)> alTecla;
    std::function<void(double, double)> alCursor;
    std::function<void(double)> alScroll;
    std::function<void(unsigned)> alChar;     // caracter escrito con el teclado del telefono
    std::function<void(int, int)> alBoton;    // boton (0 izq, 1 der), accion (1 pulsar, 0 soltar)

    Tactil() { std::memset(teclas, 0, sizeof teclas); raton[0] = raton[1] = false; }

    float radio() const { return 0.075f * (float)std::min(fbw, fbh); }
    float radioJoy() const { return 0.13f * (float)std::min(fbw, fbh); }

    void layout() {
        btns.clear();
        float r = radio(), W = (float)fbw, H = (float)fbh;
        auto add = [&](int id, const char* t, float x, float y, float rr, int tecla, bool mant) { btns.push_back({id, t, x, y, rr, tecla, mant}); };
        if (chat) {       // escribiendo: botones arriba, el teclado ocupa la mitad de abajo
            add(T_ENVIAR, "ENVIAR", W - 1.3f * r, 0.9f * r, 0.8f * r, 257, false);
            add(T_KB, "TECLADO", W - 3.3f * r, 0.9f * r, 0.8f * r, -1, false);
            add(T_PAUSA, "CERRAR", W - 5.3f * r, 0.9f * r, 0.8f * r, TK_ESC, false);
            return;
        }
        if (ui) {
            if (texto) { add(T_ENVIAR, "OK", W - 1.3f * r, 0.9f * r, 0.8f * r, 257, false); add(T_KB, "TECLADO", W - 3.3f * r, 0.9f * r, 0.8f * r, -1, false); }
            if (uiJuego) add(T_PAUSA, "ATRAS", W - 1.3f * r, 0.9f * r, 0.8f * r, TK_ESC, false);
            return;
        }
        add(T_PAUSA, "PAUSA", W - 1.0f * r, 0.8f * r, 0.7f * r, TK_ESC, false);
        if (pil) {
            add(T_G, "BAJAR", W - 2.6f * r, 0.8f * r, 0.7f * r, TK_G, false);
            add(T_CAM, "CAMARA", W - 4.2f * r, 0.8f * r, 0.7f * r, TK_TAB, false);
            add(T_GASL, "GAS-", W - 2.0f * r, H - 2.4f * r, r, TK_CTRL, true);
            add(T_GASM, "GAS+", W - 5.0f * r, H - 2.4f * r, r, TK_SHIFT, true);
            add(T_FRENO, "FRENO", W - 8.0f * r, H - 2.4f * r, r, TK_SPACE, true);
            add(T_E, "E", W - 2.0f * r, H - 5.2f * r, r, TK_E, true);
            add(T_Q, "Q", W - 5.0f * r, H - 5.2f * r, r, TK_Q, true);
            return;
        }
        add(T_CHAT, "CHAT", W - 1.0f * r, 2.4f * r, 0.7f * r, 257, false);                  // chat y comandos (abre el teclado)
        add(T_SOLTAR, "SOLTAR", W - 2.6f * r, 2.4f * r, 0.7f * r, TK_Q, false);             // Q: botar el objeto elegido
        add(T_HORA, "HORA", W - 4.2f * r, 2.4f * r, 0.7f * r, 84, false);                   // T: cambiar la hora
        add(T_GUARDAR, "GUARDAR", W - 5.8f * r, 2.4f * r, 0.7f * r, 294, false);            // F5
        add(T_MENU, "MENU", W - 2.6f * r, 0.8f * r, 0.7f * r, TK_E, false);
        add(T_VOLAR, "VOLAR", W - 4.2f * r, 0.8f * r, 0.7f * r, TK_F, false);
        add(T_PINCEL, "PINCEL", W - 5.8f * r, 0.8f * r, 0.7f * r, TK_B, false);
        add(T_G, "G", W - 7.4f * r, 0.8f * r, 0.7f * r, TK_G, false);
        add(T_H, "H", W - 9.0f * r, 0.8f * r, 0.7f * r, TK_H, false);
        add(T_PONER, "PONER", W - 2.0f * r, H - 2.4f * r, r, -1, true);
        add(T_ROMPER, "ROMPER", W - 5.0f * r, H - 2.4f * r, r, -1, true);
        add(T_SALTO, "SALTO", W - 3.5f * r, H - 5.2f * r, 1.1f * r, TK_SPACE, true);
        if (volando) add(T_BAJAR, "BAJAR", W - 6.8f * r, H - 5.2f * r, r, TK_SHIFT, true);
        if (extra) {
            add(T_Z, "Z -", W - 8.0f * r, H - 2.4f * r, 0.8f * r, TK_Z, false);
            add(T_C, "C +", W - 9.6f * r, H - 2.4f * r, 0.8f * r, TK_C, false);
        }
    }
    int hitBtn(float x, float y) const {
        for (size_t i = 0; i < btns.size(); i++) {
            float dx = x - btns[i].cx, dy = y - btns[i].cy, rr = btns[i].r * 1.15f;
            if (dx * dx + dy * dy <= rr * rr) return (int)i;
        }
        return -1;
    }
    int hotbarHit(float x, float y) const {
        if (pil || ui) return -1;
        float k = std::max(0.8f, fbh / 720.0f);
        float S = std::floor(48.0f * k), gap = std::floor(5.0f * k);
        float total = 9 * S + 8 * gap, x0 = (fbw - total) / 2.0f, y0 = fbh - S - 16 * k;
        if (y < y0 - 0.2f * S || y > y0 + S + 0.4f * S) return -1;
        for (int i = 0; i < 9; i++) {
            float sx = x0 + i * (S + gap);
            if (x >= sx - gap * 0.5f && x <= sx + S + gap * 0.5f) return i;
        }
        return -1;
    }
    bool boton(int id) const {       // esta pulsado ese boton?
        for (auto& d : dedos) if (d.id >= 0 && d.rol == 3 && d.btn == id) return true;
        return false;
    }
    bool joystickActivo() const { for (auto& d : dedos) if (d.id >= 0 && d.rol == 1) return true; return false; }

    // recalcula las teclas y botones del raton "mantenidos"
    void recalc() {
        layout();
        std::memset(teclas, 0, sizeof teclas);
        raton[0] = raton[1] = false;
        float R = radioJoy();
        for (auto& d : dedos) {
            if (d.id < 0) continue;
            if (d.rol == 1) {
                float dx = (d.x - jx0) / R, dy = (d.y - jy0) / R;
                if (dy < -0.35f) teclas[TK_W] = true;
                if (dy > 0.35f) teclas[TK_S] = true;
                if (dx < -0.35f) teclas[TK_A] = true;
                if (dx > 0.35f) teclas[TK_D] = true;
                jx = d.x; jy = d.y;
            } else if (d.rol == 3) {
                for (auto& b : btns) if (b.id == d.btn && b.mantener) {
                    if (b.tecla >= 0) teclas[b.tecla] = true;
                    if (b.id == T_PONER) raton[1] = true;
                    if (b.id == T_ROMPER) raton[0] = true;
                }
            }
        }
    }
    Dedo* buscar(int id) { for (auto& d : dedos) if (d.id == id) return &d; return nullptr; }

    void down(int id, float x, float y) {
        activo = true;
        Dedo* d = buscar(id);
        if (!d) for (auto& e : dedos) if (e.id < 0) { d = &e; break; }
        if (!d) return;
        *d = Dedo();
        d->id = id; d->x = d->x0 = x; d->y = d->y0 = y;
        layout();
        int b = hitBtn(x, y);
        if (b >= 0) {
            d->rol = 3; d->btn = btns[b].id;
            if (btns[b].id == T_KB) pedirKb = true;
            if (!btns[b].mantener && btns[b].tecla >= 0 && alTecla) alTecla(btns[b].tecla);
            recalc();
            return;
        }
        if (chat) { d->rol = 0; pedirKb = true; return; }       // tocar en cualquier lado vuelve a mostrar el teclado
        if (ui) {
            d->rol = 4;
            if (alCursor) alCursor(x, y);
            return;
        }
        int s = hotbarHit(x, y);
        if (s >= 0) { d->rol = 5; if (alTecla) alTecla(TK_1 + s); return; }
        if (x < fbw * 0.45f) { d->rol = 1; jx0 = x; jy0 = y; jx = x; jy = y; }
        else { d->rol = 2; }
        recalc();
    }
    void move(int id, float x, float y) {
        Dedo* d = buscar(id);
        if (!d) return;
        float dx = x - d->x, dy = y - d->y;
        d->x = x; d->y = y;
        if (d->rol == 1) recalc();
        else if (d->rol == 2) {
            const double ganancia = 1.5;
            cx += dx * ganancia; cy += dy * ganancia;
            if (alCursor) alCursor(cx, cy);
        } else if (d->rol == 4) {
            float ddx = x - d->x0, ddy = y - d->y0;
            if (!d->scroll && ddx * ddx + ddy * ddy > 18.0f * 18.0f) { d->scroll = true; d->acc = 0; }
            if (d->scroll) {
                d->acc += dy;
                const float paso = std::max(30.0f, fbh * 0.05f);
                while (d->acc <= -paso) { if (alScroll) alScroll(-1); d->acc += paso; }
                while (d->acc >= paso) { if (alScroll) alScroll(1); d->acc -= paso; }
            }
        }
    }
    void up(int id, float x, float y) {
        Dedo* d = buscar(id);
        if (!d) return;
        (void)x; (void)y;
        if (d->rol == 4 && !d->scroll) {
            if (alCursor) alCursor(d->x0, d->y0);
            if (alBoton) { alBoton(0, 1); alBoton(0, 0); }
        }
        d->id = -1;
        recalc();
    }
    void cancelar() { for (auto& d : dedos) d.id = -1; recalc(); }
};
static Tactil g_tac;

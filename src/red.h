// Multijugador por red (LAN / IP / Tailscale). Un jugador "hospeda" su mundo y los demas se unen por IP.
//  - TCP con mensajes [u32 largo][u8 tipo][datos]
//  - Al unirse, el anfitrion manda la semilla y los chunks editados; despues solo viajan las ediciones y las posiciones.
#pragma once
#include "red_sock.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

enum { RM_HELLO = 1, RM_WELCOME, RM_FILE, RM_SYNCEND, RM_EDITS, RM_STATE, RM_PLAYERS, RM_JOIN, RM_LEAVE, RM_VEH_SET = 10, RM_VEH_CLEAR, RM_VEH_POSE, RM_CHAT };
static const int RED_PUERTO = 25565;

struct RedEdit { int32_t x, y, z; uint8_t t; };
struct Remoto {
    int id = 0; std::string nombre;
    float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, vel = 0; uint8_t flags = 0, held = 255;   // flags: 1 volando, 2 sentado/pilotando, 4 golpeando; held: objeto en la mano (255 = nada)
    float sx = 0, sy = 0, sz = 0, syaw = 0, fase = 0, swing = 0; bool visto = false;       // suavizado y animacion (lado local)
};
struct RedEstado { float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, vel = 0; uint8_t flags = 0, held = 255; };

struct RedW {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
    void f(float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); }
    void s(const std::string& t) { u8((uint8_t)std::min<size_t>(t.size(), 200)); for (size_t i = 0; i < t.size() && i < 200; i++) b.push_back((uint8_t)t[i]); }
    void raw(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
};
struct RedR {
    const uint8_t* p; size_t n, i = 0; bool ok = true;
    RedR(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
    uint8_t u8() { if (i + 1 > n) { ok = false; return 0; } return p[i++]; }
    uint32_t u32() { if (i + 4 > n) { ok = false; return 0; } uint32_t v = 0; for (int k = 0; k < 4; k++) v |= (uint32_t)p[i + k] << (8 * k); i += 4; return v; }
    float f() { uint32_t u = u32(); float v; std::memcpy(&v, &u, 4); return v; }
    std::string s() { size_t l = u8(); if (i + l > n) { ok = false; return ""; } std::string t((const char*)p + i, l); i += l; return t; }
    size_t resto() const { return n - i; }
};

struct RedConn { int s = -1; int id = 0; std::vector<uint8_t> in, out; size_t outPos = 0; std::string nombre; bool vivo = true, listo = false; };

struct Red {
    bool host = false, cli = false;
    int srv = -1, miId = 0, sigId = 1;
    std::string miNombre = "Jugador", error;
    std::vector<RedConn> con;                 // anfitrion: un cliente por casilla;  cliente: con[0] es el anfitrion
    std::map<int, Remoto> rem;                // jugadores remotos (id -> estado)
    std::vector<RedEdit> sal;                 // ediciones locales pendientes de enviar
    RedEstado yo;                             // mi estado (lo llena el juego cada cuadro)
    uint32_t semilla = 0; float spawn[3] = {0, 0, 0}; bool hayAnfitrionPos = false;
    float horaRecibida = -1, horaLocal = 0;   // anfitrion: horaLocal (la fija el juego); cliente: horaRecibida
    double tEnvio = 0;
    std::string dirMundo;
    std::function<void(const RedEdit*, int)> aplicar;     // aplicar ediciones que llegaron
    std::function<void()> guardarYa;                      // anfitrion: baja los chunks sucios a disco antes de sincronizar
    std::function<void(uint8_t, RedR&, int)> alMensaje;   // mensajes del vehiculo (tipo, datos, id del que los manda)
    std::function<void(int, const std::string&)> alChat;  // llego un mensaje de chat (id del autor, texto)
    std::function<void(int)> alNuevo;                     // anfitrion: se acaba de unir el jugador con ese id
    bool activo() const { return host || cli; }
    void difundir(uint8_t tipo, const RedW& w, int excepto = -2) {
        for (auto& c : con) if ((host ? c.listo : true) && c.id != excepto) poner(c, tipo, w);
    }
    void enviarA(int id, uint8_t tipo, const RedW& w) { for (auto& c : con) if (c.listo && c.id == id) poner(c, tipo, w); }
    void chatEnviar(const std::string& t) {
        if (host) { if (alChat) alChat(0, t); RedW w; w.u8(0); w.s(t); difundir(RM_CHAT, w); }
        else if (!con.empty()) { RedW w; w.s(t); poner(con[0], RM_CHAT, w); }
    }

    static void poner(RedConn& c, uint8_t tipo, const RedW& w) {
        uint32_t len = (uint32_t)w.b.size() + 1;
        for (int i = 0; i < 4; i++) c.out.push_back((uint8_t)(len >> (8 * i)));
        c.out.push_back(tipo);
        c.out.insert(c.out.end(), w.b.begin(), w.b.end());
    }
    static bool tomar(RedConn& c, uint8_t& tipo, std::vector<uint8_t>& datos) {
        if (c.in.size() < 5) return false;
        uint32_t len = 0; for (int i = 0; i < 4; i++) len |= (uint32_t)c.in[i] << (8 * i);
        if (len == 0 || len > 4u * 1024 * 1024) { c.vivo = false; c.in.clear(); return false; }
        if (c.in.size() < 4 + (size_t)len) return false;
        tipo = c.in[4];
        datos.assign(c.in.begin() + 5, c.in.begin() + 4 + len);
        c.in.erase(c.in.begin(), c.in.begin() + 4 + len);
        return true;
    }
    static bool nombreChunkValido(const std::string& n) {
        if (n.size() < 8 || n.size() > 48 || n.compare(0, 2, "c_") != 0 || n.compare(n.size() - 4, 4, ".bin") != 0) return false;
        for (char ch : n) if (!((ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.' || ch == 'c' || ch == 'b' || ch == 'i' || ch == 'n')) return false;
        return true;
    }

    // ---------------------------------------------------------------- anfitrion
    bool hospedar(int puerto) {
        srv = rs_listen(puerto);
        if (srv < 0) { error = "No pude abrir el puerto " + std::to_string(puerto) + " (otro juego abierto?)"; return false; }
        host = true; miId = 0;
        return true;
    }
    void anfitrionHello(RedConn& c, RedR& r) {
        c.nombre = r.s(); if (c.nombre.empty()) c.nombre = "Jugador";
        c.id = sigId++;
        if (guardarYa) guardarYa();
        std::vector<std::string> arch;
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(dirMundo, ec)) {
            std::string n = e.path().filename().string();
            if (nombreChunkValido(n)) arch.push_back(n);
        }
        RedW w; w.u32(semilla); w.u8((uint8_t)c.id); w.f(yo.x); w.f(yo.y); w.f(yo.z); w.u32((uint32_t)arch.size());
        poner(c, RM_WELCOME, w);
        for (auto& n : arch) {
            FILE* f = std::fopen((dirMundo + "/" + n).c_str(), "rb");
            if (!f) continue;
            RedW fw; fw.s(n);
            uint8_t buf[65536]; size_t k;
            while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) fw.raw(buf, k);
            std::fclose(f);
            poner(c, RM_FILE, fw);
        }
        poner(c, RM_SYNCEND, RedW());
        { RedW j; j.u8(0); j.s(miNombre); poner(c, RM_JOIN, j); }
        for (auto& o : con) if (&o != &c && o.listo) { RedW j; j.u8((uint8_t)o.id); j.s(o.nombre); poner(c, RM_JOIN, j); }
        RedW j; j.u8((uint8_t)c.id); j.s(c.nombre);
        for (auto& o : con) if (&o != &c && o.listo) poner(o, RM_JOIN, j);
        rem[c.id].id = c.id; rem[c.id].nombre = c.nombre;
        c.listo = true;
        if (alNuevo) alNuevo(c.id);
        std::printf("RED: se unio %s (id %d)\n", c.nombre.c_str(), c.id);
    }

    // ---------------------------------------------------------------- cliente
    // Se conecta y baja el mundo ANTES de empezar (bloquea). Devuelve false y deja el motivo en 'error'.
    bool unirse(const std::string& ip, int puerto, const std::string& destino) {
        int s = rs_connect(ip.c_str(), puerto, 8000);
        if (s < 0) { error = "No se pudo conectar a " + ip + ":" + std::to_string(puerto); return false; }
        con.clear(); con.emplace_back(); RedConn& c = con[0]; c.s = s;
        { RedW w; w.s(miNombre); poner(c, RM_HELLO, w); }
        std::error_code ec;
        std::filesystem::remove_all(destino, ec);
        std::filesystem::create_directories(destino, ec);
        dirMundo = destino;
        bool fin = false; int ancho = 0; int nArch = 0;
        double t0 = 0; int espera = 0;
        while (!fin) {
            vaciar(c);
            uint8_t buf[65536];
            int n = rs_recv(s, buf, sizeof buf);
            if (n < 0) { error = "El anfitrion cerro la conexion"; return false; }
            if (n > 0) { c.in.insert(c.in.end(), buf, buf + n); espera = 0; }
            else { if (!rs_esperar(s, 100)) espera++; if (espera > 300) { error = "El anfitrion no responde"; return false; } }
            uint8_t tipo; std::vector<uint8_t> d;
            while (!fin && tomar(c, tipo, d)) {
                RedR r(d);
                if (tipo == RM_WELCOME) { semilla = r.u32(); miId = r.u8(); spawn[0] = r.f(); spawn[1] = r.f(); spawn[2] = r.f(); nArch = (int)r.u32(); hayAnfitrionPos = true; }
                else if (tipo == RM_FILE) {
                    std::string nom = r.s();
                    if (nombreChunkValido(nom)) {
                        FILE* f = std::fopen((destino + "/" + nom).c_str(), "wb");
                        if (f) { std::fwrite(d.data() + r.i, 1, r.resto(), f); std::fclose(f); }
                    }
                    ancho++;
                    if (ancho % 50 == 0) std::printf("RED: bajando mundo %d/%d\n", ancho, nArch);
                } else if (tipo == RM_SYNCEND) fin = true;
            }
            (void)t0;
        }
        cli = true;
        return true;
    }

    // ---------------------------------------------------------------- cada cuadro
    void vaciar(RedConn& c) {
        while (c.outPos < c.out.size()) {
            int n = rs_send(c.s, c.out.data() + c.outPos, (int)std::min<size_t>(c.out.size() - c.outPos, 65536));
            if (n < 0) { c.vivo = false; return; }
            if (n == 0) return;
            c.outPos += n;
        }
        c.out.clear(); c.outPos = 0;
    }
    void procesar(RedConn& c, uint8_t tipo, const std::vector<uint8_t>& d) {
        RedR r(d);
        if (host) {
            if (tipo == RM_HELLO && !c.listo) anfitrionHello(c, r);
            else if (!c.listo) return;
            else if (tipo == RM_STATE) {
                Remoto& m = rem[c.id]; m.id = c.id; if (m.nombre.empty()) m.nombre = c.nombre;
                m.x = r.f(); m.y = r.f(); m.z = r.f(); m.yaw = r.f(); m.pitch = r.f(); m.vel = r.f(); m.flags = r.u8(); m.held = r.u8();
            } else if (tipo == RM_EDITS) {
                std::vector<RedEdit> v; leerEdits(r, v);
                if (!v.empty() && aplicar) aplicar(v.data(), (int)v.size());
                RedW w; w.raw(d.data(), d.size());
                for (auto& o : con) if (&o != &c && o.listo) poner(o, RM_EDITS, w);
            } else if (tipo == RM_CHAT) {
                std::string t = r.s();
                if (!t.empty()) { if (alChat) alChat(c.id, t); RedW w; w.u8((uint8_t)c.id); w.s(t); difundir(RM_CHAT, w); }
            } else if (tipo >= RM_VEH_SET && tipo <= RM_VEH_POSE) {
                if (alMensaje) alMensaje(tipo, r, c.id);
                RedW w; w.raw(d.data(), d.size());
                difundir(tipo, w, c.id);
            }
        } else {
            if (tipo == RM_PLAYERS) {
                horaRecibida = r.f();
                int n = r.u8();
                for (int i = 0; i < n && r.ok; i++) {
                    int id = r.u8(); Remoto& m = rem[id]; m.id = id;
                    m.x = r.f(); m.y = r.f(); m.z = r.f(); m.yaw = r.f(); m.pitch = r.f(); m.vel = r.f(); m.flags = r.u8(); m.held = r.u8();
                }
            } else if (tipo == RM_EDITS) {
                std::vector<RedEdit> v; leerEdits(r, v);
                if (!v.empty() && aplicar) aplicar(v.data(), (int)v.size());
            } else if (tipo == RM_CHAT) { int id = r.u8(); std::string t = r.s(); if (alChat) alChat(id, t); }
            else if (tipo >= RM_VEH_SET && tipo <= RM_VEH_POSE) { if (alMensaje) alMensaje(tipo, r, 0); }
            else if (tipo == RM_JOIN) { int id = r.u8(); std::string n = r.s(); rem[id].id = id; rem[id].nombre = n; }
            else if (tipo == RM_LEAVE) { rem.erase(r.u8()); }
        }
    }
    static void leerEdits(RedR& r, std::vector<RedEdit>& v) {
        uint32_t n = r.u32();
        for (uint32_t i = 0; i < n && r.ok; i++) { RedEdit e; e.x = (int32_t)r.u32(); e.y = (int32_t)r.u32(); e.z = (int32_t)r.u32(); e.t = r.u8(); if (r.ok) v.push_back(e); }
    }
    void estadoA(RedW& w, const RedEstado& e) { w.f(e.x); w.f(e.y); w.f(e.z); w.f(e.yaw); w.f(e.pitch); w.f(e.vel); w.u8(e.flags); w.u8(e.held); }

    void paso(double ahora) {
        if (!activo()) return;
        if (host) {
            for (int k = 0; k < 4; k++) { int s = rs_accept(srv); if (s < 0) break; RedConn c; c.s = s; con.push_back(std::move(c)); }
        }
        for (auto& c : con) {
            for (int it = 0; it < 64; it++) {
                uint8_t buf[65536];
                int n = rs_recv(c.s, buf, sizeof buf);
                if (n < 0) { c.vivo = false; break; }
                if (n == 0) break;
                c.in.insert(c.in.end(), buf, buf + n);
            }
            uint8_t tipo; std::vector<uint8_t> d;
            int tope = 4000;
            while (c.vivo && tope-- > 0 && tomar(c, tipo, d)) procesar(c, tipo, d);
        }
        // ediciones locales
        if (!sal.empty()) {
            RedW w; w.u32((uint32_t)sal.size());
            for (auto& e : sal) { w.u32((uint32_t)e.x); w.u32((uint32_t)e.y); w.u32((uint32_t)e.z); w.u8(e.t); }
            sal.clear();
            for (auto& c : con) if (host ? c.listo : true) poner(c, RM_EDITS, w);
        }
        // estado: 20 veces por segundo
        if (ahora - tEnvio >= 0.05) {
            tEnvio = ahora;
            if (host) {
                for (auto& c : con) {
                    if (!c.listo) continue;
                    RedW w; w.f(horaLocal);
                    int n = 0; RedW cuerpo;
                    { cuerpo.u8(0); estadoA(cuerpo, yo); n++; }
                    for (auto& kv : rem) if (kv.first != c.id) { cuerpo.u8((uint8_t)kv.first); RedEstado e{kv.second.x, kv.second.y, kv.second.z, kv.second.yaw, kv.second.pitch, kv.second.vel, kv.second.flags, kv.second.held}; estadoA(cuerpo, e); n++; }
                    w.u8((uint8_t)n); w.raw(cuerpo.b.data(), cuerpo.b.size());
                    poner(c, RM_PLAYERS, w);
                }
            } else if (!con.empty()) { RedW w; estadoA(w, yo); poner(con[0], RM_STATE, w); }
        }
        for (auto& c : con) if (c.vivo) { c.listo = host ? c.listo : true; vaciar(c); }
        // desconectados
        for (size_t i = 0; i < con.size();) {
            if (!con[i].vivo) {
                if (host) {
                    if (con[i].listo) {
                        std::printf("RED: salio %s\n", con[i].nombre.c_str());
                        RedW w; w.u8((uint8_t)con[i].id);
                        for (auto& o : con) if (&o != &con[i] && o.listo && o.vivo) poner(o, RM_LEAVE, w);
                        rem.erase(con[i].id);
                    }
                    rs_close(con[i].s); con.erase(con.begin() + i);
                } else { error = "Se perdio la conexion con el anfitrion"; rs_close(con[i].s); con.clear(); cli = false; rem.clear(); break; }
            } else i++;
        }
    }
    void cerrar() {
        for (auto& c : con) { vaciar(c); rs_close(c.s); }
        con.clear();
        if (srv >= 0) rs_close(srv);
        srv = -1; host = cli = false;
    }
};
static Red g_red;

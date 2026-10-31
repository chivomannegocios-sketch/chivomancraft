// Capa minima de sockets TCP (Windows / Linux / Android). Los manejadores son enteros; -1 = error.
#pragma once
#include <cstdint>
int rs_listen(int puerto);                                   // escucha (no bloqueante)
int rs_accept(int srv);                                      // acepta uno (no bloqueante); -1 si no hay
int rs_connect(const char* host, int puerto, int ms);        // conecta (bloquea hasta ms); luego queda no bloqueante
int rs_send(int s, const void* d, int n);                    // bytes enviados, 0 si no cabe ahora, -1 si se cerro
int rs_recv(int s, void* d, int n);                          // bytes leidos, 0 si no hay nada, -1 si se cerro
int rs_esperar(int s, int ms);                               // espera a que haya datos para leer (1 si hay)
void rs_close(int s);
const char* rs_ip_local();                                   // IP de esta PC en la red local (para decirsela a los amigos)
// UDP para descubrir partidas en la red local (WiFi)
int rs_udp_abrir(int puerto);                                // escucha anuncios en ese puerto (no bloqueante); -1 si falla
int rs_udp_recibir(int s, char* d, int n, char* ip, int ipn);// bytes leidos (0 si nada) y la IP de quien lo mando
int rs_udp_difundir(int puerto, const char* d, int n);       // manda un anuncio a toda la red local

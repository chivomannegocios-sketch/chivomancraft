// Android: imita el pedacito de GLFW que usa el juego (ventana, entrada, tiempo) sobre NativeActivity + EGL (OpenGL ES 3.0).
// Asi main.cpp es el mismo en PC y en celular. Solo se compila con -DANDROID_GAME (ver android/app/src/main/cpp/CMakeLists.txt).
#pragma once
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <algorithm>
#include "tactil.h"

#define GLFW_CONTEXT_VERSION_MAJOR 1
#define GLFW_CONTEXT_VERSION_MINOR 2
#define GLFW_OPENGL_PROFILE 3
#define GLFW_OPENGL_CORE_PROFILE 4
#define GLFW_SAMPLES 5
#define GLFW_CURSOR 6
#define GLFW_CURSOR_NORMAL 7
#define GLFW_CURSOR_DISABLED 8
#define GLFW_RAW_MOUSE_MOTION 9
#define GLFW_TRUE 1
#define GLFW_PRESS 1
#define GLFW_RELEASE 0
#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_SPACE 32
#define GLFW_KEY_W 87
#define GLFW_KEY_A 65
#define GLFW_KEY_S 83
#define GLFW_KEY_D 68
#define GLFW_KEY_F 70
#define GLFW_KEY_F5 294
#define GLFW_KEY_N 78
#define GLFW_KEY_ENTER 257
#define GLFW_KEY_KP_ENTER 335
#define GLFW_KEY_B 66
#define GLFW_KEY_V 86
#define GLFW_KEY_1 49
#define GLFW_KEY_7 55
#define GLFW_KEY_9 57
#define GLFW_KEY_LEFT_SHIFT 340
#define GLFW_KEY_LEFT_CONTROL 341
#define GLFW_KEY_E 69
#define GLFW_KEY_T 84
#define GLFW_KEY_R 82
#define GLFW_KEY_X 88
#define GLFW_KEY_0 48
#define GLFW_KEY_G 71
#define GLFW_KEY_H 72
#define GLFW_KEY_Z 90
#define GLFW_KEY_C 67
#define GLFW_KEY_Q 81
#define GLFW_KEY_TAB 258

struct GLFWwindow { int dummy; };
typedef void (*GLFWglproc)(void);
typedef void (*FMouse)(GLFWwindow*, double, double);
typedef void (*FScroll)(GLFWwindow*, double, double);
typedef void (*FKey)(GLFWwindow*, int, int, int, int);
typedef void (*FBtn)(GLFWwindow*, int, int, int);

static GLFWwindow A_w;
static android_app* A_app = nullptr;
static EGLDisplay A_dpy = EGL_NO_DISPLAY;
static EGLSurface A_surf = EGL_NO_SURFACE;
static EGLContext A_ctx = EGL_NO_CONTEXT;
static EGLConfig A_cfg = nullptr;
static int A_w_px = 0, A_h_px = 0;       // tamano de la superficie de dibujo
static float A_esc = 1.0f;               // pantalla -> superficie (dibujamos a menor resolucion y Android la estira)
static bool A_cerrar = false, A_ventanaNueva = false;
static bool A_pausado = false;           // avisa a main.cpp para guardar al pasar a segundo plano
static void* A_libGLES = nullptr;

static void A_log(const char* s) { __android_log_print(ANDROID_LOG_INFO, "JuegoVoxel", "%s", s); }

static void A_destruirSuperficie() {
    if (A_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(A_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, A_ctx);
        if (A_surf != EGL_NO_SURFACE) eglDestroySurface(A_dpy, A_surf);
    }
    A_surf = EGL_NO_SURFACE;
}
static void A_crearSuperficie() {
    ANativeWindow* win = A_app->window;
    if (!win || A_dpy == EGL_NO_DISPLAY) return;
    if (A_surf != EGL_NO_SURFACE) A_destruirSuperficie();
    int ow = ANativeWindow_getWidth(win), oh = ANativeWindow_getHeight(win);
    float esc = std::min(1.0f, 720.0f / (float)std::min(ow, oh));      // el lado corto se dibuja a 720 pixeles como maximo
    int bw = std::max(2, (int)(ow * esc)), bh = std::max(2, (int)(oh * esc));
    EGLint fmt = 0;
    eglGetConfigAttrib(A_dpy, A_cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(win, bw, bh, fmt);
    A_surf = eglCreateWindowSurface(A_dpy, A_cfg, win, nullptr);
    if (A_surf == EGL_NO_SURFACE) { A_log("No se pudo crear la superficie EGL"); return; }
    eglMakeCurrent(A_dpy, A_surf, A_surf, A_ctx);
    EGLint w = 0, h = 0;
    eglQuerySurface(A_dpy, A_surf, EGL_WIDTH, &w);
    eglQuerySurface(A_dpy, A_surf, EGL_HEIGHT, &h);
    A_w_px = w > 0 ? w : bw; A_h_px = h > 0 ? h : bh;
    A_esc = (float)A_w_px / (float)std::max(1, ow);
}

static void A_cmd(android_app*, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: A_ventanaNueva = true; break;
        case APP_CMD_TERM_WINDOW: A_destruirSuperficie(); g_tac.cancelar(); break;
        case APP_CMD_PAUSE: A_pausado = true; break;
        case APP_CMD_DESTROY: A_cerrar = true; break;
        default: break;
    }
}
static int32_t A_input(android_app*, AInputEvent* e) {
    int tipo = AInputEvent_getType(e);
    if (tipo == AINPUT_EVENT_TYPE_MOTION) {
        int32_t a = AMotionEvent_getAction(e);
        int act = a & AMOTION_EVENT_ACTION_MASK;
        int idx = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        float s = A_esc;
        switch (act) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                g_tac.down(AMotionEvent_getPointerId(e, idx), AMotionEvent_getX(e, idx) * s, AMotionEvent_getY(e, idx) * s);
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                g_tac.up(AMotionEvent_getPointerId(e, idx), AMotionEvent_getX(e, idx) * s, AMotionEvent_getY(e, idx) * s);
                break;
            case AMOTION_EVENT_ACTION_CANCEL:
                g_tac.cancelar();
                break;
            case AMOTION_EVENT_ACTION_MOVE: {
                size_t n = AMotionEvent_getPointerCount(e);
                for (size_t i = 0; i < n; i++) g_tac.move(AMotionEvent_getPointerId(e, i), AMotionEvent_getX(e, i) * s, AMotionEvent_getY(e, i) * s);
                break;
            }
            default: break;
        }
        return 1;
    }
    if (tipo == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getKeyCode(e) == AKEYCODE_BACK) {      // boton "atras" = Esc
            if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN && g_tac.alTecla) g_tac.alTecla(TK_ESC);
            return 1;
        }
    }
    return 0;
}

static double glfwGetTime() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    static double t0 = ts.tv_sec + ts.tv_nsec * 1e-9;
    return ts.tv_sec + ts.tv_nsec * 1e-9 - t0;
}
static int glfwInit() {
    if (!A_app) return 0;
    A_app->onAppCmd = A_cmd;
    A_app->onInputEvent = A_input;
    A_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (A_dpy == EGL_NO_DISPLAY || !eglInitialize(A_dpy, nullptr, nullptr)) return 0;
    const EGLint attr[] = {EGL_RENDERABLE_TYPE, 0x0040 /*ES3*/, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(A_dpy, attr, &A_cfg, 1, &n) || n < 1) {
        const EGLint attr2[] = {EGL_RENDERABLE_TYPE, 0x0040, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE};
        if (!eglChooseConfig(A_dpy, attr2, &A_cfg, 1, &n) || n < 1) return 0;
    }
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    A_ctx = eglCreateContext(A_dpy, A_cfg, EGL_NO_CONTEXT, ctxAttr);
    return A_ctx != EGL_NO_CONTEXT;
}
static void glfwWindowHint(int, int) {}
static void glfwPollEvents() {
    for (;;) {
        int fd = 0, ev = 0; void* dato = nullptr;
        int timeout = (A_surf == EGL_NO_SURFACE && !A_cerrar) ? -1 : 0;      // sin ventana (segundo plano) se espera sin gastar bateria
        int r = ALooper_pollOnce(timeout, &fd, &ev, &dato);
        if (r >= 0) {
            android_poll_source* src = (android_poll_source*)dato;
            if (src) src->process(A_app, src);
            if (A_app->destroyRequested) A_cerrar = true;
            if (A_ventanaNueva) { A_ventanaNueva = false; if (A_app->window) A_crearSuperficie(); }
            continue;
        }
        if (r == ALOOPER_POLL_CALLBACK) continue;
        if (A_surf != EGL_NO_SURFACE || A_cerrar) break;
    }
}
static GLFWwindow* glfwCreateWindow(int, int, const char*, void*, void*) {
    while (A_surf == EGL_NO_SURFACE && !A_cerrar) glfwPollEvents();
    return A_cerrar ? nullptr : &A_w;
}
static void glfwMakeContextCurrent(GLFWwindow*) {}
static void glfwSwapInterval(int n) { eglSwapInterval(A_dpy, n); }
static void glfwSwapBuffers(GLFWwindow*) { if (A_surf != EGL_NO_SURFACE) eglSwapBuffers(A_dpy, A_surf); }
static GLFWglproc glfwGetProcAddress(const char* nombre) {
    void* p = (void*)eglGetProcAddress(nombre);
    if (!p) {
        if (!A_libGLES) A_libGLES = dlopen("libGLESv3.so", RTLD_NOW);
        if (A_libGLES) p = dlsym(A_libGLES, nombre);
    }
    return (GLFWglproc)p;
}
static void glfwSetCursorPosCallback(GLFWwindow* w, FMouse f) { g_tac.alCursor = [f, w](double x, double y) { f(w, x, y); }; }
static void glfwSetScrollCallback(GLFWwindow* w, FScroll f) { g_tac.alScroll = [f, w](double dy) { f(w, 0.0, dy); }; }
static void glfwSetKeyCallback(GLFWwindow* w, FKey f) { g_tac.alTecla = [f, w](int k) { f(w, k, 0, GLFW_PRESS, 0); }; }
static void glfwSetMouseButtonCallback(GLFWwindow* w, FBtn f) { g_tac.alBoton = [f, w](int b, int a) { f(w, b, a, 0); }; }
static void glfwSetInputMode(GLFWwindow*, int, int) {}
static int glfwRawMouseMotionSupported() { return 0; }
static void glfwSetWindowTitle(GLFWwindow*, const char*) {}
static int glfwGetKey(GLFWwindow*, int k) { return (k >= 0 && k < 512 && g_tac.teclas[k]) ? GLFW_PRESS : 0; }
static int glfwGetMouseButton(GLFWwindow*, int b) { return (b >= 0 && b < 2 && g_tac.raton[b]) ? GLFW_PRESS : 0; }
static void glfwGetFramebufferSize(GLFWwindow*, int* w, int* h) { *w = A_w_px; *h = A_h_px; }
static void glfwGetWindowSize(GLFWwindow*, int* w, int* h) { *w = A_w_px; *h = A_h_px; }
static int glfwWindowShouldClose(GLFWwindow*) { return A_cerrar; }
static void glfwSetWindowShouldClose(GLFWwindow*, int) { A_cerrar = true; if (A_app && A_app->activity) ANativeActivity_finish(A_app->activity); }
static void glfwTerminate() {
    if (A_dpy != EGL_NO_DISPLAY) {
        A_destruirSuperficie();
        if (A_ctx != EGL_NO_CONTEXT) eglDestroyContext(A_dpy, A_ctx);
        eglTerminate(A_dpy);
    }
    A_dpy = EGL_NO_DISPLAY; A_ctx = EGL_NO_CONTEXT;
}

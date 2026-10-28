// Pipeline grafico propio (estilo "shader pack" pero dentro del motor): sombras de sol suaves, oclusion ambiental,
// bloom, antialiasing (FXAA) y tonemapping. Todo en OpenGL 3.3, sin dependencias.
#pragma once

static const char* VP_SRC = R"(#version 330 core
out vec2 vUV;
void main(){
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// ---- sombras: solo profundidad desde el sol
static const char* VSH_SRC = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uSVP;
uniform vec3 uRel;
uniform float uVS;
void main(){ gl_Position = uSVP * vec4(uRel + aPos * uVS, 1.0); })";
static const char* FSH_SRC = R"(#version 330 core
void main(){})";

// ---- oclusion ambiental en pantalla (a partir de la profundidad)
static const char* FAO_SRC = R"(#version 330 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uProf;
uniform vec2 uTan;
uniform vec2 uNF;
uniform vec2 uRes;
float lin(float d){ float z = d * 2.0 - 1.0; return 2.0 * uNF.x * uNF.y / (uNF.y + uNF.x - z * (uNF.y - uNF.x)); }
vec3 posv(vec2 uv, float zl){ return vec3((uv * 2.0 - 1.0) * uTan * zl, -zl); }
float h12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
void main(){
    float d = texture(uProf, vUV).r;
    if (d >= 0.99999) { frag = vec4(1.0); return; }
    float zl = lin(d);
    if (zl > 140.0) { frag = vec4(1.0); return; }
    vec2 px = 1.0 / uRes;
    vec3 P = posv(vUV, zl);
    vec2 uR = vUV + vec2(px.x, 0.0), uL = vUV - vec2(px.x, 0.0), uU = vUV + vec2(0.0, px.y), uD = vUV - vec2(0.0, px.y);
    vec3 pr = posv(uR, lin(texture(uProf, uR).r)), pl = posv(uL, lin(texture(uProf, uL).r));
    vec3 pu = posv(uU, lin(texture(uProf, uU).r)), pd = posv(uD, lin(texture(uProf, uD).r));
    vec3 dx = abs(pr.z - P.z) < abs(P.z - pl.z) ? pr - P : P - pl;
    vec3 dy = abs(pu.z - P.z) < abs(P.z - pd.z) ? pu - P : P - pd;
    vec3 N = cross(dx, dy);
    float nl = length(N);
    if (nl < 1e-9) { frag = vec4(1.0); return; }
    N /= nl;
    if (N.z < 0.0) N = -N;
    vec3 T = normalize(abs(N.y) < 0.9 ? cross(N, vec3(0.0, 1.0, 0.0)) : cross(N, vec3(1.0, 0.0, 0.0)));
    vec3 B = cross(N, T);
    float facing = clamp(dot(N, normalize(-P)), 0.0, 1.0);   // 1 = mirando de frente a la superficie, 0 = rasante
    float ang = h12(gl_FragCoord.xy) * 6.2831853;
    const int NS = 12;
    const float R = 0.85;
    float oc = 0.0;
    for (int i = 0; i < NS; i++) {
        float fi = (float(i) + 0.5) / float(NS);
        float a = ang + float(i) * 2.399963;
        float r = sqrt(fi);
        vec3 v = T * (cos(a) * r) + B * (sin(a) * r) + N * sqrt(max(0.0, 1.0 - fi));
        float s = mix(0.25, 1.0, fi * fi);
        vec3 S = P + v * R * s;
        if (S.z > -0.05) continue;
        vec2 nd = S.xy / (-S.z) / uTan;
        vec2 uv = nd * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
        float sz = lin(texture(uProf, uv).r);
        float bias = 0.03 + 0.003 * zl + R * 0.55 * (1.0 - facing);
        if (sz < -S.z - bias) oc += smoothstep(0.0, 1.0, R / max(abs(zl - sz), 0.001));
    }
    float ao = 1.0 - oc / float(NS);
    ao = clamp(ao * ao, 0.0, 1.0);
    ao = mix(1.0, ao, smoothstep(0.12, 0.45, facing));        // en superficies rasantes (horizonte) no se oscurece
    ao = mix(ao, 1.0, smoothstep(50.0, 120.0, zl));
    frag = vec4(ao, ao, ao, 1.0);
})";

// ---- bloom: brillos, bajada y subida
static const char* FBR_SRC = R"(#version 330 core
in vec2 vUV; out vec4 frag; uniform sampler2D uTex; uniform float uThr;
void main(){
    vec3 c = texture(uTex, vUV).rgb;
    float l = max(c.r, max(c.g, c.b));
    float w = smoothstep(uThr, uThr + 0.6, l);
    frag = vec4(c * w, 1.0);
})";
static const char* FDN_SRC = R"(#version 330 core
in vec2 vUV; out vec4 frag; uniform sampler2D uTex; uniform vec2 uPx;
void main(){
    vec3 s = texture(uTex, vUV).rgb * 4.0;
    s += texture(uTex, vUV + uPx * vec2(-1.0, -1.0)).rgb;
    s += texture(uTex, vUV + uPx * vec2( 1.0, -1.0)).rgb;
    s += texture(uTex, vUV + uPx * vec2(-1.0,  1.0)).rgb;
    s += texture(uTex, vUV + uPx * vec2( 1.0,  1.0)).rgb;
    frag = vec4(s / 8.0, 1.0);
})";
static const char* FUP_SRC = R"(#version 330 core
in vec2 vUV; out vec4 frag; uniform sampler2D uTex; uniform vec2 uPx; uniform float uW;
void main(){
    vec3 s = texture(uTex, vUV + uPx * vec2(-1.0, 0.0)).rgb * 2.0 + texture(uTex, vUV + uPx * vec2(1.0, 0.0)).rgb * 2.0
           + texture(uTex, vUV + uPx * vec2(0.0, -1.0)).rgb * 2.0 + texture(uTex, vUV + uPx * vec2(0.0, 1.0)).rgb * 2.0
           + texture(uTex, vUV + uPx * vec2(-1.0, -1.0)).rgb + texture(uTex, vUV + uPx * vec2(1.0, -1.0)).rgb
           + texture(uTex, vUV + uPx * vec2(-1.0, 1.0)).rgb + texture(uTex, vUV + uPx * vec2(1.0, 1.0)).rgb
           + texture(uTex, vUV).rgb * 4.0;
    frag = vec4(s / 16.0 * uW, 1.0);
})";

// ---- composicion final: antialiasing, AO, bloom, curva de tonos, color, vineta
static const char* FCO_SRC = R"(#version 330 core
in vec2 vUV; out vec4 frag;
uniform sampler2D uEsc; uniform sampler2D uAO; uniform sampler2D uBl;
uniform vec2 uPx; uniform float uBloom; uniform float uAOF;
vec3 S(vec2 uv){ return texture(uEsc, uv).rgb; }
float lum(vec3 c){ c = c / (1.0 + c); return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 fxaa(vec2 uv){
    vec3 nw = S(uv + uPx * vec2(-1.0, -1.0)), ne = S(uv + uPx * vec2(1.0, -1.0));
    vec3 sw = S(uv + uPx * vec2(-1.0, 1.0)), se = S(uv + uPx * vec2(1.0, 1.0)), m = S(uv);
    float lnw = lum(nw), lne = lum(ne), lsw = lum(sw), lse = lum(se), lm = lum(m);
    float lmin = min(lm, min(min(lnw, lne), min(lsw, lse))), lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));
    if (lmax - lmin < 0.045) return m;
    vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
    float red = max((lnw + lne + lsw + lse) * 0.25 * 0.125, 1.0 / 128.0);
    float rc = 1.0 / (min(abs(dir.x), abs(dir.y)) + red);
    dir = clamp(dir * rc, vec2(-8.0), vec2(8.0)) * uPx;
    vec3 a = 0.5 * (S(uv + dir * (1.0 / 3.0 - 0.5)) + S(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 b = a * 0.5 + 0.25 * (S(uv + dir * -0.5) + S(uv + dir * 0.5));
    float lb = lum(b);
    return (lb < lmin || lb > lmax) ? a : b;
}
vec3 curva(vec3 x){
    const float k = 0.72;
    vec3 o = k + (1.0 - k) * (1.0 - exp(-(x - k) / (1.0 - k)));
    return mix(x, o, step(vec3(k), x));
}
float h12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
void main(){
    vec3 c = fxaa(vUV);
    float ao = 0.0;
    for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) ao += texture(uAO, vUV + uPx * (vec2(float(i), float(j)) - 1.5)).r;
    ao /= 16.0;
    c *= mix(1.0, ao, uAOF);
    c += texture(uBl, vUV).rgb * uBloom;
    c = curva(c);
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.20);                       // un poco de contraste
    float g = dot(c, vec3(0.299, 0.587, 0.114));
    c = max(mix(vec3(g), c, 1.16), 0.0);                              // colores mas vivos
    vec2 q = vUV - 0.5;
    c *= 1.0 - 0.16 * smoothstep(0.35, 0.85, length(q * vec2(1.0, 0.9)));   // vineta suave
    c += (h12(gl_FragCoord.xy) - 0.5) / 255.0;
    frag = vec4(clamp(c, 0.0, 1.0), 1.0);
})";

struct Post {
    GLuint pSombra = 0, pAO = 0, pBr = 0, pDn = 0, pUp = 0, pCo = 0;
    GLuint fboSh = 0, texSh = 0;
    int shRes = 2048;
    GLuint fboEsc = 0, texEsc = 0, texProf = 0, fboAO = 0, texAO = 0;
    int w = 0, h = 0;
    static const int NB = 5;
    GLuint fboB[NB] = {}, texB[NB] = {};
    int bw[NB] = {}, bh[NB] = {};
    bool ok = true;
};

static GLuint ppTex(int w, int h, GLint ifmt, GLenum fmt, GLenum tipo) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, tipo, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fmt == GL_DEPTH_COMPONENT ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fmt == GL_DEPTH_COMPONENT ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
static GLuint ppFBO(GLuint color, GLuint depth, bool& ok) {
    GLuint f = 0;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    if (color) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    else { glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE); }
    if (depth) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { std::fprintf(stderr, "Framebuffer incompleto\n"); ok = false; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return f;
}
static void ppIniciar(Post& p) {
    p.pSombra = programa(VSH_SRC, FSH_SRC);
    p.pAO = programa(VP_SRC, FAO_SRC);
    p.pBr = programa(VP_SRC, FBR_SRC);
    p.pDn = programa(VP_SRC, FDN_SRC);
    p.pUp = programa(VP_SRC, FUP_SRC);
    p.pCo = programa(VP_SRC, FCO_SRC);
    p.texSh = ppTex(p.shRes, p.shRes, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT);
    p.fboSh = ppFBO(0, p.texSh, p.ok);
}
static void ppTamano(Post& p, int w, int h) {
    if (w == p.w && h == p.h) return;
    if (p.w) {
        GLuint t[3] = {p.texEsc, p.texProf, p.texAO};
        glDeleteTextures(3, t);
        glDeleteFramebuffers(1, &p.fboEsc); glDeleteFramebuffers(1, &p.fboAO);
        for (int i = 0; i < Post::NB; i++) { glDeleteTextures(1, &p.texB[i]); glDeleteFramebuffers(1, &p.fboB[i]); }
    }
    p.w = w; p.h = h;
    p.texEsc = ppTex(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    p.texProf = ppTex(w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT);
    p.fboEsc = ppFBO(p.texEsc, p.texProf, p.ok);
    p.texAO = ppTex(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    p.fboAO = ppFBO(p.texAO, 0, p.ok);
    int bw = w, bh = h;
    for (int i = 0; i < Post::NB; i++) {
        bw = std::max(1, bw / 2); bh = std::max(1, bh / 2);
        p.bw[i] = bw; p.bh[i] = bh;
        p.texB[i] = ppTex(bw, bh, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
        p.fboB[i] = ppFBO(p.texB[i], 0, p.ok);
    }
}
// pasadas de pantalla completa despues de dibujar la escena (vao: un VAO vacio)
static void ppComponer(Post& p, GLuint vao, int fbw, int fbh, float tanX, float tanY, float zNear, float zFar, bool conAO) {
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);
    if (conAO) {   // oclusion ambiental
        glBindFramebuffer(GL_FRAMEBUFFER, p.fboAO);
        glViewport(0, 0, fbw, fbh);
        glUseProgram(p.pAO);
        glBindTexture(GL_TEXTURE_2D, p.texProf);
        glUniform1i(glGetUniformLocation(p.pAO, "uProf"), 0);
        glUniform2f(glGetUniformLocation(p.pAO, "uTan"), tanX, tanY);
        glUniform2f(glGetUniformLocation(p.pAO, "uNF"), zNear, zFar);
        glUniform2f(glGetUniformLocation(p.pAO, "uRes"), (float)fbw, (float)fbh);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // bloom: brillos -> bajada -> subida
    glBindFramebuffer(GL_FRAMEBUFFER, p.fboB[0]);
    glViewport(0, 0, p.bw[0], p.bh[0]);
    glUseProgram(p.pBr);
    glBindTexture(GL_TEXTURE_2D, p.texEsc);
    glUniform1i(glGetUniformLocation(p.pBr, "uTex"), 0);
    glUniform1f(glGetUniformLocation(p.pBr, "uThr"), 0.95f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(p.pDn);
    glUniform1i(glGetUniformLocation(p.pDn, "uTex"), 0);
    GLint dnPx = glGetUniformLocation(p.pDn, "uPx");
    for (int i = 1; i < Post::NB; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, p.fboB[i]);
        glViewport(0, 0, p.bw[i], p.bh[i]);
        glBindTexture(GL_TEXTURE_2D, p.texB[i - 1]);
        glUniform2f(dnPx, 1.0f / p.bw[i - 1], 1.0f / p.bh[i - 1]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glUseProgram(p.pUp);
    glUniform1i(glGetUniformLocation(p.pUp, "uTex"), 0);
    GLint upPx = glGetUniformLocation(p.pUp, "uPx"), upW = glGetUniformLocation(p.pUp, "uW");
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
    for (int i = Post::NB - 2; i >= 0; i--) {
        glBindFramebuffer(GL_FRAMEBUFFER, p.fboB[i]);
        glViewport(0, 0, p.bw[i], p.bh[i]);
        glBindTexture(GL_TEXTURE_2D, p.texB[i + 1]);
        glUniform2f(upPx, 1.0f / p.bw[i + 1], 1.0f / p.bh[i + 1]);
        glUniform1f(upW, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
    // composicion final a la pantalla
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbw, fbh);
    glUseProgram(p.pCo);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, p.texEsc);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, p.texAO);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, p.texB[0]);
    glUniform1i(glGetUniformLocation(p.pCo, "uEsc"), 0);
    glUniform1i(glGetUniformLocation(p.pCo, "uAO"), 1);
    glUniform1i(glGetUniformLocation(p.pCo, "uBl"), 2);
    glUniform2f(glGetUniformLocation(p.pCo, "uPx"), 1.0f / fbw, 1.0f / fbh);
    glUniform1f(glGetUniformLocation(p.pCo, "uBloom"), 0.22f);
    glUniform1f(glGetUniformLocation(p.pCo, "uAOF"), conAO ? 0.7f : 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

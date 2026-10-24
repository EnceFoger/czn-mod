// CZN Speed v2 - modul Zygisk, konsep config-file (seperti Kuro Companion)
// Config : /storage/emulated/0/Android/data/<pkg>/files/czn_speed.cfg  (edit pakai file manager biasa, tanpa root)
// Log    : folder yang sama, czn_speed.log
// Cara   : setelah libssr.so termuat, memanggil cocos2d::Director::setTimeScaleFactor(float)
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <android/log.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <cmath>
#include <thread>
#include "zygisk.hpp"

#define PKG "com.smilegate.chaoszero.stove.google"
#define TAG "CZNSpeed"

// Offset fallback (build 1.0.841) bila dlsym gagal
static const uintptr_t OFF_GET_INSTANCE = 0x1cb7070; // Director::getInstance()
static const uintptr_t OFF_SET_FACTOR   = 0x2162acc; // Director::setTimeScaleFactor(float)
static const uintptr_t OFF_GET_FACTOR   = 0x2162bf8; // Director::getTimeScaleFactor()

typedef void *(*GetInstanceFn)();
typedef void (*SetFactorFn)(void *, float);
typedef float (*GetFactorFn)(void *);

static char g_dir[256];
static char g_cfg[300];
static char g_log[300];

static void logf_(const char *fmt, ...) {
    char buf[400];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_write(ANDROID_LOG_INFO, TAG, buf);
    FILE *f = fopen(g_log, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

static bool dir_ok(const char *p) { return access(p, W_OK) == 0; }

static void pick_dir() {
    const char *ext1 = "/storage/emulated/0/Android/data/" PKG;
    const char *ext2 = "/storage/emulated/0/Android/data/" PKG "/files";
    mkdir(ext1, 0771); mkdir(ext2, 0771);
    if (dir_ok(ext2)) snprintf(g_dir, sizeof g_dir, "%s", ext2);
    else {
        const char *in = "/data/data/" PKG "/files";
        mkdir(in, 0771);
        snprintf(g_dir, sizeof g_dir, "%s", in);
    }
    snprintf(g_cfg, sizeof g_cfg, "%s/czn_speed.cfg", g_dir);
    snprintf(g_log, sizeof g_log, "%s/czn_speed.log", g_dir);
}

static const char *DEFAULT_CFG =
    "# CZN Speed settings. Dibaca ulang otomatis setiap ~2 detik.\n"
    "# 1.0 = fitur mati. Baris yang salah/kosong memakai nilai default.\n"
    "game_speed=1.0\n"
    "# Batas atas game_speed (1.0 .. 100.0, default 5.0):\n"
    "max_game_speed=5.0\n";

static void ensure_default_cfg() {
    if (access(g_cfg, F_OK) == 0) return;
    FILE *f = fopen(g_cfg, "w");
    if (!f) { logf_("tidak bisa membuat config di %s", g_cfg); return; }
    fputs(DEFAULT_CFG, f); fclose(f);
    logf_("config default dibuat: %s", g_cfg);
}

struct Cfg { float speed = 1.0f; float maxSpeed = 5.0f; };

static float parse_f(const char *k, const char *v, float def) {
    char *end = nullptr;
    float x = strtof(v, &end);
    if (end == v || !std::isfinite(x)) { logf_("config: nilai '%s=%s' tidak valid -> %.1f", k, v, def); return def; }
    return x;
}

static Cfg load_cfg() {
    Cfg c;
    FILE *f = fopen(g_cfg, "r");
    if (!f) return c;
    char line[200];
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        while (*k == ' ' || *k == '\t') k++;
        for (char *e = k + strlen(k); e > k && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
        while (*v == ' ' || *v == '\t') v++;
        if (!strcmp(k, "game_speed")) c.speed = parse_f(k, v, 1.0f);
        else if (!strcmp(k, "max_game_speed")) c.maxSpeed = parse_f(k, v, 5.0f);
    }
    fclose(f);
    if (c.maxSpeed < 1.0f) c.maxSpeed = 1.0f;
    if (c.maxSpeed > 100.0f) c.maxSpeed = 100.0f;
    if (c.speed < 0.1f) c.speed = 0.1f;
    if (c.speed > c.maxSpeed) c.speed = c.maxSpeed;
    return c;
}

static uintptr_t find_base(const char *needle) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[600]; uintptr_t base = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, needle)) continue;
        unsigned long s, e, off;
        if (sscanf(line, "%lx-%lx %*4s %lx", &s, &e, &off) == 3 && off == 0) { base = s; break; }
    }
    fclose(f);
    return base;
}

static void worker() {
    pick_dir();
    logf_("=== CZN Speed v2 mulai, pid=%d, dir=%s ===", getpid(), g_dir);
    ensure_default_cfg();

    uintptr_t base = 0;
    for (int i = 0; i < 300 && !base; i++) { base = find_base("/libssr.so"); if (!base) sleep(1); }
    if (!base) { logf_("GAGAL: libssr.so tidak ditemukan"); return; }
    logf_("libssr.so base=%p", (void *) base);
    sleep(20);

    GetInstanceFn getInst = nullptr; SetFactorFn setF = nullptr; GetFactorFn getF = nullptr;
    void *h = dlopen("libssr.so", RTLD_NOW | RTLD_NOLOAD);
    if (h) {
        getInst = (GetInstanceFn) dlsym(h, "_ZN7cocos2d8Director11getInstanceEv");
        setF = (SetFactorFn) dlsym(h, "_ZN7cocos2d8Director18setTimeScaleFactorEf");
        getF = (GetFactorFn) dlsym(h, "_ZN7cocos2d8Director18getTimeScaleFactorEv");
    }
    if (getInst && setF && getF) logf_("simbol: dlsym OK");
    else {
        getInst = (GetInstanceFn) (base + OFF_GET_INSTANCE);
        setF = (SetFactorFn) (base + OFF_SET_FACTOR);
        getF = (GetFactorFn) (base + OFF_GET_FACTOR);
        logf_("simbol: pakai offset fallback");
    }

    Cfg cfg = load_cfg();
    float last = 1.0f, lastLogged = -1.0f;
    int tick = 0;
    for (;; tick++) {
        if (tick % 4 == 0) cfg = load_cfg(); // ~2 detik
        if (cfg.speed != 1.0f || last != 1.0f) {
            void *dir = getInst();
            if (dir) {
                setF(dir, cfg.speed); last = cfg.speed;
                if (cfg.speed != lastLogged) {
                    logf_("diterapkan game_speed=%.2f, baca-ulang dari engine=%.2f", cfg.speed, getF(dir));
                    lastLogged = cfg.speed;
                }
            } else if (tick % 20 == 0) logf_("Director::getInstance() = null, menunggu engine");
        }
        usleep(500 * 1000);
    }
}

class CznSpeed : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override { api_ = api; env_ = env; }
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        hit_ = false;
        if (args->nice_name) {
            const char *n = env_->GetStringUTFChars(args->nice_name, nullptr);
            hit_ = n && strcmp(n, PKG) == 0;
            if (n) env_->ReleaseStringUTFChars(args->nice_name, n);
        }
        if (!hit_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (hit_) std::thread(worker).detach();
    }
private:
    zygisk::Api *api_ = nullptr; JNIEnv *env_ = nullptr; bool hit_ = false;
};

REGISTER_ZYGISK_MODULE(CznSpeed)

// CZN Speed - modul Zygisk: pengali kecepatan game (hook-free)
// Target : com.smilegate.chaoszero.stove.google (build 1.0.841, libssr.so)
// Cara   : setelah libssr.so termuat, memanggil cocos2d::Director::setTimeScaleFactor(float)
//          secara berkala. Nilai kecepatan dibaca dari /data/adb/czn_speed.conf lewat proses companion (root).
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <thread>
#include "zygisk.hpp"

#define TAG "CZNSpeed"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static const char *TARGET_PKG = "com.smilegate.chaoszero.stove.google";
static const char *CONF_PATH  = "/data/adb/czn_speed.conf";

// Offset fallback (build 1.0.841) bila dlsym gagal
static const uintptr_t OFF_GET_INSTANCE = 0x1cb7070; // cocos2d::Director::getInstance()
static const uintptr_t OFF_SET_TS_FACTOR = 0x2162acc; // cocos2d::Director::setTimeScaleFactor(float)

typedef void *(*GetInstanceFn)();
typedef void (*SetFactorFn)(void *, float);

static uintptr_t find_base(const char *needle) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[600];
    uintptr_t base = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, needle)) continue;
        unsigned long s, e, off;
        if (sscanf(line, "%lx-%lx %*4s %lx", &s, &e, &off) == 3 && off == 0) { base = s; break; }
    }
    fclose(f);
    return base;
}

static void worker(int fd) {
    uintptr_t base = 0;
    for (int i = 0; i < 300 && !base; i++) {
        base = find_base("/libssr.so");
        if (!base) sleep(1);
    }
    if (!base) { LOGI("libssr.so tidak ditemukan"); return; }
    LOGI("libssr.so base=%p", (void *) base);
    sleep(20); // beri waktu engine selesai inisialisasi

    GetInstanceFn getInst = nullptr;
    SetFactorFn setFactor = nullptr;
    void *h = dlopen("libssr.so", RTLD_NOW | RTLD_NOLOAD);
    if (h) {
        getInst = (GetInstanceFn) dlsym(h, "_ZN7cocos2d8Director11getInstanceEv");
        setFactor = (SetFactorFn) dlsym(h, "_ZN7cocos2d8Director18setTimeScaleFactorEf");
    }
    if (!getInst || !setFactor) {
        getInst = (GetInstanceFn) (base + OFF_GET_INSTANCE);
        setFactor = (SetFactorFn) (base + OFF_SET_TS_FACTOR);
        LOGI("pakai offset fallback");
    }

    float last = 1.0f;
    for (;;) {
        char b = 1;
        if (write(fd, &b, 1) != 1) break;
        float v = 1.0f;
        if (read(fd, &v, sizeof v) != (ssize_t) sizeof v) break;
        // Jangan ganggu faktor bawaan game saat pengali = 1.0
        if (v != 1.0f || last != 1.0f) {
            void *dir = getInst();
            if (dir) { setFactor(dir, v); last = v; }
        }
        usleep(500 * 1000);
    }
}

class CznSpeed : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override { api_ = api; env_ = env; }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        bool hit = false;
        if (args->nice_name) {
            const char *name = env_->GetStringUTFChars(args->nice_name, nullptr);
            hit = name && strcmp(name, TARGET_PKG) == 0;
            if (name) env_->ReleaseStringUTFChars(args->nice_name, name);
        }
        if (!hit) { api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); return; }
        fd_ = api_->connectCompanion();
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (fd_ < 0) return;
        std::thread(worker, fd_).detach();
    }

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    int fd_ = -1;
};

// Berjalan sebagai root: baca file konfigurasi dan kirim nilainya
static void companion_handler(int fd) {
    char b;
    while (read(fd, &b, 1) == 1) {
        float v = 1.0f;
        FILE *f = fopen(CONF_PATH, "r");
        if (f) { if (fscanf(f, "%f", &v) != 1) v = 1.0f; fclose(f); }
        if (v < 0.1f || v > 10.0f) v = 1.0f;
        if (write(fd, &v, sizeof v) != (ssize_t) sizeof v) break;
    }
}

REGISTER_ZYGISK_MODULE(CznSpeed)
REGISTER_ZYGISK_COMPANION(companion_handler)

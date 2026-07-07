/**
 * @file IBLBaker.cpp
 * @brief IBL 环境贴图烘焙工具
 */

#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <cmath>
#include <algorithm>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

struct float2 { float x, y; float2() : x(0), y(0) {} float2(float x_, float y_) : x(x_), y(y_) {} };
struct float3 {
    float x, y, z;
    float3() : x(0), y(0), z(0) {}
    float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    float3 operator+(const float3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    float3 operator-(const float3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    float3 operator*(float s) const { return {x*s, y*s, z*s}; }
    float3 operator*(const float3& o) const { return {x*o.x, y*o.y, z*o.z}; }
    float3& operator+=(const float3& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    float3 normalize() const { float l = length(); return l > 0 ? *this*(1.0f/l) : *this; }
};
static float3 cross(const float3& a, const float3& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

static float2 DirToEquiUV(float3 dir) {
    float theta = std::acos(std::clamp(dir.y, -1.0f, 1.0f));
    float phi   = std::atan2(dir.z, dir.x);
    return { (phi + 3.14159265f) / (6.2831853f), theta / 3.14159265f };
}

static float3 SampleEquirectBilinear(const std::vector<float3>& img, int w, int h, float u, float v) {
    u = u - std::floor(u); v = std::clamp(v, 0.0f, 1.0f);
    float fx = u * w - 0.5f, fy = v * h - 0.5f;
    int x0 = (int(std::floor(fx)) + w) % w, x1 = (x0 + 1) % w;
    int y0 = std::clamp(int(std::floor(fy)), 0, h-1), y1 = std::clamp(y0 + 1, 0, h-1);
    float tx = fx - std::floor(fx), ty = fy - std::floor(fy);
    return (img[y0*w+x0]*(1-tx)+img[y0*w+x1]*tx)*(1-ty) + (img[y1*w+x0]*(1-tx)+img[y1*w+x1]*tx)*ty;
}

static std::vector<float3> BakeIrradiance(const std::vector<float3>& equi, int w, int h, int fs=64) {
    std::vector<float3> res(6*fs*fs);
    float sd = 0.025f;
    for (int face = 0; face < 6; face++)
        for (int y = 0; y < fs; y++)
            for (int x = 0; x < fs; x++) {
                float sc = 2.0f*(x+0.5f)/fs-1, tc = 2.0f*(y+0.5f)/fs-1;
                float3 dirs[] = {{1,-tc,-sc},{-1,-tc,sc},{sc,1,tc},{sc,-1,-tc},{sc,-tc,1},{-sc,-tc,-1}};
                float3 D = dirs[face].normalize();
                float3 irr = {0,0,0}; int smp = 0;
                for (float p = 0; p < 6.2831853f; p += sd)
                    for (float t = 0; t < 1.5707963f; t += sd) {
                        float3 T = {std::sin(t)*std::cos(p), std::sin(t)*std::sin(p), std::cos(t)};
                        float3 U = {0,1,0}; if (std::abs(D.y) > 0.999f) U = {1,0,0};
                        float3 R = cross(D.normalize(), U).normalize(); U = cross(R, D).normalize();
                        float3 S = (R*T.x + U*T.y + D*T.z).normalize();
                        auto uv = DirToEquiUV(S);
                        irr += SampleEquirectBilinear(equi, w, h, uv.x, uv.y) * std::cos(t) * std::sin(t);
                        smp++;
                    }
                if (smp > 0) irr = irr * (3.14159265f / smp);
                res[face*fs*fs + y*fs + x] = irr;
            }
    return res;
}

static std::vector<std::vector<float3>> BakePrefiltered(const std::vector<float3>& equi, int w, int h, int bs=128, int ml=5) {
    std::vector<std::vector<float3>> res(ml);
    for (int m = 0; m < ml; m++) {
        int sz = std::max(4, bs >> m); res[m].resize(6*sz*sz);
        float r = float(m)/(ml-1);
        int samples = std::max(32, int(512*(1-r+0.5)));
        float sd = 6.2831853f / samples;
        for (int face = 0; face < 6; face++)
            for (int y = 0; y < sz; y++)
                for (int x = 0; x < sz; x++) {
                    float sc = 2.0f*(x+0.5f)/sz-1, tc = 2.0f*(y+0.5f)/sz-1;
                    float3 dirs[] = {{1,-tc,-sc},{-1,-tc,sc},{sc,1,tc},{sc,-1,-tc},{sc,-tc,1},{-sc,-tc,-1}};
                    float3 D = dirs[face].normalize();
                    float3 U = {0,1,0}; if (std::abs(D.y) > 0.999f) U = {1,0,0};
                    float3 R = cross(D, U).normalize(); U = cross(R, D).normalize();
                    float3 total = {0,0,0}; float tw = 0;
                    for (float p = 0; p < 6.2831853f; p += sd)
                        for (float t = 0; t < 1.5707963f; t += sd) {
                            float ct = std::cos(t), st = std::sin(t);
                            float3 H = {st*std::cos(p), st*std::sin(p), ct};
                            float3 L = (D*2.0f*(D.x*H.x+D.y*H.y+D.z*H.z) - H).normalize();
                            float nd = std::max(L.y, 0.0f);
                            if (nd > 0) {
                                auto uv = DirToEquiUV(L);
                                total = total + SampleEquirectBilinear(equi, w, h, uv.x, uv.y)*nd;
                                tw += nd;
                            }
                        }
                    if (tw > 0) total = total*(1.0f/tw);
                    res[m][face*sz*sz + y*sz + x] = total;
                }
    }
    return res;
}

static bool WriteBaked(const std::string& path, const std::vector<float3>& data, int fs, const char* magic, int ml=1) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(magic, 4);
    unsigned char fp[16] = {}; fp[0]=fs&0xFF; fp[1]=(fs>>8)&0xFF; fp[2]=ml&0xFF;
    out.write((const char*)fp, 16);
    uint32_t fs_=uint32_t(fs), ml_=uint32_t(ml);
    out.write((const char*)&fs_, 4); out.write((const char*)&ml_, 4);
    out.write((const char*)data.data(), data.size()*3*4);
    printf("  Wrote %s: %dx%d mips=%d\n", path.c_str(), fs, fs, ml);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 3) { printf("Usage: IBLBaker <input.hdr> <output_dir>\n"); return 1; }
    int w, h, c;
    float* d = stbi_loadf(argv[1], &w, &h, &c, 3);
    if (!d) { fprintf(stderr, "Failed: %s\n", argv[1]); return 1; }
    printf("Loaded %dx%d HDR\n", w, h);
    std::vector<float3> equi(w*h);
    for (int i = 0; i < w*h; i++) equi[i] = {d[i*3], d[i*3+1], d[i*3+2]};
    stbi_image_free(d);
    std::string out = argv[2];
    printf("Baking irradiance...\n");
    WriteBaked(out+"/irradiance.baked", BakeIrradiance(equi, w, h), 64, "IRR\0");
    printf("Baking prefiltered...\n");
    auto pf = BakePrefiltered(equi, w, h);
    std::vector<float3> all;
    for (auto& v : pf) all.insert(all.end(), v.begin(), v.end());
    WriteBaked(out+"/specular_env.baked", all, 128, "SPC\0", (int)pf.size());
    printf("Done.\n");
    return 0;
}
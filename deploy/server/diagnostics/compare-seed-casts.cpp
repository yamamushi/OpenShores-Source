#include <cstdint>
#include <cstdio>
__attribute__((noinline)) uint32_t legacy(double value) { return uint32_t(value); }
__attribute__((noinline)) uint32_t portable(double value) { return uint32_t(int64_t(value)); }
int main() {
    uint32_t state=1;
    unsigned changed=0;
    for(unsigned i=0;i<100000;++i) {
        state=state*1664525u+1013904223u;
        double noise=1.0-double(state&0x7fffffffu)/1073741824.0;
        double scaled=noise*4294967295.0;
        changed+=legacy(scaled)!=portable(scaled);
    }
    unsigned radiusChanged=0;
    for(unsigned i=0;i<10000;++i) {
        double radius=double(i)*1000000000.125;
        radiusChanged+=legacy(radius)!=portable(radius);
    }
    std::printf("noise conversions changed=%u/100000; orbital conversions changed=%u/10000\n",changed,radiusChanged);
#if defined(__x86_64__)
    return changed || radiusChanged;
#else
    return 0;
#endif
}

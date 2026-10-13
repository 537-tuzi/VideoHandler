#include "dspdenoiser.h"

#include "util/fft.h"

#include <cmath>

float DspDenoiser::Biquad::run(float x, int ch) {
    float y = b0 * x + z1[static_cast<size_t>(ch)];
    z1[static_cast<size_t>(ch)] = b1 * x - a1 * y + z2[static_cast<size_t>(ch)];
    z2[static_cast<size_t>(ch)] = b2 * x - a2 * y;
    return y;
}

void DspDenoiser::setup(int sampleRate, int channels, bool removeHum, bool removeWind) {
    auto makeNotch = [sampleRate](double f0, double q) {// RBJ 陷波
        double w0 = 2 * kPi * f0 / sampleRate;
        double cw = cos(w0), alpha = sin(w0) / (2 * q), a0 = 1 + alpha;
        Biquad b;
        b.b0 = static_cast<float>(1 / a0);
        b.b1 = static_cast<float>(-2 * cw / a0);
        b.b2 = static_cast<float>(1 / a0);
        b.a1 = static_cast<float>(-2 * cw / a0);
        b.a2 = static_cast<float>((1 - alpha) / a0);
        return b;
    };
    auto makeHighpass = [sampleRate](double f0, double q) {// RBJ 二阶高通
        double w0 = 2 * kPi * f0 / sampleRate;
        double cw = cos(w0), alpha = sin(w0) / (2 * q), a0 = 1 + alpha;
        Biquad b;
        b.b0 = static_cast<float>((1 + cw) / 2 / a0);
        b.b1 = static_cast<float>(-(1 + cw) / a0);
        b.b2 = static_cast<float>((1 + cw) / 2 / a0);
        b.a1 = static_cast<float>(-2 * cw / a0);
        b.a2 = static_cast<float>((1 - alpha) / a0);
        return b;
    };
    filters.clear();
    if (removeHum)
        for (int h = 1; h <= 3; ++h)// 50Hz 电流 hum 及其 2/3 次谐波
            filters.push_back(makeNotch(50.0 * h, 30.0));
    if (removeWind)
        filters.push_back(makeHighpass(100.0, 0.7071));// 风声低频隆隆
    for (auto& f : filters) {
        f.z1.assign(static_cast<size_t>(channels), 0.f);
        f.z2.assign(static_cast<size_t>(channels), 0.f);
    }
    rate = sampleRate;
    this->channels = channels;
}

int DspDenoiser::inRate() const {
    return rate;
}

int DspDenoiser::inChannels() const {
    return channels;
}

std::vector<float> DspDenoiser::process(const float* in, int64_t frames) {
    std::vector<float> out(in, in + frames * channels);// 一份拷贝后在副本上滤波
    run(out.data(), frames);
    return out;
}

void DspDenoiser::run(float* samples, int64_t frames) {
    if (filters.empty() || frames <= 0) return;
    const int64_t total = frames * channels;
    int c = 0;
    for (int64_t i = 0; i < total; ++i) {// 逐样本过整条滤波链，缓存友好
        float v = samples[i];
        for (auto& f : filters) v = f.run(v, c);
        samples[i] = v;
        if (++c == channels) c = 0;
    }
}

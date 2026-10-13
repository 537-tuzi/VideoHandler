#include "pitchshifter.h"

#include <SoundTouch.h>

#include <exception>

namespace {

constexpr uint32_t kRecvFrames = 4096;// 每轮收取块大小

}// namespace

struct PitchShifter::Impl {
    soundtouch::SoundTouch st;
    int rate = 0, ch = 0;

    std::vector<float> drain() {// 循环收取全部可用输出，不设容量上限假设
        std::vector<float> out;
        std::vector<float> buf(static_cast<size_t>(kRecvFrames) * ch);
        while (true) {
            const uint32_t got = st.receiveSamples(buf.data(), kRecvFrames);
            if (got == 0) break;
            out.insert(out.end(), buf.begin(), buf.begin() + got * ch);
        }
        return out;
    }
};

PitchShifter::PitchShifter() : impl_(std::make_unique<Impl>()) {}

PitchShifter::~PitchShifter() = default;

bool PitchShifter::setup(int sampleRate, int channels, double semiTones, QString* err) {
    try {
        impl_->st.setSampleRate(sampleRate);
        impl_->st.setChannels(channels);
        impl_->st.setTempo(1.0);// 变调不变速
        impl_->st.setPitchSemiTones(semiTones);
        impl_->rate = sampleRate;
        impl_->ch = channels;
        return true;
    } catch (const std::exception& e) {
        if (err) *err = QStringLiteral("变调引擎初始化失败：%1").arg(QString::fromUtf8(e.what()));
        return false;
    }
}

int PitchShifter::inRate() const {
    return impl_->rate;
}

int PitchShifter::inChannels() const {
    return impl_->ch;
}

std::vector<float> PitchShifter::process(const float* in, int64_t frames) {
    if (frames <= 0) return {};
    impl_->st.putSamples(in, static_cast<uint32_t>(frames));
    return impl_->drain();
}

std::vector<float> PitchShifter::flush() {
    impl_->st.flush();
    return impl_->drain();
}

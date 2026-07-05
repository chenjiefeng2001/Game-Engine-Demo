#include "Engine/Audio/AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Engine {
namespace Audio {

// ============================================================================
// 低通滤波器效果 (LPF)
// ============================================================================
class LowPassFilter : public AudioEffect
{
public:
    explicit LowPassFilter(float cutoff = 1000.0f, uint32_t sampleRate = 44100)
        : m_Cutoff(cutoff)
        , m_SampleRate(sampleRate)
    {
        Reset();
    }

    void Process(float* input, float* output, size_t numFrames, uint32_t sampleRate) override
    {
        if (sampleRate != m_SampleRate)
        {
            m_SampleRate = sampleRate;
            RecalculateCoefficients();
        }

        // 一阶 IIR 低通滤波器
        for (size_t i = 0; i < numFrames * 2; ++i) // 立体声交错
        {
            float in = input[i];
            m_Z = in * m_A0 + m_Z * m_B1;
            output[i] = m_Z;
        }
    }

    void Reset() override
    {
        m_Z = 0.0f;
        RecalculateCoefficients();
    }

    const char* GetEffectName() const override { return "LowPassFilter"; }

    void SetCutoff(float cutoff)
    {
        m_Cutoff = cutoff;
        RecalculateCoefficients();
    }

private:
    void RecalculateCoefficients()
    {
        if (m_SampleRate == 0) return;
        float dt = 1.0f / static_cast<float>(m_SampleRate);
        float rc = 1.0f / (2.0f * 3.14159265f * std::max(m_Cutoff, 1.0f));
        float alpha = dt / (rc + dt);
        m_A0 = alpha;
        m_B1 = 1.0f - alpha;
    }

    float m_Cutoff = 1000.0f;
    uint32_t m_SampleRate = 44100;
    float m_A0 = 0.0f;
    float m_B1 = 0.0f;
    float m_Z = 0.0f; // 延迟单元
};

// ============================================================================
// FIR 卷积效果 (用于 LTI 空间音频建模)
// ============================================================================
class FIRConvolution : public AudioEffect
{
public:
    explicit FIRConvolution(const std::vector<float>& ir = {1.0f})
        : m_IR(ir)
    {
    }

    void SetIR(const std::vector<float>& ir)
    {
        m_IR = ir;
        m_DelayLine.resize(m_IR.size(), 0.0f);
        Reset();
    }

    void Process(float* input, float* output, size_t numFrames, uint32_t sampleRate) override
    {
        (void)sampleRate;
        if (m_IR.empty())
        {
            if (output != input)
                std::memcpy(output, input, numFrames * 2 * sizeof(float));
            return;
        }

        // 立体声 FIR 卷积（各声道独立）
        for (size_t f = 0; f < numFrames; ++f)
        {
            // 左声道
            m_DelayLine[m_DelayIndex] = input[f * 2];
            float sumL = 0.0f;
            size_t idx = m_DelayIndex;
            for (size_t i = 0; i < m_IR.size(); ++i)
            {
                sumL += m_DelayLine[idx] * m_IR[i];
                idx = (idx == 0) ? m_IR.size() - 1 : idx - 1;
            }
            output[f * 2] = sumL;

            // 右声道
            m_DelayLine2[m_DelayIndex] = input[f * 2 + 1];
            float sumR = 0.0f;
            idx = m_DelayIndex;
            for (size_t i = 0; i < m_IR.size(); ++i)
            {
                sumR += m_DelayLine2[idx] * m_IR[i];
                idx = (idx == 0) ? m_IR.size() - 1 : idx - 1;
            }
            output[f * 2 + 1] = sumR;

            m_DelayIndex = (m_DelayIndex + 1) % m_IR.size();
        }
    }

    void Reset() override
    {
        m_DelayIndex = 0;
        std::fill(m_DelayLine.begin(), m_DelayLine.end(), 0.0f);
        std::fill(m_DelayLine2.begin(), m_DelayLine2.end(), 0.0f);
    }

    const char* GetEffectName() const override { return "FIRConvolution"; }

private:
    std::vector<float> m_IR;
    std::vector<float> m_DelayLine;
    std::vector<float> m_DelayLine2;
    size_t m_DelayIndex = 0;
};

// ============================================================================
// AudioEffect 工厂函数
// ============================================================================
std::unique_ptr<AudioEffect> CreateAudioEffect(const std::string& type, float param1, uint32_t sampleRate)
{
    if (type == "LowPassFilter")
    {
        return std::make_unique<LowPassFilter>(param1, sampleRate);
    }
    else if (type == "FIRConvolution")
    {
        return std::make_unique<FIRConvolution>();
    }
    return nullptr;
}

} // namespace Audio
} // namespace Engine
#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

// Index order must match the StringArray in createParameterLayout().
enum StyleIndex
{
    kRomantic = 0,
    kSad,
    kHipHop,
    kTrap,
    kDrill,
    kNumStyles
};

// One set of "full intensity" targets per style. Intensity (0-100%) blends
// from a neutral chain toward these values, and the live analysis nudges them.
struct StyleProfile
{
    float targetLevelDb;     // loudness the auto gain aims for (RMS dBFS)
    float idealCentroidHz;   // brightness this style wants the vocal to sit at
    float hpfHz;             // high-pass corner
    float tiltDb;            // + brighter / - darker, around 300 Hz to 3 kHz
    float presenceDb;        // peak boost around 3.5 kHz
    float airDb;             // high shelf around 11 kHz
    float deessThreshDb;     // de-esser threshold (sidechain band 5-9 kHz)
    float compThreshDb;      // compressor threshold
    float compRatio;         // compressor ratio
    float compAttackMs;
    float compReleaseMs;
    float reverbWet;         // reverb wet level at full intensity
    float reverbRoom;        // reverb room size 0..1
    float reverbDamping;     // reverb damping 0..1
};

class AutoVoxProcessor : public juce::AudioProcessor
{
public:
    AutoVoxProcessor();
    ~AutoVoxProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AutoVox"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Live analysis values, read by the editor meter (thread-safe).
    struct Analysis
    {
        std::atomic<float> levelDb { -60.0f };
        std::atomic<float> centroidHz { 3000.0f };
        std::atomic<float> sibilance { 0.0f };     // 5-9 kHz share of spectrum, 0..1
        std::atomic<float> pitchRangeSt { 0.0f };  // rough pitch spread in semitones
    };
    Analysis analysis;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ---- analysis (runs on every block, always on) ----
    void analyseSample(float mono);
    void analyseWindow();

    static constexpr int kWindow = 2048;
    static constexpr int kHop = 1024;
    std::array<float, kWindow> ring {};
    std::array<float, kWindow> hann {};
    std::array<float, 2 * kWindow> fftData {};
    juce::dsp::FFT fft { 11 };  // 2^11 = 2048 points
    int ringPos = 0;
    int hopCounter = 0;

    double sampleRate = 44100.0;
    float smLevelDb = -60.0f;
    float smCentroid = 3000.0f;
    float smSibilance = 0.0f;
    float smPitchRange = 0.0f;
    static constexpr int kPitchHist = 128;
    std::array<float, kPitchHist> pitchHist {};
    int pitchCount = 0;
    int pitchPos = 0;

    // ---- DSP chain ----
    using Biquad = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                  juce::dsp::IIR::Coefficients<float>>;
    Biquad hpf, tiltLow, tiltHigh, presence, air;
    juce::dsp::Compressor<float> comp;
    juce::dsp::Reverb reverb;
    juce::dsp::Limiter<float> limiter;

    // de-esser detector (own bandpass so it is independent of JUCE filter state)
    struct DeesserBandpass
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void setup(double sr, float freq, float q);
        float process(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    } sibDetector;
    float deessEnv = 0.0f;

    float lastInGain = 1.0f;
    float lastMakeup = 1.0f;
    float lastHpf = -1e9f, lastTilt = -1e9f, lastPres = -1e9f, lastAir = -1e9f;

    void updateEq(float hpfHz, float tiltDb, float presDb, float airDb);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoVoxProcessor)
};

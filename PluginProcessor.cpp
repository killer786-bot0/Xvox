#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include "PluginEditor.h"

namespace
{
// ---------------------------------------------------------------------------
// STYLE PROFILES
// Order: targetLevel, idealCentroid, hpf, tilt, presence, air, deessThresh,
//        compThresh, ratio, attack, release, reverbWet, reverbRoom, damping
// ---------------------------------------------------------------------------
const std::array<StyleProfile, kNumStyles> kProfiles {{
    // ROMANTIC: warm, smooth and spacious. Gentle compression, slow release,
    // soft top end and a lush room so the voice floats.
    { -17.f, 2800.f, 90.f, 0.3f, 1.5f, 2.0f, -28.f, -18.f, 2.5f, 15.f, 150.f, 0.28f, 0.60f, 0.50f },

    // SAD: dark, intimate, lots of space. Slightly rolled-off top, slow attack,
    // long release and the biggest, darkest reverb tail.
    { -18.f, 2200.f, 110.f, -0.8f, 0.5f, -0.5f, -30.f, -16.f, 2.2f, 25.f, 220.f, 0.40f, 0.80f, 0.60f },

    // HIP HOP: punchy and forward. Clear presence, medium compression,
    // a small room only, so the rap stays up front.
    { -14.f, 3200.f, 100.f, 0.5f, 2.5f, 1.0f, -26.f, -14.f, 3.5f, 8.f, 110.f, 0.08f, 0.40f, 0.50f },

    // TRAP: dry, bright and tight. Fast compression, strong presence and air,
    // almost no reverb so the vocal sits on top of the beat.
    { -12.f, 3500.f, 120.f, 0.8f, 3.5f, 3.0f, -24.f, -12.f, 4.5f, 3.f, 80.f, 0.03f, 0.30f, 0.40f },

    // DRILL: dry, aggressive and very controlled. Fastest attack, highest ratio,
    // bright top end, minimal space.
    { -12.f, 3300.f, 130.f, 1.0f, 3.0f, 2.5f, -22.f, -11.f, 5.0f, 2.f, 70.f, 0.02f, 0.20f, 0.30f },
}};

bool changed(float& last, float now, float eps)
{
    if (std::abs(now - last) < eps)
        return false;
    last = now;
    return true;
}

float clampedHann(int i, int n)
{
    return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1));
}
} // namespace

//==============================================================================
AutoVoxProcessor::AutoVoxProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int i = 0; i < kWindow; ++i)
        hann[(size_t) i] = clampedHann(i, kWindow);
}

AutoVoxProcessor::~AutoVoxProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout AutoVoxProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "style", 1 }, "Style",
        juce::StringArray { "Romantic", "Sad", "Hip Hop", "Trap", "Drill" }, 0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "intensity", 1 }, "Intensity",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
void AutoVoxProcessor::DeesserBandpass::setup(double sr, float freq, float q)
{
    const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (float) (alpha / a0);
    b1 = 0.0f;
    b2 = (float) (-alpha / a0);
    a1 = (float) (-2.0 * std::cos(w0) / a0);
    a2 = (float) ((1.0 - alpha) / a0);
    z1 = z2 = 0.0f;
}

void AutoVoxProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;
    const int numCh = juce::jmax(1, getTotalNumOutputChannels());

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = (juce::uint32) numCh;

    hpf.prepare(spec);
    tiltLow.prepare(spec);
    tiltHigh.prepare(spec);
    presence.prepare(spec);
    air.prepare(spec);

    comp.prepare(spec);
    comp.setThreshold(0.0f);
    comp.setRatio(1.0f);

    reverb.prepare(spec);
    reverb.reset();

    limiter.prepare(spec);
    limiter.setThreshold(-1.0f);
    limiter.setRelease(50.0f);

    sibDetector.setup(sr, 6500.0f, 1.5f);
    deessEnv = 0.0f;

    // force coefficient refresh on first block
    lastHpf = lastTilt = lastPres = lastAir = -1e9f;
    updateEq(20.0f, 0.0f, 0.0f, 0.0f);

    lastInGain = 1.0f;
    lastMakeup = 1.0f;

    ring.fill(0.0f);
    ringPos = 0;
    hopCounter = 0;
}

bool AutoVoxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

//==============================================================================
// ANALYSIS: loudness (RMS), brightness (spectral centroid), sibilance (5-9 kHz share),
// pitch-range proxy (zero-crossing rate -> rough f0, spread over ~3 seconds).
void AutoVoxProcessor::analyseSample(float mono)
{
    ring[(size_t) ringPos] = mono;
    ringPos = (ringPos + 1) & (kWindow - 1);

    if (++hopCounter >= kHop)
    {
        hopCounter = 0;
        analyseWindow();
    }
}

void AutoVoxProcessor::analyseWindow()
{
    double sumSq = 0.0;
    int zeroCrossings = 0;
    float prev = 0.0f;

    fftData.fill(0.0f);
    for (int i = 0; i < kWindow; ++i)
    {
        const float s = ring[(size_t) ((ringPos + i) & (kWindow - 1))];  // oldest first
        fftData[(size_t) i] = s * hann[(size_t) i];
        sumSq += (double) s * s;
        if (i > 0 && ((s >= 0.0f) != (prev >= 0.0f)))
            ++zeroCrossings;
        prev = s;
    }

    // loudness
    const float rms = (float) std::sqrt(sumSq / kWindow);
    const float levelDb = juce::Decibels::gainToDecibels(rms, -100.0f);
    smLevelDb = 0.85f * smLevelDb + 0.15f * levelDb;

    // spectrum
    fft.performFrequencyOnlyForwardTransform(fftData.data());
    const float binHz = (float) sampleRate / (float) kWindow;
    float weighted = 0.0f, total = 0.0f, sib = 0.0f;
    for (int k = 1; k < kWindow / 2; ++k)
    {
        const float f = (float) k * binHz;
        const float m = fftData[(size_t) k];
        weighted += f * m;
        total += m;
        if (f >= 5000.0f && f <= 9000.0f)
            sib += m;
    }
    const float centroid = total > 1e-9f ? weighted / total : smCentroid;
    const float sibRatio = total > 1e-9f ? sib / total : 0.0f;
    smCentroid = juce::jlimit(200.0f, 12000.0f, 0.9f * smCentroid + 0.1f * centroid);
    smSibilance = 0.9f * smSibilance + 0.1f * sibRatio;

    // pitch-range proxy: zero-crossing based f0 estimate (rough, for melodic spread only)
    if (levelDb > -45.0f)
    {
        const double durationSec = kWindow / sampleRate;
        const float f0 = (float) (zeroCrossings / 2.0 / durationSec);
        if (f0 >= 70.0f && f0 <= 800.0f)
        {
            pitchHist[(size_t) pitchPos] = f0;
            pitchPos = (pitchPos + 1) % kPitchHist;
            pitchCount = juce::jmin(pitchCount + 1, kPitchHist);
        }
    }
    if (pitchCount >= 16)
    {
        std::array<float, kPitchHist> sorted = pitchHist;
        std::sort(sorted.begin(), sorted.begin() + pitchCount);
        const float lo = sorted[(size_t) (pitchCount / 10)];
        const float hi = sorted[(size_t) ((pitchCount * 9) / 10)];
        smPitchRange = 12.0f * std::log2(juce::jmax(1.0f, hi) / juce::jmax(1.0f, lo));
    }

    analysis.levelDb.store(smLevelDb);
    analysis.centroidHz.store(smCentroid);
    analysis.sibilance.store(smSibilance);
    analysis.pitchRangeSt.store(smPitchRange);
}

//==============================================================================
void AutoVoxProcessor::updateEq(float hpfHz, float tiltDb, float presDb, float airDb)
{
    using Coef = juce::dsp::IIR::Coefficients<float>;

    if (changed(lastHpf, hpfHz, 0.5f))
        hpf.state = Coef::makeHighPass(sampleRate, hpfHz);

    if (changed(lastTilt, tiltDb, 0.05f))
    {
        tiltLow.state = Coef::makeLowShelf(sampleRate, 300.0f, 0.707f,
                                           juce::Decibels::decibelsToGain(-tiltDb));
        tiltHigh.state = Coef::makeHighShelf(sampleRate, 3000.0f, 0.707f,
                                             juce::Decibels::decibelsToGain(tiltDb));
    }

    if (changed(lastPres, presDb, 0.05f))
        presence.state = Coef::makePeakFilter(sampleRate, 3500.0f, 0.9f,
                                              juce::Decibels::decibelsToGain(presDb));

    if (changed(lastAir, airDb, 0.05f))
        air.state = Coef::makeHighShelf(sampleRate, 11000.0f, 0.707f,
                                        juce::Decibels::decibelsToGain(airDb));
}

//==============================================================================
void AutoVoxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (n == 0 || numCh == 0)
        return;

    // 1) analysis always runs, even when bypassed
    for (int i = 0; i < n; ++i)
    {
        const float mono = numCh > 1 ? 0.5f * (buffer.getSample(0, i) + buffer.getSample(1, i))
                                     : buffer.getSample(0, i);
        analyseSample(mono);
    }

    if (apvts.getRawParameterValue("bypass")->load() > 0.5f)
        return;

    const int styleIdx = juce::jlimit(0, kNumStyles - 1,
                                      (int) std::round(apvts.getRawParameterValue("style")->load()));
    const float k = juce::jlimit(0.0f, 1.0f, apvts.getRawParameterValue("intensity")->load());
    const StyleProfile& p = kProfiles[(size_t) styleIdx];

    // blend neutral -> style target by intensity
    auto mix = [k](float neutral, float target) { return neutral + (target - neutral) * k; };

    // 2) auto gain staging: nudge toward the style loudness target (max +-12 dB)
    const float inGainDb = juce::jlimit(-12.0f, 12.0f, p.targetLevelDb - smLevelDb) * k;
    const float newInGain = juce::Decibels::decibelsToGain(inGainDb);
    for (int ch = 0; ch < numCh; ++ch)
        buffer.applyGainRamp(ch, 0, n, lastInGain, newInGain);
    lastInGain = newInGain;

    // 3) high-pass + adaptive 3-band EQ (tilt, presence, air)
    //    brightErr > 0 means the vocal is brighter than the style wants.
    const float brightErr = std::log2(juce::jmax(1.0f, smCentroid) / p.idealCentroidHz);
    const float hpfHz = mix(20.0f, p.hpfHz);
    const float tiltDb = mix(0.0f, p.tiltDb);
    const float presDb = mix(0.0f, p.presenceDb) + juce::jlimit(-2.0f, 2.0f, -2.0f * brightErr) * k;
    const float airDb = mix(0.0f, p.airDb) + juce::jlimit(-4.0f, 4.0f, -4.0f * brightErr) * k;
    updateEq(hpfHz, tiltDb, presDb, airDb);

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    hpf.process(ctx);
    tiltLow.process(ctx);
    tiltHigh.process(ctx);
    presence.process(ctx);
    air.process(ctx);

    // 4) adaptive de-esser: more sibilance in the 5-9 kHz band -> lower threshold
    if (k > 0.001f)
    {
        const float thrDb = p.deessThreshDb - juce::jlimit(0.0f, 6.0f, (smSibilance - 0.15f) * 30.0f);
        for (int i = 0; i < n; ++i)
        {
            const float L = buffer.getSample(0, i);
            const float R = numCh > 1 ? buffer.getSample(1, i) : L;
            const float mono = 0.5f * (L + R);

            const float det = std::abs(sibDetector.process(mono));
            const float c = det > deessEnv ? 0.3f : 0.9995f;   // fast attack, slow release
            deessEnv = c * deessEnv + (1.0f - c) * det;

            const float overDb = juce::Decibels::gainToDecibels(deessEnv, -100.0f) - thrDb;
            const float grDb = overDb > 0.0f ? -overDb * (1.0f - 1.0f / 3.0f) * k : 0.0f;  // 3:1
            const float g = juce::Decibels::decibelsToGain(grDb);
            if (g < 0.9999f)
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.setSample(ch, i, buffer.getSample(ch, i) * g);
        }
    }

    // 5) compressor: style ratio/attack/release, softened for wide-pitch melodic singing
    float ratio = mix(1.0f, p.compRatio);
    if (smPitchRange > 9.0f)
        ratio = mix(1.0f, p.compRatio * 0.85f);
    comp.setThreshold(mix(0.0f, p.compThreshDb));
    comp.setRatio(juce::jmax(1.0f, ratio));
    comp.setAttack(mix(10.0f, p.compAttackMs));
    comp.setRelease(mix(100.0f, p.compReleaseMs));
    comp.process(ctx);

    // auto makeup: roughly recovers the level the compressor took away
    const float makeupDb = juce::jlimit(0.0f, 6.0f, -p.compThreshDb * (1.0f - 1.0f / p.compRatio) * 0.5f) * k;
    const float newMakeup = juce::Decibels::decibelsToGain(makeupDb);
    for (int ch = 0; ch < numCh; ++ch)
        buffer.applyGainRamp(ch, 0, n, lastMakeup, newMakeup);
    lastMakeup = newMakeup;

    // 6) style space: reverb send blended in (romantic/sad get room, trap/drill almost dry)
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = p.reverbRoom;
    rp.damping = p.reverbDamping;
    rp.wetLevel = p.reverbWet * k;
    rp.dryLevel = 1.0f;
    rp.width = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters(rp);
    reverb.process(ctx);

    // 7) soft safety limiter at -1 dBFS
    limiter.process(ctx);
}

//==============================================================================
juce::AudioProcessorEditor* AutoVoxProcessor::createEditor()
{
    return new AutoVoxEditor(*this);
}

void AutoVoxProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AutoVoxProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoVoxProcessor();
}

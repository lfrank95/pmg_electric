#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../play/Strummer.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

/** Anzeige-Zustand für den Editor (wird vom Audio-Thread geschrieben, vom UI-Timer gelesen) */
struct VisState {
    struct Str { std::atomic<double> hitMs{ 0 }; std::atomic<int> dir{ 1 }; std::atomic<float> vel{ 0 }; std::atomic<int> fret{ 0 }; std::atomic<float> palm{ 0 }; std::atomic<double> dampMs{ 0 }; std::atomic<double> fingerMs{ 0 }; std::atomic<int> fingerPhase{ 0 }; };
    Str s[6];
    std::atomic<float> handPos{ 0 }, palmQ{ 0.3f }, level{ 0 };
    std::atomic<int> lastDir{ 0 }; std::atomic<double> lastStrumMs{ 0 };
    std::atomic<bool> playing{ false }; std::atomic<int> bar{ 0 }; std::atomic<float> beat{ 0 }; std::atomic<int> slot{ -1 };
    juce::SpinLock lock; juce::String chordInfo;      // nur kurz gesperrt
};

class RiggingProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener {
public:
    RiggingProcessor();
    ~RiggingProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    juce::VST3ClientExtensions* getVST3ClientExtensions() override;

    juce::AudioProcessorValueTreeState apvts;
    VisState vis;
    // Akkord-Slots (Kanal 1, C0–B0): von der Oberfläche gesetzt, im Projekt gespeichert, im Audio-Thread übernommen
    rig::Slot getSlot(int i) const { const juce::SpinLock::ScopedLockType sl(slotLock); return slots[i]; }
    void setSlot(int i, const rig::Slot& s);
    int activeSlot() const { return vis.slot.load(); }
    void auditionSlot(int i) { audition = i; }
    std::atomic<int> audition{ -1 };
    void newTake();                       // "Neuer Take": Take-Nummer hochzählen (Parameter, wird im Projekt gespeichert)
    void learnAccentsFromRecent();        // Akzent-Raster aus den zuletzt gehörten Trigger-Velocities lernen
    const rig::AccentGrid& accentGrid() const { return grid; }
    static constexpr int NUM_ACCENT_PRESETS = 8;

private:
    void parameterChanged(const juce::String& id, float v) override;
    void applyAllParams();
    void drainParams();
    juce::SpinLock paramLock; std::vector<std::pair<juce::String, float>> paramQueue;
    rig::Slot slots[12]; mutable juce::SpinLock slotLock; std::atomic<bool> slotsDirty{ true };
    void writeSlotsToState(); void readSlotsFromState(); void defaultSlots();
    void applyParam(const juce::String& id, float v, rig::Engine& E, rig::Hand& H);
    void pushEvents(rig::Engine& E, const rig::Events& ev, double tNowHand);
    void renderSide(int side, float* out, int n);

    struct Side { rig::Engine E; rig::Hand H; std::unique_ptr<rig::MidiStrummer> S; };
    Side side[2]; bool sideReady[2] = { false, false };
    rig::AccentGrid grid;
    double fs = 48000; int os = 2;
    double lookSec = 0.0; std::atomic<int> pendingLatency{ -1 };
    // Takt-Uhr aus dem Host (Blockanfang)
    struct Transport { bool playing = false; double ppq = 0, bpm = 120; int num = 4, den = 4; double tBlockHand = 0; } tr;
    std::vector<rig::AccentGrid::Hit> recentHits; juce::CriticalSection hitsLock;
    // Doubler: zweite Engine in eigenem Thread
    std::thread worker; std::mutex wm; std::condition_variable wcv; bool wGo = false, wDone = true, wQuit = false; float* wOut = nullptr; int wN = 0;
    std::atomic<bool> doubler{ false };
    std::unique_ptr<juce::VST3ClientExtensions> vst3ext;
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RiggingProcessor)
};

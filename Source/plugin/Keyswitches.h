// VST3 IKeyswitchController: sagt Cubase, welche Tasten Trigger sind ("Import Key Switches" → Expression Map).
// Bewusst ohne Steinberg-Basisklassen (FObject): eigenes Refcounting, nur Header.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <pluginterfaces/vst/ivstnoteexpression.h>
#include "../play/Strummer.h"
#include <atomic>
#include <cstring>

class RiggingKeyswitches : public Steinberg::Vst::IKeyswitchController {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
    {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IKeyswitchController_iid) || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown_iid)) { addRef(); *obj = this; return Steinberg::kResultOk; }
        *obj = nullptr; return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++refs; }
    Steinberg::uint32 PLUGIN_API release() override { return --refs; }   // Lebensdauer gehört dem Prozessor
    // Kanal 1 (channel 0): 12 Akkord-Slots C0–B0 + 9 Trigger C1–G#1. Kanal 2 (channel 1): Richtung C1–E1.
    Steinberg::int32 PLUGIN_API getKeyswitchCount(Steinberg::int32 busIndex, Steinberg::int16 channel) override
    {
        if (busIndex != 0) return 0;
        return channel == 0 ? 21 : channel == 1 ? 5 : 0;
    }
    Steinberg::tresult PLUGIN_API getKeyswitchInfo(Steinberg::int32 busIndex, Steinberg::int16 channel, Steinberg::int32 i, Steinberg::Vst::KeyswitchInfo& info) override
    {
        if (busIndex != 0 || i < 0 || i >= getKeyswitchCount(busIndex, channel)) return Steinberg::kInvalidArgument;
        std::memset(&info, 0, sizeof(info));
        info.typeId = Steinberg::Vst::kNoteOnKeyswitchTypeID;
        info.keyRemapped = -1; info.unitId = -1; info.flags = 0;
        if (channel == 0 && i < 12) {
            info.keyswitchMin = info.keyswitchMax = rig::SLOT_BASE + i;
            const std::string t = "Slot " + std::to_string(i + 1), st = "S" + std::to_string(i + 1);
            copy(info.title, t.c_str()); copy(info.shortTitle, st.c_str());
        } else if (channel == 0) {
            const int k = i - 12; info.keyswitchMin = info.keyswitchMax = rig::TRIGGER_BASE + k;
            copy(info.title, rig::TRIGGER_NAMES[k]); copy(info.shortTitle, shortName(k));
        } else {
            static const char* T[5] = { "Down (einmal)", "Up (einmal)", "Nur Down", "Nur Up", "Automatisch" }, *ST[5] = { "Dn", "Up", "Dn+", "Up+", "Aut" };
            info.keyswitchMin = info.keyswitchMax = 24 + i;
            copy(info.title, T[i]); copy(info.shortTitle, ST[i]);
        }
        return Steinberg::kResultOk;
    }
private:
    static const char* shortName(int i) { static const char* S[9] = { "Dn", "Up", "Chk", "Gho", "Bas", "PDn", "PUp", "ArD", "ArU" }; return S[i]; }
    static void copy(Steinberg::Vst::String128 dst, const char* src) { int k = 0; for (; src[k] && k < 127; k++) dst[k] = (Steinberg::Vst::TChar) src[k]; dst[k] = 0; }
    std::atomic<Steinberg::uint32> refs{ 1 };
};

struct RiggingVST3Ext : public juce::VST3ClientExtensions {
    RiggingKeyswitches ks;
    int32_t queryIEditController(const Steinberg::TUID iid, void** obj) override
    {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IKeyswitchController_iid)) { ks.addRef(); *obj = static_cast<Steinberg::Vst::IKeyswitchController*>(&ks); return Steinberg::kResultOk; }
        *obj = nullptr; return Steinberg::kNoInterface;
    }
};

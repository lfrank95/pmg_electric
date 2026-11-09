#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Keyswitches.h"

using namespace juce;
namespace {
const StringArray GUITARS { "S-Typ", "T-Typ", "LP-Typ", "335-Typ" };
const char* GUITAR_KEYS[4] = { "S", "T", "LP", "335" };
}

AudioProcessorValueTreeState::ParameterLayout RiggingProcessor::makeLayout()
{
    using P = AudioParameterFloat; using R = NormalisableRange<float>;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto f = [&](const char* id, const char* name, float lo, float hi, float def, const char* unit = "") { v.push_back(std::make_unique<P>(ParameterID{ id, 1 }, name, R(lo, hi), def, AudioParameterFloatAttributes().withLabel(unit))); };
    // Gitarre
    v.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "guitar", 1 }, "Gitarre", GUITARS, 0));
    v.push_back(std::make_unique<AudioParameterInt>(ParameterID{ "pickup", 1 }, "Pickup-Wahl", 0, 4, 4));
    f("vol", "Volume (Gitarre)", 0, 1, 1); f("tone", "Tone (Gitarre)", 0, 1, 1);
    f("pickPos", "Anschlagposition", 15, 320, 110, "mm"); f("hard", "Plektrum hart", 0, 1, 0.8f);
    f("action", "Saitenlage", 0, 1, 0.5f); f("age", "Saitenalter", 0, 1, 0.15f); f("pickHeight", "Pickup-Höhe", 0, 1, 0.35f);
    f("tune", "Stimmung", -100, 100, 0, "ct");
    f("pickMM", "Plektrum-Dicke", 0.4f, 2.0f, 0.88f, "mm");
    f("cableM", "Kabel", 1.0f, 10.0f, 4.5f, "m");
    v.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "pickMat", 1 }, "Plektrum-Material", StringArray{ "Celluloid", "Nylon", "Tortex", "Ultex" }, 0));
    // Hände / Absicht
    f("palm", "Palm Mute", 0, 1, 0); f("palmPos", "Hand-Position", 0, 1, 0.3f); f("human", "Menschlich", 0, 1, 0.6f);
    f("lookahead", "Vorlauf", 0, 300, 60, "ms");
    v.push_back(std::make_unique<AudioParameterInt>(ParameterID{ "take", 1 }, "Take", 1, 999, 1));
    v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "doubler", 1 }, "Doubler", false));
    StringArray presets; for (auto& p : rig::accentPresets()) presets.add(String::fromUTF8(p.name));
    v.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "accent", 1 }, "Akzent-Raster", presets, 0));
    f("accentStrength", "Akzent-Stärke", 0, 1, 0.6f); f("swing", "Swing", 0, 1, 0);
    // Amp
    f("gain", "Gain", 0, 1, 0.75f); f("bass", "Bass", 0, 1, 0.5f); f("mid", "Mid", 0, 1, 0.5f); f("treble", "Treble", 0, 1, 0.6f);
    f("master", "Master", 0, 1, 0.45f); f("sag", "Sag", 0, 1, 0.5f); f("out", "Ausgang", 0, 1, 0.5f);
    v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "closed", 1 }, "Box geschlossen", false));
    v.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "noise", 1 }, "Rauschen/Brummen", true));
    f("micPos", "Mikro-Position", 0, 1, 0.3f); f("micDist", "Mikro-Abstand", 0, 1, 0.25f); f("room", "Raum", 0, 1, 0.25f); f("feedback", "Feedback", 0, 1, 0);
    return { v.begin(), v.end() };
}

RiggingProcessor::RiggingProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)), apvts(*this, nullptr, "PMGELECTRIC", makeLayout())
{
    for (auto* p : getParameters()) if (auto* r = dynamic_cast<RangedAudioParameter*>(p)) apvts.addParameterListener(r->getParameterID(), this);
    vst3ext = std::make_unique<RiggingVST3Ext>();
    defaultSlots(); writeSlotsToState();
    side[0].H = rig::Hand(101); side[1].H = rig::Hand(202);
    for (int i = 0; i < 2; i++) side[i].S = std::make_unique<rig::MidiStrummer>(side[i].H);
    worker = std::thread([this] {
        for (;;) {
            std::unique_lock<std::mutex> lk(wm); wcv.wait(lk, [&] { return wGo || wQuit; }); if (wQuit) return; wGo = false; lk.unlock();
            renderSide(1, wOut, wN);
            lk.lock(); wDone = true; wcv.notify_all();
        }
    });
}
RiggingProcessor::~RiggingProcessor()
{
    { std::lock_guard<std::mutex> lk(wm); wQuit = true; } wcv.notify_all(); if (worker.joinable()) worker.join();
}
VST3ClientExtensions* RiggingProcessor::getVST3ClientExtensions() { return vst3ext.get(); }

void RiggingProcessor::prepareToPlay(double sampleRate, int)
{
    fs = sampleRate; os = fs >= 88000 ? 1 : 2;
    for (int i = 0; i < 2; i++) {
        auto& sd = side[i];
        sd.E.p.serial = i == 0 ? 4711 : 4712;
        sd.E.init(fs, os); sd.E.setSeed(i == 0 ? 101 : 202);
        sd.S->reset(); sd.E.queue.clear();
        sideReady[i] = true;
    }
    applyAllParams(); drainParams();
    for (int i = 0; i < 2; i++) side[i].E.warmUp(0.2);   // nach dem Setzen aller Parameter nochmal einschwingen: Laden ohne Knack
    setLatencySamples((int) std::lround(lookSec * fs));
}
void RiggingProcessor::releaseResources() {}

void RiggingProcessor::applyAllParams()
{
    for (auto* p : getParameters()) if (auto* r = dynamic_cast<RangedAudioParameter*>(p)) parameterChanged(r->getParameterID(), r->convertFrom0to1(r->getValue()));
}
void RiggingProcessor::parameterChanged(const String& id, float v)
{
    if (id == "lookahead") { lookSec = v / 1000.0; pendingLatency = (int) std::lround(lookSec * fs); return; }
    if (id == "doubler") { doubler = v > 0.5f; return; }
    if (id == "accent") { const int i = jlimit(0, (int) rig::accentPresets().size() - 1, (int) std::lround(v)); const auto& p = rig::accentPresets()[(size_t) i]; grid.set(p); return; }
    if (id == "accentStrength") { grid.strength = v; return; }
    if (id == "swing") { grid.swing = v; return; }
    // alles andere betrifft Engine/Hände → im Audio-Thread anwenden (Umbauten dürfen nicht nebenläufig zum Rendern passieren)
    const SpinLock::ScopedLockType sl(paramLock); paramQueue.push_back({ id, v });
}
void RiggingProcessor::drainParams()
{
    const SpinLock::ScopedTryLockType tl(paramLock); if (!tl.isLocked()) return;
    for (auto& pc : paramQueue) {
        if (pc.first == "take") { for (auto& sd : side) sd.H.take = (int) std::lround(pc.second); continue; }
        for (int i = 0; i < 2; i++) if (sideReady[i]) applyParam(pc.first, pc.second, side[i].E, side[i].H);
    }
    paramQueue.clear();
}
void RiggingProcessor::applyParam(const String& id, float v, rig::Engine& E, rig::Hand& H)
{
    // Achtung: Umbauten (Tonestack, Pickup-Schaltung, Modell) sind nicht Echtzeit-sicher – kleine Knackser beim Drehen sind hier ok.
    if (id == "guitar") E.setGuitar(GUITAR_KEYS[jlimit(0, 3, (int) std::lround(v))]);
    else if (id == "pickup") E.setPickup(jlimit(0, (int) E.model->selector.size() - 1, (int) std::lround(v)));
    else if (id == "vol") E.setVolTone(v, E.p.tone); else if (id == "tone") E.setVolTone(E.p.vol, v);
    else if (id == "pickPos") E.p.pickPos = v; else if (id == "hard") E.p.hard = v;
    else if (id == "action") E.setAction(v); else if (id == "age") E.setAge(v); else if (id == "pickHeight") E.setPickHeight(v);
    else if (id == "tune") E.setTune(v, 0);
    else if (id == "cableM") E.setCable(v);
    else if (id == "pickMM") E.p.pickMM = v; else if (id == "pickMat") E.p.pickMat = jlimit(0, 3, (int) std::lround(v));
    else if (id == "palm") pushEvents(E, H.setIntent(rig::Hand::Palm, v, E.frame / fs + lookSec), E.frame / fs + lookSec);
    else if (id == "palmPos") pushEvents(E, H.setIntent(rig::Hand::PalmQ, v, E.frame / fs + lookSec), E.frame / fs + lookSec);
    else if (id == "human") { H.human = v; E.p.human = v; }
    else if (id == "gain") E.p.gain = v;
    else if (id == "bass") E.setToneStack(v, E.p.mid, E.p.treble); else if (id == "mid") E.setToneStack(E.p.bass, v, E.p.treble); else if (id == "treble") E.setToneStack(E.p.bass, E.p.mid, v);
    else if (id == "master") E.p.master = v; else if (id == "sag") E.setSag(v); else if (id == "out") E.p.out = v;
    else if (id == "closed") E.setClosed(v > 0.5f); else if (id == "noise") E.p.noise = v > 0.5f;
    else if (id == "micPos") E.setMic(v, E.p.micDist, E.p.room); else if (id == "micDist") E.setMic(E.p.micPos, v, E.p.room); else if (id == "room") E.setMic(E.p.micPos, E.p.micDist, v);
    else if (id == "feedback") E.setFeedback(v);
}

// ---------- Slots ----------
static juce::String shapeToString(const rig::Shape& f) { juce::StringArray a; for (int s = 0; s < 6; s++) a.add(juce::String(f[s])); return a.joinIntoString(","); }
static rig::Shape shapeFromString(const juce::String& str) { rig::Shape f; f.f.fill(-1); auto a = juce::StringArray::fromTokens(str, ",", ""); for (int s = 0; s < 6 && s < a.size(); s++) f[s] = a[s].getIntValue(); return f; }
void RiggingProcessor::defaultSlots()
{
    // Vorbelegung wie in der Testgitarre: gängige Akkorde, Griff je nach Lage
    static const char* names[12] = { "G", "C", "D", "Em", "Am", "F", "E", "A", "Dm", "H7", "E5", "A5" };
    const juce::SpinLock::ScopedLockType sl(slotLock);
    for (int i = 0; i < 12; i++) { slots[i] = rig::Slot{}; slots[i].name = names[i]; }
    slotsDirty = true;
}
void RiggingProcessor::setSlot(int i, const rig::Slot& sl)
{
    if (i < 0 || i >= 12) return;
    { const juce::SpinLock::ScopedLockType l(slotLock); slots[i] = sl; }
    slotsDirty = true; writeSlotsToState();
}
void RiggingProcessor::writeSlotsToState()
{
    juce::ValueTree t("SLOTS");
    for (int i = 0; i < 12; i++) { const rig::Slot sl = getSlot(i); juce::ValueTree c("SLOT"); c.setProperty("name", juce::String::fromUTF8(sl.name.c_str()), nullptr);
        c.setProperty("fr", sl.hasFr ? shapeToString(sl.fr) : juce::String(), nullptr); c.setProperty("fixed", sl.fixed, nullptr); t.appendChild(c, nullptr); }
    auto old = apvts.state.getChildWithName("SLOTS"); if (old.isValid()) apvts.state.removeChild(old, nullptr);
    apvts.state.appendChild(t, nullptr);
}
void RiggingProcessor::readSlotsFromState()
{
    auto t = apvts.state.getChildWithName("SLOTS"); if (!t.isValid()) { defaultSlots(); writeSlotsToState(); return; }
    const juce::SpinLock::ScopedLockType l(slotLock);
    for (int i = 0; i < 12 && i < t.getNumChildren(); i++) { auto c = t.getChild(i); rig::Slot sl; sl.name = c.getProperty("name").toString().toStdString();
        const juce::String fr = c.getProperty("fr").toString(); if (fr.isNotEmpty()) { sl.fr = shapeFromString(fr); sl.hasFr = true; } sl.fixed = (bool) c.getProperty("fixed"); slots[i] = sl; }
    slotsDirty = true;
}
void RiggingProcessor::newTake()
{
    if (auto* p = dynamic_cast<AudioParameterInt*>(apvts.getParameter("take"))) { const int t = p->get() % 999 + 1; p->beginChangeGesture(); *p = t; p->endChangeGesture(); }
}
void RiggingProcessor::learnAccentsFromRecent()
{
    std::vector<rig::AccentGrid::Hit> hits; { const ScopedLock sl(hitsLock); hits = recentHits; }
    grid.learn(hits);
}

void RiggingProcessor::pushEvents(rig::Engine& E, const rig::Events& ev, double tNowHand)
{
    const double nowMs = Time::getMillisecondCounterHiRes();
    for (auto& e : ev) {
        rig::Event x = e; x.at = (long) std::lround(std::max(0.0, e.when) * fs); E.push(x);
        if (&E != &side[0].E) continue;      // Anzeige folgt Take A
        const double ms = nowMs + (e.when - tNowHand) * 1000.0 + lookSec * 0; const size_t s = (size_t) std::max(0, e.s);
        switch (e.t) {
        case rig::Event::Pluck: vis.s[s].hitMs = ms; vis.s[s].dir = e.dir; vis.s[s].vel = (float) e.vel; vis.s[s].fret = e.fret; vis.lastDir = e.dir; vis.lastStrumMs = ms; break;
        case rig::Event::Damp: if (e.s < 0) for (auto& st : vis.s) st.dampMs = ms; else vis.s[s].dampMs = ms; break;
        case rig::Event::Palm: if (e.s >= 0) { vis.s[s].palm = (float) e.v; vis.palmQ = (float) e.q; } break;
        case rig::Event::Finger: vis.s[s].fingerMs = ms; vis.s[s].fingerPhase = e.phase == rig::Event::Lift ? 1 : 2; if (e.phase == rig::Event::Press) vis.s[s].fret = e.fret; break;
        default: break;
        }
    }
}

void RiggingProcessor::renderSide(int sd, float* out, int n)
{
    auto& S = side[sd];
    for (int i = 0; i < n; i += 480) S.E.process(out + i, std::min(480, n - i));
}

void RiggingProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (int lat = pendingLatency.exchange(-1); lat >= 0) setLatencySamples(lat);
    drainParams();
    if (slotsDirty.exchange(false)) { const SpinLock::ScopedTryLockType tl(slotLock); if (tl.isLocked()) { for (auto& sd : side) for (int i = 0; i < 12; i++) sd.S->slots[i] = slots[i]; } else slotsDirty = true; }
    // Transport
    tr.playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        tr.playing = pos->getIsPlaying();
        if (auto ppq = pos->getPpqPosition()) tr.ppq = *ppq;
        if (auto bpm = pos->getBpm()) tr.bpm = *bpm > 0 ? *bpm : 120;
        if (auto ts = pos->getTimeSignature()) { tr.num = ts->numerator; tr.den = ts->denominator; }
    }
    const double tBlock = side[0].E.frame / fs;             // Host-Zeit des Blockanfangs (in Engine-Frames)
    tr.tBlockHand = tBlock + lookSec;
    vis.playing = tr.playing;
    rig::Clock clock = [this, tBlock](double tHand) -> std::optional<rig::ClockPos> {
        if (!tr.playing) return std::nullopt;
        const double ppq = tr.ppq + ((tHand - lookSec) - tBlock) * tr.bpm / 60.0;
        const double barLen = tr.num * 4.0 / tr.den; const int bar = (int) std::floor(ppq / barLen); const double inBar = ppq - bar * barLen;
        rig::ClockPos c; c.bar = bar; c.step16 = inBar * 4; c.beat = inBar / (4.0 / tr.den); c.num = tr.num; c.den = tr.den; c.bpm = tr.bpm; c.barLen = barLen;
        return c;
    };
    const bool dbl = doubler.load(); const int nSides = dbl ? 2 : 1;
    for (int k = 0; k < nSides; k++) { side[k].H.clock = clock; side[k].S->clock = clock; side[k].S->grid = &grid; }
    if (tr.playing) { if (auto c = clock(tr.tBlockHand)) { vis.bar = c->bar; vis.beat = (float) c->beat; } }

    // MIDI → Strummer, in Paketen gleicher Zeit
    int lastPos = -1000000;
    auto flushAll = [&](double t) { for (int k = 0; k < nSides; k++) pushEvents(side[k].E, side[k].S->flush(t), tr.tBlockHand); };
    for (const auto meta : midi) {
        const auto m = meta.getMessage(); const int pos = meta.samplePosition;
        if (lastPos > -1000000 && pos - lastPos > 0.002 * fs) flushAll(tr.tBlockHand + lastPos / fs);
        lastPos = pos;
        const double t = tr.tBlockHand + pos / fs; const int ch = m.getChannel();
        for (int k = 0; k < nSides; k++) {
            auto& S = *side[k].S; rig::Events ev;
            if (m.isNoteOn()) ev = S.noteOn(ch, m.getNoteNumber(), m.getVelocity(), t);
            else if (m.isNoteOff()) ev = S.noteOff(ch, m.getNoteNumber(), t);
            else if (m.isController()) ev = S.cc(m.getControllerNumber(), m.getControllerValue(), t);
            else if (m.isPitchWheel()) ev = S.pitchBend(m.getPitchWheelValue(), t);
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { S.reset(); ev = side[k].H.damp(-1, t); }
            pushEvents(side[k].E, ev, tr.tBlockHand);
        }
        if (m.isNoteOn() && ch == 1 && rig::triggerOf(m.getNoteNumber()) != rig::Trigger::None && tr.playing) {
            if (auto c = clock(t)) { const ScopedLock sl(hitsLock); recentHits.push_back({ c->step16, (double) m.getVelocity() }); if (recentHits.size() > 512) recentHits.erase(recentHits.begin(), recentHits.begin() + 256); }
        }
    }
    if (lastPos > -1000000) flushAll(tr.tBlockHand + lastPos / fs);
    midi.clear();
    // Anhören aus der Oberfläche: Slot greifen und einmal abschlagen
    if (int a = audition.exchange(-1); a >= 0) for (int k = 0; k < nSides; k++) {
        auto& S = *side[k].S; const double t = tr.tBlockHand + 0.005;
        rig::Events ev = S.selectSlot(a, t, t + 0.09); rig::Events r = S.runTrigger(rig::Trigger::Down, 100, t + 0.09); ev.insert(ev.end(), r.begin(), r.end());
        pushEvents(side[k].E, ev, tr.tBlockHand);
    }

    // Rendern: Take A hier, Take B im Worker
    float* L = buffer.getWritePointer(0); float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    if (dbl && R) {
        { std::lock_guard<std::mutex> lk(wm); wOut = R; wN = n; wGo = true; wDone = false; } wcv.notify_all();
        renderSide(0, L, n);
        { std::unique_lock<std::mutex> lk(wm); wcv.wait(lk, [&] { return wDone; }); }
    } else {
        renderSide(0, L, n);
        if (R) FloatVectorOperations::copy(R, L, n);
    }
    float pk = 0; for (int i = 0; i < n; i++) pk = std::max(pk, std::abs(L[i])); vis.level = pk;
    vis.slot = side[0].S->slotActive;
    const auto& S0 = *side[0].S; if (S0.chord) { const SpinLock::ScopedTryLockType tl(vis.lock); if (tl.isLocked()) vis.chordInfo = String::fromUTF8(S0.lastInfo.c_str()); vis.handPos = (float) S0.handPos; }
}

void RiggingProcessor::getStateInformation(MemoryBlock& dest) { if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest); }
void RiggingProcessor::setStateInformation(const void* data, int size) { if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName(apvts.state.getType())) { apvts.replaceState(ValueTree::fromXml(*xml)); readSlotsFromState(); } }
AudioProcessorEditor* RiggingProcessor::createEditor() { return new RiggingEditor(*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RiggingProcessor(); }

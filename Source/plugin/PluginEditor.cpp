#include "PluginEditor.h"
using namespace juce;

namespace {
const Colour BG(0xff14161a), PANEL(0xff1d2127), INK(0xffe8e4d8), DIM(0xff8a8f99), ACC(0xfff2a33a), WOOD(0xff3a2a1e), STR(0xffd7cfb8);
constexpr double SCALE_MM = 648.0;
}

// ---------------- Board ----------------
void BoardView::paint(Graphics& g)
{
    auto& V = proc.vis; const double now = Time::getMillisecondCounterHiRes();
    auto r = getLocalBounds().toFloat().reduced(6);
    g.setColour(PANEL); g.fillRoundedRectangle(r, 8);
    // Geometrie: links Griffbrett (Bund 0–15, ~ erste 400 mm), rechts Korpus bis Steg
    const float x0 = r.getX() + 36, x1 = r.getRight() - 24, w = x1 - x0;
    auto xAt = [&](double mm) { return (float) (x0 + w * jlimit(0.0, 1.0, mm / SCALE_MM)); };
    const float yTop = r.getY() + 34, yBot = r.getBottom() - 30, dy = (yBot - yTop) / 5;
    gx0 = x0; gw = w; gyBot = yBot; gdy = dy;
    auto yStr = [&](int s) { return yBot - s * dy; };     // Saite 0 (tiefes E) unten
    // Korpus
    const float xBody = xAt(SCALE_MM * 0.62);
    g.setColour(WOOD); g.fillRoundedRectangle(xBody, yTop - 22, x1 - xBody + 14, yBot - yTop + 44, 14);
    // Griffbrett + Bünde
    g.setColour(Colour(0xff2a2420)); g.fillRect(x0, yTop - 8, xBody - x0, yBot - yTop + 16);
    g.setColour(Colour(0xff777066));
    for (int f = 0; f <= 15; f++) { const double mm = SCALE_MM * (1 - std::pow(2.0, -f / 12.0)); const float x = xAt(mm); g.fillRect(x, yTop - 8, f == 0 ? 3.f : 1.f, yBot - yTop + 16);
        if (f == 3 || f == 5 || f == 7 || f == 9 || f == 12 || f == 15) { const double mm2 = SCALE_MM * (1 - std::pow(2.0, -(f - 0.5) / 12.0)); g.setColour(Colour(0xff8f877a)); g.fillEllipse(xAt(mm2) - 3, (yTop + yBot) / 2 - 3, 6, 6); g.setColour(Colour(0xff777066)); } }
    // Pickups (Hals ~ 150 mm vor Steg, Steg-PU ~ 40 mm) und Steg
    g.setColour(Colour(0xff111111));
    for (double d : { 150.0, 95.0, 40.0 }) g.fillRoundedRectangle(xAt(SCALE_MM - d) - 5, yTop - 10, 10, yBot - yTop + 20, 3);
    g.setColour(Colour(0xffb8b4a8)); g.fillRect(xAt(SCALE_MM) - 2, yTop - 14, 5.f, yBot - yTop + 28);
    // Hand-Position (Anschlag) und Handkante (Palm)
    const double pickMm = proc.apvts.getRawParameterValue("pickPos")->load();
    const float xHand = xAt(SCALE_MM - pickMm);
    g.setColour(ACC.withAlpha(0.35f)); g.drawLine(xHand, yTop - 18, xHand, yBot + 18, 2);
    g.setColour(ACC); g.setFont(11); g.drawText("Hand", xHand - 20, yTop - 32, 40, 12, Justification::centred);
    const float palmQ = V.palmQ.load();
    for (int s = 0; s < 6; s++) {
        const float p = V.s[s].palm.load(); if (p < 0.03f) continue;
        const float xc = xAt(SCALE_MM - 12 - 55 * palmQ), wP = 14 + 50 * p;
        g.setColour(Colour(0xffd9b48a).withAlpha(0.15f + 0.5f * p)); g.fillRoundedRectangle(xc - wP / 2, yStr(s) - dy * 0.36f, wP, dy * 0.72f, 5);
    }
    // Saiten
    for (int s = 0; s < 6; s++) {
        const float y = yStr(s); const double hitAge = (now - V.s[s].hitMs.load()) / 1000.0; const double dampAge = (now - V.s[s].dampMs.load()) / 1000.0;
        const float amp = (float) (hitAge >= 0 && hitAge < 2.5 ? std::exp(-hitAge * 1.6) * V.s[s].vel.load() : 0);
        const float th = 1.2f + 0.45f * (5 - s);
        g.setColour(dampAge >= 0 && dampAge < 0.5 ? DIM : STR.interpolatedWith(ACC, jlimit(0.f, 1.f, amp)));
        // Schwingung andeuten: Saite wird dicker/heller
        g.fillRect(x0, y - th / 2 - amp * 3, x1 - x0, th + amp * 6);
        // gegriffener Bund
        const int fr = V.s[s].fret.load();
        if (fr > 0) { const double mm = SCALE_MM * (1 - std::pow(2.0, -(fr - 0.45) / 12.0)); const double fa = (now - V.s[s].fingerMs.load()) / 1000.0;
            const bool lifting = V.s[s].fingerPhase.load() == 1 && fa < 0.25; g.setColour(lifting ? DIM : Colour(0xffe7c9a1)); g.fillEllipse(xAt(mm) - 7, y - 7, 14, 14); g.setColour(BG); g.setFont(10); g.drawText(String(fr), (int) xAt(mm) - 8, (int) y - 7, 16, 14, Justification::centred); }
        else if (fr < 0) { g.setColour(DIM); g.setFont(12); g.drawText("x", (int) x0 - 30, (int) y - 8, 20, 16, Justification::centred); }
        // Anschlag: Dreieck in Strum-Richtung (ab = nach oben im Bild zur hohen Saite? nein: ab = von tief nach hoch = im Bild nach oben)
        if (hitAge >= 0 && hitAge < 0.45) { const float a = (float) (1 - hitAge / 0.45); const int dir = V.s[s].dir.load(); Path t; const float xh = xHand + 16;
            if (dir > 0) t.addTriangle(xh - 6, y + 7, xh + 6, y + 7, xh, y - 8); else t.addTriangle(xh - 6, y - 7, xh + 6, y - 7, xh, y + 8);
            g.setColour(ACC.withAlpha(a)); g.fillPath(t); g.fillEllipse(xHand - 5 - 4 * a, y - 5 - 4 * a, 10 + 8 * a, 10 + 8 * a); }
    }
    // Saitennamen
    g.setColour(DIM); g.setFont(11); const char* names[6] = { "E", "A", "D", "G", "H", "e" };
    for (int s = 0; s < 6; s++) g.drawText(names[s], (int) r.getX() + 6, (int) yStr(s) - 8, 18, 16, Justification::centred);
    // Richtung des letzten Strums
    const double sa = (now - V.lastStrumMs.load()) / 1000.0;
    if (sa >= 0 && sa < 0.8) { g.setColour(ACC.withAlpha((float) (1 - sa / 0.8))); g.setFont(Font(FontOptions(18, Font::bold))); g.drawText(V.lastDir.load() > 0 ? CharPointer_UTF8("\xe2\x86\x93 ab") : CharPointer_UTF8("\xe2\x86\x91 auf"), (int) x1 - 70, (int) r.getY() + 4, 70, 22, Justification::centredRight); }
    // Pegel
    const float lv = V.level.load(); g.setColour(lv > 0.9f ? Colours::red : ACC.withAlpha(0.7f)); g.fillRect(r.getX() + 8, r.getBottom() - 10, (r.getWidth() - 16) * jlimit(0.f, 1.f, lv), 4.f);
    // Slot-Griff, der bearbeitet wird: Ringe auf den Bünden, o/x am Sattel (Klicken setzt/entfernt Finger)
    if (showEdit) {
        g.setColour(Colour(0xff7fd3ff));
        for (int s = 0; s < 6; s++) {
            const float y = yStr(s); const int f = editShape[s];
            if (f > 0) { const double mm = SCALE_MM * (1 - std::pow(2.0, -(f - 0.45) / 12.0)); g.drawEllipse(xAt(mm) - 9, y - 9, 18, 18, 2.2f); }
            g.setFont(12); g.drawText(f < 0 ? "x" : f == 0 ? "o" : "", (int) x0 - 26, (int) y - 8, 16, 16, Justification::centred);
        }
        g.setFont(11); g.drawText(CharPointer_UTF8("Griff bearbeiten: Bund anklicken = Finger setzen/entfernen, links vom Sattel = offen/gedämpft"), (int) x0, (int) r.getY() + 4, 520, 14, Justification::left);
    }
}
void BoardView::mouseDown(const MouseEvent& e)
{
    if (!showEdit || !onEdit) return;
    const int s = jlimit(0, 5, (int) std::lround((gyBot - e.position.y) / gdy));
    const float x = e.position.x;
    if (x < gx0) { onEdit(s, editShape[s] < 0 ? 0 : -1); return; }
    const double mm = (x - gx0) / gw * SCALE_MM;
    int f = 1; while (f < 15 && mm > SCALE_MM * (1 - std::pow(2.0, -f / 12.0))) f++;
    onEdit(s, editShape[s] == f ? 0 : f);
}

// ---------------- Editor ----------------
RiggingEditor::RiggingEditor(RiggingProcessor& p) : AudioProcessorEditor(p), proc(p), board(p)
{
    setLookAndFeel(nullptr);
    getLookAndFeel().setColour(Slider::rotarySliderFillColourId, ACC); getLookAndFeel().setColour(Slider::rotarySliderOutlineColourId, Colour(0xff3a3f48)); getLookAndFeel().setColour(Slider::thumbColourId, INK);
    getLookAndFeel().setColour(Label::textColourId, INK); getLookAndFeel().setColour(ComboBox::backgroundColourId, Colour(0xff2a2f38)); getLookAndFeel().setColour(TextButton::buttonColourId, Colour(0xff2a2f38));
    addAndMakeVisible(board);
    title.setText("PMG ELECTRIC", dontSendNotification); title.setFont(Font(FontOptions(26, Font::bold))); title.setColour(Label::textColourId, ACC); addAndMakeVisible(title);
    chordLbl.setFont(Font(FontOptions(15))); addAndMakeVisible(chordLbl);
    takeLbl.setFont(Font(FontOptions(13))); takeLbl.setColour(Label::textColourId, DIM); addAndMakeVisible(takeLbl);
    posLbl.setFont(Font(FontOptions(13))); posLbl.setColour(Label::textColourId, DIM); addAndMakeVisible(posLbl);
    newTakeBtn.onClick = [this] { proc.newTake(); }; addAndMakeVisible(newTakeBtn);
    learnBtn.onClick = [this] { proc.learnAccentsFromRecent(); }; addAndMakeVisible(learnBtn);
    learnBtn.setTooltip("Akzent-Raster aus den zuletzt abgespielten Trigger-Velocities lernen");

    // ---------- Akkord-Slots (Kanal 1, C0–B0) ----------
    for (int i = 0; i < 12; i++) {
        slotBtn[i].setClickingTogglesState(false); slotBtn[i].onClick = [this, i] { selectEditSlot(i); };
        slotBtn[i].setTooltip("Slot " + String(i + 1) + " – MIDI-Taste " + MidiMessage::getMidiNoteName(rig::SLOT_BASE + i, true, true, 3) + " auf Kanal 1");
        addAndMakeVisible(slotBtn[i]);
    }
    for (int r = 0; r < 12; r++) rootBox.addItem(String::fromUTF8(rig::ROOT_NAMES[r]), r + 1);
    { int id = 1; for (auto& q : rig::qualities()) qualBox.addItem(*q.q ? String::fromUTF8(q.q) : String(CharPointer_UTF8("Dur")), id++); }
    rootBox.onChange = [this] { slotNameChanged(); }; qualBox.onChange = [this] { slotNameChanged(); };
    prevBtn.onClick = [this] { stepVoicing(-1); }; nextBtn.onClick = [this] { stepVoicing(1); };
    fixedBtn.onClick = [this] { if (updatingUi) return; auto sl = proc.getSlot(editSlot); sl.fixed = fixedBtn.getToggleState(); if (sl.fixed && !sl.hasFr) { auto v = voicingsFor(sl.name); if (!v.empty()) { for (int k = 0; k < 6; k++) sl.fr[k] = v[0]->fr[k]; sl.hasFr = true; } } proc.setSlot(editSlot, sl); refreshSlotUi(); };
    hearBtn.onClick = [this] { proc.auditionSlot(editSlot); };
    resetBtn.onClick = [this] { auto sl = proc.getSlot(editSlot); sl.hasFr = false; sl.fixed = false; proc.setSlot(editSlot, sl); voicIdx = -1; refreshSlotUi(); };
    fixedBtn.setTooltip(CharPointer_UTF8("An: genau dieser Griff. Aus: der Spieler wählt den Griff je nach Lage der Hand."));
    voicLbl.setJustificationType(Justification::centred); slotHint.setColour(Label::textColourId, DIM); slotHint.setFont(Font(FontOptions(12)));
    for (Component* c : std::initializer_list<Component*>{ &rootBox, &qualBox, &prevBtn, &nextBtn, &voicLbl, &fixedBtn, &hearBtn, &resetBtn, &slotHint }) addAndMakeVisible(c);
    board.onEdit = [this](int s, int f) { auto sl = proc.getSlot(editSlot); if (!sl.hasFr) { auto v = voicingsFor(sl.name); sl.fr.f.fill(-1); if (!v.empty()) for (int k = 0; k < 6; k++) sl.fr[k] = v[0]->fr[k]; }
        sl.fr[s] = f; sl.hasFr = true; sl.fixed = true; proc.setSlot(editSlot, sl); voicIdx = -1; refreshSlotUi(); };
    board.showEdit = true;

    groups.push_back({ "Gitarre", {}, {} }); auto& G = groups.back();
    guitarBox.addItemList({ "S-Typ", "T-Typ", "LP-Typ", "335-Typ" }, 1); addAndMakeVisible(guitarBox); guitarAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "guitar", guitarBox);
    pickBox.addItemList({ "Celluloid", "Nylon", "Tortex", "Ultex" }, 1); addAndMakeVisible(pickBox); pickAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "pickMat", pickBox);
    for (auto [id, nm] : std::initializer_list<std::pair<const char*, const char*>>{ { "pickup", "Pickup" }, { "vol", "Volume" }, { "tone", "Tone" }, { "pickPos", "Anschlag mm" }, { "pickMM", "Plektrum mm" }, { "cableM", "Kabel m" }, { "hard", "Anschlag hart" }, { "action", "Saitenlage" }, { "age", "Saitenalter" }, { "pickHeight", "PU-Höhe" }, { "tune", "Stimmung" } }) addKnob(G, id, CharPointer_UTF8(nm));
    groups.push_back({ CharPointer_UTF8("Hände"), {}, {} }); auto& H = groups.back();
    for (auto [id, nm] : std::initializer_list<std::pair<const char*, const char*>>{ { "palm", "Palm Mute" }, { "palmPos", "Hand-Pos." }, { "human", "Menschlich" }, { "lookahead", "Vorlauf ms" }, { "accentStrength", "Akzente" }, { "swing", "Swing" } }) addKnob(H, id, CharPointer_UTF8(nm));
    for (auto& p2 : rig::accentPresets()) accentBox.addItem(String::fromUTF8(p2.name), accentBox.getNumItems() + 1);
    addAndMakeVisible(accentBox); accentAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "accent", accentBox);
    addAndMakeVisible(doublerBtn); doublerAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "doubler", doublerBtn);
    groups.push_back({ "Amp & Box", {}, {} }); auto& A = groups.back();
    for (auto [id, nm] : std::initializer_list<std::pair<const char*, const char*>>{ { "gain", "Gain" }, { "bass", "Bass" }, { "mid", "Mid" }, { "treble", "Treble" }, { "master", "Master" }, { "sag", "Sag" }, { "out", "Ausgang" }, { "micPos", "Mikro" }, { "micDist", "Abstand" }, { "room", "Raum" }, { "feedback", "Feedback" } }) addKnob(A, id, CharPointer_UTF8(nm));
    addAndMakeVisible(closedBtn); closedAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "closed", closedBtn);
    addAndMakeVisible(noiseBtn); noiseAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "noise", noiseBtn);
    setSize(1120, 812); setResizable(false, false);
    selectEditSlot(0);
    startTimerHz(10);
}
RiggingEditor::~RiggingEditor() { setLookAndFeel(nullptr); }

void RiggingEditor::addKnob(Group& g, const String& id, const String& name)
{
    Knob k; k.s = std::make_unique<Slider>(Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow); k.s->setTextBoxStyle(Slider::TextBoxBelow, false, 56, 16); k.s->setNumDecimalPlacesToDisplay(2);
    k.l = std::make_unique<Label>(); k.l->setText(name, dontSendNotification); k.l->setJustificationType(Justification::centred); k.l->setFont(Font(FontOptions(12))); k.l->setColour(Label::textColourId, DIM);
    k.a = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, *k.s);
    addAndMakeVisible(*k.s); addAndMakeVisible(*k.l); g.knobs.push_back(std::move(k));
}

std::vector<const rig::Voicing*> RiggingEditor::voicingsFor(const std::string& name) const
{
    std::vector<const rig::Voicing*> v; for (auto& x : rig::voicings()) if (name == x.name) v.push_back(&x);
    // am Hals entlang sortiert (wie man durchblättert)
    auto lage = [](const rig::Voicing* p) { int mn = 99; bool open = false; for (int s = 0; s < 6; s++) { if (p->fr[s] > 0) mn = std::min(mn, p->fr[s]); if (p->fr[s] == 0) open = true; } return (mn == 99 ? 0 : mn) - (open ? 0.5 : 0.0); };
    std::stable_sort(v.begin(), v.end(), [&](auto a, auto b) { return lage(a) < lage(b); });
    return v;
}
void RiggingEditor::selectEditSlot(int i) { editSlot = i; voicIdx = -1; refreshSlotUi(); }
void RiggingEditor::slotNameChanged()
{
    if (updatingUi) return;
    auto sl = proc.getSlot(editSlot);
    const int r = rootBox.getSelectedId() - 1, q = qualBox.getSelectedId() - 1;
    if (r < 0 || q < 0) return;
    sl.name = std::string(rig::ROOT_NAMES[r]) + rig::qualities()[(size_t) q].q; sl.hasFr = false; sl.fixed = false;
    proc.setSlot(editSlot, sl); voicIdx = -1; refreshSlotUi();
}
void RiggingEditor::stepVoicing(int d)
{
    auto sl = proc.getSlot(editSlot); auto v = voicingsFor(sl.name); if (v.empty()) return;
    voicIdx = (voicIdx < 0 ? (d > 0 ? 0 : (int) v.size() - 1) : (voicIdx + d + (int) v.size()) % (int) v.size());
    for (int k = 0; k < 6; k++) sl.fr[k] = v[(size_t) voicIdx]->fr[k];
    sl.hasFr = true; sl.fixed = true; proc.setSlot(editSlot, sl); refreshSlotUi(); proc.auditionSlot(editSlot);
}
void RiggingEditor::refreshSlotUi()
{
    updatingUi = true;
    const auto sl = proc.getSlot(editSlot);
    // Name in Grundton + Typ zerlegen
    int root = -1, qual = -1;
    for (int r = 11; r >= 0 && root < 0; r--) { const std::string rn = rig::ROOT_NAMES[r]; if (sl.name.rfind(rn, 0) == 0) { const std::string rest = sl.name.substr(rn.size()); int qi = 0; for (auto& q : rig::qualities()) { if (rest == q.q) { root = r; qual = qi; break; } qi++; } } }
    rootBox.setSelectedId(root + 1, dontSendNotification); qualBox.setSelectedId(qual + 1, dontSendNotification);
    auto v = voicingsFor(sl.name);
    if (sl.hasFr && voicIdx < 0) for (size_t k = 0; k < v.size(); k++) { bool eq = true; for (int s = 0; s < 6; s++) if (v[k]->fr[s] != sl.fr[s]) eq = false; if (eq) { voicIdx = (int) k; break; } }
    String fr; if (sl.hasFr) for (int s = 0; s < 6; s++) fr << (sl.fr[s] < 0 ? "x" : String(sl.fr[s])) << " ";
    voicLbl.setText(!sl.hasFr ? String(CharPointer_UTF8("automatisch (")) + String((int) v.size()) + " Griffe)" : voicIdx >= 0 ? "Griff " + String(voicIdx + 1) + "/" + String((int) v.size()) + "  " + fr : String(CharPointer_UTF8("eigener Griff  ")) + fr, dontSendNotification);
    fixedBtn.setToggleState(sl.fixed, dontSendNotification);
    slotHint.setText("Slot " + String(editSlot + 1) + ": Taste " + MidiMessage::getMidiNoteName(rig::SLOT_BASE + editSlot, true, true, 3) + String(CharPointer_UTF8(" auf Kanal 1 wählt ihn, C1/C#1 strummen.")), dontSendNotification);
    // Griff auf dem Griffbrett: fester Griff, sonst der, den der Spieler unten am Hals nehmen würde
    if (sl.hasFr) board.editShape = sl.fr; else if (!v.empty()) for (int s = 0; s < 6; s++) board.editShape[s] = v[0]->fr[s];
    updatingUi = false;
}
void RiggingEditor::timerCallback()
{
    const int act = proc.activeSlot();
    for (int i = 0; i < 12; i++) {
        const auto sl = proc.getSlot(i);
        slotBtn[i].setButtonText(String(i + 1) + "  " + (sl.name.empty() ? String("–") : String::fromUTF8(sl.name.c_str())) + (sl.fixed ? "*" : ""));
        slotBtn[i].setColour(TextButton::buttonColourId, i == editSlot ? Colour(0xff3d4a5c) : Colour(0xff2a2f38));
        slotBtn[i].setColour(TextButton::textColourOffId, i == act ? ACC : INK);
    }
    String c; { const SpinLock::ScopedLockType sl(proc.vis.lock); c = proc.vis.chordInfo; }
    chordLbl.setText(c.isEmpty() ? "– kein Akkord –" : c, dontSendNotification);
    const int take = (int) proc.apvts.getRawParameterValue("take")->load();
    takeLbl.setText("Take " + String(take), dontSendNotification);
    posLbl.setText(proc.vis.playing.load() ? "Takt " + String(proc.vis.bar.load() + 1) + "  Schlag " + String(proc.vis.beat.load() + 1, 1) : "Transport steht", dontSendNotification);
}

void RiggingEditor::paint(Graphics& g)
{
    g.fillAll(BG);
    g.setFont(Font(FontOptions(13, Font::bold)));
    for (auto& grp : groups) { g.setColour(PANEL); g.fillRoundedRectangle(grp.area.toFloat(), 8); g.setColour(ACC); g.drawText(grp.title, grp.area.getX() + 10, grp.area.getY() + 4, 200, 16, Justification::left); }
}
void RiggingEditor::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto top = r.removeFromTop(34);
    title.setBounds(top.removeFromLeft(190)); chordLbl.setBounds(top.removeFromLeft(330)); takeLbl.setBounds(top.removeFromLeft(70)); newTakeBtn.setBounds(top.removeFromLeft(100).reduced(0, 4)); top.removeFromLeft(10);
    posLbl.setBounds(top.removeFromLeft(170)); learnBtn.setBounds(top.removeFromRight(130).reduced(0, 4));
    r.removeFromTop(6);
    { auto row = r.removeFromTop(28); const int w = row.getWidth() / 12; for (int i = 0; i < 12; i++) slotBtn[i].setBounds(row.removeFromLeft(w).reduced(2, 0)); }
    r.removeFromTop(6);
    { auto row = r.removeFromTop(28);
      rootBox.setBounds(row.removeFromLeft(70)); row.removeFromLeft(4); qualBox.setBounds(row.removeFromLeft(90)); row.removeFromLeft(10);
      prevBtn.setBounds(row.removeFromLeft(28)); voicLbl.setBounds(row.removeFromLeft(230)); nextBtn.setBounds(row.removeFromLeft(28)); row.removeFromLeft(10);
      fixedBtn.setBounds(row.removeFromLeft(100)); hearBtn.setBounds(row.removeFromLeft(80)); row.removeFromLeft(6); resetBtn.setBounds(row.removeFromLeft(100)); row.removeFromLeft(10);
      slotHint.setBounds(row); }
    r.removeFromTop(6);
    board.setBounds(r.removeFromTop(250));
    r.removeFromTop(8);
    const int kw = 74, kh = 96;
    auto layoutGroup = [&](Group& g, Rectangle<int> area, std::function<void(Rectangle<int>&)> extra) {
        g.area = area; auto in = area.reduced(8); in.removeFromTop(18);
        if (extra) extra(in);
        int x = in.getX(), y = in.getY();
        for (auto& k : g.knobs) { if (x + kw > in.getRight()) { x = in.getX(); y += kh; } k.l->setBounds(x, y, kw, 14); k.s->setBounds(x, y + 14, kw, kh - 16); x += kw; }
    };
    layoutGroup(groups[0], r.removeFromTop(kh + 30), [&](Rectangle<int>& in) { auto col = in.removeFromLeft(100); guitarBox.setBounds(col.removeFromTop(24).translated(0, 14)); pickBox.setBounds(col.removeFromTop(24).translated(0, 22)); in.removeFromLeft(6); });
    r.removeFromTop(8);
    layoutGroup(groups[1], r.removeFromTop(kh + 30), [&](Rectangle<int>& in) { auto right = in.removeFromRight(130); accentBox.setBounds(right.removeFromTop(24).translated(0, 20)); doublerBtn.setBounds(right.removeFromTop(24).translated(0, 28)); });
    r.removeFromTop(8);
    layoutGroup(groups[2], r.removeFromTop(kh + 30), [&](Rectangle<int>& in) { auto right = in.removeFromRight(110); closedBtn.setBounds(right.removeFromTop(24).translated(0, 20)); noiseBtn.setBounds(right.removeFromTop(24).translated(0, 28)); });
}

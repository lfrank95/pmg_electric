#pragma once
#include "PluginProcessor.h"

/** Saiten-Visualisierung: Griff, Anschlag (Richtung ab/auf), Handkante (Palm), Hand-Position, Dämpfen */
class BoardView : public juce::Component, private juce::Timer {
public:
    explicit BoardView(RiggingProcessor& p) : proc(p) { startTimerHz(40); }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    // Griff des Slots, der gerade bearbeitet wird (Umriss), und Rückmeldung beim Klicken: Saite, Bund (0 = offen, -1 = gedämpft)
    bool showEdit = false; rig::Shape editShape{};
    std::function<void(int, int)> onEdit;
private:
    void timerCallback() override { repaint(); }
    RiggingProcessor& proc;
    float gx0 = 0, gw = 1, gyBot = 0, gdy = 1;    // Geometrie aus dem letzten paint
};

class RiggingEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit RiggingEditor(RiggingProcessor&);
    ~RiggingEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    struct Knob { std::unique_ptr<juce::Slider> s; std::unique_ptr<juce::Label> l; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a; };
    struct Group { juce::String title; std::vector<Knob> knobs; juce::Rectangle<int> area; };
    void addKnob(Group& g, const juce::String& id, const juce::String& name);
    RiggingProcessor& proc;
    BoardView board;
    std::vector<Group> groups;
    juce::ComboBox guitarBox, accentBox, pickBox; juce::ToggleButton doublerBtn{ "Doubler" }, closedBtn{ "Box zu" }, noiseBtn{ "Rauschen" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> guitarAtt, accentAtt, pickAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> doublerAtt, closedAtt, noiseAtt;
    juce::TextButton newTakeBtn{ "Neuer Take" }, learnBtn{ "Aus MIDI lernen" };
    juce::Label title, chordLbl, takeLbl, posLbl;
    // Akkord-Slots
    juce::TextButton slotBtn[12];
    juce::ComboBox rootBox, qualBox; juce::TextButton prevBtn{ "<" }, nextBtn{ ">" }, hearBtn{ "Anhören" }, resetBtn{ "Zurücksetzen" };
    juce::ToggleButton fixedBtn{ "Griff fest" }; juce::Label voicLbl, slotHint;
    int editSlot = 0, voicIdx = -1; bool updatingUi = false;
    void selectEditSlot(int i);
    void refreshSlotUi();
    void slotNameChanged();
    void stepVoicing(int d);
    std::vector<const rig::Voicing*> voicingsFor(const std::string& name) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RiggingEditor)
};

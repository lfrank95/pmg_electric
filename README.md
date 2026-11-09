# PMG Electric

**Physical Modeling Guitarist – Electric.** Physical-Modeling-E-Gitarre als VST3 – ein Instrument, das man **programmiert**, nicht spielt.
Sechs Saiten (Wellenleiter mit zwei Schwingungsebenen, Steifigkeit, Bünde, Griffqualität, Handkante), Tonabnehmer als
Schaltung, vier 12AX7-Stufen, Tonestack, Wheelhouse-Endstufe, 10"-Lautsprecher, Membran/Mikro/Raum, Feedback.
Zwei virtuelle Hände setzen MIDI-**Absicht** in menschliches Spiel um (Umgreifen, Timing, Anschlagwinkel, Palm-Druck mit
natürlichen Abweichungen). Der Zufall hängt an der Taktposition: gleiche Stelle, gleicher Take → gleiches Ergebnis;
„Neuer Take" würfelt neu.

Schwester-Projekt von [Wheelhouse](https://github.com/lfrank95/wheelhouse) (Hammond/Leslie).

## MIDI-Modell

**Kanal 1 – Absicht (schnell):** Akkord hinschreiben, Rhythmus mit Triggern.

| Taste | Bedeutung |
|---|---|
| C0–B0 (12–23) | Akkord-Slot 1–12 wählen (in der Oberfläche belegt: Akkord oder eigener Griff) |
| C1–G#1 (24–32) | Trigger: Down, Up, Choke, Ghost, Bass, Part Down, Part Up, Arp Down, Arp Up. Velocity = Kraft **und** Tempo |
| ab C2 | gehaltene Akkordtöne. Werden erkannt (28 Akkordtypen inkl. 9, 11, 13, m7b5, 7#9 …), zum Griff ergänzt, spielen selbst nichts |

**Kanal 2 – wie notiert (volle Kontrolle):** Was in den Noten steht, wird als echte Bewegung gespielt.

* Ein Ton = Anschlag. Mehrere Töne gleichzeitig (tot quantisiert) = Strum über **genau diese Saiten**. Der Spieler sucht die
  spielbarste Saitenzuordnung und greift rechtzeitig um.
* Richtung: Die Schlaghand pendelt im Takt (Zählzeit/Achtel ab, „und" auf; mit Sechzehnteln im Takt Sechzehntel-Pendel).
* Lücken zwischen den Saiten: Greifhand dämpft, das Plektrum streicht tot darüber. Große Lücken (z. B. E + e): Plektrum + Finger.
* Note-Off: Finger hebt ab bzw. Hand dämpft die offene Saite – kurze Noten sind Staccato-Strums.
* Velocity: stärkste Note = Kraft und Tempo, einzelne Noten betonen ihre Saite.
* Legato bei Einzeltönen: Hammer-on, Pull-off, Slide automatisch.

| Taste (Kanal 2) | Bedeutung |
|---|---|
| C1 / C#1 | diesen Anschlag ab / auf erzwingen |
| D1 / D#1 | ab jetzt nur Abschläge / nur Aufschläge |
| E1 | wieder automatisch (Pendel) |

**Controller (beide Kanäle):** CC1 Palm-Druck, CC2 Handposition, CC11 Intensität, CC64 aus = Abdämpfen, Pitch Bend = Bend.
Alle Controller sind **Tendenzen**: die Hand folgt mit ~70 ms Trägheit, Überschwingen, Drift und kleinen Fehlern.

## Akkord-Slots und Griffe

Zwölf Slots in der Oberfläche. Jeder Slot hat einen Akkord (Grundton + Typ) und entweder **automatisch** (der Spieler nimmt
den Griff, der zur Lage der Hand passt) oder einen **festen Griff**: mit < > durch alle Griffe blättern, die der Gitarrist für
diesen Akkord kennt (offen, E-/A-/D-Form am Hals entlang, Griffe auf den oberen Saiten; 336 Akkorde, über 2400 Griffe),
oder direkt auf dem Griffbrett klicken (eigener Griff, auch Cluster und Doppelgriffe). „Anhören" spielt den Slot.
Die Slots werden mit dem Projekt gespeichert.

## Plektrum

Das Plektrum ist kein Schalter, sondern Mechanik: eine Blattfeder (Steifigkeit ∝ Dicke³, E-Modul und Dämpfung des
Materials, freie Länge über den Fingern, nachgiebiger Griff) mit Spitzenmasse, die mit Hand-Geschwindigkeit auf die Saite
trifft. Die Saite hat Spannung und Wellenwiderstand aus Stimmung, Mensur und Saitenstärke (10er-Satz); im Kontakt drückt
die Spitze sie Sample für Sample weg, die Kante kippt mit der Durchbiegung, die Saite rutscht über die Fläche in die
Tiefe ab (regt dabei die zweite Schwingungsebene an) und löst sich, wenn die Eintauchtiefe überwunden ist – mit
Auslenkung **und** Geschwindigkeit. Danach schwingt die Spitze frei aus und kann die Saite noch einmal streifen.
Umsponnene Saiten lassen die Kante über die Windungen reiten. Parameter: Dicke (0,4–2 mm) und Material
(Celluloid, Nylon, Tortex, Ultex).

## Modelle

Die vier Typen unterscheiden sich in dem, was den Klang der Vorbilder physikalisch macht: Mensur (Spannung), Spulen
(Induktivität, Widerstand, Kapazität → Lage und Güte der Resonanz), **Wirbelstromverluste** durch Kappen, Grundplatten und
Stahlkerne (breiterer, tieferer Gipfel, weicherer Höhenabfall – das Cremige der Humbucker), Ausgangspegel (treibt den Amp
anders), Potis und Tonkondensator (Vintage-S: Steg ohne Tonregler → hohe Güte, das Glockige bei 4 kHz), Korpusmoden,
Federn, Halsmaterial (Höhen-Abklingzeit).

Der Tonabnehmer selbst ist Geometrie: Fluss aus dem Abstand Saite–Pol (senkrecht **und** seitlich, über ein Abtastfenster
entlang der Saite gemittelt, Polradius je Bauart), Spannung als Flussänderung. Gestaffelte Pole und Griffbrettradius ergeben
je Saite einen eigenen Abstand (Pegel, Zug, Anschlag am Pol); die Saite sitzt nie exakt mittig über dem Pol. Humbucker haben
zwei ungleich gewickelte Spulen (Brummrest, Rest Einzelspulen-Charakter). Wirbelströme als schwach gekoppelte Sekundärwicklung
(frequenzabhängig) plus Grundplattenanteil. Kabellänge als Parameter (110 pF/m). Unvergossene Spulen nehmen Korpusschall
auf und pfeifen bei hohem Gain und viel Feedback.

## Hände und Griffbrett

* **Palm Mute** ist ein Kontakt, kein Dämpfungsfaktor: die Handkante liegt als progressive, stark gedämpfte Hautfeder mit
  Vorspannung auf der Saite (drei Punkte, seitlich Reibung). Druck (CC1) und Position (CC2) bestimmen Abklingen,
  Tonhöhenanstieg und Nichtlinearität bei hartem Anschlag von selbst.
* **Gegriffene Töne** enden nicht am harten Sattel, sondern an Bund + Fingerkuppe: ein Teil der Welle wird dort
  geschluckt (Höhen zuerst), je nach Festigkeit des Griffs.
* **Pull-off** ist ein Anschlag mit der Fingerkuppe (weich, schwer, griffig, seitlich) über dieselbe Kontaktmechanik wie
  das Plektrum. **Slides** entstehen automatisch bei Legato über drei und mehr Bünde.
* **Strum-Geometrie:** Die Zeit zwischen den Saiten folgt aus Saitenabstand und Handgeschwindigkeit; die Hand
  beschleunigt durch den Schlag, der Plektrumwinkel dreht sich mit, die Bogenbahn taucht in der Mitte tiefer ein.

## Cubase

* **Expression Map:** Das Plugin meldet per `IKeyswitchController` die 12 Slots und 9 Trigger auf Kanal 1 und die 5
  Richtungstasten auf Kanal 2. In Cubase: Expression-Map-Setup → „Key Switches importieren" (Import Key Switches).
* **Vorlauf:** Der Knopf „Vorlauf" (0–300 ms) wird als Plugin-Latenz gemeldet; Cubase gleicht sie aus. Damit können
  Finger *vor* dem Anschlag landen (Umgreifen dauert real 50–120 ms).
* **Take:** Der Take ist ein Parameter und wird mit dem Projekt gespeichert – ein Export klingt exakt wie das Abhören.

## Bauen

GitHub Actions baut bei jedem Push die VST3 für Windows (x64) und macOS (Universal). JUCE 8.0.15 wird per CMake
FetchContent geholt. Lokal:

```
cmake -B build -A x64            # Windows
cmake --build build --config Release --target PMGElectric_VST3
```

`tools/render_test.cpp` rendert eine MIDI-Datei ohne JUCE zu WAV (`pmg_render in.mid out.wav [take] [gain]`).

## Lizenz

AGPL-3.0, siehe LICENSE.

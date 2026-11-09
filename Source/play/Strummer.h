// PMG Electric – MIDI → Absicht → Hände. Port von dsp.js MidiStrummer / AccentGrid / makeClock.
#pragma once
#include "Hand.h"
#include "Voicings.h"
#include <map>
#include <string>
#include <cstring>

namespace rig {

inline const int OPEN_PC[6] = { 4, 9, 2, 7, 11, 4 }, MIDI_OPEN[6] = { 40, 45, 50, 55, 59, 64 };
inline const char* ROOT_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "B", "H" };
inline const int SLOT_BASE = 12;    // Kanal 1, C0–B0: Akkord-Slots 1–12

enum class Trigger { None, Down, Up, Choke, Ghost, Bass, PartDown, PartUp, Arp, ArpUp };
inline const int TRIGGER_BASE = 24; // C1 … G#1
inline const char* TRIGGER_NAMES[9] = { "Down", "Up", "Choke", "Ghost", "Bass", "Part Down", "Part Up", "Arp Down", "Arp Up" };
inline Trigger triggerOf(int note) { const int i = note - TRIGGER_BASE; return (i >= 0 && i < 9) ? (Trigger) (i + 1) : Trigger::None; }

// ---------- Akzent-Raster ----------
struct AccentPreset { const char* name; int num, den; std::vector<double> acc, tim; };
inline const std::vector<AccentPreset>& accentPresets()
{
    static const std::vector<AccentPreset> P = {
        { "Rock 4/4", 4, 4, { 1, -0.5, 0.2, -0.5, 0.5, -0.5, 0.2, -0.4, 0.8, -0.5, 0.2, -0.5, 0.5, -0.5, 0.3, -0.3 }, std::vector<double>(16, 0) },
        { "Pop 8tel", 4, 4, { 1, -0.3, -0.2, -0.3, 0.4, -0.3, 0.1, -0.3, 0.7, -0.3, -0.2, -0.3, 0.4, -0.3, 0.2, -0.1 }, { 0, 0, .05, 0, 0, 0, .05, 0, 0, 0, .05, 0, 0, 0, .05, 0 } },
        { "Backbeat", 4, 4, { 0.3, -0.5, 0, -0.5, 1, -0.4, 0, -0.5, 0.3, -0.5, 0, -0.5, 1, -0.4, 0.1, -0.3 }, std::vector<double>(16, 0) },
        { "Reggae", 4, 4, { -0.8, -1, 1, -0.6, -0.8, -1, 1, -0.6, -0.8, -1, 1, -0.6, -0.8, -1, 1, -0.5 }, { 0, 0, .08, 0, 0, 0, .08, 0, 0, 0, .08, 0, 0, 0, .08, 0 } },
        { "Shuffle", 4, 4, { 1, -1, -0.3, 0.1, 0.5, -1, -0.3, 0.1, 0.8, -1, -0.3, 0.1, 0.5, -1, -0.3, 0.2 }, { 0, 0, 0, .33, 0, 0, 0, .33, 0, 0, 0, .33, 0, 0, 0, .33 } },
        { "Walzer 3/4", 3, 4, { 1, -0.5, -0.2, -0.5, 0.1, -0.5, -0.1, -0.5, 0.2, -0.5, 0, -0.4 }, std::vector<double>(12, 0) },
        { "6/8", 6, 8, { 1, -0.5, -0.2, -0.3, -0.5, -0.1, 0.7, -0.5, -0.2, -0.3, -0.5, 0 }, std::vector<double>(12, 0) },
        { "Gleichmaessig", 4, 4, std::vector<double>(16, 0), std::vector<double>(16, 0) } };
    return P;
}
class AccentGrid {
public:
    int num = 4, den = 4; std::vector<double> acc, tim; double strength = 0.6, swing = 0;
    AccentGrid() { set(accentPresets()[0]); }
    void set(const AccentPreset& g) { num = g.num; den = g.den; acc = g.acc; tim = g.tim; }
    int steps() const { return (int) std::lround(num * 16.0 / den); }
    struct Out { double vel, dt; };
    Out at(double step16, double bpm) const
    {
        const int n = steps(); int i = (int) std::lround(step16) % n; if (i < 0) i += n;
        if ((int) acc.size() < n) return { 1, 0 };
        const double off = step16 - std::round(step16);
        const double w = std::max(0.0, 1 - std::abs(off) * 1.6) * strength;
        const double s16 = 60 / bpm / 4;
        double sw = 0; if (swing > 0 && (i % 2) == 1) sw = swing * 0.33 * s16;
        return { 1 + 0.35 * acc[(size_t) i] * w, (tim[(size_t) i] * s16) * w + sw * std::max(0.3, w) };
    }
    struct Hit { double step16, vel; };
    bool learn(const std::vector<Hit>& hits)
    {
        const int n = steps(); std::vector<double> sum((size_t) n, 0), cnt((size_t) n, 0);
        for (auto& h : hits) { int i = (int) std::lround(h.step16) % n; if (i < 0) i += n; sum[(size_t) i] += h.vel; cnt[(size_t) i]++; }
        double avg = 0, mx = -1e9, mn = 1e9; int used = 0;
        for (int i = 0; i < n; i++) if (cnt[(size_t) i] > 0) { const double m = sum[(size_t) i] / cnt[(size_t) i]; avg += m; mx = std::max(mx, m); mn = std::min(mn, m); used++; }
        if (used < 2) return false;
        avg /= used; const double spread = std::max(8.0, mx - mn);
        acc.assign((size_t) n, 0);
        for (int i = 0; i < n; i++) acc[(size_t) i] = cnt[(size_t) i] > 0 ? std::max(-1.0, std::min(1.0, (sum[(size_t) i] / cnt[(size_t) i] - avg) / spread * 2)) : -0.6;
        return true;
    }
};

// ---------- Takt-Uhr aus Tempo-Map (für Offline-Rendern aus MIDI-Dateien) ----------
struct TempoPt { double t, bpm; };
struct SigPt { double t; int num, den; };
inline Clock makeClock(std::vector<TempoPt> tempos, std::vector<SigPt> sigs)
{
    if (tempos.empty()) tempos.push_back({ 0, 120 });
    if (sigs.empty()) sigs.push_back({ 0, 4, 4 });
    auto quarters = [tempos](double t) { double q = 0, lt = 0, bpm = tempos[0].bpm; for (auto& tp : tempos) { if (tp.t > t) break; q += (tp.t - lt) * bpm / 60; lt = tp.t; bpm = tp.bpm; } return std::pair<double, double>{ q + (t - lt) * bpm / 60, bpm }; };
    return [=](double t) -> std::optional<ClockPos> {
        auto [q, bpm] = quarters(t);
        SigPt sig = sigs[0]; double qLast = 0; int barAcc = 0;
        for (size_t i = 0; i < sigs.size(); i++) {
            const double qSig = sigs[i].t == 0 ? 0 : quarters(sigs[i].t).first;
            if (qSig > q) break;
            if (i > 0) { const SigPt& prev = sigs[i - 1]; const double barLen = prev.num * 4.0 / prev.den; barAcc += (int) std::floor((qSig - qLast) / barLen); }
            qLast = qSig; sig = sigs[i];
        }
        const double barLen = sig.num * 4.0 / sig.den, rel = q - qLast; const int barsIn = (int) std::floor(rel / barLen);
        const double inBar = rel - barsIn * barLen;
        ClockPos c; c.bar = barAcc + barsIn; c.beat = inBar / (4.0 / sig.den); c.num = sig.num; c.den = sig.den; c.step16 = inBar * 4; c.bpm = bpm; c.barLen = barLen;
        return c;
    };
}

// ---------- Strummer ----------
/** Akkord-Slot: Akkordname (Griff je nach Lage) oder eigener/fester Griff */
struct Slot { std::string name; Shape fr{}; bool hasFr = false, fixed = false; bool used() const { return hasFr || !name.empty(); } };

class MidiStrummer {
public:
    explicit MidiStrummer(Hand& h) : H(h) {}
    Hand& H;
    std::map<int, int> held;              // Kanal 1: Note → Velocity
    struct Chord { std::string name, src; Shape shape; }; std::optional<Chord> chord;
    int handPos = 0;
    double pm = 0, palmQ = 0.3, intensity = 1; bool sustain = true;
    std::map<int, int> melody;            // Kanal 2: Note → Saite
    std::string lastInfo;
    std::function<void(const Chord&)> onChord;
    Clock clock; const AccentGrid* grid = nullptr;
    struct Pend { Trigger kind; int vel; double t; }; std::vector<Pend> pending;
    int batchVel = 0; bool dirty = false;
    Slot slots[12]; int slotActive = -1, slotPending = -1;
    struct N2 { int note, vel; double t; }; std::vector<N2> p2;     // Kanal 2: Noten des aktuellen Zeitpakets
    int dirOnce = 0, dirLatch = 0, lastDir2 = 0; double lastT2 = -10; std::map<int, int> barSub;

    struct Vm { double vel, speed; };
    Vm velMap(int v) const { const double x = std::pow(v / 127.0, 0.8); return { (0.35 + 0.85 * x) * intensity, 30 - 20 * x }; }

    // ---------- Akkorde erkennen und Griff wählen ----------
    std::optional<Chord> recognize()
    {
        if (held.empty()) return std::nullopt;
        std::vector<int> notes; for (auto& kv : held) notes.push_back(kv.first);
        bool pcs[12] = {}; int npc = 0; for (int n : notes) { if (!pcs[n % 12]) { pcs[n % 12] = true; npc++; } }
        const int bassPc = notes[0] % 12; double avg = 0; for (int n : notes) avg += n; avg /= notes.size();
        std::string bestName; double bestScore = -1e9; bool found = false;
        for (int r = 0; r < 12; r++) for (auto& Q : qualities()) {
            bool cp[12] = {}; for (int iv : Q.iv) cp[(r + iv) % 12] = true;
            int inChord = 0; for (int p = 0; p < 12; p++) if (pcs[p] && cp[p]) inChord++;
            if (inChord < npc) continue;
            double missing = 0; for (int iv : Q.iv) if (!pcs[(r + iv) % 12]) missing += std::find(Q.opt.begin(), Q.opt.end(), iv) != Q.opt.end() ? 0.3 : 1;
            double score = (missing == 0 ? 100 : 60 - 10 * missing) + (r == bassPc ? 15 : 0) - (double) Q.iv.size() + ((std::strcmp(Q.q, "") == 0 || std::strcmp(Q.q, "m") == 0) ? 3 : 0);
            if (npc == 1) score = (std::strcmp(Q.q, "5") == 0 ? 50 : 0) + (r == bassPc ? 20 : 0);
            if (npc == 2 && std::strcmp(Q.q, "5") == 0 && missing == 0) score += 30;
            const std::string name = std::string(ROOT_NAMES[r]) + Q.q;
            if (!hasVoicing(name)) continue;
            if (!found || score > bestScore) { bestName = name; bestScore = score; found = true; }
        }
        if (!found) return std::nullopt;
        return pickVoicing(bestName, bassPc, avg);
    }
    static bool hasVoicing(const std::string& name) { for (auto& v : voicings()) if (name == v.name) return true; return false; }
    /** Griff für einen Akkord: Bass, Register, Lage der Hand, Grundbibliothek bevorzugt. bassPc < 0 / avg <= 0: egal */
    std::optional<Chord> pickVoicing(const std::string& name, int bassPc = -1, double avg = 0) const
    {
        const Voicing* bv = nullptr; double bsc = -1e9;
        for (auto& v : voicings()) {
            if (name != v.name) continue;
            int lowest = 0; while (lowest < 6 && v.fr[lowest] < 0) lowest++; if (lowest >= 6) continue;
            const int lowPc = (OPEN_PC[lowest] + v.fr[lowest]) % 12;
            const int nextPc = lowest < 5 && v.fr[lowest + 1] >= 0 ? (OPEN_PC[lowest + 1] + v.fr[lowest + 1]) % 12 : -1;
            const double bassOk = bassPc < 0 ? 1 : lowPc == bassPc ? 1 : (nextPc == bassPc && (lowPc - bassPc + 12) % 12 == 7) ? 0.7 : 0;
            double vavg = 0; int np = 0, pos = 0; for (int s = 0; s < 6; s++) if (v.fr[s] >= 0) { vavg += MIDI_OPEN[s] + v.fr[s]; np++; if (v.fr[s] > 0) pos = pos == 0 ? v.fr[s] : std::min(pos, v.fr[s]); }
            vavg /= std::max(1, np);
            const std::string src = v.src;
            const double sc = 40 * bassOk - (avg > 0 ? std::abs(vavg - avg) * 2.0 : 0) - std::abs(pos - handPos) * 1.2 - (src.find("+12") != std::string::npos ? 6 : 0)
                + (src == "offen" ? (handPos <= 4 ? 10 : 4) : 0) + (v.lib ? 6 : 0) + 1.5 * np;
            if (!bv || sc > bsc) { bv = &v; bsc = sc; }
        }
        if (!bv) return std::nullopt;
        Chord c; c.name = name; c.src = bv->src; for (int s = 0; s < 6; s++) c.shape[s] = bv->fr[s];
        return c;
    }
    Events applyChord(const Chord& c, double t, std::optional<double> tStrum)
    {
        Events ev;
        if (!chord || c.name != chord->name || !(c.shape == chord->shape)) {
            chord = c;
            handPos = 0; for (int s = 0; s < 6; s++) if (c.shape[s] > 0) handPos = handPos == 0 ? c.shape[s] : std::min(handPos, c.shape[s]);
            Hand::Grip g = H.changeGrip(c.shape, tStrum ? *tStrum : t + 0.06);
            ev = std::move(g.ev);
            lastInfo = c.name + " · " + c.src + " ·"; for (int s = 0; s < 6; s++) lastInfo += c.shape[s] < 0 ? " x" : " " + std::to_string(c.shape[s]);
            if (onChord) onChord(*chord);
        }
        return ev;
    }
    Events selectSlot(int i, double t, std::optional<double> tStrum)
    {
        if (i < 0 || i >= 12 || !slots[i].used()) return {};
        const Slot& sl = slots[i]; std::optional<Chord> c;
        if (sl.hasFr && (sl.fixed || sl.name.empty())) { Chord x; x.name = sl.name.empty() ? "Slot " + std::to_string(i + 1) : sl.name; x.src = "Slot " + std::to_string(i + 1); x.shape = sl.fr; c = x; }
        else if (!sl.name.empty()) c = pickVoicing(sl.name);
        if (!c) return {};
        slotActive = i; held.clear(); dirty = false;
        return applyChord(*c, t, tStrum);
    }

    // ---------- Kanal 2: wie notiert ----------
    /** Richtung: erzwungen (C1/C#1 einmalig, D1/D#1 einrastend) oder das Pendel der Schlaghand im Takt */
    int strumDir(double t)
    {
        const int forced = dirOnce ? dirOnce : dirLatch; dirOnce = 0;
        int dir = 1;
        std::optional<ClockPos> c; if (clock) c = clock(t);
        if (c) {
            const double st = c->step16; const long r = std::lround(st);
            if (std::abs(st - r) < 0.2 && (r & 1)) barSub[c->bar] = 1;
            int sub = 2; auto it = barSub.find(c->bar); if (it != barSub.end()) sub = it->second; else { auto ip = barSub.find(c->bar - 1); if (ip != barSub.end()) sub = ip->second; }
            if (sub == 1) barSub[c->bar] = 1;
            if (barSub.size() > 64) barSub.erase(barSub.begin());
            dir = (std::lround(st / sub) % 2) == 0 ? 1 : -1;
        } else if (lastDir2 && t - lastT2 < 0.35) dir = -lastDir2;
        if (forced) dir = forced;
        lastDir2 = dir; lastT2 = t;
        return dir;
    }
    struct Assign { double cost; Shape fr; int maxRun, first, last; };
    std::optional<Assign> assignStrings(std::vector<int> notes) const
    {
        const int n = std::min(6, (int) notes.size()); notes.resize((size_t) n);
        std::optional<Assign> best; int pick[6] = {};
        std::function<void(int, int)> rec = [&](int i, int sMin) {
            if (i == n) {
                Shape fr; fr.f.fill(-1); for (int k = 0; k < n; k++) fr[pick[k]] = notes[(size_t) k] - MIDI_OPEN[pick[k]];
                int mn = 99, mx = 0, opens = 0; for (int s = 0; s < 6; s++) { if (fr[s] > 0) { mn = std::min(mn, fr[s]); mx = std::max(mx, fr[s]); } if (fr[s] == 0) opens++; }
                if (mn == 99) { mn = 0; mx = 0; }
                const int span = mx - mn; if (span > 6) return;
                int gaps = 0, run = 0, maxRun = 0; for (int s = pick[0]; s <= pick[n - 1]; s++) if (fr[s] < 0) { gaps++; run++; maxRun = std::max(maxRun, run); } else run = 0;
                double cost = 4 * std::max(0, span - 3) * 3 + 1.5 * span + 3 * gaps + (maxRun >= 2 ? 3 : 0) + 1.8 * std::abs((mn ? mn : handPos) - handPos)
                    - (mn <= 4 ? 1.5 * opens : 0) + 0.2 * mn;
                for (int s = 0; s < 6; s++) if (fr[s] > 0 && H.frets[s] == fr[s]) cost -= 1.5;
                if (!best || cost < best->cost) best = Assign{ cost, fr, maxRun, pick[0], pick[n - 1] };
                return;
            }
            for (int s = sMin; s <= 6 - (n - i); s++) { const int f = notes[(size_t) i] - MIDI_OPEN[s]; if (f < 0 || f > 20) continue; pick[i] = s; rec(i + 1, s + 1); }
        };
        rec(0, 0);
        if (!best && n > 1) { std::vector<int> rest(notes.begin() + 1, notes.end()); return assignStrings(rest); }
        return best;
    }
    Events playAsWritten(std::vector<N2> group, double t)
    {
        std::sort(group.begin(), group.end(), [](const N2& a, const N2& b) { return a.note < b.note; });
        std::vector<N2> u; for (auto& g : group) if (u.empty() || u.back().note != g.note) u.push_back(g);
        int vmax = 0; for (auto& g : u) vmax = std::max(vmax, g.vel);
        Vm m = velMap(vmax);
        if (clock && grid) { if (auto c = clock(t)) { auto a = grid->at(c->step16, c->bpm); m.vel *= a.vel; m.speed /= std::sqrt(a.vel); t += a.dt; } }
        if (u.size() == 1) {
            const N2& g = u[0]; const int forced = dirOnce ? dirOnce : dirLatch; dirOnce = 0;
            Shape sh = chord ? chord->shape : Shape{};
            int best = -1, bf = 0; double bsc = -1e9;
            for (int s = 0; s < 6; s++) {
                const int f = g.note - MIDI_OPEN[s]; if (f < 0 || f > 20) continue;
                double sc = 0;
                if (sh[s] >= 0 && sh[s] == f) sc += 50;
                if (H.frets[s] == f && f > 0) sc += 10;
                sc -= std::abs(f - std::max(handPos, 1)) * 2;
                for (auto& kv : melody) if (kv.second == s) { sc -= 30; break; }
                if (f == 0) sc += 3;
                if (sc > bsc) { bsc = sc; best = s; bf = f; }
            }
            if (best < 0) return {};
            melody[g.note] = best;
            if (bf > 0 && (bf < handPos || bf > handPos + 3)) handPos = bf < handPos ? bf : bf - 3;
            return H.note(best, bf, t, velMap(g.vel).vel * (m.vel / velMap(vmax).vel), Hand::Auto, false, forced);
        }
        std::vector<int> notes; for (auto& g : u) notes.push_back(g.note);
        auto a = assignStrings(notes); if (!a) return {};
        Shape shape = a->fr; for (int s = a->first; s <= a->last; s++) if (shape[s] < 0) shape[s] = -2;
        double velS[6] = {};
        for (auto& g : u) for (int s = 0; s < 6; s++) if (a->fr[s] >= 0 && MIDI_OPEN[s] + a->fr[s] == g.note) { velS[s] = std::pow((double) g.vel / vmax, 0.6); melody[g.note] = s; }
        int mn = 0; for (int s = 0; s < 6; s++) if (a->fr[s] > 0) mn = mn == 0 ? a->fr[s] : std::min(mn, a->fr[s]); if (mn) handPos = mn;
        Chord c; c.name = "wie notiert"; c.src = "Kanal 2"; c.shape = a->fr; chord = c;
        lastInfo = "Kanal 2 ·"; for (int s = 0; s < 6; s++) lastInfo += a->fr[s] < 0 ? " x" : " " + std::to_string(a->fr[s]);
        if (onChord) onChord(*chord);
        if (a->maxRun >= 2) { dirOnce = 0; return H.hybrid(a->fr, t, m.vel, velS); }
        const int dir = strumDir(t);
        Hand::StrumOpt o; o.vel = dir > 0 ? m.vel : m.vel * 0.9; o.speed = dir > 0 ? m.speed : m.speed * 0.85; for (int s = 0; s < 6; s++) o.velS[s] = velS[s];
        return H.strum(shape, t, dir, o);
    }

    // ---------- Paket abschließen ----------
    Events updateChord(double t, std::optional<double> tStrum) { auto c = recognize(); return c ? applyChord(*c, t, tStrum) : Events{}; }
    /** nach allen Ereignissen eines Zeitpunkts (Paket) aufrufen */
    Events flush(double t)
    {
        Events ev;
        const std::optional<double> tStrum = pending.empty() ? std::nullopt : std::optional<double>(pending[0].t);
        if (slotPending >= 0) { Events r = selectSlot(slotPending, t, tStrum); ev.insert(ev.end(), r.begin(), r.end()); slotPending = -1; }
        if (dirty) { dirty = false; slotActive = -1; Events r = updateChord(t, tStrum); ev.insert(ev.end(), r.begin(), r.end()); }
        if (!pending.empty()) {
            auto pend = std::move(pending); pending.clear();
            for (auto& p : pend) { const int vel = batchVel > 0 ? std::max(p.vel, batchVel) : p.vel; Events r = runTrigger(p.kind, vel, p.t); ev.insert(ev.end(), r.begin(), r.end()); }
        }
        batchVel = 0;
        if (!p2.empty()) { auto g = std::move(p2); p2.clear(); Events r = playAsWritten(g, t); ev.insert(ev.end(), r.begin(), r.end()); }
        return ev;
    }
    Events runTrigger(Trigger kind, int vel, double t)
    {
        Vm m = velMap(vel);
        if (!chord && kind != Trigger::Choke) return {};
        Shape sh = chord ? chord->shape : Shape{};
        if (clock && grid) { if (auto c = clock(t)) { auto a = grid->at(c->step16, c->bpm); m.vel *= a.vel; m.speed /= std::sqrt(a.vel); t += a.dt; } }
        Hand::StrumOpt o; o.vel = m.vel; o.speed = m.speed;
        switch (kind) {
        case Trigger::Down: return H.strum(sh, t, 1, o);
        case Trigger::Up: o.vel = m.vel * 0.9; o.speed = m.speed * 0.85; return H.strum(sh, t, -1, o);
        case Trigger::Choke: return H.damp(-1, t);
        case Trigger::Ghost: o.vel = m.vel * 0.75; o.speed = std::min(m.speed, 7.0); o.pm = true; return H.strum(sh, t, vel > 100 ? 1 : -1, o);
        case Trigger::Bass: { int b = 0; while (b < 5 && sh[b] < 0) b++; return H.note(b, sh[b], t, m.vel, Hand::Plain); }
        case Trigger::PartDown: { Shape p = sh; p[0] = p[1] = -1; o.vel = m.vel * 0.92; o.speed = m.speed * 0.8; o.keepBass = true; return H.strum(p, t, 1, o); }
        case Trigger::PartUp: { Shape p = sh; p[0] = p[1] = -1; o.vel = m.vel * 0.85; o.speed = m.speed * 0.8; o.keepBass = true; return H.strum(p, t, -1, o); }
        case Trigger::Arp: o.speed = 45 + 40 * (1 - vel / 127.0); return H.strum(sh, t, 1, o);
        case Trigger::ArpUp: o.vel = m.vel * 0.9; o.speed = 45 + 40 * (1 - vel / 127.0); return H.strum(sh, t, -1, o);
        default: return {};
        }
    }
    Events noteOn(int ch, int note, int vel, double t)
    {
        if (ch == 1 && triggerOf(note) != Trigger::None) { pending.push_back({ triggerOf(note), vel, t }); return {}; }
        if (ch == 1 && note >= SLOT_BASE && note < SLOT_BASE + 12) { slotPending = note - SLOT_BASE; return {}; }
        if (ch == 1) { held[note] = vel; dirty = true; batchVel = std::max(batchVel, vel); return {}; }
        if (ch == 2) {
            if (note == 24) { dirOnce = 1; return {}; }
            if (note == 25) { dirOnce = -1; return {}; }
            if (note == 26) { dirLatch = 1; return {}; }
            if (note == 27) { dirLatch = -1; return {}; }
            if (note == 28) { dirLatch = 0; return {}; }
            if (note < 40) return {};
            p2.push_back({ note, vel, t }); return {};
        }
        return {};
    }
    Events noteOff(int ch, int note, double t)
    {
        if (ch == 1 && (triggerOf(note) != Trigger::None || (note >= SLOT_BASE && note < SLOT_BASE + 12))) return {};
        if (ch == 1) { held.erase(note); return {}; }
        if (ch == 2) {
            if (note < 40) return {};
            auto it = melody.find(note); if (it == melody.end()) return {};
            const int s = it->second; melody.erase(it);
            if (chord && chord->src == "Kanal 2") {
                if (chord->shape[s] >= 0) chord->shape[s] = -1;
                return H.frets[s] > 0 ? H.release(s, false, t) : H.damp(s, t);
            }
            Events ev;
            if (chord && chord->shape[s] >= 0 && H.frets[s] != chord->shape[s]) {
                ev = H.release(s, chord->shape[s] == 0, t);
                HEvent e; e.t = Event::Finger; e.s = s; e.phase = Event::Press; e.fret = chord->shape[s]; e.when = t + 0.05; ev.push_back(e);
                H.frets[s] = chord->shape[s]; H.lastFret[(size_t) s] = -1;
            } else ev = H.release(s, false, t);
            return ev;
        }
        return {};
    }
    Events cc(int num, int val, double t)
    {
        if (num == 1) { pm = val / 127.0; return H.setIntent(Hand::Palm, pm, t); }
        if (num == 2) { palmQ = val / 127.0; return H.setIntent(Hand::PalmQ, palmQ, t); }
        if (num == 11) { intensity = 1; return H.setIntent(Hand::Intensity, 0.5 + 0.7 * val / 127.0, t); }
        if (num == 64) { const bool on = val >= 64; if (!on && sustain) { sustain = false; return H.damp(-1, t); } sustain = on; }
        return {};
    }
    Events pitchBend(int v14, double t) { HEvent e; e.t = Event::Bend; e.s = -1; e.c = (v14 - 8192) / 8192.0 * 200; e.ms = 15; e.when = t; return { e }; }
    void reset() { held.clear(); melody.clear(); pending.clear(); p2.clear(); batchVel = 0; dirty = false; slotPending = -1; dirOnce = 0; }
};

} // namespace rig

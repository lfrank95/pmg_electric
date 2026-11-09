// PMG Electric – die gesamte Engine: sechs Saiten → Steg/Korpus → Tonabnehmer-Schaltung → 4×12AX7 → Tonestack →
// Endstufe (Wheelhouse) → 10"-Lautsprecher → Membran/Mikro/Raum, mit Feedback zurück auf die Saiten. Port von dsp.js Engine.
#pragma once
#include "Tubes.h"
#include "Amp.h"
#include "String.h"
#include "Cab.h"
#include <string>
#include <deque>
#include <functional>

namespace rig {

inline const double TUNING[6] = { 82.407, 110.0, 146.832, 195.998, 246.942, 329.628 };

/** Ereignis für die Engine (Zeit in Samples der Engine) */
struct Event {
    enum Type { Pluck, Damp, Release, Finger, Grip, Palm, NewStrings, Bend, Vib } t = Pluck;
    long at = 0;             // Sample-Zeitpunkt (Engine-Frame)
    int s = -1;              // Saite, -1 = alle
    int fret = -1;           // -1 = aktueller Bund ("Finger in der Luft")
    double vel = 0.8;
    int dir = 1;
    double dpos = 0, dhard = 0, scr = 1;
    enum Tech { None, Hammer, Pull, Glide, Harm } tech = None;
    double ms = 90, frac = 0.5;
    bool pm = false;         // Ghost: Hand voll aufgelegt
    bool stop = false;       // Hand bremst nach dieser Saite ab
    double vp = 0, dig = 1, ang = 0;   // Strum-Geometrie: Handgeschwindigkeit (m/s), Eintauchfaktor, Winkelversatz
    bool dead = false;       // Greifhand dämpft, Plektrum streicht tot darüber
    bool hasFing = false; GString::Finger fing{};   // Anschlag mit der Fingerkuppe (Hybrid Picking)
    bool keep = false;       // release: offen weiterklingen
    enum Phase { Lift, Press } phase = Press;
    int touch = -1;          // finger: Nachbarsaite gestreift
    double damp = 0, buzz = 0;   // grip
    double v = 0, q = -1;        // palm: Druck, Position
    double c = 0;                // bend: Cent
    double d = 0, r = 0;         // vib
};

struct Params {
    std::string guitar = "S"; int pickup = 4; double vol = 1, tone = 1, pickPos = 110, hard = 0.8;
    double gain = 0.75, bass = 0.5, mid = 0.5, treble = 0.6, master = 0.45, sag = 0.5, out = 0.5; bool closed = false, noise = true;
    double palm = 0, palmPos = 0.3, action = 0.5, micPos = 0.3, micDist = 0.25, room = 0.25, human = 0.6, age = 0.15, pickHeight = 0.35, feedback = 0.0;
    double vibDepth = 0, vibRate = 5.5, tune = 0, spread = 0; uint32_t serial = 4711; double pickMM = 0.88; int pickMat = 0; double cableM = 4.5;
};

class Engine {
public:
    Engine() = default;
    Engine(double fs_, int os_) { init(fs_, os_); }
    void init(double fs_, int os_)
    {
        fs = fs_;
        for (int i = 0; i < 6; i++) strings[i].init(fs, TUNING[i], i);
        queue.clear(); frame = 0; seed = 12345; humPh = 0;
        build(os_ > 0 ? os_ : (fs >= 88000 ? 1 : 2));
    }
    double rnd() { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0; }
    double gauss() { return std::sqrt(-2 * std::log(rnd() + 1e-12)) * std::cos(2 * PI * rnd()); }

    struct Unit { double Ra[4], Rk[4], Cc[4], Ck, mu[4], hiss[3], micF, micQ, micG, stack[5], flick = 0; bool ready = false; } unit;

    void build(int os_)
    {
        os = os_; h = 1 / (fs * os);
        up.init(os); dn.init(os);
        bufUp.assign((size_t) os, 0.0); bufU.assign((size_t) os, 0.0);
        if (!unit.ready) {
            uint32_t sd = p.serial; auto r = [&]() { sd = sd * 1664525u + 1013904223u; return sd / 4294967296.0 - 0.5; };
            auto tol = [&](double v, double pct) { return v * (1 + pct * 2 * r()); };
            for (int i = 0; i < 4; i++) unit.Ra[i] = tol(100e3, .05);
            unit.Rk[0] = tol(2700, .05); unit.Rk[1] = tol(10e3, .05); unit.Rk[2] = tol(2700, .05); unit.Rk[3] = tol(1500, .05);
            unit.Cc[0] = tol(22e-9, .1) * 1.03; unit.Cc[1] = tol(22e-9, .1) * 1.03; unit.Cc[2] = tol(22e-9, .1) * 1.03; unit.Cc[3] = tol(47e-9, .1) * 1.03; unit.Ck = tol(0.68e-6, .15);
            for (int i = 0; i < 4; i++) unit.mu[i] = 1 + 0.08 * r();
            unit.hiss[0] = 3e-6 + 4e-6 * (r() + 0.5); unit.hiss[1] = 2e-5 + 3e-5 * (r() + 0.5); unit.hiss[2] = 1e-4 * (r() + 0.5);
            unit.micF = 900 + 1800 * (r() + 0.5); unit.micQ = 15 + 25 * (r() + 0.5); unit.micG = 0.4 + 1.2 * (r() + 0.5);
            unit.stack[0] = tol(470e-12, .1); unit.stack[1] = tol(22e-9, .1); unit.stack[2] = tol(22e-9, .1); unit.stack[3] = tol(33e3, .05); unit.stack[4] = tol(220e3, .1);
            unit.ready = true;
        }
        const Unit& U = unit;
        for (int i = 0; i < 4; i++) { tubes[i] = T_12AX7; tubes[i].mu = T_12AX7.mu * U.mu[i]; }
        s1 = TriodeStage(tubes[0], U.Ra[0], U.Rk[0], U.Ck, 300, h);
        c1 = Coupling(U.Cc[0], 1e6, nullptr, h);
        s2 = TriodeStage(tubes[1], U.Ra[1], U.Rk[1], 0, 300, h);
        c2 = Coupling(U.Cc[1], 470e3, &tubes[2], h);
        s3 = TriodeStage(tubes[2], U.Ra[2], U.Rk[2], 0, 300, h);
        c3 = Coupling(U.Cc[2], 470e3, &tubes[3], h);
        s4 = TriodeStage(tubes[3], U.Ra[3], U.Rk[3], 0, 300, h);
        c4 = Coupling(U.Cc[3], 1e6, &tubes[3], h);
        { const double w = 2 * PI * U.micF / fs, rr = std::exp(-w / (2 * U.micQ)); micC = 2 * rr * std::cos(w); micR2 = rr * rr; micB = (1 - rr * rr) * std::sin(w); mic1 = 0; mic2 = 0; }
        stackBuilt = false; buildStack();
        spk.init(h, p.closed);
        amp.init(h, &spk);
        amp.setSag(p.sag);
        delayN = std::max(1, (int) std::lround(0.0019 * fs));
        rearLp.lowpass(fs, 220, 0.7);
        dl.assign((size_t) delayN + 1, 0.0); dlp = 0; uPrev = 0;
        cab.init(fs);
        cab.set(p.micPos, p.micDist, p.room);
        Vbp = 300; VbRest = 415; ripPh = 0;
        sqN = 0; sqLen = 1; sqAmp = 0; sqBp.bandpass(fs, 3000, 4); sqBp2.bandpass(fs, 3000, 4);
        pop = 0;
        applyModel();
        vibPh = 0;
        fbBuf.assign(2048, 0.0); fbP = 0; fbLp = 0;
        setFeedback(p.feedback);
        for (auto& st : strings) st.setAge(p.age);
        limEnv = 0;
        settle(0.7);
        VbRest = amp.Vb;
        calibrate();
        settle(0.4);
        VbRest = amp.Vb;
        // Abwärts-Wandler mit der Ruhelage der Membran füllen (beim Aufwärmen nicht durchlaufen), Abstrahlung darauf setzen – sonst Knack am Anfang
        { std::vector<double> b((size_t) os, spk.x[2]); double u = 0; for (int k = 0; k < 64; k++) u = dn.downsample(b.data()); uPrev = u; }
        warmUp();
        limEnv = 0;
    }
    /** stumm durch den echten Signalweg einschwingen (Wandler, Membran, Kabinett, Raum) – kein Knack beim Start oder nach Umbauten */
    void warmUp(double sec = 0.3)
    {
        const bool nz = p.noise; const long fr = frame; auto q = std::move(queue); queue.clear(); p.noise = false;
        float tmp[480]; for (int k = 0; k < (int) std::lround(sec * fs / 480); k++) process(tmp, 480);
        p.noise = nz; frame = fr; queue = std::move(q); limEnv = 0; outPk = 0;
    }
    void buildStack()
    {
        Circuit c(6, h);
        const double t = p.treble, l = std::pow(p.bass, 2) * 0.999 + 0.001, mm = std::pow(p.mid, 2) * 0.999 + 0.001;
        c.S(0, 1000);
        const double* U = unit.stack;
        c.C(0, 1, U[0]);
        c.R(1, 2, (1 - t) * U[4] + 100);
        c.R(2, 3, t * U[4] + 100);
        c.R(0, 4, U[3]);
        c.C(4, 3, U[1]);
        c.C(4, 5, U[2]);
        c.R(3, 5, l * 1e6);
        c.R(5, -1, mm * 25e3);
        c.R(2, -1, 1e6);
        c.build();
        if (stackBuilt) c.adoptStates(stack);
        stack = c; stackBuilt = true;
    }
    const GuitarModel& modelOf(const std::string& key) const
    {
        for (auto& m : guitarModels()) if (key == m.key) return m;
        return guitarModels()[0];
    }
    void buildPickup()
    {
        const GuitarModel& M = modelOf(p.guitar);
        const int si = std::max(0, std::min((int) M.selector.size() - 1, p.pickup));
        activePickups.clear(); for (int i : M.selector[(size_t) si].coils) activePickups.push_back(M.pickups[(size_t) i]);
        double invL = 0, invR = 0, Cp = 0;
        double invE = 0;
        for (auto& pk : activePickups) { invL += 1 / pk.L; invR += 1 / pk.R; Cp += pk.C; if (pk.eddy > 0) invE += 1 / pk.eddy; }
        const bool noTone = activePickups.size() == 1 && activePickups[0].noTone;
        const double L = 1 / invL, Rc = 1 / invR, pot = M.pots, Ct = M.toneC;
        Circuit c(6, 1 / fs);
        const double tone = 0.02 + 0.98 * std::pow(p.tone, 2), vol = std::pow(p.vol, 2);
        c.S(0, Rc);
        if (invE > 0) {
            // Wirbelströme = schwach gekoppelte, kurzgeschlossene Sekundärwicklung (T-Modell), plus frequenzunabhängiger Anteil
            double kap = 0; int nk = 0; for (auto& pk : activePickups) if (pk.eddy > 0) { kap += pk.kap; nk++; } kap /= nk;
            const double Rq = 1 / invE, Re = 0.5 * Rq * kap * kap, Le = Re / (2 * PI * 3500);
            c.L(0, 5, L * (1 - kap)); c.L(5, 1, L * kap);
            c.R(5, 4, Re); c.L(4, 1, Le);
            c.R(0, 1, 1.5 * Rq);
        } else c.L(0, 1, L);
        c.C(1, -1, Cp);
        c.R(1, 2, noTone ? 1e9 : tone * pot + 1000);
        c.C(2, -1, Ct);
        c.R(1, 3, (1 - vol) * pot + 10);
        c.R(3, -1, vol * pot + 10);
        c.C(3, -1, 20e-12 + 110e-12 * p.cableM);
        c.R(3, -1, 1e6);
        c.build();
        pick = c;
    }
    struct Reso { double b, c, r2, wgt, s1 = 0, s2 = 0; };
    void applyModel()
    {
        const GuitarModel& M = modelOf(p.guitar);
        model = &M;
        body.clear();
        for (auto& m : M.body) { const double w = 2 * PI * m.f / fs, r = std::exp(-w / (2 * m.q)); body.push_back({ (1 - r * r) * std::sin(w), 2 * r * std::cos(w), r * r, m.wgt }); }
        springs.clear();
        if (M.springs) for (double f : { 62.0, 95.0, 128.0 }) { const double w = 2 * PI * f / fs, r = std::exp(-w / (2 * 45)); springs.push_back({ (1 - r * r) * std::sin(w), 2 * r * std::cos(w), r * r, 1 }); }
        hollow = M.hollow;
        for (auto& st : strings) { st.scale = M.scale; st.sustainF = M.sustain; st.brightF = M.bright; st.gauge = M.gauge; st.configure(); }
        buildPickup();
        buildCoils();
    }
    struct Coil { double pos, a, ap, w, hum, norm, micro; double gap[6], u0[6], prev[6]; };
    std::vector<Coil> coils;
    /** Spulen mit Geometrie (siehe dsp.js buildCoils) */
    void buildCoils()
    {
        const GuitarModel& M = *model; const auto& sel = activePickups; coils.clear();
        const double gapBase = 4.6 - 2.6 * p.pickHeight;
        auto pkDrop = [](double mm) { return 0.0085 * std::max(0.0, mm - 40); };   // Richtung Hals tiefer eingestellt
        const double nP = 1.0 / std::max<size_t>(1, sel.size());
        for (auto& pk : sel) {
            const double a = pk.a; const bool hb = pk.hb; const double mis = pk.mis;
            struct Part { double pos, w; int hpol; };
            std::vector<Part> parts = hb ? std::vector<Part>{ { pk.pos - 9, 1 + mis, 1 }, { pk.pos + 9, 1 - mis, -1 } } : std::vector<Part>{ { pk.pos, 1, 1 } };
            for (auto& pt : parts) {
                Coil c{}; c.pos = pt.pos; c.a = a; c.ap = 0.8 * a;
                for (int si = 0; si < 6; si++) {
                    const double bulge = M.radius * std::sin(PI * si / 5.0), stag = pk.stag[si];
                    c.gap[si] = std::max(1.0, gapBase + bulge - stag + pkDrop(pt.pos));
                    c.u0[si] = (0.25 + 0.5 * rnd()) * (rnd() < 0.5 ? -1 : 1);
                    c.prev[si] = 0;
                }
                const double g0 = 3.0; c.norm = 1 / (2 * g0 / (g0 * g0 + a * a) * UNIT * 1000);
                c.w = pt.w * pk.turns * nP * (hb ? 0.5 : 1);
                c.hum = (hb ? 0.03e-3 * (pt.hpol > 0 ? 1 : -1) * pt.w : 0.03e-3) * pk.polar * nP;
                c.micro = M.micro * (pk.cover ? 1 : 0.6);
                coils.push_back(c);
            }
        }
        // Magnetzug aller Pickups des Modells, ob geschaltet oder nicht
        for (int si = 0; si < 6; si++) {
            std::vector<GString::PullPt> list;
            for (auto& pk : M.pickups) {
                const double bulge = M.radius * std::sin(PI * si / 5.0), gap = std::max(1.0, gapBase + bulge - pk.stag[si] + pkDrop(pk.pos));
                const double mag = (pk.hb ? 0.45 : 1.0) * (pk.plate ? 1.1 : 1) * (pk.cover ? 0.9 : 1);
                list.push_back({ pk.pos, gap, mag });
            }
            strings[si].setPulls(list);
        }
    }
    double coilEmf(Coil& c, double fbRaw)
    {
        double e = 0; const double ap = c.ap, a2 = c.a * c.a, mm = UNIT * 1000;
        for (int si = 0; si < 6; si++) {
            GString& s = strings[si]; const double k = s.mm2bK; Loop& V = s.V; Loop& H = s.H;
            const double yV = (0.36 * V.disp(c.pos * k) + 0.24 * (V.disp((c.pos - ap) * k) + V.disp((c.pos + ap) * k)) + 0.08 * (V.disp((c.pos - 2 * ap) * k) + V.disp((c.pos + 2 * ap) * k))) * mm;
            const double yH = 0.45 * (H.disp(c.pos * k) * mm + c.u0[si]);   // Polfeld ist seitlich flacher als nach oben
            const double g = c.gap[si], d = std::max(0.3, g - yV);
            const double r2 = d * d + yH * yH;
            const double phi = ((g * g + c.u0[si] * c.u0[si] + a2) / (r2 + a2) - 1) * c.norm + s.lgG * s.lgSum;
            e += (phi - c.prev[si]) * c.w;
            c.prev[si] = phi;
        }
        if (fbRaw != 0 && c.micro > 0) e += c.micro * fbRaw * 2.5e-4 * c.w;
        return e;
    }
    void settle(double sec)
    {
        const long n = (long) std::floor(sec * fs);
        for (long i = 0; i < n; i++) for (int k = 0; k < os; k++) chain(0);
        amp.updateDrvRout();
    }
    void calibrate()
    {
        const long n = (long) std::floor(fs * 0.08);
        double s = 0;
        for (long i = 0; i < n; i++) {
            for (int k = 0; k < os; k++) { amp.tick(std::sin(2 * PI * 1000 * (i * os + k) / (fs * os))); bufU[(size_t) k] = spk.x[2]; }
            const double y = radiate(dn.downsample(bufU.data()));
            if (i > n / 2) s += y * y;
        }
        outNorm = 0.045 / std::sqrt(s / (n - n / 2));
        cab.ref = outNorm;
        spk.reset(); uPrev = 0; std::fill(dl.begin(), dl.end(), 0.0); cab.reset(); rearLp.reset();
    }
    double chain(double x)
    {
        const double sagV = VbRest - amp.Vb;
        ripPh += 100 * h; if (ripPh >= 1) ripPh -= 1;
        const double rip = p.noise ? (ripPh - 0.5) * (0.04 + 0.02 * std::max(0.0, sagV)) : 0;
        Vbp += h / 0.5 * (300 - 0.45 * sagV - Vbp);   // Siebkette entkoppelt die Vorstufe (sonst Motorboating)
        s4.B = Vbp + rip; s3.B = Vbp + rip * 0.3; s2.B = Vbp + rip * 0.08; s1.B = Vbp + rip * 0.015;
        double xin = x;
        if (p.noise) {
            unit.flick += 0.002 * (gauss() - unit.flick);
            xin += unit.hiss[0] * (gauss() + 6 * unit.flick);
            const double sp = spk.x[2] * fs * 1e-3;
            const double m = micB * sp + micC * mic1 - micR2 * mic2; mic2 = mic1; mic1 = m;
            xin += unit.micG * 4e-6 * m;
        }
        const double v1 = s1.tick(xin);
        const double gTap = (std::pow(10.0, 2.6 * p.gain) - 1) / (std::pow(10.0, 2.6) - 1);
        const double g2 = c1.tick(v1, s1.rout, 0) * gTap + (p.noise ? unit.hiss[1] * gauss() : 0);
        const double v2 = s2.tick(g2);
        const double g3 = c2.tick(v2, s2.rout, s3.Vk);
        const double v3 = s3.tick(g3 * 0.45 + (p.noise ? unit.hiss[2] * gauss() : 0));
        const double g4 = c3.tick(v3, s3.rout, s4.Vk);
        const double v4 = s4.tick(g4 * 0.5);
        const double gcf = c4.tick(v4, s4.rout, 0);
        prePk = std::max(prePk, std::abs(gcf));
        stack.step(gcf, 0);
        const double vs = stack.v[2];
        amp.tick(vs * p.master * p.master * 0.6);
        return spk.x[2];
    }
    double radiate(double u)
    {
        const double acc = (u - uPrev) * fs; uPrev = u;
        double pr = acc;
        if (!spk.closed) { const double d = dl[(size_t) dlp]; dl[(size_t) dlp] = rearLp.run(acc); dlp = (dlp + 1) % delayN; pr = acc - 0.8 * d; }
        return cab.cone(pr, spk.x[0] * spk.P.Bl) * (outNorm > 0 ? outNorm : 1);
    }
    // ---- Parameter setzen (Name wie in der App) ----
    void setGuitar(const std::string& key) { p.guitar = key; p.pickup = std::min(p.pickup, (int) modelOf(key).selector.size() - 1); applyModel(); }
    void setPickup(int i) { p.pickup = i; applyModel(); }
    void setToneStack(double bass, double mid, double treble) { p.bass = bass; p.mid = mid; p.treble = treble; buildStack(); }
    void setVolTone(double vol, double tone) { p.vol = vol; p.tone = tone; buildPickup(); }
    void setSag(double s) { p.sag = s; amp.setSag(s); }
    void setClosed(bool c) { p.closed = c; spk.setClosed(c); }
    void setPalmAll(double v, double q) { p.palm = v; p.palmPos = q; for (auto& s : strings) s.setPalm(v, q); }
    void setAction(double a) { p.action = a; for (auto& s : strings) s.setAction(a); }
    void setAge(double a) { p.age = a; for (auto& s : strings) s.setAge(a); }
    void setPickHeight(double v) { p.pickHeight = v; buildCoils(); }
    void setCable(double m) { p.cableM = m; buildPickup(); }
    void setMic(double micPos, double micDist, double room) { p.micPos = micPos; p.micDist = micDist; p.room = room; cab.set(micPos, micDist, room); }
    void setFeedback(double f) { p.feedback = f; const double dist = 4.0 - 3.6 * f; fbN = std::max(8, std::min(2000, (int) std::lround(dist / 343 * fs))); fbG = f <= 0 ? 0 : 0.9 * f * f / dist; }
    void setSeed(uint32_t v) { seed = v; for (int i = 0; i < 6; i++) strings[i].seed = (uint32_t) (v * 7919u + 977u * (uint32_t) (i + 1)); detReady = false; applyTune(); }
    void setTune(double cents, double spread) { p.tune = cents; p.spread = spread; applyTune(); }
    void applyTune() { if (!detReady) { for (auto& d : det) d = 2 * rnd() - 1; detReady = true; } for (int i = 0; i < 6; i++) strings[i].setTune(p.tune + p.spread * det[i]); }

    void push(const Event& ev)
    {
        auto it = queue.begin(); while (it != queue.end() && it->at <= ev.at) ++it;
        queue.insert(it, ev);
    }
    void doEvent(Event ev)
    {
        auto forStrings = [&](auto fn) { if (ev.s < 0) for (auto& s : strings) fn(s); else fn(strings[ev.s]); };
        switch (ev.t) {
        case Event::Pluck: {
            GString& s = strings[ev.s];
            if (ev.fret < 0) ev.fret = s.fret;
            const bool changed = s.fret != ev.fret || s.mute == 2;
            const bool ringing = s.en > 1e-6 && s.mute != 2;
            if (ev.tech == Event::Hammer && ringing) { s.hammer(ev.fret, ev.vel); fretClick(ev.vel); return; }
            if (ev.tech == Event::Pull && ringing) { s.pulloff(ev.fret, ev.vel); return; }
            if (ev.tech == Event::Glide && ringing) { squeak(ev.s, s.fret, ev.fret, 1.5); s.glide(ev.fret, ev.ms); return; }
            if (s.fret != ev.fret) { if (s.fret > 0 && ev.fret > 0) squeak(ev.s, s.fret, ev.fret, 1); s.setFret(ev.fret); }
            s.setMute(ev.dead ? 2 : ev.pm ? 1 : 0);
            const double pos = std::max(15.0, std::min(320.0, p.pickPos + ev.dpos));
            const double hard = std::max(0.0, std::min(1.0, p.hard + ev.dhard));
            if (ev.tech == Event::Harm) s.harmonic(ev.frac, pos, ev.vel, hard);
            else { s.pickMat = p.pickMat; s.pickMM = p.pickMM; GString::Geo geo{ ev.vp, ev.dig, ev.ang }; s.pluck(pos, ev.vel, hard, ev.dir, changed, ev.scr, ev.stop, ev.hasFing ? &ev.fing : nullptr, ev.vp > 0 ? &geo : nullptr); }
            break; }
        case Event::Damp: {
            bool any = false; forStrings([&](GString& s) { if (s.en > 1e-6 && s.mute != 2) any = true; s.setMute(2); });
            if (any && ev.s < 0) { sqBp.bandpass(fs, 350 + 300 * rnd(), 1.2); sqBp2.bandpass(fs, 1200, 2); sqLen = (int) std::lround(0.02 * fs); sqN = sqLen; sqAmp = 0.0015; }
            break; }
        case Event::Release: forStrings([&](GString& s) { s.release(ev.keep); }); break;
        case Event::Finger: {
            GString& s = strings[ev.s];
            if (ev.phase == Event::Lift) { if (s.fret > 0) { if (s.en > 1e-6 && s.mute != 2) s.release(true); else s.setFret(0); } }
            else { if (s.fret != ev.fret) { if (s.en > 1e-6 && s.mute != 2) s.land(ev.fret); else s.setFret(ev.fret); } }
            if (ev.touch >= 0 && ev.touch < 6) {
                GString& nb = strings[ev.touch];
                if (nb.en > 1e-6 && nb.mute != 2) { nb.V.scaleAll(0.55); nb.noiseX = std::max(2.0, nb.V.M * 0.5); nb.noiseN = (int) std::lround(0.004 * fs); nb.noiseLen = nb.noiseN; nb.noiseA = 0.004; nb.noiseCol = 0.3; }
            }
            break; }
        case Event::Grip: strings[ev.s].setGrip(ev.damp, ev.buzz); break;
        case Event::Palm:
            if (ev.s >= 0) strings[ev.s].setPalm(ev.v, ev.q >= 0 ? ev.q : strings[ev.s].palmQ);
            else { p.palm = ev.v; if (ev.q >= 0) p.palmPos = ev.q; for (auto& s : strings) s.setPalm(p.palm, p.palmPos); }
            break;
        case Event::NewStrings: for (auto& s : strings) { s.wear = 0; s.configure(); } break;
        case Event::Bend: forStrings([&](GString& s) { s.bend(ev.c, ev.ms > 0 ? ev.ms : 25); }); break;
        case Event::Vib: p.vibDepth = ev.d; if (ev.r > 0) p.vibRate = ev.r; break;
        }
    }
    void squeak(int si, int fromF, int toF, double k)
    {
        if (si >= 3 || fromF <= 0 || toF <= 0) return;
        if (rnd() + 0.5 > 0.55 * p.human * k) return;
        const double f0 = 1800 + 2500 * (rnd() + 0.5);
        sqBp.bandpass(fs, f0, 6); sqBp2.bandpass(fs, f0 * (1.4 + 0.4 * (rnd() + 0.5)), 5);
        sqLen = (int) std::lround((0.03 + 0.05 * (rnd() + 0.5)) * fs); sqN = sqLen;
        sqAmp = (0.4 + 0.6 * (rnd() + 0.5)) * 0.0008 * std::min(2.0, std::abs(toF - fromF) / 3.0);
    }
    void fretClick(double vel)
    {
        sqBp.bandpass(fs, 2500 + 1500 * (rnd() + 0.5), 3); sqBp2.bandpass(fs, 6000, 2);
        sqLen = (int) std::lround(0.004 * fs); sqN = sqLen; sqAmp = 0.001 * vel;
    }
    /** rendert n Samples (mono). Gibt false zurück, wenn die Engine wegen NaN neu aufgebaut wurde. */
    bool process(float* out, int n)
    {
        double humAmp = 0; for (auto& c : coils) humAmp += c.hum; humAmp = std::abs(humAmp);
        const double sens = emfK * fs;
        for (int i = 0; i < n; i++) {
            while (!queue.empty() && queue.front().at <= frame) { Event ev = queue.front(); queue.pop_front(); doEvent(ev); }
            double e = 0;
            vibPh += 2 * PI * p.vibRate / fs; if (vibPh > 2 * PI) vibPh -= 2 * PI;
            const double vibC = p.vibDepth * 40 * (0.5 - 0.5 * std::cos(vibPh));
            double fb = 0, fbRaw = 0;
            if (fbG > 0) { const double v = fbBuf[(size_t) ((fbP - fbN + 2048) & 2047)]; fbRaw = v * fbG; fbLp += 0.25 * (v - fbLp); fb = fbLp * fbG * (1 + 2 * hollow); }
            double F = 0;
            for (int k = 0; k < 6; k++) { const double y = strings[k].tick(vibC); ys[k] = y; F += y; }
            double vb = 0;
            for (auto& b : body) { const double y = b.b * F + b.c * b.s1 - b.r2 * b.s2; b.s2 = b.s1; b.s1 = y; vb += b.wgt * y; }
            if (!springs.empty()) { double sv = 0; for (auto& sp : springs) { const double y = sp.b * F * 0.6 + sp.c * sp.s1 - sp.r2 * sp.s2; sp.s2 = sp.s1; sp.s1 = y; sv += y; } vb += 0.5 * sv; }
            const double inj = -(0.010 + 0.012 * hollow) * vb;
            for (int k = 0; k < 6; k++) {
                GString& st = strings[k];
                st.V.add(0, inj); st.H.add(0, 0.35 * inj);
                if (fb != 0) { const double Mk = st.V.M; st.V.add(Mk * 0.5, fb * 0.5); st.V.add(Mk * 1.5, -fb * 0.5); }
            }
            for (auto& c : coils) e += coilEmf(c, fbRaw);
            e *= sens;
            if (p.noise) {
                humPh += 2 * PI * 50 / fs; if (humPh > 2 * PI) humPh -= 2 * PI;
                e += humAmp * (std::sin(humPh) + 0.5 * std::sin(3 * humPh) + 0.3 * std::sin(5 * humPh));
            }
            if (sqN > 0) { const double env = std::sin(PI * sqN / sqLen), w = gauss(); e += sqAmp * env * (sqBp.run(w) + 0.6 * sqBp2.run(w)); sqN--; }
            pick.step(e, 0);
            double x = pick.v[3];
            inPk = std::max(inPk, std::abs(x));
            if (p.noise) {
                x += 3e-6 * gauss();
                if (rnd() < 0.2 / fs) pop = 3e-5 * (rnd() - 0.5) * 2;
                if (pop != 0) { x += pop; pop *= 0.996; if (std::abs(pop) < 1e-7) pop = 0; }
            }
            up.upsample(x, bufUp.data());
            for (int k = 0; k < os; k++) bufU[(size_t) k] = chain(bufUp[(size_t) k]);
            spk.thermal(1 / fs);
            const double direct = radiate(dn.downsample(bufU.data()));
            fbBuf[(size_t) fbP] = direct; fbP = (fbP + 1) & 2047;
            double y = cab.space(direct) * p.out * p.out * 8;
            const double a = std::abs(y);
            limEnv = a > limEnv ? a : limEnv * 0.99995;
            const double gr = limEnv > 0.95 ? 0.95 / limEnv : 1;
            y *= gr;
            out[i] = (float) y;
            outPk = std::max(outPk, std::abs(y));
            frame++;
            if (!(y == y)) { build(os); return false; }
        }
        return true;
    }

    // ---- öffentliche Zustände ----
    double fs = 48000, h = 1; int os = 2;
    Params p;
    GString strings[6];
    std::deque<Event> queue;
    long frame = 0;
    uint32_t seed = 12345;
    double emfK = 1.2e-4, outNorm = 0, inPk = 0, prePk = 0, outPk = 0, limEnv = 0;
    Speaker10 spk; PowerAmp amp; CabMic cab;
    const GuitarModel* model = nullptr; std::vector<PickupDef> activePickups;
    std::vector<Reso> body, springs; double hollow = 0;
private:
    Oversampler up, dn; std::vector<double> bufUp, bufU;
    Triode tubes[4];
    TriodeStage s1, s2, s3, s4; Coupling c1, c2, c3, c4;
    Circuit stack, pick; bool stackBuilt = false;
    double micC = 0, micR2 = 0, micB = 0, mic1 = 0, mic2 = 0;
    int delayN = 1, dlp = 0; Biquad rearLp; std::vector<double> dl; double uPrev = 0;
    double Vbp = 300, VbRest = 415, ripPh = 0, humPh = 0, pop = 0;
    int sqN = 0, sqLen = 1; double sqAmp = 0; Biquad sqBp, sqBp2;
    double vibPh = 0; std::vector<double> fbBuf; int fbP = 0, fbN = 100; double fbLp = 0, fbG = 0;
    double ys[6] = {}; double det[6] = {}; bool detReady = false;
};

} // namespace rig

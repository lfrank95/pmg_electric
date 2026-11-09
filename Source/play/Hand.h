// PMG Electric – die Hände: Absicht → menschliche Ausführung. Port von dsp.js Hand.
// Erzeugt Engine-Ereignisse mit Zeitstempel `when` (Sekunden); der Aufrufer rechnet in Samples um.
#pragma once
#include "../dsp/Engine.h"
#include <array>
#include <algorithm>
#include <functional>
#include <optional>

namespace rig {

struct ClockPos { int bar = 0; double step16 = 0, beat = 0, bpm = 120, barLen = 4; int num = 4, den = 4; };
/** Zeit (s, Hand-Zeitachse) → Taktposition; leer = kein Takt bekannt */
using Clock = std::function<std::optional<ClockPos>(double)>;

struct HEvent : Event { double when = 0; };
using Events = std::vector<HEvent>;

struct Shape { std::array<int, 6> f{}; int& operator[](int i) { return f[(size_t) i]; } int operator[](int i) const { return f[(size_t) i]; } bool operator==(const Shape& o) const { return f == o.f; } };

class Hand {
public:
    explicit Hand(uint32_t sd = 1234) { seed = sd; baseSeed = sd; grad = 0.25 + 0.2 * rnd(); }

    uint32_t seed = 1234, baseSeed = 1234; int take = 1; Clock clock;
    double human = 0.6;
    double pos = 0, last = -1; int pick = 1; double lastNote = -1, pickAt = -0.5; int lastStrumDir = 0; double lastStrumT = -10;
    Shape frets{}; bool hasShape = false; Shape shapeKey{};
    std::array<int, 6> lastFret{ -1, -1, -1, -1, -1, -1 }; std::array<double, 6> lastT{ -10, -10, -10, -10, -10, -10 };
    double gripDrift = 0;
    struct Ctl { double palm = 0, palmQ = 0.3, intensity = 1; } intent, real, base; bool hasBase = false;
    double intentT = -1, pDrift = 0, qDrift = 0, iDrift = 0, grad = 0.3;
    std::array<double, 6> lastPalmSent{ -1, -1, -1, -1, -1, -1 };
    Ctl lastReal; bool hasLastReal = false;

    double rnd() { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0; }
    double gauss() { double u = 0, v = 0; while (u == 0) u = rnd(); while (v == 0) v = rnd(); return std::sqrt(-2 * std::log(u)) * std::cos(2 * PI * v); }
    double K() const { return human / 0.6; }

    enum IntentKey { Palm, PalmQ, Intensity };
    Events setIntent(IntentKey key, double val, double t)
    {
        double& cur = key == Palm ? intent.palm : key == PalmQ ? intent.palmQ : intent.intensity;
        if (std::abs(cur - val) < 1e-4) return {};
        base = real; hasBase = true; cur = val; intentT = t;
        if (key == Palm || key == PalmQ) { const double lag = (0.04 + 0.08 * rnd()) * std::max(0.3, K()); return palmEvents(t + lag, true, 1); }
        return {};
    }
    Ctl realize(double t, double vel)
    {
        const double k = K(); const Ctl& I = intent; Ctl& R = real;
        const double dt = intentT >= 0 ? std::max(0.0, t - intentT) : 1; const Ctl& B = hasBase ? base : R;
        const double e = std::exp(-dt / (0.07 * std::max(0.4, k)));
        const double over = dt < 0.25 ? 1 + 0.12 * k * std::sin(PI * dt / 0.25) : 1;
        R.palm = I.palm * over + (B.palm - I.palm) * e;
        R.palmQ = I.palmQ + (B.palmQ - I.palmQ) * e;
        R.intensity = I.intensity + (B.intensity - I.intensity) * e;
        if (!onClock(t)) {
            pDrift = 0.92 * pDrift + 0.08 * gauss() * 0.14 * k;
            qDrift = 0.95 * qDrift + 0.05 * gauss() * 0.12 * k;
            iDrift = 0.93 * iDrift + 0.07 * gauss() * 0.06 * k;
        }
        const double v = vel > 0 ? vel : 1, jit = gauss() * (0.05 + 0.06 * v) * k;
        double p = R.palm * (1 + pDrift + jit);
        if (R.palm > 0.1 && rnd() < 0.04 * k) p *= 0.4;
        if (R.palm < 0.1 && rnd() < 0.015 * k) p += 0.06;
        const double q = std::max(0.0, std::min(1.0, R.palmQ + qDrift + gauss() * 0.03 * k));
        Ctl o; o.palm = std::max(0.0, std::min(1.0, p)); o.palmQ = q; o.intensity = R.intensity * (1 + iDrift);
        return o;
    }
    Events palmEvents(double t, bool force, double vel)
    {
        seedAt(t, -1, 5);
        const Ctl r = realize(t, vel); Events ev; const double k = K();
        for (int s = 0; s < 6; s++) {
            const double g = 1 + grad * (2.5 - s) / 2.5 * std::min(1.0, k);
            const double p = std::max(0.0, std::min(1.0, r.palm * g * (1 + gauss() * 0.04 * k)));
            if (force || std::abs(p - lastPalmSent[(size_t) s]) > 0.02) { HEvent e; e.t = Event::Palm; e.s = s; e.v = p; e.q = r.palmQ; e.when = t; ev.push_back(e); lastPalmSent[(size_t) s] = p; }
        }
        lastReal = r; hasLastReal = true;
        return ev;
    }
    bool onClock(double t) const { if (!clock) return false; return clock(t).has_value(); }
    static uint32_t fnvMix(uint32_t h, uint32_t q) { h ^= q; h *= 16777619u; h ^= h >> 13; return h; }
    double smooth(double x, int kind) const
    {
        auto hsh = [&](long i) { uint32_t v = 2166136261u; v = fnvMix(v, (uint32_t) take); v = fnvMix(v, baseSeed); v = fnvMix(v, (uint32_t) (int32_t) i); v = fnvMix(v, (uint32_t) kind); return v / 4294967296.0 * 2 - 1; };
        const double fl = std::floor(x), f = x - fl, u = 0.5 - 0.5 * std::cos(PI * f); const long i = (long) fl;
        return hsh(i) * (1 - u) + hsh(i + 1) * u;
    }
    void driftAt(double t)
    {
        if (!clock) return;
        auto c = clock(t); if (!c) return;
        const double x = c->bar + c->step16 / 16, k = K();
        pos = 14 * k * 0.9 * smooth(x / 3, 11);
        pDrift = 0.14 * k * smooth(x / 2.5, 12);
        qDrift = 0.12 * k * smooth(x / 4, 13);
        iDrift = 0.06 * k * smooth(x / 3.5, 14);
        gripDrift = smooth(x / 2, 15);
    }
    void seedAt(double t, int s, int kind)
    {
        if (!clock) return;
        auto c = clock(t); if (!c) return;
        uint32_t h = 2166136261u;
        h = fnvMix(h, (uint32_t) take); h = fnvMix(h, baseSeed); h = fnvMix(h, (uint32_t) c->bar); h = fnvMix(h, (uint32_t) (int32_t) std::lround(c->step16 * 4)); h = fnvMix(h, (uint32_t) (s + 1)); h = fnvMix(h, (uint32_t) kind);
        seed = h;
    }
    struct Grip { Events ev; std::array<double, 6> land{ -1, -1, -1, -1, -1, -1 }; };
    Grip changeGrip(const Shape& shapeIn, double tStrum, bool force = false)
    {
        Shape shape = shapeIn; for (int s = 0; s < 6; s++) if (shape[s] == -2) shape[s] = -1;   // tote Saite: Finger liegt nur auf
        seedAt(tStrum, -1, 3);
        const double k = K(); Grip g;
        const bool same = hasShape && shape == shapeKey;
        if (same && !force) { if (rnd() < 0.15 * k) rollGrip(shape, g.ev, tStrum - 0.02); return g; }
        int oldMin = 99, newMin = 99, nFing = 0;
        for (int s = 0; s < 6; s++) { if (frets[s] > 0) oldMin = std::min(oldMin, frets[s]); if (shape[s] > 0) { newMin = std::min(newMin, shape[s]); nFing++; } }
        const int shift = (oldMin < 99 && newMin < 99) ? std::abs(oldMin - newMin) : 0;
        const double dur = (0.035 + 0.012 * nFing + 0.012 * shift) * (0.7 + 0.6 * rnd()) * k;
        const double tLift = tStrum - dur - 0.01 * k;
        std::vector<int> order; for (int s = 0; s < 6; s++) if (shape[s] != frets[s]) order.push_back(s);
        if (rnd() < 0.3 * k) for (int i = (int) order.size() - 1; i > 0; i--) std::swap(order[(size_t) i], order[(size_t) (int) (rnd() * (i + 1))]);
        for (size_t i = 0; i < order.size(); i++) {
            const int s = order[i];
            const double stag = i * dur / std::max<size_t>(1, order.size()) * (0.6 + 0.8 * rnd());
            const double tl = tLift + stag;
            if (frets[s] > 0) { HEvent e; e.t = Event::Finger; e.s = s; e.phase = Event::Lift; e.when = tl; g.ev.push_back(e); }
            if (shape[s] > 0) {
                double tp = tl + (0.015 + 0.035 * rnd()) * k;
                if (rnd() < 0.12 * k) tp = tStrum + (0.004 + 0.02 * rnd());
                HEvent e; e.t = Event::Finger; e.s = s; e.phase = Event::Press; e.fret = shape[s]; e.when = tp;
                if (rnd() < 0.18 * k) { const int nb = s + (rnd() < 0.5 ? -1 : 1); if (nb >= 0 && nb < 6 && shape[nb] == 0) e.touch = nb; }
                g.ev.push_back(e); g.land[(size_t) s] = tp;
            } else if (shape[s] < 0) g.land[(size_t) s] = tl;
        }
        frets = shape; shapeKey = shape; hasShape = true;
        rollGrip(shape, g.ev, tLift);
        return g;
    }
    void rollGrip(const Shape& shape, Events& ev, double when)
    {
        const double k = K(); int mn = 0, nFr = 0, nMn = 0;
        for (int s = 0; s < 6; s++) if (shape[s] > 0) { nFr++; mn = mn == 0 ? shape[s] : std::min(mn, shape[s]); }
        for (int s = 0; s < 6; s++) if (shape[s] == mn) nMn++;
        const bool barre = mn > 0 && nMn >= 3, hard = nFr >= 4 || barre;
        for (int s = 0; s < 6; s++) {
            double damp = 0, buzz = 0;
            if (shape[s] >= 0) {
                if (barre && shape[s] == mn && s > 0 && s < 5 && rnd() < 0.45 * k) damp = 0.15 + 0.35 * rnd();
                if (shape[s] == 0 && ((s > 0 && shape[s - 1] > 0) || (s < 5 && shape[s + 1] > 0)) && rnd() < (hard ? 0.14 : 0.07) * k) damp = std::max(damp, 0.25 + 0.45 * rnd());
                if (shape[s] > 0 && rnd() < (hard ? 0.12 : 0.06) * k) buzz = 0.4 + 0.6 * rnd();
            }
            HEvent e; e.t = Event::Grip; e.s = s; e.damp = std::min(1.0, damp); e.buzz = std::min(1.0, buzz); e.when = when; ev.push_back(e);
        }
    }
    struct StrumOpt { double vel = 1, speed = 10; bool pm = false, keepBass = false, force = false; double velS[6] = { 0, 0, 0, 0, 0, 0 }; };
    Events strum(const Shape& shape, double t0, int dir, const StrumOpt& opt)
    {
        const double k = K();
        seedAt(t0, -1, 1); driftAt(t0);
        Grip g; if (!opt.keepBass) g = changeGrip(shape, t0, opt.force);
        Events ev = std::move(g.ev);
        seedAt(t0, -1, 6);
        const double gap = last < 0 ? 1 : std::max(0.03, t0 - last); last = t0;
        const double quick = std::max(0.0, std::min(1.0, (0.3 - gap) / 0.2));
        if (!onClock(t0)) pos = 0.92 * pos + 0.08 * gauss() * 14 * k;
        const double speedF = std::exp(gauss() * 0.18 * k) * (1 - 0.35 * quick);
        const double velF = (1 + gauss() * 0.08 * k) * (1 - 0.15 * quick);
        const double dhard = gauss() * 0.05 * k;
        // Bahn der Hand: Saitenabstand ~10,5 mm am Anschlagpunkt, Beschleunigung durch den Schlag, Drehung des Winkels, Bogen taucht mittig tiefer
        const double SPACING = 10.5e-3;
        const double vHand = 0.7 * SPACING * 5 / ((opt.speed > 0 ? opt.speed : 10) / 1000) * speedF;
        std::vector<int> hit;
        for (int i = 0; i < 6; i++) {
            const int s = dir > 0 ? i : 5 - i;
            if (shape[s] == -2) { hit.push_back(s); continue; }   // tot: wird mitgestrichen
            if (shape[s] < 0) { if (!opt.keepBass) { HEvent e; e.t = Event::Damp; e.s = s; e.when = t0; ev.push_back(e); } continue; }
            // voller Strum trifft immer alle gegriffenen/offenen Saiten (Teil-Strums sind eigene Trigger)
            hit.push_back(s);

        }
        const int n = (int) hit.size();
        pickAt = dir > 0 ? 5.5 : -0.5; lastNote = t0 + 0.03; lastStrumDir = dir; lastStrumT = t0;
        { Events pe = palmEvents(t0 - 0.003, false, opt.vel > 0 ? opt.vel : 1); ev.insert(ev.end(), pe.begin(), pe.end()); }
        const double inten = hasLastReal ? lastReal.intensity : 1;
        const double tilt = 0.4 + 0.5 * rnd(), tilt0 = 0.9 + 0.6 * (rnd() - 0.5) * std::min(1.0, k);
        const double accel = 0.5 + 0.7 * rnd();
        const double arcAng = (0.15 + 0.25 * rnd()) * std::min(1.0, k);
        const double arcDig = 0.15 + 0.3 * rnd();
        double tAcc = t0; const int outlier = rnd() < 0.12 * std::min(1.0, k) ? (int) std::floor(rnd() * n) : -1;
        double vPrev = 0;
        for (int kk = 0; kk < n; kk++) {
            const int s = hit[(size_t) kk]; const double p = n > 1 ? (double) kk / (n - 1) : 0;
            seedAt(t0, s, 2);
            const double vk = vHand * (1 - accel * 0.5 + accel * p);
            if (kk > 0) {
                const double gapS = std::abs(hit[(size_t) kk] - hit[(size_t) kk - 1]) * SPACING;
                double gp = gapS / (0.5 * (vk + vPrev));
                const double r = rnd(); if (r < 0.1 * k) gp *= 0.35; else if (r > 1 - 0.08 * k) gp *= 1.8;
                tAcc += gp;
            }
            vPrev = vk;
            const double t = tAcc + gauss() * 0.0008 * k;
            const double prof = dir > 0 ? 1.08 - 0.22 * p : 0.78 + 0.28 * p;
            double vel = (opt.vel > 0 ? opt.vel : 1) * inten * velF * prof * (1 + gauss() * 0.08 * k);
            if (kk == outlier) vel *= rnd() < 0.5 ? 1.3 : 0.6;
            vel = std::min(1.2, vel);
            const double scr = tilt0 * (1 + tilt * (0.5 - p) * (dir > 0 ? 1 : -1) * std::min(1.0, k)) * std::exp(gauss() * 0.2 * k);
            if (opt.velS[s] > 0) vel = std::min(1.2, vel * opt.velS[s]);
            if (shape[s] == -2) vel *= 0.55;
            HEvent e; e.t = Event::Pluck; e.s = s; e.fret = shape[s] == -2 ? -1 : shape[s]; e.dead = shape[s] == -2; e.vel = vel; e.dir = dir;
            e.dpos = pos + (p - 0.5) * 8 * dir + gauss() * 2 * k; e.dhard = dhard + gauss() * 0.04 * k; e.scr = scr; e.when = t;
            e.vp = vk; e.dig = 1 + arcDig * std::sin(PI * p); e.ang = (p - 0.5) * arcAng * dir;
            if (opt.pm) e.pm = true;
            if (g.land[(size_t) s] > t) e.fret = -1;
            if (kk == n - 1) e.stop = true;
            ev.push_back(e);
            lastFret[(size_t) s] = shape[s]; lastT[(size_t) s] = t;
        }
        return ev;
    }
    enum Tech { Auto, Plain, HammerT, PullT, GlideT, HarmT };
    Events note(int s, int fret, double t, double vel, Tech tech, bool pm = false, int forceDir = 0)
    {
        seedAt(t, s, 4); driftAt(t);
        const double k = K(); Events ev; const size_t si = (size_t) s;
        if (tech == Auto) {
            if (lastFret[si] >= 0 && t - lastT[si] < 0.35 && fret != lastFret[si] && human > 0.05 && rnd() < 0.85) {
                const int d = std::abs(fret - lastFret[si]);
                tech = (d >= 3 && lastFret[si] > 0 && fret > 0 && rnd() < (d >= 5 ? 0.9 : 0.6)) ? GlideT : fret > lastFret[si] ? HammerT : PullT;
            } else tech = Plain;
        }
        if (tech == HammerT && fret <= lastFret[si]) tech = PullT;
        if (tech == PullT && fret >= lastFret[si]) tech = HammerT;
        if (tech == GlideT && lastFret[si] < 0) tech = Plain;
        const int dF = std::abs(fret - lastFret[si]);
        lastFret[si] = fret; lastT[si] = t; frets[s] = fret; hasShape = false;
        if (tech == HammerT || tech == PullT || tech == GlideT) {
            HEvent e; e.t = Event::Pluck; e.s = s; e.fret = fret; e.vel = std::min(1.2, vel * (0.8 + gauss() * 0.08 * k));
            e.tech = tech == HammerT ? Event::Hammer : tech == PullT ? Event::Pull : Event::Glide; e.ms = tech == GlideT ? 25 + 18 * dF + 30 * rnd() : 60 + 80 * rnd(); e.when = t; ev.push_back(e);
            return ev;
        }
        // Richtung aus der Lage des Plektrums (Economy-Picking), siehe dsp.js
        if (lastNote < 0 || t - lastNote > 0.45) pickAt = s - 0.5;
        pick = forceDir ? forceDir : s > pickAt ? 1 : -1;
        pickAt = s + 0.5 * pick;
        lastNote = t;
        if (!onClock(t)) pos = 0.95 * pos + 0.05 * gauss() * 14 * k;
        { Events pe = palmEvents(t - 0.003, false, vel); ev.insert(ev.end(), pe.begin(), pe.end()); }
        const double inten = hasLastReal ? lastReal.intensity : 1;
        HEvent e; e.t = Event::Pluck; e.s = s; e.fret = fret; e.vel = std::min(1.2, vel * inten * (1 + gauss() * 0.06 * k) * (pick < 0 ? 0.92 : 1)); e.dir = pick;
        e.dpos = pos + gauss() * 3 * k; e.dhard = gauss() * 0.04 * k; e.scr = std::exp(gauss() * 0.25 * k); e.when = t; e.stop = true;
        if (tech == HarmT) { e.tech = Event::Harm; static const double fr[3] = { 0.5, 1.0 / 3, 0.25 }; e.frac = fr[(int) std::floor(rnd() * 3)]; }
        if (pm) e.pm = true;
        ev.push_back(e);
        return ev;
    }
    /** Hybrid Picking: tiefster Ton mit dem Plektrum, die anderen gleichzeitig mit den Fingern */
    Events hybrid(const Shape& shape, double t, double vel, const double* velS)
    {
        const double k = K(); seedAt(t, -1, 7); driftAt(t);
        Grip g = changeGrip(shape, t); Events ev = g.ev;
        std::vector<int> strs; for (int s = 0; s < 6; s++) if (shape[s] >= 0) strs.push_back(s);
        if (strs.empty()) return ev;
        { Events pe = palmEvents(t - 0.003, false, vel); ev.insert(ev.end(), pe.begin(), pe.end()); }
        for (size_t i = 0; i < strs.size(); i++) {
            const int s = strs[i];
            const double v = std::min(1.2, vel * (velS && velS[s] > 0 ? velS[s] : 1) * (1 + gauss() * 0.06 * k));
            HEvent e; e.t = Event::Pluck; e.s = s; e.fret = g.land[(size_t) s] > t ? -1 : shape[s]; e.vel = v; e.stop = true;
            e.when = t + (i ? (0.001 + 0.004 * rnd()) * k : 0);
            if (i == 0) { e.dir = 1; e.dpos = pos + gauss() * 2 * k; e.scr = std::exp(gauss() * 0.2 * k); }
            else { e.dir = -1; e.dpos = pos + 25 + 6 * (double) i + gauss() * 3 * k; e.hasFing = true; e.fing = { 600 + 400 * v, 0.008, 0.7, (0.3 + 0.6 * v) * 1e-3, 0.25 + 0.6 * v, 0.5 + 0.4 * rnd() }; }
            ev.push_back(e); lastFret[(size_t) s] = shape[s]; lastT[(size_t) s] = t;
        }
        lastNote = t; pickAt = strs[0] + 0.5;
        return ev;
    }
    Events release(int s, bool keep, double t) { if (s >= 0) lastFret[(size_t) s] = -1; else lastFret.fill(-1); HEvent e; e.t = Event::Release; e.s = s; e.keep = keep; e.when = t; return { e }; }
    Events damp(int s, double t) { if (s >= 0) lastFret[(size_t) s] = -1; else lastFret.fill(-1); HEvent e; e.t = Event::Damp; e.s = s; e.when = t; return { e }; }
};

} // namespace rig

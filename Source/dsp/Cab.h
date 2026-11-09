// PMG Electric – Biquad, Membran-Aufbruch/Box/Mikro/Raum (CabMic) und die Gitarrenmodelle. Port von dsp.js.
#pragma once
#include "Tubes.h"
#include <string>

namespace rig {

class Biquad {
public:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    Biquad& peak(double fs, double f, double q, double db)
    {
        const double A = std::pow(10.0, db / 40), w = 2 * PI * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0; return *this;
    }
    Biquad& lowpass(double fs, double f, double q)
    {
        const double w = 2 * PI * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; return *this;
    }
    Biquad& bandpass(double fs, double f, double q)
    {
        const double w = 2 * PI * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0; return *this;
    }
    Biquad& highpass(double fs, double f, double q)
    {
        const double w = 2 * PI * f / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; return *this;
    }
    Biquad& lowshelf(double fs, double f, double db)
    {
        const double A = std::pow(10.0, db / 40), w = 2 * PI * f / fs, c = std::cos(w), sn = std::sin(w), al = sn / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
        const double a0 = (A + 1) + (A - 1) * c + sa;
        b0 = A * ((A + 1) - (A - 1) * c + sa) / a0; b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0; b2 = A * ((A + 1) - (A - 1) * c - sa) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * c) / a0; a2 = ((A + 1) + (A - 1) * c - sa) / a0; return *this;
    }
    Biquad& highshelf(double fs, double f, double db)
    {
        const double A = std::pow(10.0, db / 40), w = 2 * PI * f / fs, c = std::cos(w), sn = std::sin(w), al = sn / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
        const double a0 = (A + 1) - (A - 1) * c + sa;
        b0 = A * ((A + 1) + (A - 1) * c + sa) / a0; b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0; b2 = A * ((A + 1) + (A - 1) * c - sa) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * c) / a0; a2 = ((A + 1) - (A - 1) * c - sa) / a0; return *this;
    }
    void reset() { z1 = z2 = 0; }
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

class CabMic {
public:
    struct Mode { double f, q, wc, we, xs, s1 = 0, s2 = 0; };
    struct Tap { int n; double g; double lp = 0; };
    struct Comb { std::vector<double> buf; int p = 0; double lp = 0, g; };
    struct AP { std::vector<double> buf; int p = 0; };
    CabMic() = default;
    explicit CabMic(double fs_) { init(fs_); }
    void init(double fs_)
    {
        fs = fs_;
        const double md[7][5] = { { 1150, 5, 0.35, 0.5, 1 }, { 1750, 7, 0.55, -0.4, 0.8 }, { 2400, 8, 0.9, 0.5, 0.7 }, { 3100, 9, 0.8, -0.6, 0.6 },
            { 3900, 7, 0.7, 0.3, 0.5 }, { 4700, 6, 0.45, -0.35, 0.5 }, { 5600, 6, 0.3, 0.2, 0.5 } };
        modes.clear(); for (auto& m : md) modes.push_back({ m[0], m[1], m[2], m[3], m[4] * 0.6 });
        edgeLp.lowpass(fs, 1800, 0.7); edgeLp2.lowpass(fs, 4500, 0.7); pistonLp.lowpass(fs, 5200, 0.8);
        panel.peak(fs, 150, 3, 2); baffle.highshelf(fs, 450, 3.5); micHp.highpass(fs, 70, 0.7); micPres.peak(fs, 5200, 1.1, 4); micLp.lowpass(fs, 11000, 0.8); prox.lowshelf(fs, 180, 0);
        edgeDelay.fill(0); edp = 0; edN = std::max(1, (int) std::lround(0.00015 * fs)); ref = 1;
        const double tp[8][2] = { { 4.1, 0.55 }, { 7.3, -0.4 }, { 11.2, 0.35 }, { 14.8, -0.3 }, { 19.5, 0.25 }, { 24.1, -0.2 }, { 31.7, 0.15 }, { 38.2, 0.12 } };
        taps.clear(); for (auto& t : tp) taps.push_back({ (int) std::lround(t[0] / 1000 * fs), t[1] });
        erBuf.assign(4096, 0.0); erp = 0;
        const double cms[4] = { 29.7, 37.1, 41.1, 47.3 };
        combs.clear(); for (int i = 0; i < 4; i++) { Comb c; c.buf.assign((size_t) std::lround(cms[i] / 1000 * fs), 0.0); c.g = 0.72 - 0.02 * i; combs.push_back(c); }
        const double apms[2] = { 5.0, 1.7 };
        aps.clear(); for (double ms : apms) { AP a; a.buf.assign((size_t) std::lround(ms / 1000 * fs), 0.0); aps.push_back(a); }
        set(0.3, 0.25, 0.25);
    }
    void set(double micPos_, double micDist, double room_)
    {
        micPos = micPos_; room = room_;
        const double cm = 1 + 29 * micDist;
        prox.lowshelf(fs, 180, 7 * std::exp(-cm / 6));
        distG = 1 / (1 + cm / 25);
        roomG = room * room * (0.25 + 0.75 * micDist);
    }
    void reset()
    {
        for (auto& m : modes) m.s1 = m.s2 = 0;
        for (Biquad* b : { &edgeLp, &edgeLp2, &pistonLp, &panel, &baffle, &micHp, &micPres, &micLp, &prox }) b->reset();
        edgeDelay.fill(0); std::fill(erBuf.begin(), erBuf.end(), 0.0);
        for (auto& c : combs) { std::fill(c.buf.begin(), c.buf.end(), 0.0); c.lp = 0; }
        for (auto& a : aps) std::fill(a.buf.begin(), a.buf.end(), 0.0);
        for (auto& t : taps) t.lp = 0;
    }
    double cone(double acc, double /*force*/)
    {
        double modC = 0, modE = 0;
        for (auto& m : modes) {
            const double a = std::abs(m.s1) * ref / m.xs;
            const double f = m.f * (1 - 0.06 * std::min(1.5, a)), q = m.q / (1 + 0.6 * std::min(2.0, a));
            const double w = 2 * PI * f / fs, r = std::exp(-w / (2 * q)), c = 2 * r * std::cos(w), r2 = r * r;
            const double y = (1 - r2) * std::sin(w) * acc + c * m.s1 - r2 * m.s2;
            m.s2 = m.s1; m.s1 = y;
            const double ys = y / (1 + 0.3 * a * a);
            modC += m.wc * ys; modE += m.we * ys;
        }
        const double pist = pistonLp.run(acc);
        const double center = pist + modC;
        const double edgeIn = edgeLp2.run(edgeLp.run(pist)) + 0.7 * modE;
        edgeDelay[(size_t) edp] = edgeIn; const double edgeOut = edgeDelay[(size_t) ((edp + 64 - edN) & 63)]; edp = (edp + 1) & 63;
        const double mix = (1 - micPos) * center + micPos * edgeOut;
        double y = baffle.run(panel.run(mix));
        y = micLp.run(micPres.run(micHp.run(prox.run(y))));
        return y;
    }
    double space(double x)
    {
        if (roomG < 1e-4) return x * distG;
        erBuf[(size_t) erp] = x;
        double er = 0;
        for (auto& t : taps) { const double v = erBuf[(size_t) ((erp - t.n + 4096) & 4095)]; t.lp += 0.35 * (v - t.lp); er += t.g * t.lp; }
        erp = (erp + 1) & 4095;
        double tail = 0;
        const double inp = (x + er) * 0.5;
        for (auto& c : combs) { const double o = c.buf[(size_t) c.p]; c.lp += 0.3 * (o - c.lp); c.buf[(size_t) c.p] = inp + c.g * c.lp; c.p = (c.p + 1) % (int) c.buf.size(); tail += o; }
        tail *= 0.25;
        for (auto& a : aps) { const double o = a.buf[(size_t) a.p], v = tail + 0.5 * o; a.buf[(size_t) a.p] = v; a.p = (a.p + 1) % (int) a.buf.size(); tail = o - 0.5 * v; }
        return x * distG + roomG * (er * 0.9 + tail * 0.45);
    }
    double fs = 48000, ref = 1, micPos = 0.3, room = 0.25, distG = 1, roomG = 0;
private:
    std::vector<Mode> modes;
    Biquad edgeLp, edgeLp2, pistonLp, panel, baffle, micHp, micPres, micLp, prox;
    std::array<double, 64> edgeDelay{}; int edp = 0, edN = 7;
    std::vector<Tap> taps; std::vector<double> erBuf; int erp = 0;
    std::vector<Comb> combs; std::vector<AP> aps;
};

// ---------------- Gitarrenmodelle ----------------
// eddy = Wirbelstromverluste (Parallelwiderstand über der Spule: Kappe, Grundplatte, Stahlkerne), 0 = keine; turns = Pegel relativ zur Strat-Einzelspule
struct PickupDef { double pos; bool hb; double L, R, C; int polar; bool cover = false, plate = false; double eddy = 0, turns = 1; bool noTone = false; double a = 2.5, kap = 0.4, mis = 0; double stag[6] = { 0, 0, 0, 0, 0, 0 }; };
struct Selector { const char* label; std::vector<int> coils; };
struct BodyMode { double f, q, wgt; };
struct GuitarModel {
    const char* key; const char* name; double scale, pots, toneC, cable;
    std::vector<BodyMode> body; double sustain; bool springs; double hollow;
    std::vector<PickupDef> pickups; std::vector<Selector> selector; double gauge; double bright = 1; double radius = 0.6, micro = 0.3;
};
inline const std::vector<GuitarModel>& guitarModels()
{
    static const std::vector<GuitarModel> M = {
        { "S", "S-Typ", 648, 250e3, 47e-9, 500e-12, { { 95, 14, 1.0 }, { 190, 10, 0.7 }, { 240, 6, 0.4 }, { 420, 4, 0.25 }, { 700, 3.5, 0.2 }, { 1300, 3, 0.15 } }, 0.85, true, 0,
          { { 160, false, 2.2, 5700, 100e-12, 1, false, false, 0, 1.0, false, 2.4, 0.4, 0, { 0.3, 0.8, 1.3, 1.3, -0.5, 0.0 } },
            { 105, false, 2.3, 5900, 105e-12, -1, false, false, 0, 1.0, false, 2.4, 0.4, 0, { 0.3, 0.8, 1.3, 1.3, -0.5, 0.0 } },
            { 42, false, 2.5, 6300, 110e-12, 1, false, false, 0, 1.05, true, 2.4, 0.4, 0, { 0.3, 0.8, 1.3, 1.3, -0.5, 0.0 } } },
          { { "Hals", { 0 } }, { "Hals+Mitte", { 0, 1 } }, { "Mitte", { 1 } }, { "Mitte+Steg", { 1, 2 } }, { "Steg", { 2 } } }, 1.0, 1.2, 0.7, 0.25 },
        { "T", "T-Typ", 648, 250e3, 47e-9, 500e-12, { { 100, 12, 0.9 }, { 200, 9, 0.6 }, { 310, 5, 0.35 }, { 480, 4, 0.25 }, { 820, 3.5, 0.2 }, { 1500, 3, 0.15 } }, 1.0, false, 0,
          { { 155, false, 2.6, 7200, 130e-12, 1, true, false, 450e3, 0.9, false, 2.2, 0.35, 0, { 0, 0, 0, 0, 0, 0 } },
            { 36, false, 3.1, 6900, 110e-12, -1, false, true, 900e3, 1.3, false, 3.0, 0.45, 0, { 0.3, 0.8, 1.2, 1.2, -0.3, 0.0 } } },
          { { "Hals", { 0 } }, { "Beide", { 0, 1 } }, { "Steg", { 1 } } }, 1.0, 1.15, 0.7, 0.3 },
        { "LP", "LP-Typ", 628, 500e3, 22e-9, 500e-12, { { 85, 9, 0.6 }, { 165, 7, 0.5 }, { 230, 5, 0.35 }, { 380, 4, 0.2 }, { 600, 3.5, 0.15 }, { 1100, 3, 0.1 } }, 1.25, false, 0,
          { { 150, true, 4.3, 7500, 150e-12, 1, true, false, 350e3, 1.7, false, 3.0, 0.42, 0.08 }, { 40, true, 4.9, 8300, 160e-12, 1, true, false, 350e3, 1.85, false, 3.0, 0.42, 0.06 } },
          { { "Hals", { 0 } }, { "Beide", { 0, 1 } }, { "Steg", { 1 } } }, 1.08, 0.88, 0.45, 0.7 },
        { "335", "335-Typ", 628, 500e3, 22e-9, 500e-12, { { 105, 7, 2.2 }, { 150, 5, 1.6 }, { 215, 5, 1.2 }, { 290, 4, 0.8 }, { 440, 3.5, 0.6 }, { 800, 3, 0.35 } }, 1.05, false, 1.0,
          { { 150, true, 4.3, 7500, 150e-12, 1, true, false, 350e3, 1.7, false, 3.0, 0.42, 0.1 }, { 40, true, 4.7, 8000, 160e-12, 1, true, false, 350e3, 1.8, false, 3.0, 0.42, 0.07 } },
          { { "Hals", { 0 } }, { "Beide", { 0, 1 } }, { "Steg", { 1 } } }, 1.05, 0.85, 0.45, 0.9 }
    };
    return M;
}

} // namespace rig

// PMG Electric – die Saite: Waveguide mit zwei Schwingungsebenen, Steifigkeit, Spannungsmodulation, Bundschnarren,
// Plektrum-Reibung, Längswellen, Magnetzug, Griffqualität, Handkante, Bending/Glide, Techniken. Port von dsp.js GString.
#pragma once
#include "Tubes.h"
#include <cstdint>

namespace rig {

// Inharmonizität B der offenen Saiten (Steifigkeit; umsponnene E A D weniger, blanke G H e mehr)
inline const double B_OPEN[6] = { 3.5e-5, 3.5e-5, 4.5e-5, 1.4e-4, 9e-5, 7e-5 };
// Masse pro Länge (kg/m) eines 10-46-Satzes (E A D G H e), Spannung folgt aus Stimmung und Mensur: T = 4 f² L² µ
inline const double MU_10[6] = { 6.83e-3, 4.28e-3, 2.26e-3, 1.14e-3, 0.67e-3, 0.40e-3 };
inline const double UNIT = 6.4e-3;   // eine Modelleinheit Auslenkung = 6,4 mm
struct PickMat { const char* name; double E, zeta, mu, rho; };
inline const PickMat PICK_MATS[4] = { { "Celluloid", 2.6, 0.03, 0.35, 1350 }, { "Nylon", 2.0, 0.09, 0.45, 1150 }, { "Tortex", 3.0, 0.05, 0.30, 1420 }, { "Ultex", 3.2, 0.02, 0.33, 1270 } };
inline double apPhaseDelay(double c, double w)
{
    const double aN = std::atan2(-std::sin(w), c + std::cos(w)), aD = std::atan2(-c * std::sin(w), 1 + c * std::cos(w));
    double ph = aN - aD; if (ph > 0) ph -= 2 * PI;
    return -ph / w;
}
inline double lpPhaseDelay(double a, double w) { return std::atan2(a * std::sin(w), 1 - a * std::cos(w)) / w; }
constexpr int NDISP = 4;

class Loop {
public:
    static constexpr int SIZE = 4096, MASK = 4095;
    std::vector<double> buf = std::vector<double>(SIZE, 0.0);
    long w = 0;
    double lp = 0, apx = 0, apy = 0; double dx[NDISP] = {}, dy[NDISP] = {}; double dcX = 0, dcY = 0, dcR = 1 - 2 * PI * 3 / 48000;
    int N = 100, Nrun = 100; double D0 = 1.5, g = 0.99, a = 0.1, c = 0, M = 50, Ltot = 100;

    void config(double fs, double freq, double T60, double T60hi, double B)
    {
        Ltot = fs / freq; M = Ltot / 2; dcR = 1 - 2 * PI * 3 / fs;
        const double w1 = 2 * PI * freq / fs;
        g = std::pow(10.0, -3 / (T60 * freq));
        const double fh = std::min(3000.0, 0.4 * fs), wh = 2 * PI * fh / fs;
        const double R = std::pow(10.0, -3 / (T60hi * freq)) / g;
        double lo = 0, hi = 0.97, aa = 0;
        if (R < 1) { for (int i = 0; i < 40; i++) { aa = 0.5 * (lo + hi); const double r = (1 - aa) / std::sqrt(1 - 2 * aa * std::cos(wh) + aa * aa); if (r > R) lo = aa; else hi = aa; } }
        a = aa;
        const int nMax = std::max(2, std::min(14, (int) std::floor(0.25 * fs / freq)));
        auto tgt = [&](int n) { return n * fs / (n * freq * std::sqrt(1 + B * n * n)); };
        auto F = [&](double cc, double ww) { return lpPhaseDelay(a, ww) + NDISP * apPhaseDelay(cc, ww); };
        double best = 0, bestErr = 1e300;
        for (double cc = 0; cc >= -0.9; cc -= 0.01) {
            const double ref = F(cc, w1) - tgt(1); double err = 0;
            for (int n = 2; n <= nMax; n++) { const double fn = n * freq * std::sqrt(1 + B * n * n), e = F(cc, 2 * PI * fn / fs) - ref - tgt(n); err += e * e; }
            if (F(cc, w1) > Ltot - 3) break;
            if (err < bestErr) { bestErr = err; best = cc; }
        }
        c = best;
        const double rest = tgt(1) - F(best, w1);
        N = std::max(2, (int) std::floor(rest) - 1);
        D0 = rest - N;
        Nrun = N;
    }
    double rd(double age) const
    {
        const long i = (long) std::floor(age); const double f = age - i; const long base = w - 1 - i;
        return buf[(size_t) (base & MASK)] * (1 - f) + buf[(size_t) ((base - 1) & MASK)] * f;
    }
    double disp(double b) const { const double P = M - b; return rd(P) - rd(2 * M - P); }
    void add(double age, double v) { const long k = (long) std::lround(age); buf[(size_t) ((w - 1 - k) & MASK)] += v; }
    /** Geschwindigkeitsstoß an Position X: hebt die Auslenkung am Punkt um A (Konstante auf alle Samples der Stegseite) */
    void kick(double X, double A) { const long a0 = (long) std::floor(X) + 1, a1 = (long) std::ceil(2 * M - X) - 1; for (long a = a0; a <= a1; a++) buf[(size_t) ((w - 1 - a) & MASK)] -= A; }
    void scaleAll(double keep) { for (int k = 0; k < N; k++) buf[(size_t) ((w - 1 - k) & MASK)] *= keep; }
    double tick(double tm)
    {
        const double Td = N + D0 - tm;
        int Ni = Nrun;
        if (Td - Ni < 1.02) Ni = (int) std::floor(Td) - 1; else if (Td - Ni >= 2.0) Ni = (int) std::floor(Td) - 1;
        if (Ni < 2) Ni = 2;
        Nrun = Ni;
        double y = buf[(size_t) ((w - Ni) & MASK)];
        // Gleichanteil sperren (~3 Hz): anhaltende Kräfte pumpen sonst gegenläufige Gleichanteile in die Wellenkomponenten
        const double yh = y - dcX + dcR * dcY; dcX = y; dcY = yh; y = yh;
        lp = g * (1 - a) * y + a * lp;
        double x = lp;
        for (int k = 0; k < NDISP; k++) { const double o = c * x + dx[k] - c * dy[k]; dx[k] = x; dy[k] = o; x = o; }
        double D = Td - Ni; if (D < 0.3) D = 0.3; else if (D > 2.5) D = 2.5;
        const double eta = (1 - D) / (1 + D);
        const double out = eta * x + apx - eta * apy;
        apx = x; apy = out;
        buf[(size_t) (w & MASK)] = out;
        w++;
        return y;
    }
};

class GString {
public:
    GString() = default;
    GString(double fs_, double f0_, int idx_) { init(fs_, f0_, idx_); }
    void init(double fs_, double f0_, int idx_)
    {
        fs = fs_; f0 = f0_; idx = idx_;
        seed = (uint32_t) (977 * (idx + 1));
        bendK = 1 - std::exp(-1 / (0.006 * fs));
        setFret(0);
    }
    double rnd() { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0 - 0.5; }

    void setFret(int f)
    {
        fret = f; freq = f0 * tuneF * std::pow(2.0, f / 12.0); configure();
        if (f > 0) {
            gripFirm = std::max(0.0, std::min(1.0, 0.55 + 0.4 * (rnd() + 0.5) - 0.15 * gripBuzz));
            const double light = idx >= 3 ? 1.3 : 0.8;
            staticC = (1.5 + 2.5 * (rnd() + 0.5)) * (1 + f / 12.0) * light * 0.25;
            pressC = (2 + 3 * (rnd() + 0.5)) * light;
            pressR = std::exp(-1 / (0.15 * fs));
        } else { staticC = 0; pressC = 0; }
        relN = -1;
    }
    void setTune(double cents) { tuneF = std::pow(2.0, cents / 1200); setFret(fret); }
    void setAge(double a) { age = a; ageC = a * 3 * rnd(); configure(); }
    void setGrip(double damp, double buzz) { gripDamp = damp; gripBuzz = buzz; configure(); }
    void land(int f)
    {
        const bool ring = en > 1e-6 && mute != 2;
        setFret(f);
        if (ring) {
            V.scaleAll(0.3); H.scaleAll(0.3);
            noiseX = std::max(2.0, V.M * 0.03); noiseN = (int) std::lround(0.003 * fs); noiseLen = noiseN; noiseA = 0.006; noiseCol = 0.25; noiseLpS = 0;
        }
    }
    struct PullPt { double mm, gap, mag; };
    struct Pull { double mm, kneg, gap, penPrev = 0; };
    std::vector<Pull> pulls;
    /** Magnetzug aller Pickups (unabhängig vom Schalter). mm = Position vom Steg, gap = Abstand dieser Saite (mm), mag = Magnetstärke relativ zum Alnico-5-Stab */
    void setPulls(const std::vector<PullPt>& list)
    {
        const double w = std::pow(1 - idx / 7.0, 2.2); pulls.clear();
        for (auto& q : list) pulls.push_back({ q.mm, 15 * q.mag * w * std::pow(3.6 / std::max(1.0, q.gap), 3), q.gap, 0 });
    }
    void configure()
    {
        const double f = freq; const bool wound = idx < 3; const double ag = std::min(1.0, age + wear);
        double T60, T60hi;
        if (mute == 2) { T60 = 0.07; T60hi = 0.02; }
        else if (mute == 1) { T60 = 0.3; T60hi = 0.05; }
        else {
            T60 = std::max(2.5, std::min(10.0, 8 * std::sqrt(110 / f))) * (1 - 0.3 * ag) * sustainF; T60hi = (wound ? 0.28 : 0.5) * (1 - 0.6 * ag) * std::sqrt(sustainF) * brightF;
            if (gripDamp > 0) { T60 *= (1 - 0.88 * gripDamp); T60hi *= (1 - 0.7 * gripDamp); }
            if (fret > 0) { const double firm = gripFirm; T60 *= 0.93 - 0.08 * (1 - firm); T60hi *= 0.62 + 0.2 * firm; }
        }
        T60hi = std::min(T60hi, T60 * 0.9);
        muL = MU_10[idx] * gauge * gauge;
        tension = 4 * f0 * f0 * std::pow(scale / 1000, 2) * muL;
        Z = std::sqrt(tension * muL);
        const double B = B_OPEN[idx] * std::pow(2.0, 2 * fret / 12.0) * (1 + 0.3 * ag) * gauge * gauge;
        V.config(fs, f, T60, T60hi, B);
        const double Mv = fs / f / 2;
        bz1 = std::max(2.0, std::min(Mv - 2, 2 * Mv * std::pow(2.0, -1 / 12.0) - Mv));
        bz2 = std::max(2.0, std::min(Mv - 2, 2 * Mv * std::pow(2.0, -2 / 12.0) - Mv));
        bz3 = Mv * 0.5;
        mm2bK = Mv / (scale * std::pow(2.0, -fret / 12.0));
        setAction(action);
        H.config(fs, f * std::pow(2.0, (0.6 + 0.8 * ag) / 1200), T60 * 0.75, T60hi * 1.2, B);
        const double cL = wound ? 3600 + 300 * idx : 4900 + 150 * (idx - 3);
        lgF = cL / (2 * scale / 1000);
        for (int k = 0; k < 3; k++) {
            const double fk = std::min(0.45 * fs, lgF * (k + 1) * (1 + 0.004 * k)), r = std::exp(-1 / ((0.035 - 0.008 * k) * fs));
            lgC[k] = 2 * r * std::cos(2 * PI * fk / fs); lgR2[k] = r * r;
        }
    }
    void setMute(int m) { if (m != mute) { mute = m; configure(); } }
    /** Handkante: einseitiger Hautkontakt (Feder + Dämpfung gegen den Wellenwiderstand) statt Dämpfungsfaktor */
    void setPalm(double p, double q)
    {
        palmP = p; palmQ = q; Palm& pm = palm; if (p > 0.01) pm.on = true;
        pm.mm = 8 + 55 * q;
        pm.k = (600 + 2400 * p * p) / 3;
        pm.c = 2 * Z * (0.08 + 1.3 * p) / 3;
        pm.preT = p > 0.01 ? 0.08e-3 + 0.52e-3 * p : 0;
    }
    void palmStep()
    {
        Palm& pm = palm; const double M = V.M, dt = 1 / fs, Z2 = 2 * Z;
        pm.pre += (pm.preT - pm.pre) * (1 - std::exp(-dt / 0.015));
        if (pm.pre < 1e-5 && pm.preT < 1e-5) { pm.on = false; return; }
        long lastX = -1;
        for (int i = 0; i < 3; i++) {
            const double mm = pm.mm + (i - 1) * 7.0;
            const long X = (long) std::lround(std::min(M - 6, M - mm * mm2bK));
            if (X < 4 || X == lastX) { pm.dm[i] = 0; pm.dh[i] = 0; continue; }
            lastX = X;
            const double y = (V.rd((double) X) - V.rd(2 * M - X)) * UNIT;
            const double vin = (y - pm.yin[i] - pm.dm[i]) / dt; pm.yin[i] = y; pm.dm[i] = 0;
            const double pen0 = y + pm.pre;
            if (pen0 <= 0 && vin <= 0) { pm.dh[i] = 0; continue; }
            const double soft = std::max(0.03, std::min(1.0, pen0 / 0.5e-3)), kk = pm.k * soft, cc = pm.c * std::sqrt(soft);
            const double v = (vin - kk * std::max(0.0, pen0) / Z2) / (1 + (kk * dt + cc) / Z2);
            const double F = Z2 * (vin - v); if (F <= 0) continue;
            const double dmm = -F * dt / Z2; pm.dm[i] = dmm;
            V.kick((double) X, dmm / UNIT);
            const double yh = (H.rd((double) X) - H.rd(2 * M - X)) * UNIT, vh = (yh - pm.yh[i] - pm.dh[i]) / dt; pm.yh[i] = yh;
            const double ch = pm.c * 0.7 * std::min(1.0, F / 0.3);
            const double vq = vh / (1 + ch / Z2), Fh = Z2 * (vh - vq), dhh = -Fh * dt / Z2; pm.dh[i] = dhh;
            H.kick((double) X, dhh / UNIT);
        }
    }
    void setAction(double a)
    {
        action = a;
        const double base = 0.10 + 0.45 * a * a;
        clear1 = base * (1 + 0.12 * idx) * (1 - 0.35 * std::min(1.0, fret / 12.0)) * (1 - 0.75 * gripBuzz);
        clear2 = base * 2.2 * (1 + 0.1 * idx);
    }
    double mm2b(double mm) const { const double vib = scale * std::pow(2.0, -fret / 12.0); return (mm / vib) * V.M; }
    void excite(Loop& L, double X, double A, int wS, double keep)
    {
        const int N = L.N; const double M = L.M;
        X = std::max(2.0, std::min(M - 2, X));
        if (keep != 1) L.scaleAll(keep);
        const double yNow = L.rd(X) - L.rd(2 * M - X);
        A -= yNow;
        sh.assign((size_t) N, 0.0); tmp.assign((size_t) N, 0.0);
        for (int k = 0; k < N; k++) {
            const double pos = k < M ? k : 2 * M - k;
            const double y = pos < X ? A * pos / X : A * std::max(0.0, M - pos) / (M - X);
            sh[(size_t) k] = k < M ? y / 2 : -y / 2;
        }
        wS = std::max(1, wS);
        for (int p = 0; p < 3; p++) {
            double acc = 0; const int half = wS >> 1;
            for (int k = -half; k <= half; k++) acc += sh[(size_t) (((k % N) + N) % N)];
            for (int k = 0; k < N; k++) { tmp[(size_t) k] = acc / (2 * half + 1); acc += sh[(size_t) ((k + half + 1) % N)] - sh[(size_t) (((k - half) % N + N) % N)]; }
            std::swap(sh, tmp);
        }
        for (int k = 0; k < N; k++) L.buf[(size_t) ((L.w - 1 - k) & Loop::MASK)] += sh[(size_t) k];
    }
    /** dir: +1 Abschlag, -1 Aufschlag. Das Plektrum ist eine Blattfeder mit Spitzenmasse im nachgiebigen Griff, die mit
        Hand-Geschwindigkeit auf die Saite trifft und sie im Kontakt Sample für Sample gegen ihren Wellenwiderstand wegdrückt.
        Die Saite rutscht über die gekippte Kante in die Tiefe ab (zweite Schwingungsebene), bis die Eintauchtiefe überwunden ist.
        stop: die Hand bremst nach der Saite ab (Einzelton). */
    struct Finger { double k, m, mu, h, v, theta; };
    struct Geo { double vp = 0, dig = 1, ang = 0; };
    void pluck(double posMM, double vel, double hard, int dir, bool fretChanged, double scr, bool stop = false, const Finger* fing = nullptr, const Geo* geo = nullptr)
    {
        dir = dir < 0 ? -1 : 1; if (scr <= 0) scr = 1;
        const double vib = scale * std::pow(2.0, -fret / 12.0), M = V.M;
        const double r1 = rnd() + 0.5, r2 = rnd() + 0.5, r3 = rnd() + 0.5;
        const double theta = fing ? fing->theta : std::max(0.1, std::min(1.4, 0.35 + 0.6 * r1 + (geo ? geo->ang : 0)));
        const double X = std::round(std::max(2.0, std::min(M - 2, M - (posMM / vib) * M)));
        if (fretChanged) { V.scaleAll(0.08); H.scaleAll(0.08); }
        noiseX = X;
        const PickMat& mat = PICK_MATS[pickMat < 0 || pickMat > 3 ? 0 : pickMat];
        const double t = pickMM * 1e-3 * (1 + 0.5 * (hard - 0.8) * 0.4);
        const double lFree = (8 + 4 * r2) * 1e-3, w = 0.018;
        const double kP = mat.E * 1e9 * w * t * t * t / (4 * lFree * lFree * lFree);
        const double kG = (6000 + 14000 * std::min(1.2, vel)) * (0.7 + 0.6 * (rnd() + 0.5));
        double k = 1 / (1 / kP + 1 / kG);
        double mP = 0.24 * mat.rho * w * t * lFree;
        double vPick = (0.3 + 2.4 * std::min(1.2, vel)) * (0.7 + 0.6 * (rnd() + 0.5));
        if (fing) { k = fing->k; mP = fing->m; vPick = fing->v * (0.8 + 0.4 * (rnd() + 0.5)); }
        else if (geo && geo->vp > 0) vPick = geo->vp * (0.9 + 0.2 * (rnd() + 0.5));
        const double phi0 = 0.42 + 0.15 * std::max(0.0, 1 - vel) + 0.3 * std::max(0.0, scr - 1) + 0.15 * (r3 - 0.5) * 2;
        double h = (0.25e-3 + 0.95e-3 * std::pow(std::min(1.2, vel), 0.9)) * (0.75 + 0.5 * (rnd() + 0.5));
        if (fing) h = fing->h * (0.8 + 0.4 * (rnd() + 0.5)); else if (geo) h *= geo->dig;
        const double cs = std::cos(theta), sn = std::sin(theta);
        const double ys0 = ((V.rd(X) - V.rd(2 * M - X)) * cs + (H.rd(X) - H.rd(2 * M - X)) * sn) * UNIT;
        const double cG = (0.3 + 0.6 * (rnd() + 0.5)) * std::min(1.2, 0.4 + vel);
        const double zet = fing ? 0.5 : mat.zeta;
        const double cP = 2 * zet * std::sqrt(k * mP);
        Pick& P = pk; P = Pick{};
        P.on = true; P.X = X; P.cs = cs; P.sn = sn; P.dir = dir; P.base = ys0 * dir; P.xt = ys0 * dir; P.vt = vPick; P.yinPrev = ys0 * dir; P.v = vPick;
        P.k = k; P.c = cP + cG; P.cP = cP; P.m = mP; P.phi0 = phi0; P.mu = fing ? fing->mu : mat.mu; P.muA = std::atan(P.mu); P.kappa = 1.5 / lFree; P.h = h; P.r = fing ? 2e-3 : 0.35e-3;
        P.lift = (fing ? 0.4 : 0.12) * vPick; P.stop = stop; P.contact = true; P.rel = -1; P.ysPrev = ys0;
        if (idx < 3 && !fing) { const double rw[3] = { 0.17e-3, 0.14e-3, 0.11e-3 }, pt[3] = { 0.42e-3, 0.34e-3, 0.26e-3 }; P.rw = rw[idx] * std::min(1.5, 0.5 + 0.5 * scr); P.pitch = pt[idx]; }
        P.along = 0.25 + 0.5 * std::min(2.0, scr); P.ph = 2 * PI * (rnd() + 0.5);
        const bool wound = idx < 3;
        const double scrape = scr * std::exp(0.4 * (r3 - 0.5) * 2);
        const double travel = (0.4 + 2.6 * std::min(2.0, scrape)) * (0.6 + 0.8 * (rnd() + 0.5)) * 1e-3;
        const double dur = std::max(0.0008, std::min(0.012, travel / vPick));
        scrN = fing ? 0 : (int) std::lround(dur * fs);
        if (wound) {
            const double pitch = idx == 0 ? 0.42e-3 : idx == 1 ? 0.34e-3 : 0.26e-3;
            scrDt = pitch / vPick * fs;
            scrNext = scrDt * (rnd() + 0.5);
            scrA = 0.05 * vel * (0.4 + hard) * std::min(2.0, scrape) * (0.6 + 0.8 * (rnd() + 0.5)) * mat.mu / 0.35;
            scrSweep = std::pow(1.6, 1.0 / std::max(1, scrN));
        } else { scrDt = 0; scrA = 0.004 * vel * (0.4 + hard) * std::min(2.0, scrape) * mat.mu / 0.35; }
        noiseCol = 0.3 + 0.5 * (rnd() + 0.5); noiseLpS = 0; noiseN = 0;
        pendOn = false; dblN = 0; lgN = 0; lgA = 0;
        nodeN = 0;
        wear += 0.00003 * vel * (wound ? 1.4 : 0.8);
    }
    /** Reibungsrauschen des Plektrums entlang der Saite, ein Sample */
    double scrapePulse()
    {
        double n = 0;
        if (scrDt > 0) {
            scrNext -= 1;
            if (scrNext <= 0) { n = scrA * (0.7 + 0.6 * (rnd() + 0.5)) * (rnd() < 0 ? -1 : 1); scrDt *= scrSweep; scrNext += scrDt * (0.85 + 0.3 * (rnd() + 0.5)); }
            noiseLpS += (rnd() * 0.15 * scrA - noiseLpS) * 0.5; n += noiseLpS;
        } else { noiseLpS += (rnd() - noiseLpS) * noiseCol; n = noiseLpS * scrA; }
        return n;
    }
    /** Plektrum-Kontakt, ein Sample */
    void pickStep()
    {
        Pick& P = pk; const double M = V.M, X = P.X, dt = 1 / fs;
        P.age++;
        const double ysm = ((V.rd(X) - V.rd(2 * M - X)) * P.cs + (H.rd(X) - H.rd(2 * M - X)) * P.sn) * UNIT;
        P.base += P.v * dt;
        if (P.contact && P.rw > 0) { const double ph1 = P.ph + 2 * PI * P.v * P.along * dt / P.pitch; P.base += P.rw * (std::sin(ph1) - std::sin(P.ph)); P.ph = ph1; }
        if (P.rel >= 0 && P.stop) P.v *= std::exp(-dt / 0.004);
        const double yin = ysm * P.dir, vin = (yin - P.yinPrev - P.dmPrev) / dt; P.yinPrev = yin; P.dmPrev = 0;
        if (P.contact) {
            const double Z2 = 2 * Z, vb = P.v;
            const double vt = (P.m * P.vt / dt + P.k * (P.base - P.xt) + P.c * vb + Z2 * vin) / (P.m / dt + P.c + Z2 + P.k * dt);
            double F = Z2 * (vt - vin);
            if (F < 0) { F = 0; P.vt += ((P.k * (P.base - P.xt) + P.c * (vb - P.vt)) / P.m) * dt; P.xt += P.vt * dt; }
            else { P.vt = vt; P.xt += vt * dt; }
            const double d = std::max(0.0, P.base - P.xt); P.dPrev = d;
            if (scrN > 0 && F > 0) { const double n = scrapePulse(); F *= std::max(0.3, 1 + 6 * n); scrN--; }
            P.dmPrev = F * dt / Z2;
            const double dm = P.dmPrev / UNIT * P.dir;
            P.Fmax = std::max(P.Fmax, F);
            V.kick(X, dm * P.cs); H.kick(X, dm * P.sn);
            double phi = P.phi0 + P.kappa * d;
            const double edge = std::max(0.0, std::min(1.0, (P.s - (P.h - P.r)) / P.r));
            phi += (PI / 2 - phi) * edge;
            const double ex = phi - P.muA;
            const double Fd = ex > 0 ? F * std::tan(std::min(1.45, ex)) : 0;
            const double vd = Fd / (Z2 + 0.5);
            if (vd > 0) { const double dq = vd * dt / UNIT; V.kick(X, -dq * P.sn); H.kick(X, dq * P.cs); }
            P.s += (vd + P.lift) * dt;
            if (P.s >= P.h) {
                P.contact = false; P.rel = P.age;
                lgA = std::min(2.5, 0.06 * F * F) * (0.5 + 0.5 * (1 - X / M)); lgN = (int) std::lround(0.0015 * fs);
                scrN = std::min(scrN, 2);
            }
        } else {
            P.vt += ((P.k * (P.base - P.xt) + P.cP * (P.v - P.vt)) / P.m) * dt; P.xt += P.vt * dt;
            if (yin >= P.xt && yin > P.ysPrev * P.dir && P.hits < 2 && P.age - P.rel > 0.0012 * fs) {
                P.contact = true; P.hits++; P.s = 0; P.h *= 0.45; P.dPrev = std::max(0.0, P.base - P.xt); P.c = P.cP + 0.3;
                noiseN = (int) std::lround(0.0012 * fs); noiseLen = noiseN; noiseA = 0.004;
            }
            if (P.age - P.rel > 0.02 * fs) P.on = false;
        }
        P.ysPrev = ysm;
    }
    int hammer(int newFret, double vel)
    {
        const int old = fret; setFret(newFret);
        const double M = V.M, X = std::max(2.0, M * 0.02 + 2);
        excite(V, X, -0.35 * vel, (int) std::lround(M * 0.03), 0.7);
        excite(H, X, 0.05 * vel, (int) std::lround(M * 0.03), 0.7);
        noiseX = X; noiseN = (int) std::lround(0.002 * fs); noiseA = 0.02 * vel; noiseLen = noiseN;
        lgA = 0.2 * vel; lgN = (int) std::lround(0.001 * fs);
        pendOn = false;
        return old;
    }
    /** Pull-off: die Fingerkuppe (weich, schwer, griffig) zieht die Saite seitlich an und lässt sie ab */
    void pulloff(int newFret, double vel)
    {
        const int oldFret = fret;
        const double vibOld = scale * std::pow(2.0, -oldFret / 12.0);
        setFret(newFret);
        const double vib = scale * std::pow(2.0, -newFret / 12.0);
        const double posMM = vibOld - 2;
        Finger fing{ 250 + 200 * vel, 0.012, 0.8, (0.12 + 0.2 * vel) * 1e-3, 0.08 + 0.2 * vel, 1.15 + 0.3 * (rnd() + 0.5) };
        pluck(std::min(posMM, vib - 5), std::min(1.0, 0.5 + 0.6 * vel), 0.5, rnd() < 0 ? -1 : 1, false, 1, true, &fing);
        noiseN = (int) std::lround(0.002 * fs); noiseA = 0.01 * vel; noiseLen = noiseN;
    }
    void harmonic(double frac, double posMM, double vel, double hard)
    {
        pluck(posMM, vel, hard, 1, true, 1, true);
        nodeP = std::max(3.0, std::min(V.M - 3, 2 * V.M * frac));
        if (nodeP > V.M) nodeP = 2 * V.M - nodeP;
        nodeN = (int) std::lround(0.12 * fs);
    }
    void bend(double cents, double ms) { bendT = cents; bendK = 1 - std::exp(-1 / (std::max(2.0, ms) / 1000 * fs)); }
    void glide(int newFret, double ms)
    {
        const double d = (fret - newFret) * 100;
        setFret(newFret);
        bendC += d; bendT = 0; bendK = 1 - std::exp(-1 / (std::max(10.0, ms) / 1000 * fs * 0.35));
    }
    void release(bool keepRinging)
    {
        if (fret > 0) { relN = (int) std::lround(0.03 * fs); relKeep = keepRinging; pressC = -6 - 8 * (rnd() + 0.5); pressR = 1.0; }
        else if (!keepRinging) setMute(2);
    }
    double tick(double vibC)
    {
        const double L = V.Ltot;
        pressC *= pressR;
        bendC += (bendT - bendC) * bendK;
        const double cents = staticC + pressC + bendC + ageC + vibC;
        const double dc = L * (1 - std::exp(-0.00057762265 * cents)), tt = std::min(0.75, 0.06 * en * L);
        const double y = V.tick(tt + dc);
        H.tick(tt * 0.8 + dc);
        en = en * 0.999 + 0.001 * y * y;
        const double M = V.M;
        if (relN >= 0) {
            if (--relN == 0) {
                noiseX = std::max(2.0, M * 0.03); noiseN = (int) std::lround(0.0025 * fs); noiseLen = noiseN; noiseA = 0.03;
                pressC = 0; pressR = 1;
                if (relKeep) { const double e = en; setFret(0); en = e; V.scaleAll(0.5); }
                else setMute(2);
                relN = -1;
            }
        }
        // Magnetzug als echte Kraft (negative Steifigkeit, gesättigt) gegen den Wellenwiderstand – nur Vertikalebene
        if (en > 1e-7) for (auto& pl : pulls) {
            const double P = std::round(M - pl.mm * mm2bK), yp = (V.rd(P) - V.rd(2 * M - P)) * UNIT * 1000;
            const double yc = std::min(yp, 0.85 * pl.gap), F = std::min(0.5, pl.kneg * 1e-3 * yc * (1 + 1.2 * yc / pl.gap));
            V.kick(P, F / (2 * Z) / fs / UNIT);
        }
        if (nodeN > 0) {
            const double P = M - nodeP;
            const double yn = V.rd(P) - V.rd(2 * M - P), d = -0.6 * yn;
            V.add(P, d * 0.5); V.add(2 * M - P, -d * 0.5);
            const double yh = H.rd(P) - H.rd(2 * M - P), dh = -0.6 * yh;
            H.add(P, dh * 0.5); H.add(2 * M - P, -dh * 0.5);
            nodeN--;
        }
        if (en > 1e-5) {
            const double c1 = clear1, c2 = clear2;
            const double P1 = M - bz1, P2 = M - bz2, P3 = M - bz3;
            const double y1 = V.rd(P1) - V.rd(2 * M - P1), y2 = V.rd(P2) - V.rd(2 * M - P2), y3 = V.rd(P3) - V.rd(2 * M - P3);
            double hit = 0;
            if (y1 < -c1) { const double d = (-c1 - y1) * 0.85; V.add(P1, d * 0.5); V.add(2 * M - P1, -d * 0.5); hit += d; }
            if (y2 < -c1 * 1.25) { const double d = (-c1 * 1.25 - y2) * 0.85; V.add(P2, d * 0.5); V.add(2 * M - P2, -d * 0.5); hit += d; }
            if (y3 < -c2) { const double d = (-c2 - y3) * 0.7; V.add(P3, d * 0.5); V.add(2 * M - P3, -d * 0.5); hit += d; }
            buzzE = hit; if (hit > 0) buzzCount++;
        }
        if (scrN > 0 && !(pk.on && pk.contact)) {
            const double n = scrapePulse() * 0.4;
            const double hp = n - scrHP; scrHP += 0.1 * (n - scrHP);
            V.kick(noiseX, hp);
            scrN--;
        }
        if (pk.on) pickStep();
        if (palm.on) palmStep();

        // Anschlag am Polstück: harter, einseitiger Kontakt als Kraft, je Sample höchstens 60 % des Eindringens zurück (stabil)
        if (en > 1e-4) for (auto& pl : pulls) {
            const double P = std::round(M - pl.mm * mm2bK), yp = (V.rd(P) - V.rd(2 * M - P)) * UNIT * 1000, pen = yp - pl.gap;
            if (pen > 0) {
                const double F = 30000 * pen * 1e-3 + 2.0 * std::max(0.0, pen - pl.penPrev) * 1e-3 * fs;
                const double dm = std::min(F / (2 * Z) / fs, 0.6 * pen * 1e-3);
                V.kick(P, -dm / UNIT);
                poleHits++; lgA = std::max(lgA, 0.15 * F); lgN = std::max(lgN, 20);
                if (noiseN <= 0) { noiseX = P; noiseN = (int) std::lround(0.0015 * fs); noiseLen = noiseN; noiseA = 0.003 * std::min(1.0, F); noiseCol = 0.15; }
            }
            pl.penPrev = pen;
        }
        if (noiseN > 0) {
            const int len = noiseLen > 0 ? noiseLen : (int) std::lround(0.004 * fs);
            const double env = std::sin(PI * noiseN / len);
            noiseLpS += (rnd() - noiseLpS) * (noiseCol > 0 ? noiseCol : 0.5);
            const double n = noiseLpS * noiseA * env * 2;
            V.add(noiseX, n * 0.5); V.add(2 * M - noiseX, -n * 0.5);
            noiseN--;
        }
        if (dblN > 0 && --dblN == 0) {
            excite(V, noiseX, dblA, std::max(1, (int) std::lround(M * 0.02)), 1);
            noiseN = (int) std::lround(0.0015 * fs); noiseLen = noiseN; noiseA *= 0.5;
        }
        double drive = 0;
        if (lgN > 0) { drive = lgA * rnd(); lgN--; }
        double s = 0;
        for (int k = 0; k < 3; k++) { const double o = drive * lgW[k] + lgC[k] * lg[2 * k] - lgR2[k] * lg[2 * k + 1]; lg[2 * k + 1] = lg[2 * k]; lg[2 * k] = o; s += o; }
        lgSum = s;
        return y;
    }
    /** EMF eines Tonabnehmers an Position bMM vom Steg; Polstück mittelt über ±2,5 mm, Humbucker: zwei Spulen */
    double emf(int k, double bMM, bool hb)
    {
        const double vib = scale * std::pow(2.0, -fret / 12.0), M = V.M, s = M / vib;
        auto at = [&](double b) { return 0.25 * (V.disp((b - 2.5) * s) + 2 * V.disp(b * s) + V.disp((b + 2.5) * s)) + 0.18 * H.disp(b * s); };
        double y = hb ? 0.5 * (at(bMM - 9) + at(bMM + 9)) : at(bMM);
        if (y > 1.6) y = 1.6; else if (y < -1.6) y = -1.6;
        const double phi = y / (1 - 0.22 * y) + lgG * lgSum;
        const double e = phi - prevPhi[k];
        prevPhi[k] = phi;
        return e;
    }

    // Zustand (öffentlich, die Engine liest einiges davon)
    double fs = 48000, f0 = 82.407; int idx = 0;
    Loop V, H;
    int fret = 0, mute = 0; double en = 0, freq = 82.407;
    double prevPhi[4] = {};
    double scale = 648; uint32_t seed = 977; double tuneF = 1, sustainF = 1, brightF = 1, gauge = 1;
    double staticC = 0, pressC = 0, pressR = 1, action = 0.5, buzzE = 0; long buzzCount = 0;
    int noiseN = 0; double noiseX = 0, noiseA = 0; int noiseLen = 0; double noiseCol = 0.5, noiseLpS = 0; int dblN = 0; double dblA = 0;
    double bendC = 0, bendT = 0, bendK = 0.003;
    int relN = -1; bool relKeep = false;
    double nodeP = 0; int nodeN = 0;
    double age = 0, ageC = 0, wear = 0;
    long poleHits = 0;
    double lg[6] = {}, lgA = 0, lgG = 0.0002; int lgN = 0; double lgC[3] = {}, lgR2[3] = {}; const double lgW[3] = { 1, 0.45, 0.2 }; double lgF = 4000, lgSum = 0;
    int scrN = 0; double scrNext = 0, scrDt = 0, scrA = 0, scrSweep = 1, scrHP = 0;
    struct Pend { double X, A, AH; int wS; double keep; int n; } pend{}; bool pendOn = false;
    struct Pick { bool on = false, contact = false, stop = false; double X = 0, cs = 1, sn = 0; int dir = 1; double base = 0, xt = 0, vt = 0, yinPrev = 0, dmPrev = 0, v = 0, k = 0, c = 0, cP = 0, m = 1e-5, phi0 = 0.4, mu = 0.35, muA = 0.34, kappa = 150, h = 1e-3, r = 0.35e-3, s = 0, lift = 0, rw = 0, pitch = 1, along = 0.5, ph = 0, dPrev = 0, Fmax = 0, ysPrev = 0; int age = 0, rel = -1, hits = 0; } pk;
    int pickMat = 0; double pickMM = 0.88;
    double muL = 1e-3, tension = 70, Z = 0.3;
    double gripDamp = 0, gripBuzz = 0, gripFirm = 0.7, palmP = 0, palmQ = 0.3;
    struct Palm { bool on = false; double mm = 25, k = 1000, c = 2, pre = 0, preT = 0; double yin[3] = {}, dm[3] = {}, yh[3] = {}, dh[3] = {}; } palm;
    double clear1 = 0.2, clear2 = 0.4, bz1 = 10, bz2 = 10, bz3 = 10, mm2bK = 1;
private:
    std::vector<double> sh, tmp;
};

} // namespace rig

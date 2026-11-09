// PMG Electric – 10"-Gitarrenlautsprecher (elektromechanisch) und Endstufe (Port von Wheelhouse RotaryAmp, Modell 122, HQ)
#pragma once
#include "Tubes.h"

namespace rig {

class Speaker10 {
public:
    struct Params { double Re = 6.4, Le = 0.45e-3, L2 = 0.9e-3, R2 = 6.0, Bl = 7.8, Mms = 0.0145, Fs = 95, Qms = 1.4, Sd = 0.034,
        xb = 0.0035, xk = 0.003, xo = 0.0003, Vbox = 0.032, Rth = 4.0, tau = 6.0; };
    Params P;
    Speaker10() = default;
    Speaker10(double h_, bool closedBox) { init(h_, closedBox); }
    void init(double h_, bool closedBox) { h = h_; x.fill(0); vPrev = 0; T = 0; i2 = 0; nI = 0; dRe = 0; w.fill(0); setClosed(closedBox); }
    void setClosed(bool c)
    {
        closed = c;
        const int n = 4;
        const double Kms_ = P.Mms * std::pow(2 * PI * P.Fs, 2), Rms = 2 * PI * P.Fs * P.Mms / P.Qms;
        const double Kbox = c ? 1.18 * 343 * 343 * P.Sd * P.Sd / P.Vbox : 0;
        Kms = Kms_;
        double A[4][4] = { { -(P.Re + P.R2) / P.Le, P.R2 / P.Le, -P.Bl / P.Le, 0 }, { P.R2 / P.L2, -P.R2 / P.L2, 0, 0 },
            { P.Bl / P.Mms, 0, -Rms / P.Mms, -(Kms + Kbox) / P.Mms }, { 0, 0, 1, 0 } };
        double B[4] = { 1 / P.Le, 0, 0, 0 };
        double E[4][2] = { { 1 / P.Le, 0 }, { 0, 0 }, { 0, 1 / P.Mms }, { 0, 0 } };
        double M[4][8];
        for (int i = 0; i < n; i++) { for (int k = 0; k < 8; k++) M[i][k] = 0; for (int j = 0; j < n; j++) M[i][j] = (i == j ? 1 : 0) - 0.5 * h * A[i][j]; M[i][n + i] = 1; }
        for (int col = 0; col < n; col++) {
            int piv = col; for (int r = col + 1; r < n; r++) if (std::abs(M[r][col]) > std::abs(M[piv][col])) piv = r;
            if (piv != col) for (int k = 0; k < 8; k++) std::swap(M[col][k], M[piv][k]);
            const double d = M[col][col]; for (int k = 0; k < 8; k++) M[col][k] /= d;
            for (int r = 0; r < n; r++) { if (r == col) continue; const double f = M[r][col]; if (f != 0) for (int k = 0; k < 8; k++) M[r][k] -= f * M[col][k]; }
        }
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) { double s = 0; for (int k = 0; k < n; k++) s += M[i][n + k] * ((k == j ? 1 : 0) + 0.5 * h * A[k][j]); Ad[i * 4 + j] = s; }
            double sb = 0; for (int k = 0; k < n; k++) sb += M[i][n + k] * B[k]; Bd[i] = 0.5 * h * sb;
            for (int ww = 0; ww < 2; ww++) { double se = 0; for (int k = 0; k < n; k++) se += M[i][n + k] * E[k][ww]; Ed[i * 2 + ww] = h * se; }
        }
        g = Bd[0];
    }
    void corrections()
    {
        const double xp = x[3], i = x[0], u = x[2];
        const double r = (xp - P.xo) / P.xb, Bl0 = P.Bl;
        const double dBl = Bl0 * (1 + (P.xo / P.xb) * (P.xo / P.xb)) / (1 + r * r) - Bl0;
        const double q = xp / P.xk;
        w[0] = -dBl * u - dRe * i;
        w[1] = dBl * i - Kms * q * q * xp;
    }
    /** Norton-Ersatz an den 8-Ohm-Klemmen: i = G v + Ih */
    double companion()
    {
        corrections();
        Ih = g * vPrev + Ad[0] * x[0] + Ad[1] * x[1] + Ad[2] * x[2] + Ad[3] * x[3] + Ed[0] * w[0] + Ed[1] * w[1];
        return g;
    }
    void step(double v)
    {
        const double vs = vPrev + v;
        const double y0 = Bd[0] * vs + Ad[0] * x[0] + Ad[1] * x[1] + Ad[2] * x[2] + Ad[3] * x[3] + Ed[0] * w[0] + Ed[1] * w[1];
        const double y1 = Bd[1] * vs + Ad[4] * x[0] + Ad[5] * x[1] + Ad[6] * x[2] + Ad[7] * x[3] + Ed[2] * w[0] + Ed[3] * w[1];
        const double y2 = Bd[2] * vs + Ad[8] * x[0] + Ad[9] * x[1] + Ad[10] * x[2] + Ad[11] * x[3] + Ed[4] * w[0] + Ed[5] * w[1];
        const double y3 = Bd[3] * vs + Ad[12] * x[0] + Ad[13] * x[1] + Ad[14] * x[2] + Ad[15] * x[3] + Ed[6] * w[0] + Ed[7] * w[1];
        x[0] = y0; x[1] = y1; x[2] = y2; x[3] = y3; vPrev = v;
        i2 += y0 * y0; nI++;
    }
    /** pro Basis-Sample: Schwingspulen-Erwärmung (Leistungskompression) */
    void thermal(double dt)
    {
        if (nI == 0) return;
        const double ii = i2 / nI; i2 = 0; nI = 0;
        const double pw = ii * P.Re * (1 + 0.00393 * T);
        T += dt * (pw * P.Rth - T) / P.tau;
        dRe = P.Re * 0.00393 * T;
    }
    void reset() { x.fill(0); vPrev = 0; T = 0; dRe = 0; i2 = 0; nI = 0; }

    std::array<double, 4> x{}; double vPrev = 0, T = 0, Ih = 0, Kms = 0, g = 0, dRe = 0;
    bool closed = false;
private:
    double h = 1, i2 = 0; int nI = 0;
    std::array<double, 2> w{};
    double Ad[16] = {}, Bd[4] = {}, Ed[8] = {};
};

class PowerAmp {
public:
    PowerAmp() = default;
    PowerAmp(double h_, Speaker10* s) { init(h_, s); }
    void init(double h_, Speaker10* s)
    {
        h = h_; spk = s;
        double ws = 0; for (int j = 1; j <= 6; j++) { playR[j - 1] = 0.07 * j / 6; playW[j - 1] = j; ws += j; }
        for (int j = 0; j < 6; j++) playW[j] /= ws;
        fluxK = 0.8 * bs * 2 * PI * 35 / 700;
        imScale = 1 / (fluxK * 25.0);
        nTurns = std::sqrt(Raa / 8.0);
        applySupply();
        reset();
    }
    void applySupply() { V0 = 438; Rs = 55 * (0.5 + 1.5 * sag); RsEff = Rs + 60; }
    void setSag(double s) { sag = std::max(0.0, std::min(1.0, s)); applySupply(); }
    void reset()
    {
        Vb = V0 - 0.2 * (V0 - 415); Vko = 25; Vkd = 4;
        drvVp[0] = drvVp[1] = 140; ccQ[0] = ccQ[1] = 140; vPrev = 0; phi = 0; play.fill(0);
    }
    void settle(double seconds)
    {
        const long steps = (long) std::floor(seconds / h);
        for (long i = 0; i < steps; i++) tick(0);
        const TriodeOut t = triode(T_12AU7, -Vkd, drvVp[0] - Vkd);
        drvRout = 1 / (1 / drvRa + std::max(t.gp, 1e-7));
    }
    void updateDrvRout() { const TriodeOut t = triode(T_12AU7, -Vkd, drvVp[0] - Vkd); drvRout = 1 / (1 / drvRa + std::max(t.gp, 1e-7)); }
    double coreH(double p, bool commit)
    {
        double bh = 0;
        for (int j = 0; j < 6; j++) {
            const double r = playR[j]; double y = play[j];
            if (p - y > r) y = p - r; else if (y - p > r) y = p + r;
            if (commit) play[j] = y;
            bh += playW[j] * y;
        }
        const double b = (1 - hystW) * p + hystW * bh;
        const double q = std::min(0.96, b * b / (bs * bs));
        return b / (1 - q);
    }
    /** u: Volt am Treibergitter (symmetrisch ±u) */
    double tick(double u)
    {
        const Pentode& P = P_6550;
        for (int j = 0; j < 2; j++) {
            const double Vgk = (j == 0 ? u : -u) - Vkd;
            double Vp = drvVp[j]; TriodeOut t;
            for (int it = 0; it < 3; it++) {
                t = triode(T_12AU7, Vgk, Vp - Vkd);
                const double d = (Vp - Vbd + drvRa * t.ip) / (1 + drvRa * t.gp);
                Vp -= d;
                if (Vp < Vkd + 1) Vp = Vkd + 1; else if (Vp > Vbd) Vp = Vbd;
                if (std::abs(d) < TOL_V) break;
            }
            drvVp[j] = Vp; ipd[j] = t.ip;
        }
        for (int j = 0; j < 2; j++) {
            const double Req = drvRout + h / Cc, src = drvVp[j] - ccQ[j];
            double Vgj = src * Rg / (Rg + Req), igj = 0;
            if (Vgj - Vko > P.vth - 12 * P.w) {
                for (int it = 0; it < 8; it++) {
                    const GridCur gc = gridCurrent(P.rgi, P.vth, P.w, Vgj - Vko);
                    const double d = ((src - Vgj) / Req - Vgj / Rg - gc.ig) / (-1 / Req - 1 / Rg - gc.dig);
                    Vgj -= d;
                    if (std::abs(d) < TOL_V) break;
                }
                igj = gridCurrent(P.rgi, P.vth, P.w, Vgj - Vko).ig;
            }
            ccQ[j] += h * ((src - Vgj) / Req) / Cc;
            Vg[j] = Vgj; ig[j] = igj;
        }
        const double Vg2k = Vscreen - Vko;
        const double a0 = pentGrid(P, Vg[0] - Vko, Vg2k, kg[0]).a;
        const double a1 = pentGrid(P, Vg[1] - Vko, Vg2k, kg[1]).a;
        const double Im = coreH(phi, false) * imScale;
        const double n = nTurns, Gs = spk->companion();
        const double G = Gs / (n * n), Ih = spk->Ih / n;
        double v = vPrev, ip0 = 0, ip1 = 0;
        for (int it = 0; it < 5; it++) {
            const double vp0 = Vb - v - Vko, vp1 = Vb + v - Vko;
            ip0 = a0 * pentAtan(P, vp0); ip1 = a1 * pentAtan(P, vp1);
            const double d0 = -a0 * pentAtanD(P, vp0), d1 = a1 * pentAtanD(P, vp1);
            const double F = 0.5 * (ip0 - ip1) - (2 * v * G + Ih) - Im, dF = 0.5 * (d0 - d1) - 2 * G;
            const double d = F / dF;
            v -= d;
            const double vmax = Vb - Vko - 2;
            if (v > vmax) v = vmax; else if (v < -vmax) v = -vmax;
            if (std::abs(d) < TOL_OUT_V) break;
        }
        vPrev = v; lastIp[0] = ip0; lastIp[1] = ip1;
        spk->step(2 * v / n);
        phi += h * 2 * v * fluxK;
        const double lim = bs * 0.98;
        if (phi > lim) phi = lim; else if (phi < -lim) phi = -lim;
        coreH(phi, true);
        const double is0 = screenCurrent(P, Vg[0] - Vko, Vg2k, kg[0]), is1 = screenCurrent(P, Vg[1] - Vko, Vg2k, kg[1]);
        const double Ik = ip0 + ip1 + is0 + is1 + ig[0] + ig[1];
        Vko += h * (Ik - Vko / Rko) / Cko;
        Vkd += h * (ipd[0] + ipd[1] - Vkd / Rkd) / Ckd;
        Vb += h * ((V0 - Vb) / RsEff - (ip0 + ip1) - Ireg) / C2;
        return 2 * v;
    }

    double h = 1; Speaker10* spk = nullptr;
    double drvRa = 47e3, Rkd = 1500, Ckd = 25e-6, Vbd = 260, drvRout = 8000;
    double Cc = 0.1e-6, Rg = 100e3, Vscreen = 310, Rko = 150, Cko = 50e-6, Raa = 4300, Ireg = 0.012;
    double C2 = 40e-6, sag = 0.5, V0 = 438, Rs = 0, RsEff = 0;
    double kg[2] = { 1.0, 1.045 }, bs = 1.5, hystW = 0.45;
    double playR[6] = {}, playW[6] = {}; std::array<double, 6> play{};
    double fluxK = 0, imScale = 0, nTurns = 1;
    double drvVp[2] = { 140, 140 }, ccQ[2] = { 140, 140 }, lastIp[2] = { 0, 0 }, Vg[2] = { 0, 0 }, ig[2] = { 0, 0 }, ipd[2] = { 0, 0 };
    double Vb = 415, Vko = 25, Vkd = 4, vPrev = 0, phi = 0;
};

} // namespace rig

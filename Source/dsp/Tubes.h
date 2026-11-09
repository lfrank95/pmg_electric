// PMG Electric – Röhrenmodelle (Koren), Oversampling (Kaiser-Sinc) und lineare Schaltungen (Knotenanalyse mit
// Trapez-Begleitmodellen). 1:1-Port der JavaScript-Engine (dsp.js); Zahlenwerte und Reihenfolgen sind bewusst gleich,
// damit die C++-Version gegen die Browser-Version verglichen werden kann.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <array>

namespace rig {

constexpr double PI = 3.14159265358979323846;
constexpr double TOL_V = 3e-3, TOL_OUT_V = 3e-2;

struct Triode { double mu, ex, kg1, kp, kvb, rgi, vth, w; };
struct Pentode { double mu, ex, kg1, kg2, kp, kvb, rgi, vth, w; };

inline const Triode T_12AX7{ 100, 1.4, 1060, 600, 300, 2000, -0.2, 0.06 };
inline const Triode T_12AU7{ 21.5, 1.3, 1180, 84, 300, 2000, -0.7, 0.12 };
inline const Pentode P_6550{ 7.9, 1.35, 890, 4200, 60, 24, 1000, -0.7, 0.18 };

struct TriodeOut { double ip = 0, gp = 0, gm = 0; };
inline TriodeOut triode(const Triode& T, double Vgk, double Vpk)
{
    TriodeOut o;
    if (Vpk < 1e-3) Vpk = 1e-3;
    const double s = std::sqrt(T.kvb + Vpk * Vpk);
    const double x = T.kp * (1.0 / T.mu + Vgk / s);
    double L, sig;
    if (x > 40) { L = x; sig = 1; } else if (x < -40) { L = std::exp(x); sig = L; }
    else { const double e = std::exp(x); L = std::log1p(e); sig = e / (1 + e); }
    const double E1 = (Vpk / T.kp) * L;
    if (E1 <= 1e-12) return o;
    const double p = std::exp((T.ex - 1) * std::log(E1));
    o.ip = 2 * p * E1 / T.kg1;
    const double k = 2 * T.ex * p / T.kg1;
    o.gp = k * (L / T.kp - Vpk * sig * Vgk * Vpk / (s * s * s));
    o.gm = k * Vpk * sig / s;
    return o;
}

struct PentGrid { double a = 0, da = 0; };
inline PentGrid pentGrid(const Pentode& P, double Vg1k, double Vg2k, double kgScale)
{
    PentGrid o;
    if (Vg2k < 1) Vg2k = 1;
    const double x = P.kp * (1.0 / P.mu + Vg1k / Vg2k);
    double L, sig;
    if (x > 40) { L = x; sig = 1; } else if (x < -40) { L = std::exp(x); sig = L; }
    else { const double e = std::exp(x); L = std::log1p(e); sig = e / (1 + e); }
    const double E1 = (Vg2k / P.kp) * L;
    if (E1 <= 1e-12) return o;
    const double e = std::exp(P.ex * std::log(E1));
    const double kg = P.kg1 * kgScale;
    o.a = 2 * e / kg; o.da = 2 * P.ex * e / E1 / kg * sig;
    return o;
}
inline double pentAtan(const Pentode& P, double Vpk) { return std::atan(std::max(Vpk, 0.5) / P.kvb); }
inline double pentAtanD(const Pentode& P, double Vpk) { const double x = std::max(Vpk, 0.5) / P.kvb; return (1.0 / P.kvb) / (1 + x * x); }
inline double screenCurrent(const Pentode& P, double Vg1k, double Vg2k, double kgScale)
{
    const double x = Vg1k + Vg2k / P.mu;
    return x > 0 ? std::exp(P.ex * std::log(x)) / (P.kg2 * kgScale) : 0;
}
struct GridCur { double ig = 0, dig = 0; };
inline GridCur gridCurrent(double rgi, double vth, double w, double Vgk)
{
    GridCur o;
    const double xg = (Vgk - vth) / w;
    if (xg > 30) { o.ig = (Vgk - vth) / rgi; o.dig = 1 / rgi; return o; }
    if (xg < -25) return o;
    const double e = std::exp(xg);
    o.ig = w * std::log1p(e) / rgi; o.dig = (e / (1 + e)) / rgi;
    return o;
}

// ---------------- Oversampling ----------------
inline double besselI0(double x) { double s = 1, t = 1; for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }

class Oversampler {
public:
    explicit Oversampler(int factor = 1) { init(factor); }
    void init(int factor)
    {
        os = factor;
        if (factor == 1) return;
        taps = 32 * os + 1; ph = (taps + os - 1) / os;
        std::vector<double> h((size_t) ph * os, 0.0);
        const double m = (taps - 1) / 2.0, beta = 8; double sum = 0;
        for (int i = 0; i < taps; i++) {
            const double n = (i - m) / os, sinc = n == 0 ? 1 : std::sin(PI * n) / (PI * n);
            const double r = 2.0 * i / (taps - 1) - 1;
            h[(size_t) i] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / besselI0(beta);
            sum += h[(size_t) i];
        }
        for (auto& v : h) v *= os / sum;
        ringUp.assign((size_t) ph * 2, 0.0); ringDown.assign((size_t) taps * 2, 0.0); posUp = 0; posDown = 0;
        hUpRev.assign((size_t) os * ph, 0.0); hDownRev.assign((size_t) taps, 0.0);
        for (int k = 0; k < os; k++) for (int j = 0; j < ph; j++) hUpRev[(size_t) (k * ph + j)] = h[(size_t) (k + (ph - 1 - j) * os)];
        for (int j = 0; j < taps; j++) hDownRev[(size_t) j] = h[(size_t) (taps - 1 - j)];
    }
    void upsample(double x, double* out)
    {
        if (os == 1) { out[0] = x; return; }
        ringUp[(size_t) posUp] = x; ringUp[(size_t) (posUp + ph)] = x;
        const int off = posUp + 1;
        for (int k = 0; k < os; k++) {
            double s = 0; const int hk = k * ph;
            for (int j = 0; j < ph; j++) s += hUpRev[(size_t) (hk + j)] * ringUp[(size_t) (off + j)];
            out[k] = s;
        }
        posUp = posUp == ph - 1 ? 0 : posUp + 1;
    }
    double downsample(const double* inp)
    {
        if (os == 1) return inp[0];
        double y = 0;
        for (int k = 0; k < os; k++) {
            const int p = posDown;
            ringDown[(size_t) p] = inp[k]; ringDown[(size_t) (p + taps)] = inp[k];
            if (k == 0) { double s = 0; const int off = p + 1; for (int j = 0; j < taps; j++) s += hDownRev[(size_t) j] * ringDown[(size_t) (off + j)]; y = s / os; }
            posDown = p == taps - 1 ? 0 : p + 1;
        }
        return y;
    }
    int os = 1;
private:
    int taps = 0, ph = 0, posUp = 0, posDown = 0;
    std::vector<double> ringUp, ringDown, hUpRev, hDownRev;
};

// ---------------- Lineare Schaltungen ----------------
class Circuit {
public:
    struct El { int t; int a, b; double val; double g = 0, i = 0, vp = 0, J = 0; };
    Circuit() = default;
    Circuit(int nodes, double h) { reset(nodes, h); }
    void reset(int nodes, double h_) { n = nodes; h = h_; el.clear(); src.clear(); dyn.clear(); v.assign((size_t) n, 0.0); }
    void R(int a, int b, double r) { el.push_back({ 0, a, b, r }); }
    void C(int a, int b, double c) { el.push_back({ 1, a, b, c }); }
    void L(int a, int b, double l) { el.push_back({ 2, a, b, l }); }
    /** Spannungsquelle mit Innenwiderstand r gegen Masse an Knoten a (Norton) */
    void S(int a, double r) { el.push_back({ 3, a, -1, r }); src.push_back((int) el.size() - 1); }
    void build()
    {
        std::vector<double> G((size_t) n * n, 0.0);
        auto stamp = [&](int a, int b, double g) {
            if (a >= 0) G[(size_t) (a * n + a)] += g;
            if (b >= 0) G[(size_t) (b * n + b)] += g;
            if (a >= 0 && b >= 0) { G[(size_t) (a * n + b)] -= g; G[(size_t) (b * n + a)] -= g; }
        };
        for (auto& e : el) {
            if (e.t == 0 || e.t == 3) e.g = 1 / std::max(e.val, 1e-3);
            else if (e.t == 1) e.g = 2 * e.val / h;
            else e.g = h / (2 * e.val);
            stamp(e.a, e.b, e.g);
        }
        const int n2 = 2 * n;
        std::vector<double> M((size_t) n * n2, 0.0);
        for (int i = 0; i < n; i++) { for (int j = 0; j < n; j++) M[(size_t) (i * n2 + j)] = G[(size_t) (i * n + j)]; M[(size_t) (i * n2 + n + i)] = 1; }
        for (int c = 0; c < n; c++) {
            int piv = c;
            for (int r = c + 1; r < n; r++) if (std::abs(M[(size_t) (r * n2 + c)]) > std::abs(M[(size_t) (piv * n2 + c)])) piv = r;
            if (piv != c) for (int k = 0; k < n2; k++) std::swap(M[(size_t) (c * n2 + k)], M[(size_t) (piv * n2 + k)]);
            const double d = M[(size_t) (c * n2 + c)];
            for (int k = 0; k < n2; k++) M[(size_t) (c * n2 + k)] /= d;
            for (int r = 0; r < n; r++) {
                if (r == c) continue;
                const double f = M[(size_t) (r * n2 + c)];
                if (f != 0) for (int k = 0; k < n2; k++) M[(size_t) (r * n2 + k)] -= f * M[(size_t) (c * n2 + k)];
            }
        }
        inv.assign((size_t) n * n, 0.0);
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) inv[(size_t) (i * n + j)] = M[(size_t) (i * n2 + n + j)];
        rhs.assign((size_t) n, 0.0);
        dyn.clear(); for (int k = 0; k < (int) el.size(); k++) if (el[(size_t) k].t == 1 || el[(size_t) k].t == 2) dyn.push_back(k);
    }
    /** Zustände (Kondensatoren/Spulen) aus einer anderen Instanz gleicher Topologie übernehmen */
    void adoptStates(const Circuit& o)
    {
        if (o.dyn.size() != dyn.size()) return;
        for (size_t k = 0; k < dyn.size(); k++) { el[(size_t) dyn[k]].i = o.el[(size_t) o.dyn[k]].i; el[(size_t) dyn[k]].vp = o.el[(size_t) o.dyn[k]].vp; }
    }
    void step(double E0, double E1 = 0)
    {
        std::fill(rhs.begin(), rhs.end(), 0.0);
        if (src.size() > 0) { auto& s0 = el[(size_t) src[0]]; rhs[(size_t) s0.a] += E0 * s0.g; }
        if (src.size() > 1) { auto& s1 = el[(size_t) src[1]]; rhs[(size_t) s1.a] += E1 * s1.g; }
        for (int k : dyn) {
            auto& e = el[(size_t) k];
            if (e.t == 1) { e.J = e.g * e.vp + e.i; if (e.a >= 0) rhs[(size_t) e.a] += e.J; if (e.b >= 0) rhs[(size_t) e.b] -= e.J; }
            else { e.J = e.i + e.g * e.vp; if (e.a >= 0) rhs[(size_t) e.a] -= e.J; if (e.b >= 0) rhs[(size_t) e.b] += e.J; }
        }
        for (int i = 0; i < n; i++) { double a = 0; const int o = i * n; for (int j = 0; j < n; j++) a += inv[(size_t) (o + j)] * rhs[(size_t) j]; v[(size_t) i] = a; }
        for (int k : dyn) {
            auto& e = el[(size_t) k];
            const double vv = (e.a >= 0 ? v[(size_t) e.a] : 0) - (e.b >= 0 ? v[(size_t) e.b] : 0);
            e.i = e.t == 1 ? e.g * vv - e.J : e.g * vv + e.J;
            e.vp = vv;
        }
    }
    std::vector<double> v;
private:
    int n = 0; double h = 1;
    std::vector<El> el; std::vector<int> src, dyn;
    std::vector<double> inv, rhs;
};

// ---------------- Vorstufe ----------------
class TriodeStage {
public:
    TriodeStage() = default;
    TriodeStage(Triode T_, double Ra_, double Rk_, double Ck_, double B_, double h_) : T(T_), Ra(Ra_), Rk(Rk_), Ck(Ck_), B(B_), h(h_) { Vp = B * 0.6; Vk = 1; ip = 0.5e-3; rout = Ra * 0.4; }
    double tick(double vg)
    {
        if (Ck > 0) {
            const double Vk0 = Vk, Vgk = vg - Vk0;
            double Vp1 = Vp; TriodeOut t;
            for (int it = 0; it < 5; it++) {
                t = triode(T, Vgk, Vp1 - Vk0);
                const double d = (Vp1 - B + Ra * t.ip) / (1 + Ra * t.gp);
                Vp1 -= d;
                if (Vp1 < Vk0 + 1) Vp1 = Vk0 + 1; else if (Vp1 > B) Vp1 = B;
                if (std::abs(d) < TOL_V) break;
            }
            t = triode(T, Vgk, Vp1 - Vk0);
            Vp = Vp1;
            Vk = (Vk0 + h * t.ip / Ck) / (1 + h / (Rk * Ck));
            rout = 1 / (1 / Ra + std::max(t.gp, 1e-7));
        } else {
            double ip1 = ip; TriodeOut t;
            for (int it = 0; it < 6; it++) {
                t = triode(T, vg - Rk * ip1, std::max(B - (Ra + Rk) * ip1, 1e-3));
                const double F = ip1 - t.ip, dF = 1 + t.gm * Rk + t.gp * (Ra + Rk);
                const double d = F / dF;
                ip1 -= d;
                if (ip1 < 0) ip1 = 0; else if (ip1 > B / (Ra + Rk)) ip1 = B / (Ra + Rk);
                if (std::abs(d) * Ra < TOL_V) break;
            }
            ip = ip1; Vp = B - Ra * ip1; Vk = Rk * ip1;
            const double gp = std::max(t.gp, 1e-7), rp = 1 / gp, mu = t.gm / gp;
            rout = 1 / (1 / Ra + 1 / (rp + (mu + 1) * Rk));
        }
        return Vp;
    }
    Triode T{}; double Ra = 100e3, Rk = 1500, Ck = 0, B = 300, h = 1;
    double Vp = 180, Vk = 1, ip = 0.5e-3, rout = 40e3;
};

/** Koppelkondensator → Gitterableitwiderstand, optional mit Gitterstrom der folgenden Röhre (Blocking) */
class Coupling {
public:
    Coupling() = default;
    Coupling(double Cc_, double Rg_, const Triode* T_, double h_) : Cc(Cc_), Rg(Rg_), T(T_), h(h_) {}
    double tick(double vpoc, double rout, double VkNext)
    {
        const double Req = rout + h / Cc, src = vpoc - q;
        double Vg = src * Rg / (Rg + Req);
        if (T && Vg - VkNext > T->vth - 12 * T->w) {
            for (int it = 0; it < 8; it++) {
                const GridCur g = gridCurrent(T->rgi, T->vth, T->w, Vg - VkNext);
                const double d = ((src - Vg) / Req - Vg / Rg - g.ig) / (-1 / Req - 1 / Rg - g.dig);
                Vg -= d;
                if (std::abs(d) < TOL_V) break;
            }
        }
        q += h * ((src - Vg) / Req) / Cc;
        return Vg;
    }
    double Cc = 22e-9, Rg = 1e6; const Triode* T = nullptr; double h = 1; double q = 150;
};

} // namespace rig

// PMG Electric – Kommandozeilen-Test: MIDI-Datei → WAV (ohne JUCE), zum Vergleich mit der JS-Engine.
// g++ -O2 -std=c++17 -I. tools/render_test.cpp -o render_test && ./render_test in.mid out.wav [take] [gain]
#include "Source/play/Strummer.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace rig;

struct MEv { double t; int st, ch, d1, d2; };
struct Smf { std::vector<MEv> ev; std::vector<TempoPt> tempos; std::vector<SigPt> sigs; };

static Smf parseMidi(const std::vector<unsigned char>& d)
{
    size_t p = 0;
    auto u32 = [&]() { uint32_t v = (uint32_t) d[p] << 24 | (uint32_t) d[p + 1] << 16 | (uint32_t) d[p + 2] << 8 | d[p + 3]; p += 4; return v; };
    auto u16 = [&]() { int v = d[p] << 8 | d[p + 1]; p += 2; return v; };
    if (std::string(d.begin(), d.begin() + 4) != "MThd") throw std::runtime_error("keine MIDI-Datei");
    p = 4; const uint32_t hl = u32(); u16(); const int ntr = u16(), div = u16(); p += hl - 6;
    struct Raw { long tick; int st, ch, d1, d2; int meta = 0; long us = 0; int num = 4, den = 4; };
    std::vector<Raw> all;
    for (int tr = 0; tr < ntr; tr++) {
        if (std::string(d.begin() + (long) p, d.begin() + (long) p + 4) != "MTrk") throw std::runtime_error("Track erwartet");
        p += 4; const uint32_t len = u32(); const size_t end = p + len; long tick = 0; int run = 0;
        auto vlq = [&]() { long v = 0; unsigned b; do { b = d[p++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80); return v; };
        while (p < end) {
            tick += vlq(); int st = d[p];
            if (st < 0x80) st = run; else { p++; if (st < 0xf0) run = st; }
            if (st == 0xff) { const int type = d[p++]; const long l = vlq(); if (type == 0x51) all.push_back({ tick, 0, 0, 0, 0, 1, (long) (d[p] << 16 | d[p + 1] << 8 | d[p + 2]) }); else if (type == 0x58) { Raw r{ tick, 0, 0, 0, 0, 2 }; r.num = d[p]; r.den = 1 << d[p + 1]; all.push_back(r); } p += (size_t) l; }
            else if (st == 0xf0 || st == 0xf7) { const long l = vlq(); p += (size_t) l; }
            else { const int hi = st & 0xf0, ch = (st & 0x0f) + 1; const int d1 = d[p++]; const int d2 = (hi == 0xc0 || hi == 0xd0) ? 0 : d[p++]; all.push_back({ tick, hi, ch, d1, d2 }); }
        }
        p = end;
    }
    std::stable_sort(all.begin(), all.end(), [](const Raw& a, const Raw& b) { return a.tick < b.tick; });
    std::vector<Raw> tempos; for (auto& e : all) if (e.meta == 1) tempos.push_back(e);
    if (tempos.empty() || tempos[0].tick > 0) tempos.insert(tempos.begin(), Raw{ 0, 0, 0, 0, 0, 1, 500000 });
    auto toSec = [&](long tick) { double t = 0; long lastTick = 0; long us = 500000; for (auto& tp : tempos) { if (tp.tick > tick) break; t += (double) (tp.tick - lastTick) / div * us / 1e6; lastTick = tp.tick; us = tp.us; } return t + (double) (tick - lastTick) / div * us / 1e6; };
    Smf out;
    for (auto& e : all) if (!e.meta) out.ev.push_back({ toSec(e.tick), e.st, e.ch, e.d1, e.d2 });
    for (auto& e : all) if (e.meta == 2) out.sigs.push_back({ toSec(e.tick), e.num, e.den });
    if (out.sigs.empty() || out.sigs[0].t > 0) out.sigs.insert(out.sigs.begin(), { 0, 4, 4 });
    for (auto& tp : tempos) out.tempos.push_back({ toSec(tp.tick), 60e6 / tp.us });
    return out;
}

static void writeWav(const char* path, const std::vector<float>& L, const std::vector<float>& R, int fs)
{
    FILE* f = fopen(path, "wb"); const uint32_t n = (uint32_t) L.size(), dataB = n * 4, ch = 2;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + dataB); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16((uint16_t) ch); w32((uint32_t) fs); w32((uint32_t) fs * ch * 2); w16((uint16_t) (ch * 2)); w16(16);
    fwrite("data", 1, 4, f); w32(dataB);
    for (uint32_t i = 0; i < n; i++) { int16_t a = (int16_t) std::lround(std::max(-1.f, std::min(1.f, L[i])) * 32767), b = (int16_t) std::lround(std::max(-1.f, std::min(1.f, R[i])) * 32767); fwrite(&a, 2, 1, f); fwrite(&b, 2, 1, f); }
    fclose(f);
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "render_test in.mid out.wav [take] [gain]\n"); return 1; }
    std::ifstream in(argv[1], std::ios::binary); std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    Smf smf = parseMidi(bytes);
    const int take = argc > 3 ? atoi(argv[3]) : 1; const double gain = argc > 4 ? atof(argv[4]) : 0.45;
    const double fs = 48000, offset = getenv("RIG_OFFSET") ? atof(getenv("RIG_OFFSET")) : 0.3; const int os = 2;
    Clock clock = makeClock(smf.tempos, smf.sigs);
    AccentGrid grid;
    std::vector<float> chan[2];
    for (int side = 0; side < 2; side++) {
        Engine E; E.p.gain = gain; E.p.master = 0.45; E.init(fs, os); E.setSeed(side == 0 ? 101 : 202);
        Hand H(side == 0 ? 101 : 202); H.take = take; H.clock = [&](double t) { return clock(t - offset); }; H.human = 0.6;
        MidiStrummer S(H); S.clock = H.clock; S.grid = &grid;
        // Test-Slots wie in ch2test.js: Slot 1 = Am fest, Slot 2 = F (Griff je nach Lage)
        S.slots[0].name = "Am"; S.slots[0].fr.f = { 0, 0, 2, 2, 1, 0 }; S.slots[0].hasFr = true; S.slots[0].fixed = true; S.slots[1].name = "F";
        Events all;
        for (size_t i = 0; i < smf.ev.size(); i++) {
            const MEv& e = smf.ev[i]; const double t = e.t + offset; Events r;
            if (e.st == 0x90 && e.d2 > 0) r = S.noteOn(e.ch, e.d1, e.d2, t);
            else if (e.st == 0x80 || (e.st == 0x90 && e.d2 == 0)) r = S.noteOff(e.ch, e.d1, t);
            else if (e.st == 0xb0) r = S.cc(e.d1, e.d2, t);
            else if (e.st == 0xe0) r = S.pitchBend(e.d2 << 7 | e.d1, t);
            all.insert(all.end(), r.begin(), r.end());
            if (i + 1 >= smf.ev.size() || smf.ev[i + 1].t - e.t > 0.002) { Events f = S.flush(t); all.insert(all.end(), f.begin(), f.end()); }
        }
        double tEnd = 0; for (auto& e : all) { Event ev = e; ev.at = (long) std::lround(std::max(0.0, e.when) * fs); E.push(ev); tEnd = std::max(tEnd, e.when); }
        const long n = (long) ((tEnd + 3.0) * fs); chan[side].resize((size_t) n);
        for (long i = 0; i < n; i += 480) E.process(chan[side].data() + i, (int) std::min<long>(480, n - i));
        fprintf(stderr, "Take %c: %zu Ereignisse, %.1f s, Peak %.3f\n", 'A' + side, all.size(), tEnd, E.outPk);
        if (getenv("RIG_LOG") && side == 0) {   // Anschläge je Zeitpunkt: Taktposition, Richtung, Saiten
            double gt = -1; int gd = 0; std::string line;
            for (auto& e : all) { if (e.t != Event::Pluck) continue;
                if (gt < 0 || std::abs(e.when - gt) > 0.06 || e.dir != gd) { if (!line.empty()) fprintf(stderr, "%s\n", line.c_str()); auto c = clock(e.when - offset); char b[64]; snprintf(b, 64, "Takt %d Pos %.2f %s", c ? c->bar + 1 : 0, c ? c->step16 / 4 + 1 : 0, e.dir > 0 ? "ab " : "auf"); line = b; gt = e.when; gd = e.dir; }
                line += std::string(" ") + "EADGHe"[e.s] + (e.dead ? "·" : "") + (e.hasFing ? "f" : ""); }
            fprintf(stderr, "%s\n", line.c_str());
        }
    }
    writeWav(argv[2], chan[0], chan[1], (int) fs);
    return 0;
}

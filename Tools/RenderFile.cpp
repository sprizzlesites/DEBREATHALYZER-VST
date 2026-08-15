// Offline A/B tool: runs the real BreathDetector over a raw mono float32 file
// and writes the processed result, so a change can be judged on actual vocals
// instead of synthetic noise.
//
//   DeBreathalyzerRender <in.f32> <sampleRate> <out-processed.f32> [out-removed.f32]
//
// Produce the input with:
//   ffmpeg -i take.wav -ac 1 -ar 44100 -f f32le take.f32
// and listen back with:
//   ffmpeg -f f32le -ar 44100 -ac 1 -i out.f32 out.wav
//
// It also prints every region it ducked (time, duration, depth), which is what
// makes it useful: you can diff the region list against where the breaths
// actually are before trusting the plugin with a mix.

#include "DSP/BreathDetector.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    std::vector<float> readRaw (const std::string& path)
    {
        std::FILE* f = std::fopen (path.c_str(), "rb");
        if (f == nullptr) { std::fprintf (stderr, "cannot open %s\n", path.c_str()); std::exit (1); }
        std::fseek (f, 0, SEEK_END);
        const long bytes = std::ftell (f);
        std::fseek (f, 0, SEEK_SET);
        std::vector<float> v ((size_t) (bytes / (long) sizeof (float)));
        if (std::fread (v.data(), sizeof (float), v.size(), f) != v.size())
            std::fprintf (stderr, "short read on %s\n", path.c_str());
        std::fclose (f);
        return v;
    }

    void writeRaw (const std::string& path, const std::vector<float>& v)
    {
        std::FILE* f = std::fopen (path.c_str(), "wb");
        if (f == nullptr) { std::fprintf (stderr, "cannot write %s\n", path.c_str()); std::exit (1); }
        std::fwrite (v.data(), sizeof (float), v.size(), f);
        std::fclose (f);
    }
}

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf (stderr,
            "usage: DeBreathalyzerRender <in.f32> <sampleRate> <out.f32> [removed.f32]\n");
        return 1;
    }

    const std::string inPath = argv[1];
    const double sampleRate = std::atof (argv[2]);
    const std::string outPath = argv[3];
    const std::string removedPath = argc > 4 ? argv[4] : std::string();

    auto input = readRaw (inPath);
    std::printf ("in: %s  %zu samples  %.1f s @ %.0f Hz\n",
                 inPath.c_str(), input.size(), input.size() / sampleRate, sampleRate);

    BreathDetector detector;
    detector.prepare (sampleRate, 512);
    BreathDetector::Parameters p;   // shipping defaults unless overridden
    for (int i = 5; i + 1 < argc; i += 2)
    {
        const std::string k = argv[i];
        const float v = (float) std::atof (argv[i + 1]);
        if      (k == "--sensitivity") p.sensitivity = v;
        else if (k == "--reduction")   p.reductionDb = v;
        else if (k == "--attack")      p.attackMs = v;
        else if (k == "--release")     p.releaseMs = v;
        else if (k == "--minlength")   p.minLengthMs = v;
    }
    detector.setParameters (p);
    std::printf ("params: sensitivity %.2f  reduction %.1f dB  min length %.0f ms\n",
                 p.sensitivity, p.reductionDb, p.minLengthMs);

    std::vector<float> gain (input.size(), 1.0f);
    // Block-wise, so the result matches what a host would produce.
    const int block = 512;
    for (size_t i = 0; i < input.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, input.size() - i);
        detector.process (input.data() + i, gain.data() + i, n);
    }

    std::vector<float> processed (input.size()), removed (input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        processed[i] = input[i] * gain[i];
        removed[i]   = input[i] * (1.0f - gain[i]);
    }

    writeRaw (outPath, processed);
    if (! removedPath.empty())
        writeRaw (removedPath, removed);

    // Report the ducked regions. "Engaged" = gain pulled at least halfway to
    // the reduction target, which ignores the attack/release skirts.
    const float target = std::pow (10.0f, p.reductionDb / 20.0f);
    const float engaged = 1.0f - 0.5f * (1.0f - target);

    int count = 0;
    double totalDucked = 0.0;
    size_t i = 0;
    std::printf ("\nducked regions:\n");
    while (i < gain.size())
    {
        if (gain[i] < engaged)
        {
            const size_t start = i;
            float deepest = 1.0f;
            while (i < gain.size() && gain[i] < engaged)
            {
                deepest = std::min (deepest, gain[i]);
                ++i;
            }
            const double t = (double) start / sampleRate;
            const double dur = (double) (i - start) / sampleRate;
            totalDucked += dur;
            ++count;
            if (count <= 40)
                std::printf ("  %7.2f s  %6.0f ms  %5.1f dB\n",
                             t, dur * 1000.0, 20.0 * std::log10 (std::max (deepest, 1.0e-6f)));
        }
        else ++i;
    }

    const double durSecs = input.size() / sampleRate;
    std::printf ("\n%d regions ducked (%.1f per minute), %.2f s total (%.1f%% of programme)\n",
                 count, count / (durSecs / 60.0), totalDucked, 100.0 * totalDucked / durSecs);
    return 0;
}

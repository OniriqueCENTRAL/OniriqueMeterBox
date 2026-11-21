// Onirique MeterBox — the reference: a record to listen to in place of the mix, and its measurements
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "Cloud.h"
#include "dsp/Meter.h"

namespace onirique
{

using namespace juce;

/** Everything measured on a reference, over the whole of it, once, when it
    is loaded. The same figures the meter gives for the mix, so that the two
    can be laid side by side. */
struct ReferenceStats
{
    mtx::LoudnessValues loudness;                   // integrated, range, maxima, true peak, PLR
    float spectrum[mtx::kSpectrumPoints] = {};      // the average over the whole record
    float correlationByBand[mtx::kSpectrumPoints] = {};
    bool spectrumMeasured = false;

    mtx::StereoValues stereo;                       // over the whole record
    float peak[2] = {}, rms[2] = {};                // amplitudes
    float crestDb = 0.0f;
    uint32_t distribution[mtx::kDistributionBins] = {};
    uint32_t distributionMost = 0;
};

/** A reference as it is held in memory: its sound at its own rate, a picture
    of it for the strip, and what was measured on it. Never changed once made:
    the audio thread, the editor and the loader all read it as it is. */
struct ReferenceTrack
{
    File file;
    String name;
    double rate = 44100.0;
    AudioBuffer<float> audio;                       // one or two channels

    /** The outline of the record for the strip: the highest sample and the
        RMS level of each column, the two sides together. */
    static constexpr int kColumns = 640;
    float columnPeak[kColumns] = {}, columnRms[kColumns] = {};

    ReferenceStats stats;

    double seconds() const noexcept { return audio.getNumSamples() / rate; }
};

/** Loads a reference off the audio and message threads, measures it, and
    plays it back in place of the mix. */
class ReferencePlayer final : private Thread
{
public:
    ReferencePlayer();
    ~ReferencePlayer() override;

    /** The files it can read. */
    static String getWildcard();
    static bool canRead (const File&);

    //==============================================================================
    // Message thread.

    /** Starts reading and measuring a file. What was loaded stays until the new one is ready. */
    void load (const File&);

    /** A track on a platform (YouTube, TikTok): fetched as a WAV by the
        Downloader of the ONIRIQUE site, kept on this computer, then loaded
        as a file. */
    void loadLink (const String& link);

    /** A title found by searching (title, artist, or a Spotify link), as the
        "Compare with a reference" page of the site finds it: Deezer's catalogue. */
    struct Found
    {
        String id, title, artist, album, coverUrl;
        int seconds = 0;
    };

    /** Searches the catalogue the page of the site searches. Blocks: never on
        the message thread or the audio thread. */
    static Array<Found> search (const String& query, String& error);

    /** The official 30 s extract of a title found by searching, fetched by the
        site, kept on this computer, then loaded as a file. */
    void loadPreview (const Found&);

    /** A track of the ONIRIQUE CLOUD, whole, at its version in front: fetched
        from its signed address, kept on this computer, then loaded as a file. */
    void loadCloud (const cloud::Track&);

    /** Whether a link is one the Downloader of the site takes. */
    static bool isPlatformLink (const String&);

    /** Where the tracks fetched from a platform are kept. */
    static File getDownloadFolder();
    void eject();

    /** Stops the loader: before anything it calls back is let go of. */
    void stop() { stopThread (4000); }

    enum class Status { empty, downloading, loading, ready, failed };
    Status getStatus() const noexcept { return status.load (std::memory_order_acquire); }
    String getError() const;

    /** The reference in place, or nothing. */
    std::shared_ptr<const ReferenceTrack> getTrack() const;

    /** Called on the loader's thread each time a reference is in place. */
    std::function<void()> onLoaded;

    /** Where it is playing, in seconds; and where it should play from next. */
    double getPosition() const noexcept { return positionSeconds.load (std::memory_order_relaxed); }
    void seek (double seconds) noexcept { seekRequest.store (seconds, std::memory_order_release); }

    //==============================================================================
    // Any thread.

    /** Which one is heard: the mix (A) or the reference (B). */
    void setListening (bool reference) noexcept { listening.store (reference, std::memory_order_relaxed); }
    bool isListening() const noexcept { return listening.load (std::memory_order_relaxed); }

    /** Whether the reference is brought to the loudness of the mix. */
    void setMatching (bool shouldMatch) noexcept { matching.store (shouldMatch, std::memory_order_relaxed); }
    bool isMatching() const noexcept { return matching.load (std::memory_order_relaxed); }

    /** Locked, the reference plays at the time of the project: second 72 of the
        song is second 72 of the reference, give or take the offset. Two versions
        of the same song stay in step, wherever the host goes. Unlocked, it
        runs on its own and loops. */
    void setLocked (bool shouldLock) noexcept { locked.store (shouldLock, std::memory_order_relaxed); }
    bool isLocked() const noexcept { return locked.load (std::memory_order_relaxed); }

    /** How far ahead of the project the reference is, in seconds, when locked. */
    double getOffset() const noexcept { return offset.load (std::memory_order_relaxed); }
    void setOffset (double seconds) noexcept { offset.store (seconds, std::memory_order_relaxed); }

    /** Whether the project's time is known: locking needs it. */
    bool knowsHostTime() const noexcept { return hostTimeKnown.load (std::memory_order_relaxed); }

    /** The gain the reference is played with, in dB, as it stands. */
    float getGainDb() const noexcept { return gainShown.load (std::memory_order_relaxed); }

    /** Whether the reference is what comes out just now. */
    bool isHeard() const noexcept { return heard.load (std::memory_order_relaxed); }

    //==============================================================================
    // Audio thread.

    void prepare (double sampleRate, int numChannels);

    /** Puts the reference in place of what is in the buffer, with a short
        crossfade either way. `playing`: whether the host is playing, which
        is what moves the reference along. `mixLufs`: the loudness the
        reference is brought to, or below -70 if there is none yet. Nothing
        is done during a render: what is exported is always the mix. */
    void process (AudioBuffer<float>&, bool playing, bool rendering, float mixLufs, double hostSeconds) noexcept;

private:
    void run() override;
    std::shared_ptr<ReferenceTrack> read (const File&, String& error);
    File download (const String& link, String& error);
    File downloadPreview (const String& id, const String& name, String& error);
    File downloadCloud (const String& url, const String& name, const String& extension, String& error);
    static File keep (InputStream&, const String& name, const String& extension, String& error, Thread*);
    void install (std::shared_ptr<const ReferenceTrack>);

    AudioFormatManager formats;

    SpinLock trackLock;
    std::shared_ptr<const ReferenceTrack> track;

    CriticalSection requestLock;
    File requested;
    String requestedLink, requestedName, requestedExtension;
    String error;

    std::atomic<Status> status { Status::empty };
    std::atomic<bool> listening { false }, matching { true }, heard { false }, locked { true }, hostTimeKnown { false };
    std::atomic<double> offset { 0.0 }, lastHostSeconds { -1.0 };
    std::atomic<double> positionSeconds { 0.0 }, seekRequest { -1.0 };
    std::atomic<float> gainShown { 0.0f };

    // The audio thread's own.
    double hostRate = 48000.0;
    int channels = 2;
    double position = 0.0;          // in samples of the reference
    float blend = 0.0f;             // 0 the mix .. 1 the reference
    float gain = 1.0f;              // as applied; eases towards the wanted one
    const ReferenceTrack* lastTrack = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferencePlayer)
};

} // namespace onirique

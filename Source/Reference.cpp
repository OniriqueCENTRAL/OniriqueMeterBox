#include "Reference.h"

namespace onirique
{

namespace
{
    // Longer than any record: what is held in memory stays reasonable.
    constexpr double kLongestSeconds = 15.0 * 60.0;

    // How quickly the switch from one to the other is made, either way.
    constexpr double kCrossfadeSeconds = 0.02;

    // How far the matching gain may go: further than that, something is wrong
    // with one of the two measurements.
    constexpr float kLowestGainDb = -40.0f, kHighestGainDb = 24.0f;

    // The Downloader of the ONIRIQUE site: the same one its page uses.
    const char* const kDownloaderUrl = "https://onirique.store/api/downloader/youtube";

    // The search of the "Compare with a reference" page of the site, and its 30 s extracts.
    const char* const kSearchUrl = "https://onirique.store/api/reference/search";
    const char* const kPreviewUrl = "https://onirique.store/api/reference/preview/";

    // A request for an extract, told apart from a link in the queue of the loader.
    const char* const kPreviewPrefix = "preview:";

    // Under this, the mix is too quiet to be matched against.
    constexpr float kQuietestMixLufs = -70.0f;

    /** Four points of a sound and where between the middle two: the Hermite
        curve through them. Good enough to play a record back at another rate. */
    inline float hermite (float x0, float x1, float x2, float x3, float t) noexcept
    {
        const auto c1 = 0.5f * (x2 - x0);
        const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * t + c2) * t + c1) * t + x1;
    }
}

ReferencePlayer::ReferencePlayer() : Thread ("MeterBox reference")
{
    formats.registerBasicFormats();
}

ReferencePlayer::~ReferencePlayer()
{
    stopThread (4000);
}

String ReferencePlayer::getWildcard()
{
    AudioFormatManager all;
    all.registerBasicFormats();
    return all.getWildcardForAllFormats();
}

bool ReferencePlayer::canRead (const File& file)
{
    AudioFormatManager all;
    all.registerBasicFormats();
    return file.existsAsFile() && all.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

//==============================================================================
void ReferencePlayer::load (const File& file)
{
    {
        const ScopedLock lock (requestLock);
        requested = file;
        requestedLink = {};
        error = {};
    }

    status.store (Status::loading, std::memory_order_release);

    if (! isThreadRunning())
        startThread (Priority::low);
    else
        notify();
}

void ReferencePlayer::loadLink (const String& link)
{
    {
        const ScopedLock lock (requestLock);
        requested = File();
        requestedLink = link.trim();
        requestedName = {};
        error = {};
    }

    status.store (Status::downloading, std::memory_order_release);

    if (! isThreadRunning())
        startThread (Priority::low);
    else
        notify();
}

void ReferencePlayer::loadPreview (const Found& found)
{
    {
        const ScopedLock lock (requestLock);
        requested = File();
        requestedLink = kPreviewPrefix + found.id;
        requestedName = found.artist + String::fromUTF8 (" – ") + found.title + String::fromUTF8 (" (extrait 30 s)");
        error = {};
    }

    status.store (Status::downloading, std::memory_order_release);

    if (! isThreadRunning())
        startThread (Priority::low);
    else
        notify();
}

Array<ReferencePlayer::Found> ReferencePlayer::search (const String& query, String& problem)
{
    Array<Found> result;
    int statusCode = 0;

    auto stream = URL (kSearchUrl).withParameter ("q", query.trim())
                      .createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                              .withConnectionTimeoutMs (15000)
                                              .withStatusCode (&statusCode));

    if (stream == nullptr)
    {
        problem = String::fromUTF8 ("Impossible de joindre le site ONIRIQUE : vérifie ta connexion.");
        return result;
    }

    const auto answer = JSON::parse (stream->readEntireStreamAsString());

    if (statusCode != 200)
    {
        const auto reason = answer.getProperty ("error", var()).toString();
        problem = reason.isNotEmpty() ? reason : String::fromUTF8 ("La recherche n'a pas abouti.");
        return result;
    }

    if (const auto* list = answer.getArray())
        for (const auto& item : *list)
        {
            Found f;
            f.id = item.getProperty ("id", var()).toString();
            f.title = item.getProperty ("title", var()).toString();
            f.artist = item.getProperty ("artist", var()).toString();
            f.album = item.getProperty ("album", var()).toString();
            f.coverUrl = item.getProperty ("cover", var()).toString();
            f.seconds = (int) item.getProperty ("duration", 0);

            if (f.id.containsOnly ("0123456789") && f.id.isNotEmpty() && f.title.isNotEmpty())
                result.add (f);
        }

    if (result.isEmpty())
        problem = String::fromUTF8 ("Aucun titre trouvé : essaie avec le titre et l'artiste.");

    return result;
}

bool ReferencePlayer::isPlatformLink (const String& text)
{
    const auto link = text.trim();

    if (! (link.startsWithIgnoreCase ("https://") || link.startsWithIgnoreCase ("http://")) || link.containsAnyOf (" \n\t"))
        return false;

    const auto host = URL (link).getDomain().toLowerCase();

    for (const auto* known : { "youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com", "youtu.be",
                               "tiktok.com", "www.tiktok.com", "m.tiktok.com", "vm.tiktok.com", "vt.tiktok.com" })
        if (host == known)
            return true;

    return false;
}

File ReferencePlayer::getDownloadFolder()
{
    // Beside the other ONIRIQUE plugins' data: ~/Library/Onirique on a Mac,
    // AppData\Roaming\Onirique on Windows.
    auto base = File::getSpecialLocation (File::userApplicationDataDirectory);

    return base.getChildFile ("Onirique").getChildFile ("MeterBox References");
}

void ReferencePlayer::eject()
{
    {
        const ScopedLock lock (requestLock);
        requested = File();
        requestedLink = {};
        error = {};
    }

    install (nullptr);
    listening.store (false, std::memory_order_relaxed);
    status.store (Status::empty, std::memory_order_release);
}

String ReferencePlayer::getError() const
{
    const ScopedLock lock (requestLock);
    return error;
}

std::shared_ptr<const ReferenceTrack> ReferencePlayer::getTrack() const
{
    const SpinLock::ScopedLockType lock (trackLock);
    return track;
}

void ReferencePlayer::install (std::shared_ptr<const ReferenceTrack> next)
{
    // The one put aside is let go of here, outside the lock: never on the audio thread.
    {
        const SpinLock::ScopedLockType lock (trackLock);
        std::swap (track, next);
    }

    positionSeconds.store (0.0, std::memory_order_relaxed);
}

void ReferencePlayer::run()
{
    while (! threadShouldExit())
    {
        File file;
        String link, name;

        {
            const ScopedLock lock (requestLock);
            std::swap (file, requested);
            std::swap (link, requestedLink);
            std::swap (name, requestedName);
        }

        if (file == File() && link.isEmpty())
        {
            wait (-1);
            continue;
        }

        String problem;

        // A track on a platform: fetched first, then read as any file.
        if (link.isNotEmpty())
        {
            file = link.startsWith (kPreviewPrefix) ? downloadPreview (link.fromFirstOccurrenceOf (kPreviewPrefix, false, false), name, problem)
                                                    : download (link, problem);

            const ScopedLock lock (requestLock);

            if (requested != File() || requestedLink.isNotEmpty() || status.load() != Status::downloading)
                continue;

            if (file == File())
            {
                error = problem;
                status.store (getTrack() != nullptr ? Status::ready : Status::failed, std::memory_order_release);
                continue;
            }

            status.store (Status::loading, std::memory_order_release);
        }

        auto loaded = read (file, problem);

        // A newer request, or an eject, came in meanwhile: this one is dropped.
        {
            const ScopedLock lock (requestLock);

            if (requested != File() || requestedLink.isNotEmpty() || status.load() != Status::loading)
                continue;

            error = problem;
        }

        if (loaded != nullptr)
        {
            install (std::move (loaded));
            status.store (Status::ready, std::memory_order_release);

            if (onLoaded != nullptr)
                onLoaded();
        }
        else
        {
            status.store (getTrack() != nullptr ? Status::ready : Status::failed, std::memory_order_release);
        }
    }
}

File ReferencePlayer::download (const String& link, String& problem)
{
    // What the page of the site sends: the link, and the format wanted.
    DynamicObject::Ptr body (new DynamicObject());
    body->setProperty ("url", link);
    body->setProperty ("format", "wav");

    const auto request = URL (kDownloaderUrl).withPOSTData (JSON::toString (var (body.get()), true));

    int statusCode = 0;
    StringPairArray headers;

    auto stream = request.createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inPostData)
                                                 .withExtraHeaders ("Content-Type: application/json")
                                                 .withConnectionTimeoutMs (6 * 60 * 1000)
                                                 .withStatusCode (&statusCode)
                                                 .withResponseHeaders (&headers));

    if (stream == nullptr)
    {
        problem = String::fromUTF8 ("Impossible de joindre le site ONIRIQUE : vérifie ta connexion.");
        return {};
    }

    if (statusCode != 200)
    {
        const auto answer = JSON::parse (stream->readEntireStreamAsString());
        const auto reason = answer.getProperty ("error", var()).toString();
        problem = reason.isNotEmpty() ? reason : String::fromUTF8 ("Le téléchargement a échoué (") + String (statusCode) + ").";
        return {};
    }

    // Named after the video, as the site names it.
    auto title = URL::removeEscapeChars (headers.getValue ("X-Title", {}).replace ("+", "%2B"));

    return keep (*stream, title.isNotEmpty() ? title : String ("Reference"), ".wav", problem, this);
}

File ReferencePlayer::downloadPreview (const String& id, const String& name, String& problem)
{
    int statusCode = 0;

    auto stream = URL (kPreviewUrl + id).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                                                .withConnectionTimeoutMs (30000)
                                                                .withStatusCode (&statusCode));

    if (stream == nullptr)
    {
        problem = String::fromUTF8 ("Impossible de joindre le site ONIRIQUE : vérifie ta connexion.");
        return {};
    }

    if (statusCode != 200)
    {
        const auto reason = JSON::parse (stream->readEntireStreamAsString()).getProperty ("error", var()).toString();
        problem = reason.isNotEmpty() ? reason : String::fromUTF8 ("L'extrait n'a pas pu être chargé.");
        return {};
    }

    return keep (*stream, name.isNotEmpty() ? name : String ("Reference"), ".mp3", problem, this);
}

File ReferencePlayer::keep (InputStream& stream, const String& name, const String& extension, String& problem, Thread* thread)
{
    const auto folder = getDownloadFolder();
    folder.createDirectory();

    const auto destination = folder.getChildFile (File::createLegalFileName (name.substring (0, 120)) + extension);
    const auto partial = destination.withFileExtension (".part");
    partial.deleteFile();

    {
        FileOutputStream out (partial);

        if (! out.openedOk())
        {
            problem = String::fromUTF8 ("Impossible d'écrire dans ") + folder.getFullPathName();
            return {};
        }

        HeapBlock<char> chunk (1 << 16);

        while (! stream.isExhausted())
        {
            if (thread != nullptr && thread->threadShouldExit())
                return {};

            const auto got = stream.read (chunk.get(), 1 << 16);

            if (got <= 0)
                break;

            out.write (chunk.get(), (size_t) got);
        }

        out.flush();
    }

    if (partial.getSize() < 1024 || ! partial.moveFileTo (destination))
    {
        partial.deleteFile();
        problem = String::fromUTF8 ("Le téléchargement a été interrompu.");
        return {};
    }

    return destination;
}

std::shared_ptr<ReferenceTrack> ReferencePlayer::read (const File& file, String& problem)
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        problem = "Ce fichier ne se lit pas : " + file.getFileName();
        return nullptr;
    }

    if (reader->sampleRate < 8000.0 || reader->lengthInSamples <= 0)
    {
        problem = "Ce fichier est vide : " + file.getFileName();
        return nullptr;
    }

    auto result = std::make_shared<ReferenceTrack>();
    result->file = file;
    result->name = file.getFileNameWithoutExtension();
    result->rate = reader->sampleRate;

    const auto channels = jlimit (1, 2, (int) reader->numChannels);
    const auto length = (int) jmin ((int64) (kLongestSeconds * reader->sampleRate), reader->lengthInSamples);

    result->audio.setSize (channels, length);
    reader->read (&result->audio, 0, length, 0, true, channels > 1);

    if (threadShouldExit())
        return nullptr;

    //==============================================================================
    // Measured the way the mix is, over the whole of it.
    auto& stats = result->stats;
    auto meter = std::make_unique<mtx::Meter>();

    meter->prepare (result->rate, channels);
    meter->setAveraging (mtx::averageAll);

    const float* in[2] = { result->audio.getReadPointer (0), result->audio.getReadPointer (channels > 1 ? 1 : 0) };
    const auto block = 4096;
    double bandSum[mtx::kSpectrumPoints] = {};
    int bandCount = 0;
    mtx::SpectrumFrame frame;

    for (int at = 0; at < length; at += block)
    {
        if (threadShouldExit())
            return nullptr;

        const auto count = jmin (block, length - at);
        const float* from[2] = { in[0] + at, in[1] + at };

        meter->process (from, channels, count, true);
        meter->analyse();

        // The correlation by frequency is that of the moment: it is averaged
        // here over every moment there was something to hear.
        meter->readSpectrum (frame);

        if (frame.measured && meter->loudness().momentary > -50.0f)
        {
            for (int p = 0; p < mtx::kSpectrumPoints; ++p)
                bandSum[p] += frame.correlation[p];

            ++bandCount;
        }
    }

    stats.loudness = meter->loudness();
    meter->readSpectrum (frame);

    stats.spectrumMeasured = frame.measured;
    std::copy (std::begin (frame.average), std::end (frame.average), std::begin (stats.spectrum));

    for (int p = 0; p < mtx::kSpectrumPoints; ++p)
        stats.correlationByBand[p] = bandCount > 0 ? (float) (bandSum[p] / bandCount) : 0.0f;

    for (int bin = 0; bin < mtx::kDistributionBins; ++bin)
    {
        stats.distribution[bin] = meter->distribution (bin);
        stats.distributionMost = jmax (stats.distributionMost, stats.distribution[bin]);
    }

    //==============================================================================
    // The stereo picture and the waveform figures, over the whole record,
    // and its outline for the strip.
    {
        double ll = 0.0, rr = 0.0, lr = 0.0;
        float peak[2] = {};

        for (int col = 0; col < ReferenceTrack::kColumns; ++col)
        {
            const auto from = (int) ((int64) col * length / ReferenceTrack::kColumns);
            const auto to = jmax (from + 1, (int) ((int64) (col + 1) * length / ReferenceTrack::kColumns));
            float most = 0.0f;
            double squares = 0.0;

            for (int i = from; i < jmin (to, length); ++i)
            {
                const double l = in[0][i], r = in[1][i];

                ll += l * l;
                rr += r * r;
                lr += l * r;
                peak[0] = jmax (peak[0], (float) std::abs (l));
                peak[1] = jmax (peak[1], (float) std::abs (r));
                most = jmax (most, (float) jmax (std::abs (l), std::abs (r)));
                squares += 0.5 * (l * l + r * r);
            }

            result->columnPeak[col] = most;
            result->columnRms[col] = (float) std::sqrt (squares / jmax (1, to - from));
        }

        const auto both = std::sqrt (ll * rr);
        const auto middle = 0.25 * (ll + rr + 2.0 * lr);
        const auto sides = 0.25 * (ll + rr - 2.0 * lr);
        const auto mean = 0.5 * (ll + rr);
        auto& stereo = stats.stereo;

        stereo.level = mean > 1.0e-18 ? (float) (10.0 * std::log10 (2.0 * mean / length)) : mtx::kFloorDb;
        stereo.correlation = both > 1.0e-18 ? (float) jlimit (-1.0, 1.0, lr / both) : 0.0f;
        stereo.balance = (float) jlimit (-24.0, 24.0, 10.0 * std::log10 ((rr + 1.0e-12) / (ll + 1.0e-12)));
        stereo.width = middle + sides > 1.0e-18 ? (float) jlimit (0.0, 1.0, sides / (middle + sides)) : 0.0f;
        stereo.monoLoss = mean > 1.0e-18 ? (float) jmax (-60.0, 10.0 * std::log10 (jmax (middle, 1.0e-24) / mean)) : 0.0f;

        for (int c = 0; c < 2; ++c)
        {
            stats.peak[c] = peak[c];
            stats.rms[c] = (float) std::sqrt ((c == 0 ? ll : rr) / length);
        }

        const auto loudest = jmax (stats.rms[0], stats.rms[1]);
        stats.crestDb = loudest > 1.0e-6f ? 20.0f * std::log10 (jmax (peak[0], peak[1]) / loudest) : 0.0f;
    }

    if (stats.loudness.integrated <= kQuietestMixLufs)
    {
        problem = String::fromUTF8 ("Rien à mesurer dans ce fichier : ") + file.getFileName();
        return nullptr;
    }

    return result;
}

//==============================================================================
void ReferencePlayer::prepare (double sampleRate, int numChannels)
{
    hostRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    channels = numChannels;
}

void ReferencePlayer::process (AudioBuffer<float>& buffer, bool playing, bool rendering, float mixLufs, double hostSeconds) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto numOut = jmin (buffer.getNumChannels(), 2);

    // Held for the block: whoever else takes it only copies or swaps a pointer.
    const SpinLock::ScopedLockType lock (trackLock);
    const auto* current = track.get();

    if (current != lastTrack)
    {
        lastTrack = current;
        position = 0.0;
    }

    const auto wanted = current != nullptr && ! rendering && listening.load (std::memory_order_relaxed);

    // A render is always the mix, from its first sample.
    if (rendering)
        blend = 0.0f;

    if (current == nullptr || numSamples <= 0 || numOut < 1)
    {
        blend = 0.0f;
        heard.store (false, std::memory_order_relaxed);
        return;
    }

    // Locked to the project: where the reference is follows where the host is.
    hostTimeKnown.store (hostSeconds >= 0.0, std::memory_order_relaxed);

    if (hostSeconds >= 0.0)
        lastHostSeconds.store (hostSeconds, std::memory_order_relaxed);

    const auto followsHost = locked.load (std::memory_order_relaxed) && hostSeconds >= 0.0;

    // A click on its outline: locked, it moves the reference against the
    // project (the offset); free, it moves the reference itself.
    if (const auto seekTo = seekRequest.exchange (-1.0, std::memory_order_acq_rel); seekTo >= 0.0)
    {
        const auto known = lastHostSeconds.load (std::memory_order_relaxed);

        if (locked.load (std::memory_order_relaxed) && known >= 0.0)
            offset.store (seekTo - known, std::memory_order_relaxed);
        else
            position = jlimit (0.0, (double) current->audio.getNumSamples() - 1.0, seekTo * current->rate);
    }

    if (followsHost)
    {
        position = (hostSeconds + offset.load (std::memory_order_relaxed)) * current->rate;

        // Seconds turned back into samples land a hair off the sample they
        // stand for (250239.99999): that would be one sample late.
        if (std::abs (position - std::round (position)) < 1.0e-4)
            position = std::round (position);
    }

    // The gain that brings the reference to the loudness of the mix.
    auto wantedDb = 0.0f;
    const auto referenceLufs = current->stats.loudness.integrated;

    if (matching.load (std::memory_order_relaxed) && mixLufs > kQuietestMixLufs && referenceLufs > kQuietestMixLufs)
        wantedDb = jlimit (kLowestGainDb, kHighestGainDb, mixLufs - referenceLufs);

    gainShown.store (wantedDb, std::memory_order_relaxed);

    const auto target = Decibels::decibelsToGain (wantedDb);
    const auto gainStep = (target - gain) * jmin (1.0f, (float) numSamples / (float) (0.1 * hostRate)) / (float) numSamples;
    const auto blendStep = (float) (1.0 / (kCrossfadeSeconds * hostRate)) * (wanted ? 1.0f : -1.0f);

    // Nothing to do while only the mix is heard: the reference still moves
    // along with the host, so that the two stay in step.
    const auto length = current->audio.getNumSamples();
    const auto step = current->rate / hostRate;
    const auto refChannels = current->audio.getNumChannels();
    const float* src[2] = { current->audio.getReadPointer (0), current->audio.getReadPointer (refChannels > 1 ? 1 : 0) };

    if (blend <= 0.0f && ! wanted)
    {
        gain = target;

        if (playing && ! followsHost)
            position = std::fmod (position + step * numSamples, (double) length);

        positionSeconds.store (position / current->rate, std::memory_order_relaxed);
        heard.store (false, std::memory_order_relaxed);
        return;
    }

    float* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numOut > 1 ? 1 : 0) };
    const auto exact = std::abs (step - 1.0) < 1.0e-9;

    for (int i = 0; i < numSamples; ++i)
    {
        blend = jlimit (0.0f, 1.0f, blend + blendStep);
        gain += gainStep;

        float sample[2] = {};

        // Locked, before its start or after its end, the reference is silent.
        const auto inside = ! followsHost || (position >= 0.0 && position < (double) length);

        if (playing && ! inside)
        {
            position += step;
        }
        else if (playing)
        {
            const auto index = jlimit (0, length - 1, (int) position);

            if (exact)
            {
                sample[0] = src[0][index];
                sample[1] = src[1][index];
            }
            else
            {
                const auto t = (float) (position - index);
                const auto at = [length] (int k) { return (k % length + length) % length; };
                const int k0 = at (index - 1), k1 = index, k2 = at (index + 1), k3 = at (index + 2);

                for (int c = 0; c < 2; ++c)
                    sample[c] = hermite (src[c][k0], src[c][k1], src[c][k2], src[c][k3], t);
            }

            position += step;

            if (position >= (double) length && ! followsHost)
                position -= (double) length;
        }

        const auto wet = blend * gain, dry = 1.0f - blend;

        if (numOut > 1)
        {
            out[0][i] = dry * out[0][i] + wet * sample[0];
            out[1][i] = dry * out[1][i] + wet * sample[1];
        }
        else
        {
            out[0][i] = dry * out[0][i] + wet * 0.5f * (sample[0] + sample[1]);
        }
    }

    positionSeconds.store (position / current->rate, std::memory_order_relaxed);
    heard.store (blend > 0.5f, std::memory_order_relaxed);
}

} // namespace onirique

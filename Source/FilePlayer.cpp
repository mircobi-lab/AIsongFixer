#include "FilePlayer.h"
#include "PluginProcessor.h"

namespace asf
{
juce::AudioFormatManager& FilePlayer::formats()
{
    static juce::AudioFormatManager fm;
    static const bool registered = [] { fm.registerBasicFormats(); return true; }();
    juce::ignoreUnused (registered);
    return fm;
}

FilePlayer::FilePlayer()
{
    readAhead.startThread (juce::Thread::Priority::normal);
}

FilePlayer::~FilePlayer()
{
    unload();
    readAhead.stopThread (2000);
}

void FilePlayer::computeOverview (juce::AudioFormatReader& reader)
{
    const int bins = 1200;
    overview.assign ((size_t) bins, 0.0f);
    const juce::int64 n = reader.lengthInSamples;
    if (n <= 0) return;
    const int chunk = 8192;
    juce::AudioBuffer<float> buf ((int) reader.numChannels, chunk);
    float peak = 0;
    for (juce::int64 pos = 0; pos < n; pos += chunk)
    {
        const int len = (int) std::min<juce::int64> (chunk, n - pos);
        buf.clear();
        reader.read (&buf, 0, len, pos, true, true);
        for (int i = 0; i < len; ++i)
        {
            float m = 0;
            for (int c = 0; c < buf.getNumChannels(); ++c) m = std::max (m, std::abs (buf.getSample (c, i)));
            const size_t bin = (size_t) juce::jlimit<juce::int64> (0, bins - 1, (pos + i) * bins / n);
            overview[bin] = std::max (overview[bin], m);
            peak = std::max (peak, m);
        }
    }
    if (peak > 0)
        for (auto& v : overview) v /= peak;
}

bool FilePlayer::load (const juce::File& f, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (f));
    if (reader == nullptr)
    {
        error = f.getFileName() + " non e' un formato che leggo. Accetto WAV, AIFF, FLAC e OGG: se Suno ti ha dato un MP3, scaricalo in WAV.";
        return false;
    }
    if (reader->lengthInSamples <= 0)
    {
        error = "Il file e' vuoto";
        return false;
    }

    computeOverview (*reader);
    const double fileRate = reader->sampleRate;
    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);

    {
        const juce::ScopedLock sl (lock);
        transport.stop();
        transport.setSource (nullptr);
        source = std::move (newSource);
        transport.setSource (source.get(), 32768, &readAhead, fileRate);
        transport.prepareToPlay (block, rate);
        transport.setPosition (0.0);
        file = f;
        wantPlay.store (false);
        loaded.store (true);
    }
    return true;
}

void FilePlayer::unload()
{
    const juce::ScopedLock sl (lock);
    loaded.store (false);
    wantPlay.store (false);
    transport.stop();
    transport.setSource (nullptr);
    source.reset();
    file = juce::File();
    overview.clear();
}

void FilePlayer::prepare (double sampleRate, int blockSize)
{
    const juce::ScopedLock sl (lock);
    rate = sampleRate;
    block = blockSize;
    transport.prepareToPlay (blockSize, sampleRate);
}

void FilePlayer::release()
{
    const juce::ScopedLock sl (lock);
    transport.releaseResources();
}

bool FilePlayer::render (juce::AudioBuffer<float>& buffer)
{
    if (! loaded.load()) return false;

    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked())
    {
        buffer.clear();
        return true;
    }

    juce::AudioSourceChannelInfo info (&buffer, 0, buffer.getNumSamples());
    transport.getNextAudioBlock (info);

    const double len = transport.getLengthInSeconds();
    if (wantPlay.load() && ! transport.isPlaying() && len > 0.0)
    {
        if (looping.load())
        {
            transport.setPosition (0.0);
            transport.start();
        }
        else
        {
            wantPlay.store (false);
        }
    }
    return true;
}

void FilePlayer::play()
{
    const juce::ScopedLock sl (lock);
    if (! loaded.load()) return;
    if (transport.getCurrentPosition() >= transport.getLengthInSeconds() - 1e-4)
        transport.setPosition (0.0);
    transport.start();
    wantPlay.store (true);
}

void FilePlayer::stop()
{
    const juce::ScopedLock sl (lock);
    transport.stop();
    wantPlay.store (false);
}

void FilePlayer::setPosition (double s)
{
    const juce::ScopedLock sl (lock);
    if (loaded.load()) transport.setPosition (juce::jlimit (0.0, transport.getLengthInSeconds(), s));
}

double FilePlayer::getPosition()
{
    const juce::ScopedLock sl (lock);
    return transport.getCurrentPosition();
}

double FilePlayer::getLength() const
{
    return const_cast<FilePlayer*> (this)->transport.getLengthInSeconds();
}

//==============================================================================
juce::String Exporter::render (const juce::File& source, const juce::File& destination, const juce::MemoryBlock& pluginState,
                               std::atomic<float>* progressOut, std::function<bool()> shouldStop)
{
    std::unique_ptr<juce::AudioFormatReader> reader (FilePlayer::formats().createReaderFor (source));
    if (reader == nullptr) return "Formato non supportato";

    const double fileRate = reader->sampleRate;
    const int chans = juce::jlimit (1, 2, (int) reader->numChannels);
    const juce::int64 total = reader->lengthInSamples;
    if (total <= 0) return "Il file e' vuoto";

    FixerProcessor p;
    if (pluginState.getSize() > 0) p.setStateInformation (pluginState.getData(), (int) pluginState.getSize());
    p.apvts.getParameter ("bypass")->setValueNotifyingHost (0.0f);
    p.apvts.getParameter ("delta")->setValueNotifyingHost (0.0f);
    p.apvts.getParameter ("fair")->setValueNotifyingHost (0.0f);

    const int block = 512;
    p.setNonRealtime (true);
    p.prepareToPlay (fileRate, block);

    // la catena introduce lat campioni: l'uscita k corrisponde all'ingresso k - lat, quindi si parte da lat
    // e si pompano lat campioni in più per svuotare i ritardi. Escono esattamente 'total' campioni.
    const juce::int64 lat = p.getLatencySamples();
    const juce::int64 pumped = total + lat;

    juce::File tmp = destination.getSiblingFile (destination.getFileNameWithoutExtension() + "_part.wav");
    tmp.deleteFile();

    juce::WavAudioFormat wav;
    // il writer prende possesso dello stream, anche se la creazione fallisce
    std::unique_ptr<juce::OutputStream> stream (new juce::FileOutputStream (tmp));
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk()) return "Impossibile scrivere " + destination.getFileName();
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), fileRate,
                                                                          (unsigned int) chans, 24, {}, 0));
    if (writer == nullptr) { tmp.deleteFile(); return "Scrittura WAV non riuscita"; }

    juce::AudioBuffer<float> in (chans, block), work (chans, block);
    juce::MidiBuffer midi;
    juce::int64 done = 0;
    while (done < pumped)
    {
        if (shouldStop && shouldStop()) { writer.reset(); tmp.deleteFile(); return "CANCELLED"; }

        const int n = (int) std::min<juce::int64> (block, pumped - done);
        in.clear();
        if (done < total)
            reader->read (&in, 0, (int) std::min<juce::int64> (n, total - done), done, true, true);

        work.makeCopyOf (in);
        p.processBlock (work, midi);

        // scarta i campioni di ritardo: l'uscita globale g (>= lat) va scritta alla posizione g - lat
        const juce::int64 skip = std::min<juce::int64> (n, std::max<juce::int64> (0, lat - done));
        if (n > skip)
        {
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), chans, (int) skip, (int) (n - skip));
            if (! writer->writeFromAudioSampleBuffer (view, 0, view.getNumSamples()))
            {
                writer.reset(); tmp.deleteFile();
                return "Errore di scrittura su disco";
            }
        }
        done += n;
        if (progressOut) progressOut->store (juce::jlimit (0.0f, 1.0f, (float) done / (float) pumped));
    }
    writer.reset();

    if (! tmp.moveFileTo (destination)) return "Impossibile salvare " + destination.getFileName();
    if (progressOut) progressOut->store (1.0f);
    return {};
}

bool Exporter::start (const juce::File& source, const juce::File& destination, const juce::MemoryBlock& pluginState)
{
    if (isThreadRunning()) return false;
    src = source; dest = destination; state = pluginState;
    progress.store (0.0f);
    result.store (Result::none);
    startThread (juce::Thread::Priority::normal);
    return true;
}

void Exporter::run()
{
    const auto err = render (src, dest, state, &progress, [this] { return threadShouldExit(); });
    if (err == "CANCELLED")
    {
        result.store (Result::cancelled);
        return;
    }
    {
        const juce::ScopedLock sl (msgLock);
        message = err;
    }
    result.store (err.isEmpty() ? Result::done : Result::failed);
}
}

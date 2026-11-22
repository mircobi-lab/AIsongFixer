#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

/*  Lettore di file per lo Standalone: il file sostituisce l'ingresso e passa per tutta la catena
    (pulizia, A/B, FAIR, DELTA). Il ricampionamento alla frequenza della scheda lo fa AudioTransportSource.
    I metodi pubblici si chiamano dal thread dei messaggi; render() dal thread audio, con try-lock:
    se il lock è occupato da un caricamento, quel blocco esce in silenzio invece di bloccare l'audio.  */
namespace asf
{
class FilePlayer
{
public:
    FilePlayer();
    ~FilePlayer();

    // legge il file (WAV, AIFF, FLAC, OGG), calcola la forma d'onda e lo prepara; false con 'error' se non va
    bool load (const juce::File&, juce::String& error);
    void unload();

    void prepare (double sampleRate, int blockSize);
    void release();

    // thread audio: riempie il buffer con il file. Restituisce false se nessun file è caricato
    bool render (juce::AudioBuffer<float>& buffer);

    void play();
    void stop();
    bool isPlaying() const { return wantPlay.load(); }
    void setLooping (bool on) { looping.store (on); }
    bool isLooping() const { return looping.load(); }
    void setPosition (double seconds);
    double getPosition();
    double getLength() const;
    bool isLoaded() const { return loaded.load(); }
    juce::File getFile() const { return file; }

    // forma d'onda per la vista: massimo assoluto per colonna, normalizzato 0..1
    const std::vector<float>& getOverview() const { return overview; }

    static juce::AudioFormatManager& formats();

private:
    void computeOverview (juce::AudioFormatReader&);

    juce::TimeSliceThread readAhead { "AISongFixer file reader" };
    juce::CriticalSection lock;
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::AudioTransportSource transport;
    std::atomic<bool> loaded { false }, looping { true }, wantPlay { false };
    juce::File file;
    std::vector<float> overview;
    double rate = 44100;
    int block = 512;
};

// Esporta il file elaborato in WAV a 24 bit, alla frequenza del file, su un thread a parte
class Exporter : private juce::Thread
{
public:
    Exporter() : juce::Thread ("AISongFixer export") {}
    ~Exporter() override { stopThread (60000); }

    bool start (const juce::File& source, const juce::File& destination, const juce::MemoryBlock& pluginState);
    void cancel() { signalThreadShouldExit(); }
    bool isBusy() const { return isThreadRunning(); }
    float getProgress() const { return progress.load(); }

    enum class Result { none, done, failed, cancelled };
    Result takeResult() { return result.exchange (Result::none); }
    juce::String getMessage() const { const juce::ScopedLock sl (msgLock); return message; }
    juce::File getDestination() const { return dest; }

    // elabora tutto il file con la catena del plugin (senza A/B, FAIR, DELTA). Restituisce "" se va bene
    static juce::String render (const juce::File& source, const juce::File& destination, const juce::MemoryBlock& pluginState,
                                std::atomic<float>* progress = nullptr, std::function<bool()> shouldStop = {});

private:
    void run() override;

    juce::File src, dest;
    juce::MemoryBlock state;
    std::atomic<float> progress { 0 };
    std::atomic<Result> result { Result::none };
    juce::CriticalSection msgLock;
    juce::String message;
};
}

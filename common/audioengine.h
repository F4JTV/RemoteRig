// Capture et restitution audio via PortAudio.
//
// Le protocole travaille toujours en 48 kHz mono. La carte son, elle, fait
// ce qu'elle veut : le moteur negocie le meilleur debit disponible et
// reechantillonne a la frontiere si necessaire, de sorte que le reste du
// programme n'ait jamais a s'en soucier.
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <atomic>
#include <vector>
#include "ringbuffer.h"
#include "resampler.h"

typedef void PaStream;
struct PaStreamCallbackTimeInfo;

namespace rr {

struct AudioDevice {
    int     index = -1;
    QString name;
    QString hostApi;
    int     hostApiIndex = -1;
    int     maxIn = 0;
    int     maxOut = 0;
    double  defaultSampleRate = 0.0;
    double  defaultLowLatency = 0.0;
};

class AudioEngine {
public:
    static constexpr int kAudioRate = 48000;

    AudioEngine();
    ~AudioEngine();

    static bool initialiseLibrary();
    static void terminateLibrary();

    // hostApiIndex < 0 : tous les backends confondus.
    static QList<AudioDevice> inputDevices(int hostApiIndex = -1);
    static QList<AudioDevice> outputDevices(int hostApiIndex = -1);
    static QList<QPair<int, QString>> hostApis();
    static int  defaultHostApi();
    static int  defaultInput();
    static int  defaultOutput();

    // Debit qui sera reellement utilise, ou 0 si le peripherique est inutilisable.
    static double probeRate(int deviceIndex, bool input);

    bool startCapture(int deviceIndex, int framesPerBuffer = 480);
    bool startPlayback(int deviceIndex, int framesPerBuffer = 480);
    void stopCapture();
    void stopPlayback();
    void stopAll();

    bool captureRunning()  const { return m_inStream  != nullptr; }
    bool playbackRunning() const { return m_outStream != nullptr; }

    // Debit impose par la carte son (48000 si elle a accepte notre demande).
    int  captureDeviceRate()  const { return m_inDevRate; }
    int  playbackDeviceRate() const { return m_outDevRate; }
    bool captureResampled()   const { return m_inDevRate  != kAudioRate; }
    bool playbackResampled()  const { return m_outDevRate != kAudioRate; }

    // Cote application : toujours du 48 kHz mono.
    size_t readCaptured(int16_t *dst, size_t samples);
    size_t capturedAvailable() const { return m_inRing.available(); }
    void   pushPlayback(const int16_t *src, size_t samples);
    size_t playbackQueued() const { return m_outRing.available(); }
    void   flushPlayback() { m_outRing.reset(); }
    void   flushCapture()  { m_inRing.reset(); }

    void setCaptureMuted(bool m)  { m_inMuted.store(m); }
    void setPlaybackMuted(bool m) { m_outMuted.store(m); }
    void setCaptureGain(float g)  { m_inGain.store(g); }
    void setPlaybackGain(float g) { m_outGain.store(g); }

    float captureLevel();
    float playbackLevel();

    QString lastError() const { return m_lastError; }

private:
    static int inCallback(const void *in, void *out, unsigned long frames,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          unsigned long statusFlags, void *user);
    static int outCallback(const void *in, void *out, unsigned long frames,
                           const PaStreamCallbackTimeInfo *timeInfo,
                           unsigned long statusFlags, void *user);

    PaStream *m_inStream  = nullptr;
    PaStream *m_outStream = nullptr;
    int m_inDevRate  = kAudioRate;
    int m_outDevRate = kAudioRate;

    RingBuffer m_inRing{kAudioRate};    // 1 s, toujours en 48 kHz
    RingBuffer m_outRing{kAudioRate};

    Resampler m_inResamp;    // debit carte -> 48 kHz
    Resampler m_outResamp;   // 48 kHz -> debit carte

    // Tampons de travail dimensionnes a l'ouverture : aucune allocation
    // ne doit se produire depuis un callback temps reel.
    std::vector<int16_t> m_inScratch, m_inConverted;
    std::vector<int16_t> m_outPulled, m_outConverted, m_outFifo;
    size_t m_outFifoPos = 0;

    std::atomic<bool>  m_inMuted{false};
    std::atomic<bool>  m_outMuted{false};
    std::atomic<float> m_inGain{1.0f};
    std::atomic<float> m_outGain{1.0f};
    std::atomic<float> m_inPeak{0.0f};
    std::atomic<float> m_outPeak{0.0f};
    QString m_lastError;
};

} // namespace rr

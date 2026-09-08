#include "audioengine.h"
#include "protocol.h"

#include <QCoreApplication>

#include <portaudio.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_WIN32) && defined(__has_include)
#  if __has_include(<pa_win_wasapi.h>)
#    include <pa_win_wasapi.h>
#    define RR_HAVE_WASAPI 1
#  endif
#endif

namespace rr {

static bool g_paInit = false;

bool AudioEngine::initialiseLibrary()
{
    if (g_paInit) return true;
    g_paInit = (Pa_Initialize() == paNoError);
    return g_paInit;
}

void AudioEngine::terminateLibrary()
{
    if (g_paInit) { Pa_Terminate(); g_paInit = false; }
}

AudioEngine::AudioEngine() { initialiseLibrary(); }
AudioEngine::~AudioEngine() { stopAll(); }

// ---------------------------------------------------------------- parametres
namespace {

// Assemble les parametres d'ouverture d'un peripherique. Sous Windows, si le
// backend est WASAPI, on demande la conversion automatique : la carte peut
// rester bloquee sur 44,1 kHz dans le panneau son, Windows convertit lui-meme
// et nous rend du 48 kHz sans surcout notable.
struct ParamBuilder {
    PaStreamParameters p{};
#ifdef RR_HAVE_WASAPI
    PaWasapiStreamInfo wasapi{};
#endif

    ParamBuilder(const ParamBuilder &) = delete;
    ParamBuilder &operator=(const ParamBuilder &) = delete;

    ParamBuilder(int dev, bool input)
    {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(dev);
        p.device = dev;
        p.channelCount = 1;
        p.sampleFormat = paInt16;
        p.suggestedLatency = d ? (input ? d->defaultLowInputLatency
                                        : d->defaultLowOutputLatency)
                               : 0.02;
        p.hostApiSpecificStreamInfo = nullptr;
#ifdef RR_HAVE_WASAPI
        const PaHostApiInfo *h = d ? Pa_GetHostApiInfo(d->hostApi) : nullptr;
        if (h && h->type == paWASAPI) {
            wasapi.size        = sizeof(PaWasapiStreamInfo);
            wasapi.hostApiType = paWASAPI;
            wasapi.version     = 1;
            wasapi.flags       = paWinWasapiAutoConvert;
            p.hostApiSpecificStreamInfo = &wasapi;
        }
#endif
    }
};

double negotiate(int dev, bool input)
{
    if (!g_paInit || dev < 0) return 0.0;
    const PaDeviceInfo *d = Pa_GetDeviceInfo(dev);
    if (!d) return 0.0;

    ParamBuilder pb(dev, input);
    const PaStreamParameters *in  = input ? &pb.p : nullptr;
    const PaStreamParameters *out = input ? nullptr : &pb.p;

    // 48 kHz d'abord : c'est le debit du protocole, d'Opus, et de tous les
    // logiciels numeriques. Aucun reechantillonnage s'il passe.
    if (Pa_IsFormatSupported(in, out, double(AudioEngine::kAudioRate)) == paFormatIsSupported)
        return double(AudioEngine::kAudioRate);

    // Sinon le debit natif de la carte, puis quelques valeurs classiques.
    if (d->defaultSampleRate > 0.0 &&
        Pa_IsFormatSupported(in, out, d->defaultSampleRate) == paFormatIsSupported)
        return d->defaultSampleRate;

    for (double r : {44100.0, 96000.0, 32000.0, 24000.0, 16000.0, 8000.0}) {
        if (Pa_IsFormatSupported(in, out, r) == paFormatIsSupported) return r;
    }
    return 0.0;
}

QList<AudioDevice> enumerate(bool wantInput, int hostApiIndex)
{
    QList<AudioDevice> list;
    if (!g_paInit) return list;
    const int n = Pa_GetDeviceCount();
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
        if (!d) continue;
        if (hostApiIndex >= 0 && d->hostApi != hostApiIndex) continue;
        if (wantInput  && d->maxInputChannels  < 1) continue;
        if (!wantInput && d->maxOutputChannels < 1) continue;

        AudioDevice a;
        a.index  = i;
        a.name   = QString::fromLocal8Bit(d->name);
        a.maxIn  = d->maxInputChannels;
        a.maxOut = d->maxOutputChannels;
        a.hostApiIndex = d->hostApi;
        a.defaultSampleRate = d->defaultSampleRate;
        a.defaultLowLatency = wantInput ? d->defaultLowInputLatency
                                        : d->defaultLowOutputLatency;
        const PaHostApiInfo *h = Pa_GetHostApiInfo(d->hostApi);
        if (h) a.hostApi = QString::fromLocal8Bit(h->name);
        list.append(a);
    }
    return list;
}

} // namespace

double AudioEngine::probeRate(int deviceIndex, bool input) { return negotiate(deviceIndex, input); }

QList<AudioDevice> AudioEngine::inputDevices(int hostApiIndex)  { return enumerate(true,  hostApiIndex); }
QList<AudioDevice> AudioEngine::outputDevices(int hostApiIndex) { return enumerate(false, hostApiIndex); }

QList<QPair<int, QString>> AudioEngine::hostApis()
{
    QList<QPair<int, QString>> l;
    if (!g_paInit) return l;
    const int n = Pa_GetHostApiCount();
    for (int i = 0; i < n; ++i) {
        const PaHostApiInfo *h = Pa_GetHostApiInfo(i);
        if (h && h->deviceCount > 0)
            l.append({i, QString::fromLocal8Bit(h->name)});
    }
    return l;
}

int AudioEngine::defaultHostApi() { return g_paInit ? Pa_GetDefaultHostApi() : -1; }
int AudioEngine::defaultInput()   { return g_paInit ? Pa_GetDefaultInputDevice()  : -1; }
int AudioEngine::defaultOutput()  { return g_paInit ? Pa_GetDefaultOutputDevice() : -1; }

// ------------------------------------------------------------------ callbacks
int AudioEngine::inCallback(const void *in, void *, unsigned long frames,
                            const PaStreamCallbackTimeInfo *, unsigned long, void *user)
{
    auto *self = static_cast<AudioEngine *>(user);
    const int16_t *src = static_cast<const int16_t *>(in);
    if (!src) return paContinue;

    if (self->m_inMuted.load()) {
        self->m_inPeak.store(0.0f);
        self->m_inResamp.reset();
        return paContinue;
    }

    if (self->m_inScratch.size() < frames) return paContinue;   // securite

    const float gain = self->m_inGain.load();
    float peak = 0.0f;
    for (unsigned long i = 0; i < frames; ++i) {
        float v = float(src[i]) * gain;
        if (v >  32767.0f) v =  32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        self->m_inScratch[i] = int16_t(v);
        const float a = std::fabs(v) / 32768.0f;
        if (a > peak) peak = a;
    }

    if (self->m_inResamp.bypass()) {
        self->m_inRing.write(self->m_inScratch.data(), frames);
    } else {
        self->m_inConverted.clear();
        self->m_inResamp.process(self->m_inScratch.data(), frames, self->m_inConverted);
        self->m_inRing.write(self->m_inConverted.data(), self->m_inConverted.size());
    }

    if (peak > self->m_inPeak.load()) self->m_inPeak.store(peak);
    return paContinue;
}

int AudioEngine::outCallback(const void *, void *out, unsigned long frames,
                             const PaStreamCallbackTimeInfo *, unsigned long, void *user)
{
    auto *self = static_cast<AudioEngine *>(user);
    int16_t *dst = static_cast<int16_t *>(out);

    if (self->m_outMuted.load()) {
        std::memset(dst, 0, frames * sizeof(int16_t));
        self->m_outPeak.store(0.0f);
        self->m_outFifo.clear();
        self->m_outFifoPos = 0;
        self->m_outResamp.reset();
        return paContinue;
    }

    if (self->m_outResamp.bypass()) {
        self->m_outRing.readOrSilence(dst, frames);
    } else {
        // On complete la file de sortie jusqu'a couvrir la demande. Le tirage
        // depuis l'anneau comble les manques par du silence, donc la boucle
        // se termine toujours.
        while (self->m_outFifo.size() - self->m_outFifoPos < frames) {
            const size_t need = frames - (self->m_outFifo.size() - self->m_outFifoPos);
            size_t pull = self->m_outResamp.inputNeeded(need);
            if (pull > self->m_outPulled.size()) pull = self->m_outPulled.size();
            if (pull == 0) break;
            self->m_outRing.readOrSilence(self->m_outPulled.data(), pull);
            self->m_outResamp.process(self->m_outPulled.data(), pull, self->m_outFifo);
        }

        const size_t have = self->m_outFifo.size() - self->m_outFifoPos;
        const size_t take = std::min<size_t>(have, frames);
        if (take) std::memcpy(dst, self->m_outFifo.data() + self->m_outFifoPos,
                              take * sizeof(int16_t));
        if (take < frames) std::memset(dst + take, 0, (frames - take) * sizeof(int16_t));
        self->m_outFifoPos += take;

        // Compactage periodique, sans reallocation.
        if (self->m_outFifoPos > 0 && self->m_outFifoPos == self->m_outFifo.size()) {
            self->m_outFifo.clear();
            self->m_outFifoPos = 0;
        } else if (self->m_outFifoPos > 4096) {
            self->m_outFifo.erase(self->m_outFifo.begin(),
                                  self->m_outFifo.begin() + long(self->m_outFifoPos));
            self->m_outFifoPos = 0;
        }
    }

    const float gain = self->m_outGain.load();
    float peak = 0.0f;
    for (unsigned long i = 0; i < frames; ++i) {
        float v = float(dst[i]) * gain;
        if (v >  32767.0f) v =  32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        dst[i] = int16_t(v);
        const float a = std::fabs(v) / 32768.0f;
        if (a > peak) peak = a;
    }
    if (peak > self->m_outPeak.load()) self->m_outPeak.store(peak);
    return paContinue;
}

// ------------------------------------------------------------------ ouverture
bool AudioEngine::startCapture(int deviceIndex, int framesPerBuffer)
{
    stopCapture();
    if (!g_paInit || deviceIndex < 0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "PortAudio unavailable");
        return false;
    }

    const double rate = negotiate(deviceIndex, true);
    if (rate <= 0.0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "This input device accepted no sample rate");
        return false;
    }
    m_inDevRate = int(rate + 0.5);
    m_inResamp.init(m_inDevRate, kAudioRate);

    // Le tampon demande est exprime au rythme de la carte : on garde la meme
    // duree qu'a 48 kHz pour ne pas allonger la latence sur une carte lente.
    int frames = framesPerBuffer;
    if (frames > 0 && m_inDevRate != kAudioRate)
        frames = int(double(frames) * double(m_inDevRate) / double(kAudioRate) + 0.5);

    const size_t cap = size_t(frames > 0 ? frames : 4096) + 1024;
    m_inScratch.assign(cap, 0);
    m_inConverted.clear();
    m_inConverted.reserve(m_inResamp.maxOutput(cap) + 64);

    ParamBuilder pb(deviceIndex, true);
    m_inRing.reset();
    m_inResamp.reset();

    PaStream *s = nullptr;
    PaError e = Pa_OpenStream(&s, &pb.p, nullptr, rate,
                              frames > 0 ? static_cast<unsigned long>(frames) : paFramesPerBufferUnspecified,
                              paClipOff, &AudioEngine::inCallback, this);
    if (e != paNoError) { m_lastError = QString::fromLocal8Bit(Pa_GetErrorText(e)); return false; }
    e = Pa_StartStream(s);
    if (e != paNoError) {
        Pa_CloseStream(s);
        m_lastError = QString::fromLocal8Bit(Pa_GetErrorText(e));
        return false;
    }
    m_inStream = s;
    return true;
}

bool AudioEngine::startPlayback(int deviceIndex, int framesPerBuffer)
{
    stopPlayback();
    if (!g_paInit || deviceIndex < 0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "PortAudio unavailable");
        return false;
    }

    const double rate = negotiate(deviceIndex, false);
    if (rate <= 0.0) {
        m_lastError = QCoreApplication::translate("AudioEngine", "This output device accepted no sample rate");
        return false;
    }
    m_outDevRate = int(rate + 0.5);
    m_outResamp.init(kAudioRate, m_outDevRate);

    int frames = framesPerBuffer;
    if (frames > 0 && m_outDevRate != kAudioRate)
        frames = int(double(frames) * double(m_outDevRate) / double(kAudioRate) + 0.5);

    const size_t cap = size_t(frames > 0 ? frames : 4096) + 1024;
    m_outPulled.assign(m_outResamp.inputNeeded(cap) + 64, 0);
    m_outFifo.clear();
    m_outFifo.reserve(m_outResamp.maxOutput(m_outPulled.size()) * 2 + 8192);
    m_outFifoPos = 0;

    ParamBuilder pb(deviceIndex, false);
    m_outRing.reset();
    m_outResamp.reset();

    PaStream *s = nullptr;
    PaError e = Pa_OpenStream(&s, nullptr, &pb.p, rate,
                              frames > 0 ? static_cast<unsigned long>(frames) : paFramesPerBufferUnspecified,
                              paClipOff, &AudioEngine::outCallback, this);
    if (e != paNoError) { m_lastError = QString::fromLocal8Bit(Pa_GetErrorText(e)); return false; }
    e = Pa_StartStream(s);
    if (e != paNoError) {
        Pa_CloseStream(s);
        m_lastError = QString::fromLocal8Bit(Pa_GetErrorText(e));
        return false;
    }
    m_outStream = s;
    return true;
}

void AudioEngine::stopCapture()
{
    if (!m_inStream) return;
    Pa_StopStream(m_inStream);
    Pa_CloseStream(m_inStream);
    m_inStream = nullptr;
}

void AudioEngine::stopPlayback()
{
    if (!m_outStream) return;
    Pa_StopStream(m_outStream);
    Pa_CloseStream(m_outStream);
    m_outStream = nullptr;
    m_outFifo.clear();
    m_outFifoPos = 0;
}

void AudioEngine::stopAll() { stopCapture(); stopPlayback(); }

size_t AudioEngine::readCaptured(int16_t *dst, size_t samples)
{
    return m_inRing.read(dst, samples);
}

void AudioEngine::pushPlayback(const int16_t *src, size_t samples)
{
    // Si la file deborde (horloges des deux machines legerement differentes),
    // on jette les plus anciens echantillons plutot que d'accumuler du retard.
    if (m_outRing.freeSpace() < samples) {
        int16_t drop[480];
        size_t need = samples - m_outRing.freeSpace();
        while (need > 0) {
            const size_t n = need > 480 ? 480 : need;
            const size_t got = m_outRing.read(drop, n);
            if (got == 0) break;
            need -= got;
        }
    }
    m_outRing.write(src, samples);
}

float AudioEngine::captureLevel()  { return m_inPeak.exchange(0.0f); }
float AudioEngine::playbackLevel() { return m_outPeak.exchange(0.0f); }

} // namespace rr

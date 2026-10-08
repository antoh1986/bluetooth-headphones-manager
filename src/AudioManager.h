#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>

struct pa_context;
struct pa_glib_mainloop;
struct pa_card_info;
struct pa_sink_info;
struct pa_server_info;

// One card profile of a Bluetooth audio device as the sound server offers it,
// e.g. "a2dp-sink" / "High Fidelity Playback (A2DP Sink, codec LDAC)".
struct AudioProfile {
    // Rough playback quality, from the codec (see profileQuality()).
    enum class Quality { None, Low, Medium, Best };

    QString name;
    QString description;
    bool    available = true;
    bool    hasOutput = false;   // the profile provides a sink
    bool    hasInput  = false;   // ... and/or a source (headset microphone)
    quint32 priority  = 0;
    Quality quality   = Quality::None;   // derived from the fields above

    bool operator==(const AudioProfile &o) const {
        return name == o.name && description == o.description &&
               available == o.available && hasOutput == o.hasOutput &&
               hasInput == o.hasInput && priority == o.priority;
    }
};

// The sound-server side of Bluetooth audio devices: their card profiles
// (A2DP codec / headset mode) and whether the system's default output is
// theirs. Talks to the PulseAudio API (libpulse), which pipewire-pulse serves
// too. libpulse runs on its GLib main loop, i.e. on the GLib context Qt's own
// event dispatcher iterates, so everything stays on the GUI thread. Like
// BluezManager, every call is asynchronous and state is tracked through the
// server's change events -- no polling. The UI only uses the accessors,
// slots and changed() signal below.
class AudioManager : public QObject
{
    Q_OBJECT
public:
    // Where the system sound goes, from one Bluetooth device's point of view.
    enum class Output {
        Unknown,    // no sound server, or it has no card for the device
        Elsewhere,  // the default output is another device
        Here,       // the default output is this device
    };

    explicit AudioManager(QObject *parent = nullptr);
    ~AudioManager() override;

    void start();

    // All keyed by the Bluetooth address (any case).
    QList<AudioProfile> profiles(const QString &address) const;
    QString activeProfile(const QString &address) const;
    Output output(const QString &address) const;
    QString defaultOutputName() const { return m_defaultSinkDescription; }

public slots:
    void setProfile(const QString &address, const QString &profile);
    // Make the device the default output, switching it to its best playback
    // profile first if the active one has no output (e.g. "Off").
    void routeOutputTo(const QString &address);

signals:
    void changed();

private:
    struct Callbacks;            // libpulse C callbacks (AudioManager.cpp)
    friend struct Callbacks;

    struct Card {
        QString address;
        QList<AudioProfile> profiles;
        QString active;

        bool operator==(const Card &o) const {
            return address == o.address && profiles == o.profiles && active == o.active;
        }
    };
    struct Sink {
        QString name;
        QString address;
    };

    void connectToServer();
    void dropContext();
    void onReady();
    void onLost();
    void updateCard(const pa_card_info &info);
    void updateSink(const pa_sink_info &info);
    void updateServer(const pa_server_info &info);
    void removeCard(quint32 index);
    void removeSink(quint32 index);
    void setDefaultSink(const QString &name);
    const Card *cardFor(const QString &address) const;
    static bool activeHasOutput(const Card &card);

    pa_glib_mainloop     *m_mainloop = nullptr;
    pa_context           *m_context = nullptr;
    bool                  m_ready = false;
    int                   m_retryMs = 1000;   // reconnect back-off
    QHash<quint32, Card>  m_cards;            // Bluetooth cards by card index
    QHash<quint32, Sink>  m_sinks;            // Bluetooth sinks by sink index
    QString               m_defaultSink;      // default sink name
    QString               m_defaultSinkDescription;
    QString               m_pendingRoute;     // address waiting for its sink
};

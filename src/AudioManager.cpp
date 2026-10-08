// libpulse first: its GLib header must not see Qt's "signals"/"slots" macros.
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include "AudioManager.h"

#include <QAbstractEventDispatcher>
#include <QTimer>
#include <QDebug>

#include <algorithm>

namespace {

void drop(pa_operation *op)
{
    if (op)
        pa_operation_unref(op);
}

// Bluetooth address of a card or sink, upper case; empty if it is not a
// Bluetooth one. PipeWire sets api.bluez5.address, PulseAudio's
// module-bluez5-device puts the address in device.string.
QString bluetoothAddress(const pa_proplist *props)
{
    const char *addr = pa_proplist_gets(props, "api.bluez5.address");
    if (!addr) {
        const char *bus = pa_proplist_gets(props, PA_PROP_DEVICE_BUS);
        if (bus && qstrcmp(bus, "bluetooth") == 0)
            addr = pa_proplist_gets(props, PA_PROP_DEVICE_STRING);
    }
    return addr ? QString::fromUtf8(addr).toUpper() : QString();
}

// Headset modes (HSP/HFP) carry narrow- or wide-band speech; LDAC, aptX HD /
// Lossless and LC3plus are the high-resolution A2DP codecs; every other A2DP
// codec (SBC, SBC-XQ, AAC, aptX, Opus, ...) sits in between. The codec is
// named in the profile name (PipeWire: a2dp-sink-ldac) and/or description
// ("..., codec LDAC"); the server's own priority does not follow quality.
AudioProfile::Quality profileQuality(const AudioProfile &p)
{
    if (!p.hasOutput)
        return AudioProfile::Quality::None; // "Off", capture only
    const QString id = (p.name + QLatin1Char(' ') + p.description).toLower();
    for (const char *speech : {"headset", "handsfree", "head unit", "hsp", "hfp"}) {
        if (id.contains(QLatin1String(speech)))
            return AudioProfile::Quality::Low;
    }
    for (const char *hires : {"ldac", "aptx hd", "aptx_hd", "aptx-hd",
                              "aptx lossless", "aptx_lossless", "lc3plus"}) {
        if (id.contains(QLatin1String(hires)))
            return AudioProfile::Quality::Best;
    }
    return AudioProfile::Quality::Medium;
}

// Menu order: Off, playback-only (A2DP), playback + microphone (headset).
int profileGroup(const AudioProfile &p)
{
    if (!p.hasOutput && !p.hasInput)
        return 0;
    if (!p.hasInput)
        return 1;
    return p.hasOutput ? 2 : 3;
}

} // namespace

struct AudioManager::Callbacks
{
    static AudioManager *self(pa_context *c, void *userdata)
    {
        auto *mgr = static_cast<AudioManager *>(userdata);
        return c == mgr->m_context ? mgr : nullptr; // ignore a dropped context
    }

    static void state(pa_context *c, void *userdata)
    {
        AudioManager *mgr = self(c, userdata);
        if (!mgr)
            return;
        switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            mgr->onReady();
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            mgr->onLost();
            break;
        default:
            break;
        }
    }

    static void event(pa_context *c, pa_subscription_event_type_t t, uint32_t index,
                      void *userdata)
    {
        AudioManager *mgr = self(c, userdata);
        if (!mgr)
            return;
        const bool removed =
            (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE;
        switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
        case PA_SUBSCRIPTION_EVENT_CARD:
            if (removed)
                mgr->removeCard(index);
            else
                drop(pa_context_get_card_info_by_index(c, index, &card, mgr));
            break;
        case PA_SUBSCRIPTION_EVENT_SINK:
            if (removed)
                mgr->removeSink(index);
            else
                drop(pa_context_get_sink_info_by_index(c, index, &sink, mgr));
            break;
        case PA_SUBSCRIPTION_EVENT_SERVER:
            drop(pa_context_get_server_info(c, &server, mgr));
            break;
        default:
            break;
        }
    }

    // eol != 0: end of a list (1) or an error such as an object that went
    // away before we asked for it (-1).
    static void card(pa_context *c, const pa_card_info *info, int eol, void *userdata)
    {
        if (AudioManager *mgr = self(c, userdata); mgr && !eol && info)
            mgr->updateCard(*info);
    }

    static void sink(pa_context *c, const pa_sink_info *info, int eol, void *userdata)
    {
        if (AudioManager *mgr = self(c, userdata); mgr && !eol && info)
            mgr->updateSink(*info);
    }

    static void server(pa_context *c, const pa_server_info *info, void *userdata)
    {
        if (AudioManager *mgr = self(c, userdata); mgr && info)
            mgr->updateServer(*info);
    }

    // userdata: what the operation was, for the log.
    static void result(pa_context *c, int success, void *userdata)
    {
        if (!success)
            qWarning().noquote() << "Audio:" << static_cast<const char *>(userdata)
                                 << "failed:" << pa_strerror(pa_context_errno(c));
    }
};

AudioManager::AudioManager(QObject *parent)
    : QObject(parent)
{
}

AudioManager::~AudioManager()
{
    dropContext();
    if (m_mainloop)
        pa_glib_mainloop_free(m_mainloop);
}

void AudioManager::start()
{
    // libpulse's GLib main loop only runs if Qt's event loop is a GLib one,
    // which it is unless Qt was built without GLib or QT_NO_GLIB is set.
    const QAbstractEventDispatcher *dispatcher = QAbstractEventDispatcher::instance();
    if (!dispatcher || !dispatcher->inherits("QEventDispatcherGlib")) {
        qWarning() << "Audio: Qt is not running a GLib event loop; audio output and"
                      " profile controls are disabled";
        return;
    }
    m_mainloop = pa_glib_mainloop_new(nullptr);
    connectToServer();
}

void AudioManager::connectToServer()
{
    pa_proplist *props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "Bluetooth Headphones Manager");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "bluetooth-headphones-manager");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ICON_NAME, "bluetooth-headphones-manager");
    m_context = pa_context_new_with_proplist(pa_glib_mainloop_get_api(m_mainloop),
                                             nullptr, props);
    pa_proplist_free(props);
    if (!m_context) {
        qWarning() << "Audio: cannot create a sound server context";
        return;
    }
    pa_context_set_state_callback(m_context, &Callbacks::state, this);

    // NOFAIL: if the server is not up yet, wait for it instead of failing.
    if (pa_context_connect(m_context, nullptr,
                           pa_context_flags_t(PA_CONTEXT_NOFAIL | PA_CONTEXT_NOAUTOSPAWN),
                           nullptr) < 0)
        onLost();
}

void AudioManager::dropContext()
{
    if (!m_context)
        return;
    pa_context_set_state_callback(m_context, nullptr, nullptr);
    pa_context_set_subscribe_callback(m_context, nullptr, nullptr);
    pa_context_disconnect(m_context);
    pa_context_unref(m_context);
    m_context = nullptr;
}

void AudioManager::onReady()
{
    qInfo().noquote() << "Audio: connected to the sound server"
                      << pa_context_get_server(m_context);
    m_ready = true;
    m_retryMs = 1000;

    pa_context_set_subscribe_callback(m_context, &Callbacks::event, this);
    drop(pa_context_subscribe(m_context,
                              pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_CARD |
                                                     PA_SUBSCRIPTION_MASK_SINK |
                                                     PA_SUBSCRIPTION_MASK_SERVER),
                              nullptr, nullptr));
    drop(pa_context_get_server_info(m_context, &Callbacks::server, this));
    drop(pa_context_get_card_info_list(m_context, &Callbacks::card, this));
    drop(pa_context_get_sink_info_list(m_context, &Callbacks::sink, this));
}

// The connection failed or the server went away (e.g. pipewire-pulse
// restarted): forget its state and connect again after a back-off.
void AudioManager::onLost()
{
    qWarning().noquote() << "Audio: no connection to the sound server:"
                         << pa_strerror(pa_context_errno(m_context))
                         << QStringLiteral("(retrying in %1 s)").arg(m_retryMs / 1000);
    dropContext();

    const bool hadState = m_ready || !m_cards.isEmpty();
    m_ready = false;
    m_cards.clear();
    m_sinks.clear();
    m_defaultSink.clear();
    m_defaultSinkDescription.clear();
    m_pendingRoute.clear();
    if (hadState)
        emit changed();

    QTimer::singleShot(m_retryMs, this, &AudioManager::connectToServer);
    m_retryMs = qMin(m_retryMs * 2, 60000);
}

void AudioManager::updateCard(const pa_card_info &info)
{
    Card card;
    card.address = bluetoothAddress(info.proplist);
    if (card.address.isEmpty())
        return; // not a Bluetooth device

    for (uint32_t i = 0; i < info.n_profiles; ++i) {
        const pa_card_profile_info2 *p = info.profiles2[i];
        AudioProfile profile;
        profile.name = QString::fromUtf8(p->name);
        profile.description = QString::fromUtf8(p->description);
        if (profile.description.isEmpty())
            profile.description = profile.name;
        profile.available = p->available != 0;
        profile.hasOutput = p->n_sinks > 0;
        profile.hasInput = p->n_sources > 0;
        profile.priority = p->priority;
        profile.quality = profileQuality(profile);
        card.profiles << profile;
    }
    std::stable_sort(card.profiles.begin(), card.profiles.end(),
                     [](const AudioProfile &a, const AudioProfile &b) {
                         return profileGroup(a) < profileGroup(b);
                     });
    if (info.active_profile2)
        card.active = QString::fromUtf8(info.active_profile2->name);

    const auto it = m_cards.constFind(info.index);
    if (it != m_cards.constEnd() && *it == card)
        return;
    if (it == m_cards.constEnd() || it->active != card.active)
        qInfo().noquote() << "Audio:" << card.address << "profile" << card.active;
    m_cards.insert(info.index, card);
    emit changed();
}

void AudioManager::updateSink(const pa_sink_info &info)
{
    const QString name = QString::fromUtf8(info.name);
    bool dirty = false;

    if (name == m_defaultSink) {
        const QString description = QString::fromUtf8(info.description);
        if (description != m_defaultSinkDescription) {
            m_defaultSinkDescription = description;
            dirty = true;
        }
    }

    const QString address = bluetoothAddress(info.proplist);
    if (!address.isEmpty()) {
        const auto it = m_sinks.constFind(info.index);
        if (it == m_sinks.constEnd() || it->name != name || it->address != address) {
            m_sinks.insert(info.index, Sink{name, address});
            dirty = true;
        }
        if (address == m_pendingRoute) {
            m_pendingRoute.clear();
            setDefaultSink(name);
        }
    }

    if (dirty)
        emit changed();
}

void AudioManager::updateServer(const pa_server_info &info)
{
    const QString sink = QString::fromUtf8(info.default_sink_name ? info.default_sink_name : "");
    if (sink == m_defaultSink)
        return;
    qInfo().noquote() << "Audio: default output is" << (sink.isEmpty() ? QStringLiteral("none") : sink);
    m_defaultSink = sink;
    m_defaultSinkDescription.clear();
    if (!sink.isEmpty())
        drop(pa_context_get_sink_info_by_name(m_context, info.default_sink_name,
                                              &Callbacks::sink, this));
    emit changed();
}

void AudioManager::removeCard(quint32 index)
{
    const auto it = m_cards.find(index);
    if (it == m_cards.end())
        return;
    if (it->address == m_pendingRoute)
        m_pendingRoute.clear();
    m_cards.erase(it);
    emit changed();
}

void AudioManager::removeSink(quint32 index)
{
    if (m_sinks.remove(index))
        emit changed();
}

const AudioManager::Card *AudioManager::cardFor(const QString &address) const
{
    const QString addr = address.toUpper();
    for (const Card &card : m_cards) {
        if (card.address == addr)
            return &card;
    }
    return nullptr;
}

bool AudioManager::activeHasOutput(const Card &card)
{
    for (const AudioProfile &p : card.profiles) {
        if (p.name == card.active)
            return p.hasOutput;
    }
    return false;
}

QList<AudioProfile> AudioManager::profiles(const QString &address) const
{
    const Card *card = cardFor(address);
    return card ? card->profiles : QList<AudioProfile>();
}

QString AudioManager::activeProfile(const QString &address) const
{
    const Card *card = cardFor(address);
    return card ? card->active : QString();
}

AudioManager::Output AudioManager::output(const QString &address) const
{
    const Card *card = m_ready ? cardFor(address) : nullptr;
    if (!card)
        return Output::Unknown;
    // WirePlumber 0.5 keeps a device's sink across profile changes, even on
    // "Off"; nothing reaches the device then.
    if (!activeHasOutput(*card))
        return Output::Elsewhere;
    const QString addr = address.toUpper();
    for (const Sink &sink : m_sinks) {
        if (sink.address == addr && sink.name == m_defaultSink)
            return Output::Here;
    }
    return Output::Elsewhere;
}

void AudioManager::setProfile(const QString &address, const QString &profile)
{
    if (!m_ready)
        return;
    const auto it = std::find_if(m_cards.constBegin(), m_cards.constEnd(),
                                 [addr = address.toUpper()](const Card &c) {
                                     return c.address == addr;
                                 });
    if (it == m_cards.constEnd() || it->active == profile)
        return;
    qInfo().noquote() << "Audio: switching" << it->address << "to profile" << profile;
    drop(pa_context_set_card_profile_by_index(m_context, it.key(),
                                              profile.toUtf8().constData(),
                                              &Callbacks::result,
                                              const_cast<char *>("switching the profile")));
}

void AudioManager::routeOutputTo(const QString &address)
{
    if (!m_ready)
        return;
    const QString addr = address.toUpper();
    const Card *card = cardFor(addr);
    if (!card)
        return;
    QString sinkName;
    for (const Sink &sink : m_sinks) {
        if (sink.address == addr) {
            sinkName = sink.name;
            break;
        }
    }
    if (activeHasOutput(*card)) {
        if (!sinkName.isEmpty())
            setDefaultSink(sinkName);
        return;
    }

    // The active profile has no output: switch to the best playback profile
    // first. PipeWire with WirePlumber 0.5 keeps the device's sink meanwhile,
    // so it can be made the default right away; otherwise updateSink() does
    // that once the new sink appears.
    const AudioProfile *best = nullptr;
    for (const AudioProfile &p : card->profiles) {
        if (p.available && p.hasOutput && (!best || p.priority > best->priority))
            best = &p;
    }
    if (!best) {
        qWarning().noquote() << "Audio:" << addr << "has no playback profile";
        return;
    }
    const QString profile = best->name;   // setProfile() leaves *card alone
    if (sinkName.isEmpty())
        m_pendingRoute = addr;
    setProfile(addr, profile);
    if (!sinkName.isEmpty())
        setDefaultSink(sinkName);
}

void AudioManager::setDefaultSink(const QString &name)
{
    qInfo().noquote() << "Audio: making" << name << "the default output";
    drop(pa_context_set_default_sink(m_context, name.toUtf8().constData(),
                                     &Callbacks::result,
                                     const_cast<char *>("setting the default output")));
}

#pragma once
#include <QString>
struct _MSFactory;
namespace stel {
struct DummyAudioDevices { QString silence, tone, speaker; };
DummyAudioDevices installDummyAudio(_MSFactory *factory);
bool writeDummyCameraImage(const QString &path);
inline QString dummyMicTone() { return "stel:dummy-mic-tone"; }
inline QString dummyMicSilence() { return "stel:dummy-mic-silence"; }
inline QString dummySpeaker() { return "stel:dummy-speaker"; }
inline QString dummyCamera() { return "stel:dummy-camera"; }
}

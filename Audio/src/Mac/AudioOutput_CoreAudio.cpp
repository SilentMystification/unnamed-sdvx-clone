/*
	CoreAudio backend for macOS 10.4 Tiger/PowerPC, where SDL2 (Audio/src/Unix/AudioOutput_SDL.cpp)
	does not run (see the Graphics/windowing port notes in
	/home/andrew/.claude/plans/humming-sleeping-stonebraker.md for the same class of blocker
	on the windowing side).

	Uses the classic AudioHardware/AudioDeviceIOProc API (stable since Mac OS X 10.0), not the
	AUHAL/AudioComponent-based API that later became preferred - it's simpler, needs no
	AudioComponent boilerplate, and is exactly what existing Mac OS X 10.4-era CoreAudio example
	code and documentation use. AudioHardwareGetProperty is soft-deprecated in favor of
	AudioObjectGetPropertyData starting exactly at 10.4, but still fully functional there.

	Unlike WASAPI (which negotiates a specific format) this queries the default output device's
	own native stream format and mixes into whatever that is - safer across whatever hardware a
	given G4 Mac actually has, matching how the SDL backend just accepts SDL's negotiated format.
	IsIntegerFormat() reports which one it got, exactly like the other two backends.

	UNVERIFIED: no macOS/PPC hardware in the environment this was written in to run-test against.
*/
#include "stdafx.h"
#include "AudioOutput.hpp"

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>

class AudioOutput_Impl
{
public:
	AudioDeviceID m_device = kAudioDeviceUnknown;
	AudioStreamBasicDescription m_format;
	IMixer* m_mixer = nullptr;
	bool m_running = false;

	~AudioOutput_Impl()
	{
		CloseDevice();
	}

	void CloseDevice()
	{
		if(m_device == kAudioDeviceUnknown)
			return;
		if(m_running)
		{
			AudioDeviceStop(m_device, &AudioOutput_Impl::IOProcTrampoline);
			m_running = false;
		}
		AudioDeviceRemoveIOProc(m_device, &AudioOutput_Impl::IOProcTrampoline);
		m_device = kAudioDeviceUnknown;
	}

	bool Init()
	{
		UInt32 size = sizeof(m_device);
		OSStatus err = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice, &size, &m_device);
		if(err != noErr || m_device == kAudioDeviceUnknown)
		{
			Log("Failed to find the default CoreAudio output device", Logger::Severity::Error);
			return false;
		}

		size = sizeof(m_format);
		err = AudioDeviceGetProperty(m_device, 0, false, kAudioDevicePropertyStreamFormat, &size, &m_format);
		if(err != noErr)
		{
			Log("Failed to query the CoreAudio output device's stream format", Logger::Severity::Error);
			m_device = kAudioDeviceUnknown;
			return false;
		}

		Logf("CoreAudio output: %d channels, %d Hz, %s", Logger::Severity::Info,
			(int)m_format.mChannelsPerFrame, (int)m_format.mSampleRate,
			(m_format.mFormatFlags & kAudioFormatFlagIsFloat) ? "float" : "integer");

		err = AudioDeviceAddIOProc(m_device, &AudioOutput_Impl::IOProcTrampoline, this);
		if(err != noErr)
		{
			Log("Failed to register the CoreAudio IO callback", Logger::Severity::Error);
			m_device = kAudioDeviceUnknown;
			return false;
		}
		return true;
	}

	void Start()
	{
		if(m_running || m_device == kAudioDeviceUnknown)
			return;
		if(AudioDeviceStart(m_device, &AudioOutput_Impl::IOProcTrampoline) == noErr)
			m_running = true;
	}
	void Stop()
	{
		if(!m_running)
			return;
		AudioDeviceStop(m_device, &AudioOutput_Impl::IOProcTrampoline);
		m_running = false;
	}

	Vector<uint8> m_scratch;

	// Runs on CoreAudio's own realtime IO thread.
	OSStatus IOProc(AudioBufferList* outOutputData)
	{
		if(outOutputData->mNumberBuffers == 0)
			return noErr;
		uint32 bytesPerSample = m_format.mBitsPerChannel / 8;

		if(!(m_format.mFormatFlags & kAudioFormatFlagIsNonInterleaved))
		{
			// Common case - one buffer, channels interleaved LRLRLR..., matches what
			// IMixer::Mix already produces directly.
			AudioBuffer& buf = outOutputData->mBuffers[0];
			uint32 numSamples = buf.mDataByteSize / (bytesPerSample * m_format.mChannelsPerFrame);
			if(m_mixer && numSamples > 0)
				m_mixer->Mix(buf.mData, numSamples);
			return noErr;
		}

		// Non-interleaved: one mono buffer per channel (mBuffers[c].mDataByteSize is
		// already per-channel, not multiplied by channel count). The previous code here
		// unconditionally treated this exactly like the interleaved case above - divided
		// the per-channel byte count by channelsPerFrame again (undercounting frames by
		// roughly 2x for stereo) and only ever wrote into mBuffers[0], leaving every other
		// channel's buffer as whatever was already in memory. That plays back as constant
		// audible corruption, not just a glitch at stream transitions - matches the real
		// "crackly audio" report better than the async-preview-load theory did, since that
		// fix (confirmed fast and non-blocking via logging) didn't change this at all.
		if(outOutputData->mNumberBuffers < m_format.mChannelsPerFrame)
			return noErr;

		uint32 numSamples = outOutputData->mBuffers[0].mDataByteSize / bytesPerSample;
		if(!m_mixer || numSamples == 0)
			return noErr;

		size_t scratchBytes = (size_t)numSamples * bytesPerSample * m_format.mChannelsPerFrame;
		if(m_scratch.size() < scratchBytes)
			m_scratch.resize(scratchBytes);
		m_mixer->Mix(m_scratch.data(), numSamples);

		for(UInt32 c = 0; c < outOutputData->mNumberBuffers && c < m_format.mChannelsPerFrame; c++)
		{
			uint8* dst = (uint8*)outOutputData->mBuffers[c].mData;
			const uint8* src = m_scratch.data() + (size_t)c * bytesPerSample;
			for(uint32 i = 0; i < numSamples; i++)
			{
				memcpy(dst + (size_t)i * bytesPerSample, src, bytesPerSample);
				src += (size_t)bytesPerSample * m_format.mChannelsPerFrame;
			}
		}
		return noErr;
	}

	static OSStatus IOProcTrampoline(AudioDeviceID, const AudioTimeStamp*, const AudioBufferList*,
		const AudioTimeStamp*, AudioBufferList* outOutputData, const AudioTimeStamp*, void* inClientData)
	{
		return static_cast<AudioOutput_Impl*>(inClientData)->IOProc(outOutputData);
	}
};

AudioOutput::AudioOutput()
{
	m_impl = new AudioOutput_Impl();
}
AudioOutput::~AudioOutput()
{
	delete m_impl;
}
bool AudioOutput::Init(bool exclusive)
{
	// CoreAudio's shared HAL device model has no exclusive-mode concept comparable to
	// WASAPI's - the `exclusive` request is accepted but has no effect here.
	return m_impl->Init();
}
void AudioOutput::Start(IMixer* mixer)
{
	m_impl->m_mixer = mixer;
	m_impl->Start();
}
void AudioOutput::Stop()
{
	m_impl->Stop();
	m_impl->m_mixer = nullptr;
}
uint32_t AudioOutput::GetNumChannels() const
{
	return m_impl->m_format.mChannelsPerFrame;
}
uint32_t AudioOutput::GetSampleRate() const
{
	return (uint32_t)m_impl->m_format.mSampleRate;
}
double AudioOutput::GetBufferLength() const
{
	// Not queried from the device (see WASAPI/SDL backends for the same simplification on
	// their own less-precise paths); 0 matches the SDL backend's existing behavior.
	return 0;
}
bool AudioOutput::IsIntegerFormat() const
{
	return !(m_impl->m_format.mFormatFlags & kAudioFormatFlagIsFloat);
}
#endif // __APPLE__

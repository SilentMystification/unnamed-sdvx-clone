#include "stdafx.h"
#include "AudioStreamBase.hpp"

class AudioStreamWav : public AudioStreamBase
{
private:
	struct WavHeader
	{
		char id[4];
		uint32 nLength;

		bool operator==(const char *rhs) const
		{
			return strncmp(id, rhs, 4) == 0;
		}
		bool operator!=(const char *rhs) const
		{
			return !(*this == rhs);
		}
	};

	struct WavFormat
	{
		uint16 nFormat;
		uint16 nChannels;
		uint32 nSampleRate;
		uint32 nByteRate;
		uint16 nBlockAlign;
		uint16 nBitsPerSample;
	};
	Buffer m_Internaldata;
	WavFormat m_format;
	Vector<float> m_pcm;
	int64 m_playbackPointer = 0;
	uint64 m_dataPosition = 0;
	uint32 m_decode_ms_adpcm(const Buffer &encoded, Buffer *decoded, uint64 pos);

	// WAV is a fixed little-endian format (RIFF spec) regardless of host - these are
	// no-ops on a little-endian host and byte-swap in place on a big-endian one (PPC).
	// Without this, every multi-byte header field (chunk length, sample rate, etc.) and
	// every 16-bit PCM sample comes out wrong on PPC - chunk length in particular getting
	// byte-swapped into a huge garbage value feeds straight into a Buffer::resize() call
	// below, which is a near-guaranteed crash on any WAV load, not just wrong audio.
	static void m_swapHeader(WavHeader& h);
	static void m_swapFormat(WavFormat& f);
	static void m_swapPcm16(void* data, size_t byteLen);

protected:
	bool Init(Audio *audio, const String &path, bool preload) override;
	int32 GetStreamPosition_Internal() override;
	int32 GetStreamRate_Internal() override;
	void SetPosition_Internal(int32 pos) override;
	int32 DecodeData_Internal() override;
	float *GetPCM_Internal() override;
	uint32 GetSampleRate_Internal() const override;
	uint64 GetSampleCount_Internal() const override;

public:
	AudioStreamWav() = default;
	~AudioStreamWav();
	static Ref<AudioStream> Create(class Audio *audio, const String &path, bool preload);
};

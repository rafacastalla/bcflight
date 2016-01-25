#ifndef RASPICAM_H
#define RASPICAM_H

#include <fstream>
#include <functional>
#include <Thread.h>
#include <Camera.h>

class Main;
class Link;
typedef struct video_context video_context;
typedef struct audio_context audio_context;
typedef struct AVFormatContext AVFormatContext;
typedef struct AVStream AVStream;

class Raspicam : public Camera
{
public:
	Raspicam( Link* link );
	~Raspicam();

protected:
	void SetupRecord();
	bool LiveThreadRun();
	bool RecordThreadRun();
	bool MixedThreadRun();

	int LiveSend( char* data, int datalen );
	int RecordWrite( char* data, int datalen, int64_t pts = 0, bool audio = false );

	Link* mLink;
	HookThread<Raspicam>* mLiveThread;
	HookThread<Raspicam>* mRecordThread;
	video_context* mVideoContext;
	audio_context* mAudioContext;
	bool mNeedNextEnc1ToBeFilled;
	bool mNeedNextEnc2ToBeFilled;
	bool mNeedNextAudioToBeFilled;
	bool mLiveSkipNextFrame;
	int mLiveFrameCounter;
	uint64_t mLiveTicks;
	uint64_t mRecordTicks;
	uint64_t mLedTick;
	bool mLedState;


	// Record
	AVFormatContext* mRecordContext;
	AVStream* mAudioStream;
	AVStream* mVideoStream;
	uint64_t mRecordPTSBase;
	uint32_t mRecordFrameCounter;

	char* mRecordFrameData;
	int mRecordFrameDataSize;
	int mRecordFrameSize;

	std::ofstream* mRecordStream; // TODO : use board-specific file instead
};

#endif // RASPICAM_H
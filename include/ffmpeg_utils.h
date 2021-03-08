/*****************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or
nondisclosure agreement with Intel Corporation and may not be copied
or disclosed except in accordance with the terms of that agreement.
Copyright(c) 2005-2014 Intel Corporation. All Rights Reserved.

*****************************************************************************/

#pragma once

#include <stdio.h>

#include "mfxvideo++.h"
extern "C" {
//In Windows, Add "FFMPEG_ROOT" environment variable to point to the root directory of the FFmpeg Dev package.
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
}

#define STREAM_FRAME_RATE 25 /* 25 images/s */

/* The control parameters for the demux/decoding process */
struct demuxControl {
    int audioIdx;
    FILE* audioOutPCM;
    AVCodec *audioDec;
    AVCodecContext *audioDec_ctx;
    AVDictionary *opts;
    int videoIdx;
    AVFormatContext* avfCtx;
    bool enableFilter;
    AVBSFContext* bsfCtx;
    mfxU32 videoType;
    mfxU16 width;
    mfxU16 height;
    bool enableOutput;
};

/* The control parameters for the mux/encoding process */
struct muxControl {
    int audioIdx;
    FILE* audioInPCM;
    AVCodec *audioCodec;
    AVCodecContext *audioEnc_ctx;
    AVStream* audioStream;
    AVFrame* audioFrame;
    AVPacket *audioPacket;
    int64_t	currentPts;
    bool defaultFormat; //True: the default file format is MKV
    AVFormatContext* avfCtx;
    mfxU32 CodecId;
    mfxI32 frameRateD;
    mfxI32 frameRateN;
    mfxU16 nBitRate;   
    mfxU16 nDstWidth; // destination picture width, specified if resizing required
    mfxU16 nDstHeight; // destination picture height, specified if resizing required
    mfxU32 nProcessedFramesNum;
};

// Initialize the Demuxer controller based on the input file
mfxStatus openDemuxControl(demuxControl* ffmpegCtrl, const char *filename);

// Release and clear the Demuxer resources
mfxStatus closeDemuxControl(demuxControl* ffmpegCtrl);

// Initialize the Demuxer controller based on the input file
mfxStatus openMuxControl(muxControl* ffmpegCtrl, const char *filename);

// Release and clear the Demuxer resources
mfxStatus closeMuxControl(muxControl* ffmpegCtrl);

// Write bit stream data for frame to file through FFMpeg muxer
mfxStatus ffmpegWriteFrame(mfxBitstream* pMfxBitstream, muxControl* muxCtrl);

// Read bit stream data from FFMpeg demuxer. Stream is read as large chunks (= many frames)
mfxStatus ffmpegReadFrame(mfxBitstream* pBS, demuxControl* filterCtrl);

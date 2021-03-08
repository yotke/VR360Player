/*****************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or
nondisclosure agreement with Intel Corporation and may not be copied
or disclosed except in accordance with the terms of that agreement.
Copyright(c) 2005-2014 Intel Corporation. All Rights Reserved.

*****************************************************************************/

#include "ffmpeg_utils.h"
#include "sample_defs.h"

#ifdef DECODE_AUDIO
/* Initialize the audio control for the audio decoding */
static bool decode_open_audio(demuxControl* audioCtrl)
{
    audioCtrl->audioDec = NULL;
    audioCtrl->audioDec_ctx = NULL;
    audioCtrl->opts = NULL;
    audioCtrl->audioOutPCM = NULL;

    // Open file for raw audio data output
    if (audioCtrl->enableOutput) {
        audioCtrl->audioOutPCM=fopen("audio.dat", "wb");
        if (!audioCtrl->audioOutPCM) {
            printf("FFMPEG: Could not open audio output file\n");
            return false;
        }
   }

    /* find decoder for the stream */
    audioCtrl->audioDec = avcodec_find_decoder(audioCtrl->avfCtx->streams[audioCtrl->audioIdx]->codecpar->codec_id);
    if (!audioCtrl->audioDec) {
        fprintf(stderr, "Failed to find %s codec\n",
                av_get_media_type_string(audioCtrl->avfCtx->streams[audioCtrl->audioIdx]->codecpar->codec_type));
        return MFX_ERR_UNKNOWN;
    }
    
    /* Allocate a codec context for the decoder */
    audioCtrl->audioDec_ctx = avcodec_alloc_context3(audioCtrl->audioDec);
    if (!audioCtrl->audioDec_ctx) {
        fprintf(stderr, "Failed to allocate the %s codec context\n",
            av_get_media_type_string(audioCtrl->avfCtx->streams[audioCtrl->audioIdx]->codecpar->codec_type));
        return false;
    }

    /* Copy codec parameters from input stream to output codec context */
    if (avcodec_parameters_to_context(audioCtrl->audioDec_ctx, audioCtrl->avfCtx->streams[audioCtrl->audioIdx]->codecpar) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
            av_get_media_type_string(audioCtrl->avfCtx->streams[audioCtrl->audioIdx]->codecpar->codec_type));
        return false;
    }

    /* Init the audio decoder */
    av_dict_set(&audioCtrl->opts, "refcounted_frames", "0", 0);
    if (avcodec_open2(audioCtrl->audioDec_ctx, audioCtrl->audioDec, &audioCtrl->opts) < 0) {
        fprintf(stderr, "Failed to open %s codec\n",
            av_get_media_type_string(audioCtrl->avfCtx->streams[audioCtrl->audioIdx]->codecpar->codec_type));
        return false;
    }

    return true;
}

/* Release the resources */
static bool decode_close_audio(demuxControl* audioCtrl)
{
    if(audioCtrl->audioOutPCM)
        fclose(audioCtrl->audioOutPCM);

    return true;
}

/* Decode and write the raw audio data to the file in signed 16bit PCM format */
static bool decode_write_audio(demuxControl* audioCtrl, AVPacket* pPacket)
{
    AVFrame* aFrame = av_frame_alloc();
    int got_frame = 0;
    int ret, data_size;

    if (pPacket) {
        if ((ret = avcodec_send_packet(audioCtrl->audioDec_ctx, pPacket)) < 0)
            return ret == AVERROR_EOF ? true : false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(audioCtrl->audioDec_ctx, aFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        else if (ret < 0)
            return false;

        data_size = av_get_bytes_per_sample(audioCtrl->audioDec_ctx->sample_fmt);
        if (data_size < 0) {
            /* This should not occur, checking just for paranoia */
            fprintf(stderr, "Failed to calculate data size\n");
            return false;
        }

        // Write the decoded signed 16bit integer sample PCM data to file(unsigned 32bit PCM)
        // The sample of stero is interleaved per channel
        if (audioCtrl->enableOutput && audioCtrl->audioOutPCM)
            for (int i = 0; i < aFrame->nb_samples; i++)
                for (int ch = 0; ch < audioCtrl->audioDec_ctx->channels; ch++)
                    fwrite(aFrame->data[ch] + data_size*i, 1, data_size, audioCtrl->audioOutPCM);
    }

    return true;
}
#endif

#ifdef ENCODE_AUDIO
// iterate the supported audio sample rate and find the highest rate
static mfxI32 selSamplingRate(const AVCodec *codec)
{
    mfxI32 retSampleRate = 0;
    if (!codec->supported_samplerates)
        retSampleRate = 44100;
    else {
        const mfxI32 *p = codec->supported_samplerates;
        while (*p) {
            if (!retSampleRate || abs(44100 - *p) < abs(44100 - retSampleRate))
                retSampleRate = *p;
            p++;
        }
    }
    return retSampleRate;
}

// Iterate the channel layout and select the layout with the highest NB channel count
static int selChLayout(const AVCodec *codec)
{
    uint64_t retChLayout = 0;

    if (!codec->channel_layouts)
        retChLayout = AV_CH_LAYOUT_STEREO;
    else {
        const uint64_t *p = codec->channel_layouts;
        while (*p) {
            int bestNBChannels = 0;
            int nb_channels = av_get_channel_layout_nb_channels(*p);
            if (nb_channels > bestNBChannels) {
                retChLayout = *p;
                bestNBChannels = nb_channels;
            }
            p++;
        }
    }
    return retChLayout;
}

/* Create a new audio stream, encoder and AV context */
static bool addAudioStream(muxControl* audioCtrl, enum AVCodecID codec_id)
{
    AVFormatContext *oc = audioCtrl->avfCtx;
    AVCodec *codec;
    audioCtrl->audioStream = NULL;

    // Create new audio stream for the container
    audioCtrl->audioStream = avformat_new_stream(oc, NULL);
    if (!audioCtrl->audioStream) {
        printf("FFMPEG: Could not alloc audio stream\n");
        return false;
    }
 
    /* find the encoder */
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        return false;
    }
    audioCtrl->audioCodec = codec;

    /* Create AVContext */
    audioCtrl->audioEnc_ctx = avcodec_alloc_context3(codec);
    if (!audioCtrl->audioEnc_ctx) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        return false;
    }

    AVCodecContext *c;
    c = audioCtrl->audioEnc_ctx;

    /* select other audio parameters supported by the encoder */
    c->codec_id		= codec_id;
    c->codec_type	= AVMEDIA_TYPE_AUDIO;
    c->sample_fmt  = codec->sample_fmts ?
        codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    c->sample_rate    = selSamplingRate(codec);
    c->channel_layout = selChLayout(codec);
    c->channels       = av_get_channel_layout_nb_channels(c->channel_layout);
    c->bit_rate		= 64000;				// Desired encoding bit rate

    // Some formats want stream headers to be separate
    if(oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* Copy codec parameters from input stream to output codec context */
    if (avcodec_parameters_from_context(audioCtrl->avfCtx->streams[1]->codecpar, audioCtrl->audioEnc_ctx) < 0) {
        fprintf(stderr, "Failed to copy %s context parameters to encoder parameter\n",
            av_get_media_type_string(audioCtrl->avfCtx->streams[1]->codecpar->codec_type));
        return false;
    }
 
    return true;
}

/* Prepare for the encoding operation */
static bool encode_open_audio(muxControl* audioCtrl)
{
    AVCodecContext *c;
    AVCodec *codec = audioCtrl->audioCodec;
    AVFrame* frame;
    AVPacket* pkt = NULL;
    int ret;

    audioCtrl->audioInPCM = NULL;
    audioCtrl->currentPts = 0;
 
    c = audioCtrl->audioEnc_ctx;
 
    // Assumes raw PCM file data : 44100 Hz, 2 channels, 16bit/sample
    // or float 32 bit/sample in case using mkv container which defaults to ogg vorbis encoder
    audioCtrl->audioInPCM = fopen("audio.dat", "rb");
    if (!audioCtrl->audioInPCM) {
        printf("FFMPEG: Could not open audio input file\n");
        return false;
    }
 
    // open audio encoder
    if(avcodec_open2(c, codec, NULL) < 0) {
        printf("FFMPEG: could not open audio encoder\n");
        return false;
    }

    /* packet for holding encoded output */
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "could not allocate the packet\n");
        return false;
    }
    audioCtrl->audioPacket = pkt;

    /* frame containing input raw audio */
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate audio frame\n");
        return false;
    }
    frame->nb_samples     = c->frame_size;
    frame->format         = c->sample_fmt;
    frame->channel_layout = c->channel_layout;

    /* allocate the data buffers */
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate audio data buffers\n");
        return false;
    }

    audioCtrl->audioFrame = frame;
    audioCtrl->currentPts = c->frame_size;

    return true;
}

/* Encode the audio frame, also flush the frames in the encoding queue */
/* This function replaces the depricated avcodec_encode_audio2() */
static bool encode_audio_frame(muxControl* audioCtrl, bool flush)
{
    AVCodecContext *avctx;
    AVPacket *pkt = audioCtrl->audioPacket;
    int res = 1;
 
    avctx = audioCtrl->audioEnc_ctx;

    while (res != 0) {
        if (flush)
            res = avcodec_send_frame(avctx, NULL);
        else
            res = avcodec_send_frame(avctx, audioCtrl->audioFrame);

        if (res < 0 && res != AVERROR(EAGAIN)) {
            //In case of flush, there will be frames in the receiving queue, 
            // so don't return yet but go to the receiving loop to continue flushing
            if (!flush) {
                fprintf(stderr, "Error sending the frame to the encoder\n");
                return res;
            }
        } else if (res == AVERROR(EAGAIN)) {
            res = avcodec_receive_packet(avctx, pkt);
            res = 1;//resend the frame
            continue;
        }

        res = avcodec_receive_packet(avctx, pkt);
        if (res == AVERROR(EAGAIN)) {
            res = 1; //resend the frame
        } else if (res == AVERROR_EOF)
            break;
        else if (res < 0) {
            fprintf(stderr, "Error receiving the audio packet from the encoder\n");
            return false;
        }
    }

    pkt->flags			|= AV_PKT_FLAG_KEY;
    pkt->stream_index	= audioCtrl->audioStream->index;
    pkt->pts			= av_rescale_q(audioCtrl->currentPts, avctx->time_base, audioCtrl->audioStream->time_base);
    pkt->dts = pkt->pts;

    audioCtrl->currentPts += avctx->frame_size;

    return true;
}

/* Read the audio sample from the PCM raw audio file, encode and write to muxed file */
static bool write_audio_frame(muxControl* audioCtrl, float videoFramePts)
{
    AVCodecContext *c;
    AVPacket* pkt = audioCtrl->audioPacket;
    AVFormatContext *oc = audioCtrl->avfCtx;
    c = audioCtrl->audioEnc_ctx;
    size_t readBytes;

    float realAudioPts = (float)audioCtrl->currentPts * c->time_base.num / c->time_base.den;
    int data_size = av_get_bytes_per_sample(c->sample_fmt);

    while(realAudioPts < videoFramePts)
    {
        //Refer to the function decode_write_audio() for the file writing sequence
        //This loop is following the sequence
        for (int i = 0; i < c->frame_size; i++) {
            for (int ch = 0; ch < c->channels; ch++) {
                readBytes = fread(audioCtrl->audioFrame->data[ch] + i*data_size, 1, data_size, audioCtrl->audioInPCM);
                if(readBytes != data_size) {
                    printf("FFMPEG: Reached end of audio PCM file\n");
                    // Need to free packet here...
                    return false;
                }
            }
        }

        if(!encode_audio_frame(audioCtrl, false))
            return false;
 
        // Write the compressed frame
        int res = av_interleaved_write_frame(oc, pkt);
        if (res != 0) {
            printf("FFMPEG: Error while writing audio frame\n");
            return false;
        }
        av_packet_unref(pkt);

        realAudioPts += (float)c->frame_size * c->time_base.num / c->time_base.den;
    }

    return true;
}

/* Flush the audio frame in the encoding queue and write to the muxed file */
static bool flush_audio(muxControl* audioCtrl)
{
    AVPacket* pkt = audioCtrl->audioPacket;
    AVFormatContext *oc = audioCtrl->avfCtx;
    
    while (1) {
        if(!encode_audio_frame(audioCtrl, true))
            return false;

        if(pkt->size > 0) {
            // Write the compressed frame
            int res = av_interleaved_write_frame(oc, pkt);
            if (res != 0) {
                printf("FFMPEG: Error while writing audio frame (flush)\n");
                return false;
            }
            av_packet_unref(pkt);
        } else
            break;
    }

    return true;
}

/* Release the resource for the encoding operation */
static bool encode_close_audio(muxControl* audioCtrl)
{
    av_frame_free(&audioCtrl->audioFrame);
    av_packet_free(&audioCtrl->audioPacket);
    avcodec_close(audioCtrl->audioEnc_ctx);

    if(audioCtrl->audioInPCM)
        fclose(audioCtrl->audioInPCM);

    return true;
}
#endif

mfxStatus openDemuxControl(demuxControl* ffmpegCtrl, const char *filename) {
    MSDK_CHECK_POINTER(ffmpegCtrl, MFX_ERR_NULL_PTR);
    MSDK_CHECK_POINTER(filename, MFX_ERR_NULL_PTR);

    const AVBitStreamFilter* avBsf;

    //Create the AVFormatContext for the FFMpeg demuxer
    ffmpegCtrl->avfCtx = NULL;
    ffmpegCtrl->enableFilter = false;

    /* register all formats and codecs */
    av_register_all();

    // Open input container
    int res = avformat_open_input(&ffmpegCtrl->avfCtx, filename, NULL, NULL);
    if(res) {
        printf("FFMPEG: Could not open input container\n");
        return MFX_ERR_UNKNOWN;
    }

    // Retrieve stream information
    res = avformat_find_stream_info(ffmpegCtrl->avfCtx, NULL);
    if(res < 0) {
        printf("FFMPEG: Couldn't find stream information\n");
        return MFX_ERR_UNKNOWN;
    }

    // Dump container info to console
    av_dump_format(ffmpegCtrl->avfCtx, 0, filename, 0);

    // Find the streams in the container
    ffmpegCtrl->videoIdx = -1;
    ffmpegCtrl->audioIdx = -1;
    for(unsigned int i=0; i<ffmpegCtrl->avfCtx->nb_streams; i++)
    {
        if(ffmpegCtrl->avfCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ffmpegCtrl->videoIdx == -1)
        {
            ffmpegCtrl->videoIdx = i;
        }
#ifdef DECODE_AUDIO
        else if(ffmpegCtrl->avfCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            ffmpegCtrl->audioIdx = i;

            if (!decode_open_audio(ffmpegCtrl)) return MFX_ERR_UNKNOWN;
        }
#endif
    }

    if(ffmpegCtrl->videoIdx == -1) {
        printf("Didn't find any video streams in container");
        return MFX_ERR_UNKNOWN; // Didn't find any video streams in container
    }

    //Search for libavcodec/avcodec.h for AVCodecID, Media SDK bitstream follows the Annex B for some streams.
    if (ffmpegCtrl->avfCtx->streams[ffmpegCtrl->videoIdx]->codecpar->codec_id ==  AV_CODEC_ID_H264) {
        avBsf = av_bsf_get_by_name("h264_mp4toannexb");
        ffmpegCtrl->enableFilter = true;
        ffmpegCtrl->videoType = MFX_CODEC_AVC;
    } else if (ffmpegCtrl->avfCtx->streams[ffmpegCtrl->videoIdx]->codecpar->codec_id ==  AV_CODEC_ID_H265) {
        avBsf = av_bsf_get_by_name("hevc_mp4toannexb");
        ffmpegCtrl->enableFilter = true;
        ffmpegCtrl->videoType = MFX_CODEC_HEVC;
    } else if (ffmpegCtrl->avfCtx->streams[ffmpegCtrl->videoIdx]->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        ffmpegCtrl->videoType = MFX_CODEC_MPEG2;
    } else {
        //TODO: Currently we only tested 3 codecs for tutorial purpose, User may extend this code for other codecs.
        return MFX_ERR_UNDEFINED_BEHAVIOR;
    }

    if (ffmpegCtrl->enableFilter) {
        res = av_bsf_alloc(avBsf, &ffmpegCtrl->bsfCtx);
        if (res < 0)
            return MFX_ERR_UNDEFINED_BEHAVIOR;

        res = avcodec_parameters_copy(ffmpegCtrl->bsfCtx->par_in, ffmpegCtrl->avfCtx->streams[ffmpegCtrl->videoIdx]->codecpar);
        if (res < 0)
            return MFX_ERR_UNDEFINED_BEHAVIOR;

        ffmpegCtrl->bsfCtx->time_base_in = ffmpegCtrl->avfCtx->streams[ffmpegCtrl->videoIdx]->time_base;
        res = av_bsf_init(ffmpegCtrl->bsfCtx);
        if (res < 0)
        {
            av_bsf_free(&ffmpegCtrl->bsfCtx);
            return MFX_ERR_UNDEFINED_BEHAVIOR;
        }
    }

    //Set the input video frame size
    ffmpegCtrl->width = ffmpegCtrl->avfCtx->streams[ffmpegCtrl->videoIdx]->codecpar->width;
    ffmpegCtrl->height = ffmpegCtrl->avfCtx->streams[ffmpegCtrl->videoIdx]->codecpar->height;

    return MFX_ERR_NONE;
}

mfxStatus closeDemuxControl(demuxControl* ffmpegCtrl) {
    av_bsf_free(&ffmpegCtrl->bsfCtx);
    avformat_close_input(&ffmpegCtrl->avfCtx);

#ifdef DECODE_AUDIO
        decode_close_audio(ffmpegCtrl);
#endif

    return MFX_ERR_NONE;
}

mfxStatus openMuxControl(muxControl* ffmpegCtrl, const char *filename) {
    MSDK_CHECK_POINTER(ffmpegCtrl, MFX_ERR_NULL_PTR);
    MSDK_CHECK_POINTER(filename, MFX_ERR_NULL_PTR);

    // Initialize libavcodec, and register all codecs and formats
    av_register_all();
    ffmpegCtrl->avfCtx = NULL;

    AVOutputFormat*	outFormat;

    //Decide the encode format and file format based on the user input
    if(ffmpegCtrl->defaultFormat)
    {
        // Defaults to H.264 video and Vorbis audio
        outFormat = av_guess_format(NULL, "dummy.mkv", NULL);
        
        if(ffmpegCtrl->CodecId == MFX_CODEC_MPEG2) {
            // Override codec to MPEG2
            outFormat->video_codec = AV_CODEC_ID_MPEG2VIDEO;
        }

        //TODO: The default audio codec is vorbis but the audio encoder has an error, so we force it to AC3
        outFormat->audio_codec = AV_CODEC_ID_AC3;
    }
    else if(ffmpegCtrl->CodecId == MFX_CODEC_AVC)
    {
        // Defaults to H.264 video and AAC audio
        outFormat = av_guess_format("mp4", NULL, NULL);
        outFormat->video_codec = AV_CODEC_ID_H264;
    }
    // TODO There is a runtime error with this format:
    // "VBV buffer size not set, using default size of 130KB..." when calling avformat_write_header(),
    // but this doesn't affect the result.
    else if(ffmpegCtrl->CodecId == MFX_CODEC_MPEG2)
    {
        outFormat = av_guess_format("mpeg", NULL, NULL);
        // Defaults to MPEG-PS
        outFormat->video_codec = AV_CODEC_ID_MPEG2VIDEO;
        // TODO: the default audio codec is MP2 but the audio encoder doesn't work, we force it to AAC
        outFormat->audio_codec = AV_CODEC_ID_AAC;
    }
    else
        return MFX_ERR_UNSUPPORTED;

    if (!outFormat) {
        printf("FFMPEG: Could not find suitable output format\n");
        return MFX_ERR_UNSUPPORTED;
    }

    // Allocate the output media context
    avformat_alloc_output_context2(&ffmpegCtrl->avfCtx, NULL, NULL, filename);
    if (!ffmpegCtrl->avfCtx) {
        printf("FFMPEG: avformat_alloc_context error\n");
        return MFX_ERR_UNSUPPORTED;
    }

    ffmpegCtrl->avfCtx->oformat = outFormat;
    
    if (outFormat->video_codec == AV_CODEC_ID_NONE) 
        return MFX_ERR_UNSUPPORTED;

    // Allocate the video stream as the output video stream context
    AVStream* videoStream = avformat_new_stream(ffmpegCtrl->avfCtx, NULL);
    if (!videoStream) {
        printf("FFMPEG: Could not alloc video stream\n");
        return MFX_ERR_UNKNOWN;
    }

    AVCodecParameters *cpar = videoStream->codecpar;
    cpar->codec_id   = outFormat->video_codec;
    cpar->codec_type = AVMEDIA_TYPE_VIDEO;
    cpar->bit_rate   = ffmpegCtrl->nBitRate*1000;
    cpar->width	     = ffmpegCtrl->nDstWidth; 
    cpar->height     = ffmpegCtrl->nDstHeight;

#ifdef ENCODE_AUDIO
    if (outFormat->audio_codec != AV_CODEC_ID_NONE) {
        if(!addAudioStream(ffmpegCtrl, outFormat->audio_codec))
            return MFX_ERR_UNKNOWN;

        if(!encode_open_audio(ffmpegCtrl))
            return MFX_ERR_UNKNOWN;
    }
#endif

    // Open the output container file
    if (avio_open(&ffmpegCtrl->avfCtx->pb, filename, AVIO_FLAG_WRITE) < 0)
    {
        printf("FFMPEG: Could not open '%s'\n", filename);
        return MFX_ERR_UNKNOWN;
    }

    // Write container header, note the time_base that set by the user might be overwritten by the avformat_write_header() call.
    // At the reference for AVStream: http://ffmpeg.org/doxygen/trunk/structAVStream.html
    // AVStream::time_base has following description:
    // "encoding: May be set by the caller before avformat_write_header() to provide a hint to the muxer about the desired timebase. 
    // In avformat_write_header(), the muxer will overwrite this field with the timebase that will actually be used for the timestamps 
    // written into the file"
    // Page https://stackoverflow.com/questions/13595288/understanding-pts-and-dts-in-video-frames explained some details.
    // TODO: there is an error message "[mp4 @ 0x62eec0] track 1: codec frame size is not set", but won't affect the result.
    if(avformat_write_header(ffmpegCtrl->avfCtx, NULL)) {
        printf("FFMPEG: avformat_write_header error!\n");
        return MFX_ERR_UNKNOWN;
    }

    ffmpegCtrl->nProcessedFramesNum = 0;

    av_dump_format(ffmpegCtrl->avfCtx, 0, filename, 1);

    return MFX_ERR_NONE;
}

mfxStatus closeMuxControl(muxControl* ffmpegCtrl) {
    ffmpegCtrl->nProcessedFramesNum = 0;

#ifdef ENCODE_AUDIO
    flush_audio(ffmpegCtrl);
#endif

    // Write the trailer, if any. 
    // The trailer must be written before you close the CodecContexts open when you wrote the
    // header; otherwise write_trailer may try to use memory that was freed on av_codec_close()
    av_write_trailer(ffmpegCtrl->avfCtx);

#ifdef ENCODE_AUDIO
    encode_close_audio(ffmpegCtrl);
#endif

    // Free the streams
    for(unsigned int i = 0; i < ffmpegCtrl->avfCtx->nb_streams; i++) {
        av_freep(&ffmpegCtrl->avfCtx->streams[i]);
    }

    avio_close(ffmpegCtrl->avfCtx->pb);

    av_free(ffmpegCtrl->avfCtx);

    return MFX_ERR_NONE;
}

mfxStatus ffmpegWriteFrame(mfxBitstream* pMfxBitstream, muxControl* muxCtrl) {
    MSDK_CHECK_POINTER(pMfxBitstream, MFX_ERR_NULL_PTR);

    AVStream* videoStream = muxCtrl->avfCtx->streams[0];

    // Note for H.264 :
    //    At the moment the SPS/PPS will be written to container again for the first frame here
    //    To eliminate this we would have to search to first slice (or right after PPS)
    AVPacket pkt;
    av_init_packet(&pkt);

    //The packet presentation time should be the input raw frame counter
    pkt.dts = muxCtrl->nProcessedFramesNum;

    // time base: this is the fundamental unit of time (in seconds) in terms of which frame timestamps are represented. for fixed-fps content,
    //            timebase should be 1/framerate and timestamp increments should be identically 1.
    AVRational tb;
    tb.num = muxCtrl->frameRateD;
    tb.den = muxCtrl->frameRateN;
    pkt.dts = av_rescale_q(pkt.dts, tb, videoStream->time_base);
    pkt.pts				= pkt.dts;
    pkt.stream_index	= videoStream->index;
    pkt.data			= pMfxBitstream->Data;
    pkt.size			= pMfxBitstream->DataLength;

    if(pMfxBitstream->FrameType == (MFX_FRAMETYPE_I | MFX_FRAMETYPE_REF | MFX_FRAMETYPE_IDR))
        pkt.flags |= AV_PKT_FLAG_KEY;

    /* Write the compressed frame to the media file. */
    int64_t tmp = pkt.pts;
    if (av_interleaved_write_frame(muxCtrl->avfCtx, &pkt)) {
        printf("FFMPEG: Error while writing video frame\n");
        return MFX_ERR_UNKNOWN;
    }

#ifdef ENCODE_AUDIO
    // Note that video + audio muxing timestamp handling in this sample is very rudimentary
    //  - Audio stream length is adjusted to same as video steram length 
    float realVideoPts = (float)tmp * videoStream->time_base.num / videoStream->time_base.den;
    write_audio_frame(muxCtrl, realVideoPts);
#endif

    return MFX_ERR_NONE;
}

mfxStatus ffmpegReadFrame(mfxBitstream* pBS, demuxControl* filterCtrl) {
    MSDK_CHECK_POINTER(pBS, MFX_ERR_NULL_PTR);

    int res = 0;
    bool videoFrameFound = false;
    AVPacket packet;

    // Read until video frame is found or no more video frames in container.
    while(!videoFrameFound)
    {
        if(!av_read_frame(filterCtrl->avfCtx, &packet))
        {
            if(packet.stream_index == filterCtrl->videoIdx)
            {
                if(filterCtrl->enableFilter)
                {
                    //
                    // Apply MP4 to H264 Annex B filter on buffer
                    //
                    res = av_bsf_send_packet(filterCtrl->bsfCtx, &packet);
                    if (res < 0) {
                        return MFX_ERR_UNDEFINED_BEHAVIOR;
                    }

                    while (!res)
                        res = av_bsf_receive_packet(filterCtrl->bsfCtx, &packet);

                    if (res < 0 && (res != AVERROR(EAGAIN) && res != AVERROR_EOF)) {
                        return MFX_ERR_UNDEFINED_BEHAVIOR;
                    }
                }

                //
                // Copy filtered buffer to bitstream
                //
                memmove(pBS->Data, pBS->Data + pBS->DataOffset, pBS->DataLength);
                        pBS->DataOffset = 0;
                        memcpy(pBS->Data + pBS->DataLength, packet.data, packet.size);
                        pBS->DataLength += packet.size; 

                av_packet_unref(&packet);

                // We are required to tell MSDK that complete frame is in the bitstream!
                pBS->DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;

                videoFrameFound = true;
            }
#ifdef DECODE_AUDIO
            else if(packet.stream_index == filterCtrl->audioIdx)
            {
                // Decode and write raw audio samples
                decode_write_audio(filterCtrl, &packet);
            }
#endif
        }
        else {
            return MFX_ERR_MORE_DATA;  // Indicates that we reached end of container and to stop video decode
        }
    }

    return MFX_ERR_NONE;
}

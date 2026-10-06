#include "LogForgeCodec.h"
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

struct LFEncoder { AVCodecContext *context; AVFrame *frame; AVPacket *packet; CMVideoFormatDescriptionRef format; char error[256]; };
static void lfError(LFEncoder *e, int code) { av_strerror(code,e->error,sizeof(e->error)); }
const char *LFCodecVersion(void) { return av_version_info(); }
const char *LFEncoderLastError(LFEncoder *e) { return e ? e->error : "Cannot initialize software ProRes encoder"; }
void LFEncoderDestroy(LFEncoder *e) {
    if(!e) return;
    avcodec_free_context(&e->context); av_frame_free(&e->frame); av_packet_free(&e->packet);
    if(e->format) CFRelease(e->format); free(e);
}
LFEncoder *LFEncoderCreate(int32_t w,int32_t h,int32_t profile) {
    if(w<=0 || h<=0 || w%2 || (profile!=2 && profile!=3)) return NULL;
    LFEncoder *e=calloc(1,sizeof(*e)); if(!e) return NULL;
    const AVCodec *codec=avcodec_find_encoder_by_name("prores_ks"); if(!codec) goto fail;
    e->context=avcodec_alloc_context3(codec); e->frame=av_frame_alloc(); e->packet=av_packet_alloc();
    if(!e->context || !e->frame || !e->packet) goto fail;
    e->context->width=w; e->context->height=h; e->context->pix_fmt=AV_PIX_FMT_YUV422P10LE;
    e->context->time_base=(AVRational){1,600000}; e->context->profile=profile; e->context->thread_count=2;
    // Slice threading emits the packet for this frame immediately. Frame threading
    // would require a delayed-packet queue and flushing at end of stream.
    e->context->thread_type=FF_THREAD_SLICE;
    e->context->color_primaries=AVCOL_PRI_BT2020; e->context->color_trc=AVCOL_TRC_UNSPECIFIED;
    e->context->colorspace=AVCOL_SPC_BT2020_NCL; e->context->color_range=AVCOL_RANGE_MPEG;
    e->context->chroma_sample_location=AVCHROMA_LOC_LEFT;
    // prores_ks uses a private profile option, not AVCodecContext.profile.
    // Leaving it at auto silently selects HQ even for the Standard path.
    int status=av_opt_set_int(e->context->priv_data,"profile",profile,0);
    if(status<0) goto fail;
    status=avcodec_open2(e->context,codec,NULL); if(status<0) goto fail;
    e->frame->width=w; e->frame->height=h; e->frame->format=e->context->pix_fmt;
    if(av_frame_get_buffer(e->frame,32)<0) goto fail;
    CFMutableDictionaryRef ext=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!ext) goto fail;
    CFDictionarySetValue(ext,kCMFormatDescriptionExtension_ColorPrimaries,kCMFormatDescriptionColorPrimaries_ITU_R_2020);
    CFDictionarySetValue(ext,kCMFormatDescriptionExtension_YCbCrMatrix,kCMFormatDescriptionYCbCrMatrix_ITU_R_2020);
    CFDictionarySetValue(ext,kCMFormatDescriptionExtension_LogTransferFunction,kCMFormatDescriptionLogTransferFunction_AppleLog);
    CFDictionarySetValue(ext,kCMFormatDescriptionExtension_FullRangeVideo,kCFBooleanFalse);
    CFDictionarySetValue(ext,kCMFormatDescriptionExtension_ChromaLocationTopField,kCMFormatDescriptionChromaLocation_Left);
    status=CMVideoFormatDescriptionCreate(NULL,profile==3?kCMVideoCodecType_AppleProRes422HQ:kCMVideoCodecType_AppleProRes422,w,h,ext,&e->format);
    CFRelease(ext); if(status) goto fail;
    return e;
fail: LFEncoderDestroy(e); return NULL;
}
CMSampleBufferRef LFEncoderEncode(LFEncoder *e,CVPixelBufferRef pixels,CMTime pts,CMTime duration) {
    if(!e || CVPixelBufferGetPixelFormatType(pixels)!=kCVPixelFormatType_422YpCbCr10 ||
       CVPixelBufferGetWidth(pixels)!=e->frame->width || CVPixelBufferGetHeight(pixels)!=e->frame->height) return NULL;
    int status=av_frame_make_writable(e->frame); if(status<0) {lfError(e,status);return NULL;}
    if(CVPixelBufferLockBaseAddress(pixels,kCVPixelBufferLock_ReadOnly)!=kCVReturnSuccess) {snprintf(e->error,sizeof(e->error),"Pixel lock failed");return NULL;}
    const uint8_t *base=CVPixelBufferGetBaseAddress(pixels); size_t stride=CVPixelBufferGetBytesPerRow(pixels);
    if(!base || stride<((e->frame->width+5)/6)*16) {CVPixelBufferUnlockBaseAddress(pixels,kCVPixelBufferLock_ReadOnly);return NULL;}
    for(int y=0;y<e->frame->height;y++) {
        const uint32_t *row=(const uint32_t *)(base+y*stride);
        uint16_t *yy=(uint16_t *)(e->frame->data[0]+y*e->frame->linesize[0]);
        uint16_t *cb=(uint16_t *)(e->frame->data[1]+y*e->frame->linesize[1]);
        uint16_t *cr=(uint16_t *)(e->frame->data[2]+y*e->frame->linesize[2]);
        for(int x=0;x<e->frame->width;x+=6) {
            const uint32_t *q=row+(x/6)*4;
            uint32_t l[6]={q[0]>>10,q[1],q[1]>>20,q[2]>>10,q[3],q[3]>>20};
            uint32_t u[3]={q[0],q[1]>>10,q[2]>>20},v[3]={q[0]>>20,q[2],q[3]>>10};
            for(int k=0;k<6 && x+k<e->frame->width;k++) yy[x+k]=l[k]&1023;
            for(int k=0;k<3 && x+k*2<e->frame->width;k++) {cb[x/2+k]=u[k]&1023;cr[x/2+k]=v[k]&1023;}
        }
    }
    CVPixelBufferUnlockBaseAddress(pixels,kCVPixelBufferLock_ReadOnly);
    e->frame->pts=CMTimeConvertScale(pts,600000,kCMTimeRoundingMethod_RoundHalfAwayFromZero).value;
    status=avcodec_send_frame(e->context,e->frame); if(status<0) {lfError(e,status);return NULL;}
    av_packet_unref(e->packet); status=avcodec_receive_packet(e->context,e->packet);
    if(status<0) {lfError(e,status);return NULL;}
    CMBlockBufferRef block=NULL; CMSampleBufferRef sample=NULL; size_t size=(size_t)e->packet->size;
    status=CMBlockBufferCreateWithMemoryBlock(NULL,NULL,size,NULL,NULL,0,size,0,&block);
    if(!status) status=CMBlockBufferReplaceDataBytes(e->packet->data,block,0,size);
    CMSampleTimingInfo timing={duration,pts,kCMTimeInvalid};
    if(!status) status=CMSampleBufferCreateReady(NULL,block,e->format,1,1,&timing,1,&size,&sample);
    if(block) CFRelease(block);
    if(status) {snprintf(e->error,sizeof(e->error),"Core Media sample error %d",status);return NULL;}
    return sample;
}

// The software decoder is also compiled into the iOS arm64 library. It receives
// one intraframe ProRes packet and copies planar 10-bit samples into caller-owned
// V210 storage; it never borrows an AVFrame as a CVPixelBuffer backing store.
struct LFDecoder { AVCodecContext *context; AVFrame *frame; AVPacket *packet; int width,height; char error[256]; };
const char *LFDecoderLastError(LFDecoder *d) { return d ? d->error : "Cannot initialize software ProRes decoder"; }
void LFDecoderDestroy(LFDecoder *d) {
    if(!d) return;
    avcodec_free_context(&d->context); av_frame_free(&d->frame); av_packet_free(&d->packet); free(d);
}
LFDecoder *LFDecoderCreate(int32_t w,int32_t h) {
    if(w<=0 || h<=0 || w%2) return NULL;
    LFDecoder *d=calloc(1,sizeof(*d)); if(!d) return NULL;
    const AVCodec *codec=avcodec_find_decoder(AV_CODEC_ID_PRORES); if(!codec) goto fail;
    d->context=avcodec_alloc_context3(codec); d->frame=av_frame_alloc(); d->packet=av_packet_alloc();
    if(!d->context || !d->frame || !d->packet) goto fail;
    d->width=w; d->height=h; d->context->width=w; d->context->height=h;
    // The decoder checks stride-aligned width before allocating a frame. Allow
    // bounded row padding (up to its largest supported 64-pixel alignment); the
    // packet format and decoded visible dimensions still must match exactly.
    d->context->max_pixels=(((int64_t)w+63)/64*64)*h;
    d->context->thread_count=2; d->context->thread_type=FF_THREAD_SLICE;
    d->context->err_recognition=AV_EF_CAREFUL|AV_EF_EXPLODE;
    if(avcodec_open2(d->context,codec,NULL)<0) goto fail;
    return d;
fail: LFDecoderDestroy(d); return NULL;
}
int32_t LFDecoderDecode(LFDecoder *d,CMSampleBufferRef sample,CVPixelBufferRef pixels) {
    if(!d || !sample || !pixels) return 0;
    snprintf(d->error,sizeof(d->error),"Invalid ProRes packet or output buffer");
    CMFormatDescriptionRef format=CMSampleBufferGetFormatDescription(sample);
    if(!format || CMFormatDescriptionGetMediaType(format)!=kCMMediaType_Video) return 0;
    FourCharCode codec=CMFormatDescriptionGetMediaSubType(format);
    if(codec!=kCMVideoCodecType_AppleProRes422 && codec!=kCMVideoCodecType_AppleProRes422HQ) return 0;
    CMVideoDimensions dimensions=CMVideoFormatDescriptionGetDimensions(format);
    if(dimensions.width!=d->width || dimensions.height!=d->height || CMSampleBufferGetNumSamples(sample)!=1 ||
       CVPixelBufferGetPixelFormatType(pixels)!=kCVPixelFormatType_422YpCbCr10 ||
       CVPixelBufferGetWidth(pixels)!=d->width || CVPixelBufferGetHeight(pixels)!=d->height) return 0;
    CMBlockBufferRef block=CMSampleBufferGetDataBuffer(sample); if(!block) return 0;
    size_t size=CMBlockBufferGetDataLength(block); if(size==0 || size>INT_MAX) return 0;
    av_packet_unref(d->packet);
    // av_new_packet allocates and zeros FFmpeg's required bitreader padding.
    int status=av_new_packet(d->packet,(int)size);
    if(status<0) {av_strerror(status,d->error,sizeof(d->error));return 0;}
    OSStatus copy=CMBlockBufferCopyDataBytes(block,0,size,d->packet->data);
    if(copy) {snprintf(d->error,sizeof(d->error),"Packet copy error %d",copy);return 0;}
    status=avcodec_send_packet(d->context,d->packet);
    if(status>=0) {av_frame_unref(d->frame);status=avcodec_receive_frame(d->context,d->frame);}
    if(status<0) {av_strerror(status,d->error,sizeof(d->error));return 0;}
    if(d->frame->width!=d->width || d->frame->height!=d->height || d->frame->format!=AV_PIX_FMT_YUV422P10LE ||
       d->frame->decode_error_flags || (d->frame->flags&AV_FRAME_FLAG_INTERLACED)) {
        snprintf(d->error,sizeof(d->error),"Unsupported or damaged decoded frame: %dx%d, format=%d, errors=%d",d->frame->width,d->frame->height,d->frame->format,d->frame->decode_error_flags);return 0;
    }
    for(int plane=0;plane<3;plane++) {
        if(!d->frame->data[plane] || d->frame->linesize[plane]<(plane==0?d->width*2:d->width)) return 0;
    }
    if(CVPixelBufferLockBaseAddress(pixels,0)!=kCVReturnSuccess) {snprintf(d->error,sizeof(d->error),"Pixel lock failed");return 0;}
    uint8_t *base=CVPixelBufferGetBaseAddress(pixels); size_t stride=CVPixelBufferGetBytesPerRow(pixels);
    if(!base || stride<((size_t)d->width+5)/6*16) {CVPixelBufferUnlockBaseAddress(pixels,0);return 0;}
    memset(base,0,stride*d->height);
    for(int y=0;y<d->height;y++) {
        const uint16_t *yy=(const uint16_t *)(d->frame->data[0]+y*d->frame->linesize[0]);
        const uint16_t *cb=(const uint16_t *)(d->frame->data[1]+y*d->frame->linesize[1]);
        const uint16_t *cr=(const uint16_t *)(d->frame->data[2]+y*d->frame->linesize[2]);
        uint32_t *row=(uint32_t *)(base+y*stride);
        for(int x=0;x<d->width;x+=6) {
            uint32_t l[6]={0},u[3]={0},v[3]={0};
            for(int k=0;k<6 && x+k<d->width;k++) l[k]=yy[x+k]&1023;
            for(int k=0;k<3 && x+k*2<d->width;k++) {u[k]=cb[x/2+k]&1023;v[k]=cr[x/2+k]&1023;}
            uint32_t *q=row+(x/6)*4;
            q[0]=u[0]|l[0]<<10|v[0]<<20; q[1]=l[1]|u[1]<<10|l[2]<<20;
            q[2]=v[1]|l[3]<<10|u[2]<<20; q[3]=l[4]|v[2]<<10|l[5]<<20;
        }
    }
    CVPixelBufferUnlockBaseAddress(pixels,0);
    d->error[0]=0; return 1;
}

//go:build !x264

package main

/*
// h264/ is jumpseat's encoder: make -C h264 before go build.
#cgo CFLAGS: -I${SRCDIR}/../h264/api
#cgo LDFLAGS: -L${SRCDIR}/../h264/build -ljumpseat_h264 -lyuv -lstdc++ -lm
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wels/codec_api.h>
#include <libyuv/convert.h>

typedef struct {
	ISVCEncoder *enc;
	uint8_t *i420;
} oh264;

// Tunables, each -1 for the encoder's own default (JUMPSEAT_OH264). The
// encoder fixes the rest (h264/README.md): screen content, low complexity,
// quality-mode rate control, one thread, one slice.
typedef struct {
	int qp, refs, vui;
} oh_opts;

static oh264 *open_encoder(int w, int h, int fps, oh_opts o) {
	oh264 *e = calloc(1, sizeof(oh264));
	if (WelsCreateSVCEncoder(&e->enc) != 0) return 0;
	SEncParamExt p;
	(*e->enc)->GetDefaultParams(e->enc, &p);
	p.iUsageType = SCREEN_CONTENT_REAL_TIME; // the mode WebRTC uses for screen sharing
	p.iPicWidth = w; p.iPicHeight = h; p.fMaxFrameRate = fps;
	// Quality-led rate control with no bitrate pressure, so frames go at
	// o.qp the way x264's crf does; set_max_kbps adds the pressure on a slow
	// link (the QP may rise up to 51).
	int qp = o.qp;
	p.iRCMode = RC_QUALITY_MODE; p.iTargetBitrate = 50000000; p.iMaxBitrate = UNSPECIFIED_BIT_RATE;
	p.iMinQp = qp; p.iMaxQp = 51;
	p.bEnableFrameSkip = 0; p.uiIntraPeriod = 0; p.iMultipleThreadIdc = 1;
	if (o.refs >= 0) p.iNumRefFrame = o.refs;
	// libyuv's ARGBToI420 is BT.601, limited range: say so (as encoder.go).
	p.sSpatialLayers[0].bVideoSignalTypePresent = o.vui != 0;
	p.sSpatialLayers[0].bFullRange = false;
	p.sSpatialLayers[0].bColorDescriptionPresent = true;
	// SMPTE 170M, as x264 writes it: BT.470BG's transfer is gamma 2.8, and
	// Chrome converting from that darkened every frame (luma bias -3.2).
	p.sSpatialLayers[0].uiColorPrimaries = CP_SMPTE170M;
	p.sSpatialLayers[0].uiTransferCharacteristics = TRC_SMPTE170M;
	p.sSpatialLayers[0].uiColorMatrix = CM_SMPTE170M;
	p.eSpsPpsIdStrategy = CONSTANT_ID;
	p.iSpatialLayerNum = 1; p.iTemporalLayerNum = 1;
	p.sSpatialLayers[0].iVideoWidth = w; p.sSpatialLayers[0].iVideoHeight = h;
	p.sSpatialLayers[0].fFrameRate = fps;
	p.sSpatialLayers[0].iSpatialBitrate = p.iTargetBitrate;
	p.sSpatialLayers[0].iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;
	p.sSpatialLayers[0].iDLayerQp = qp;
	p.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
	if ((*e->enc)->InitializeExt(e->enc, &p) != 0) return 0;
	int format = videoFormatI420;
	(*e->enc)->SetOption(e->enc, ENCODER_OPTION_DATAFORMAT, &format);
	e->i420 = malloc(w * h * 3 / 2);
	return e;
}

// set_max_kbps puts the link's bitrate on the rate control (0: none).
static void set_max_kbps(oh264 *e, int kbps) {
	SBitrateInfo rate;
	memset(&rate, 0, sizeof rate);
	rate.iLayer = SPATIAL_LAYER_ALL;
	rate.iBitrate = kbps > 0 ? kbps * 1000 : 50000000;
	(*e->enc)->SetOption(e->enc, ENCODER_OPTION_BITRATE, &rate);
	rate.iBitrate = kbps > 0 ? kbps * 1000 : UNSPECIFIED_BIT_RATE;
	(*e->enc)->SetOption(e->enc, ENCODER_OPTION_MAX_BITRATE, &rate);
}

// encode_frame writes the frame's NAL units (each with its start code) to
// out and returns their size.
static void convert_frame(oh264 *e, const uint8_t *bgrx, int w, int h) {
	uint8_t *y = e->i420, *u = y + w * h, *v = u + w * h / 4;
	ARGBToI420(bgrx, w * 4, y, w, u, w / 2, v, w / 2, w, h);
}

static int encode_frame(oh264 *e, int w, int h, int64_t pts, uint8_t *out, int cap, int *key) {
	uint8_t *y = e->i420, *u = y + w * h, *v = u + w * h / 4;
	SSourcePicture pic;
	memset(&pic, 0, sizeof pic);
	pic.iColorFormat = videoFormatI420; pic.iPicWidth = w; pic.iPicHeight = h;
	pic.iStride[0] = w; pic.iStride[1] = w / 2; pic.iStride[2] = w / 2;
	pic.pData[0] = y; pic.pData[1] = u; pic.pData[2] = v;
	pic.uiTimeStamp = pts;
	SFrameBSInfo info;
	memset(&info, 0, sizeof info);
	if ((*e->enc)->EncodeFrame(e->enc, &pic, &info) != cmResultSuccess) return -1;
	if (info.eFrameType == videoFrameTypeSkip) return 0;
	*key = info.eFrameType == videoFrameTypeIDR;
	int size = 0;
	for (int l = 0; l < info.iLayerNum; l++) {
		SLayerBSInfo *layer = &info.sLayerInfo[l];
		int bytes = 0;
		for (int n = 0; n < layer->iNalCount; n++) bytes += layer->pNalLengthInByte[n];
		if (size + bytes > cap) return -1;
		memcpy(out + size, layer->pBsBuf, bytes);
		size += bytes;
	}
	return size;
}

static void close_encoder(oh264 *e) {
	(*e->enc)->Uninitialize(e->enc);
	WelsDestroySVCEncoder(e->enc);
	free(e->i420);
	free(e);
}
*/
import "C"

import (
	"errors"
	"os"
	"strconv"
	"strings"
	"time"
	"unsafe"
)

// encoder is OpenH264 (BSD) in screen-content mode behind libyuv's SIMD
// BGRX->I420 conversion: the permissively licensed alternative to libx264.
type encoder struct {
	e         *C.oh264
	out       []byte
	converted time.Duration
}

// The encoder's defaults (h264/, bench/README.md): screen mode at low
// complexity, quality-mode rate control with a QP floor of 17 (h264/ has no
// deblocking filter, as x264 ultrafast runs without one) takes fewer bits
// and less CPU than x264 ultrafast on desktop content.
const openH264QP = 17

func newEncoder(width, height, fps int) (*encoder, error) {
	opts := C.oh_opts{qp: openH264QP, refs: -1, vui: -1}
	// JUMPSEAT_OH264 ("qp=24,refs=1") overrides them for the A/B harness.
	for _, pair := range strings.Split(os.Getenv("JUMPSEAT_OH264"), ",") {
		key, value, _ := strings.Cut(pair, "=")
		n, err := strconv.Atoi(value)
		if err != nil {
			continue
		}
		field := map[string]*C.int{"qp": &opts.qp, "refs": &opts.refs, "vui": &opts.vui}[key]
		if field != nil {
			*field = C.int(n)
		}
	}
	e := C.open_encoder(C.int(width), C.int(height), C.int(fps), opts)
	if e == nil {
		return nil, errors.New("openh264 did not open")
	}
	return &encoder{e: e, out: make([]byte, width*height*2)}, nil
}

func (e *encoder) encode(bgrx []byte, width, height int, pts int64) ([]byte, bool) {
	var key C.int
	started := time.Now()
	C.convert_frame(e.e, (*C.uint8_t)(unsafe.Pointer(&bgrx[0])), C.int(width), C.int(height))
	e.converted = time.Since(started)
	size := C.encode_frame(e.e, C.int(width), C.int(height), C.int64_t(pts),
		(*C.uint8_t)(unsafe.Pointer(&e.out[0])), C.int(len(e.out)), &key)
	if size <= 0 {
		return nil, false
	}
	return append([]byte(nil), e.out[:size]...), key != 0
}

// setMaxKbps caps the bitrate (0 lifts the cap).
func (e *encoder) setMaxKbps(kbps int) { C.set_max_kbps(e.e, C.int(kbps)) }

func (e *encoder) close() { C.close_encoder(e.e) }

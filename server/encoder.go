//go:build x264

package main

/*
#cgo LDFLAGS: -lx264 -lyuv
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <x264.h>
#define UNCAPPED_KBPS 100000
#include <libyuv/convert.h>

// apply_params applies "key=value:key=value" x264 options (x264_param_parse).
static int apply_params(x264_param_t *p, char *params) {
	for (char *pair = strtok(params, ":"); pair; pair = strtok(0, ":")) {
		char *value = strchr(pair, '=');
		if (value) *value++ = 0;
		if (x264_param_parse(p, pair, value) < 0) return -1;
	}
	return 0;
}

static x264_t *open_encoder(int w, int h, int fps, int crf, char *params, x264_picture_t *pic) {
	x264_param_t p;
	x264_param_default_preset(&p, "ultrafast", "zerolatency");
	p.i_log_level = X264_LOG_WARNING;
	p.i_width = w; p.i_height = h; p.i_csp = X264_CSP_I420;
	p.i_fps_num = fps; p.i_fps_den = 1;
	// Constant frame rate input, as zerolatency sets it, though frames come
	// only when the screen changes: with variable-rate input x264 holds each
	// frame until the next arrives, to learn its duration, so the last frame
	// of every change sat in the encoder until something else moved (idle
	// key-to-screen p50 71 -> 30 ms, p90 107 -> 48 ms; see bench/README.md).
	p.i_timebase_num = 1; p.i_timebase_den = 1000;
	p.rc.i_rc_method = X264_RC_CRF; p.rc.f_rf_constant = crf;
	// One keyframe per connection: the stream is TCP, nothing is lost.
	p.i_keyint_max = X264_KEYINT_MAX_INFINITE; p.b_repeat_headers = 1; p.b_annexb = 1;
	// VBV on from the start, so set_max_kbps can tighten it later; this cap
	// binds nothing.
	p.rc.i_vbv_max_bitrate = UNCAPPED_KBPS; p.rc.i_vbv_buffer_size = UNCAPPED_KBPS;
	// libyuv's ARGBToI420 is BT.601, limited range: say so in the stream, or
	// a decoder free to guess (Chrome picks BT.709 for HD) shifts colours.
	p.vui.i_colorprim = 6; p.vui.i_transfer = 6; p.vui.i_colmatrix = 6; p.vui.b_fullrange = 0;
	if (params && apply_params(&p, params) < 0) return 0;
	if (x264_param_apply_profile(&p, "baseline") < 0) return 0;
	if (x264_picture_alloc(pic, X264_CSP_I420, w, h) < 0) return 0;
	return x264_encoder_open(&p);
}

// set_max_kbps caps the bitrate to what the viewer's link carries, with a
// 125 ms buffer: no frame takes the link much longer than that to send.
static int set_max_kbps(x264_t *e, int kbps) {
	x264_param_t p;
	x264_encoder_parameters(e, &p);
	p.rc.i_vbv_max_bitrate = kbps;
	p.rc.i_vbv_buffer_size = kbps / 8 > 0 ? kbps / 8 : 1;
	return x264_encoder_reconfig(e, &p);
}

static void convert_frame(x264_picture_t *pic, const uint8_t *bgrx, int w, int h) {
	// libyuv's "ARGB" is B,G,R,A in memory: X's BGRX.
	ARGBToI420(bgrx, w * 4, pic->img.plane[0], pic->img.i_stride[0], pic->img.plane[1], pic->img.i_stride[1],
		pic->img.plane[2], pic->img.i_stride[2], w, h);
}

static int encode_frame(x264_t *e, x264_picture_t *pic, int64_t pts, uint8_t **out, int *key) {
	pic->i_pts = pts;
	x264_nal_t *nal; int n; x264_picture_t got;
	int size = x264_encoder_encode(e, &nal, &n, pic, &got);
	// x264 lays a frame's NAL units out back to back.
	if (size > 0) { *out = nal[0].p_payload; *key = got.b_keyframe; }
	return size;
}

*/
import "C"

import (
	"errors"
	"os"
	"time"
	"unsafe"
)

// encoder is libx264 behind libyuv's SIMD BGRX->I420 conversion, in
// process: no ffmpeg, and no 4 MB frame copied down a pipe (bench/README.md).
type encoder struct {
	x   *C.x264_t
	pic C.x264_picture_t
	// How long the last frame's colorspace conversion took (stageTimes).
	converted time.Duration
}

// Constant quality: crf 25 keeps small text sharp (48 dB against the
// lossless screen); scrolling a page costs ~1.3 Mbit/s at 60 fps (KasmVNC:
// ~15 at 24). 25, not 26: constant-rate input spends a little less per
// frame, and 25 matches what 26 gave with variable-rate input.
const encoderCRF = 25

func newEncoder(width, height, fps int) (*encoder, error) {
	e := &encoder{}
	// JUMPSEAT_X264 overrides x264 options for the A/B harness.
	var params *C.char
	if value := os.Getenv("JUMPSEAT_X264"); value != "" {
		params = C.CString(value)
		defer C.free(unsafe.Pointer(params))
	}
	e.x = C.open_encoder(C.int(width), C.int(height), C.int(fps), encoderCRF, params, &e.pic)
	if e.x == nil {
		return nil, errors.New("x264 did not open")
	}
	return e, nil
}

// encode returns one Annex B access unit and whether it is a keyframe; nil
// when x264 held the frame back.
func (e *encoder) encode(bgrx []byte, width, height int, pts int64) ([]byte, bool) {
	var out *C.uint8_t
	var key C.int
	started := time.Now()
	C.convert_frame(&e.pic, (*C.uint8_t)(unsafe.Pointer(&bgrx[0])), C.int(width), C.int(height))
	e.converted = time.Since(started)
	size := C.encode_frame(e.x, &e.pic, C.int64_t(pts), &out, &key)
	if size <= 0 {
		return nil, false
	}
	return C.GoBytes(unsafe.Pointer(out), size), key != 0
}

// setMaxKbps caps the bitrate (0 lifts the cap).
func (e *encoder) setMaxKbps(kbps int) {
	if kbps <= 0 {
		kbps = C.UNCAPPED_KBPS
	}
	C.set_max_kbps(e.x, C.int(kbps))
}

func (e *encoder) close() {
	C.x264_encoder_close(e.x)
	C.x264_picture_clean(&e.pic)
}

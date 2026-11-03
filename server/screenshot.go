package main

import (
	"image"
	"image/png"
	"net/http"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/xproto"
)

// getScreenshot answers with the screen as a PNG, through the same MIT-SHM
// grab the stream uses, whether or not anyone is watching. After voxl's
// /screenshot.
func getScreenshot(display string) http.HandlerFunc {
	return func(w http.ResponseWriter, _ *http.Request) {
		conn, err := xgb.NewConnDisplay(display)
		if err != nil {
			http.Error(w, "no X display", http.StatusServiceUnavailable)
			return
		}
		defer conn.Close()
		root := xproto.Setup(conn).DefaultScreen(conn).Root
		geometry, err := xproto.GetGeometry(conn, xproto.Drawable(root)).Reply()
		if err != nil {
			http.Error(w, "no X display", http.StatusServiceUnavailable)
			return
		}
		width, height := int(geometry.Width), int(geometry.Height)
		read, release, err := grabber(conn, root, width, height)
		if err != nil {
			http.Error(w, "no MIT-SHM", http.StatusServiceUnavailable)
			return
		}
		defer release()
		bgrx, err := read()
		if err != nil {
			http.Error(w, "grab failed", http.StatusServiceUnavailable)
			return
		}
		img := image.NewRGBA(image.Rect(0, 0, width, height))
		for i := 0; i < width*height*4; i += 4 {
			img.Pix[i], img.Pix[i+1], img.Pix[i+2], img.Pix[i+3] = bgrx[i+2], bgrx[i+1], bgrx[i], 255
		}
		w.Header().Set("content-type", "image/png")
		// Speed over size: a screenshot is read once, by an agent, right away.
		_ = (&png.Encoder{CompressionLevel: png.BestSpeed}).Encode(w, img)
	}
}

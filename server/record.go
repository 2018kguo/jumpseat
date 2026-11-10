package main

import (
	"os"
	"time"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/xproto"
)

// record writes the next n changed frames of the display, raw BGRX one after
// another, for bench/: the encoder benchmark replays them, so every build is
// judged on identical input.
func record(display, path string, n int) error {
	conn, err := xgb.NewConnDisplay(display)
	if err != nil {
		return err
	}
	defer conn.Close()
	changes := make(chan struct{}, 1)
	if err := watchDamage(conn, changes); err != nil {
		return err
	}
	root := xproto.Setup(conn).DefaultScreen(conn).Root
	geometry, err := xproto.GetGeometry(conn, xproto.Drawable(root)).Reply()
	if err != nil {
		return err
	}
	read, release, err := grabber(conn, root, int(geometry.Width)&^1, int(geometry.Height)&^1)
	if err != nil {
		return err
	}
	defer release()
	out, err := os.Create(path)
	if err != nil {
		return err
	}
	defer out.Close()
	for range n {
		<-changes
		// Paced as the streamer grabs: a frame at most every interval.
		time.Sleep(time.Second / maxFPS)
		pixels, err := read()
		if err != nil {
			return err
		}
		if _, err := out.Write(pixels); err != nil {
			return err
		}
	}
	return nil
}

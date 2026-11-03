package main

import (
	"fmt"
	"os"
	"time"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/damage"
	"github.com/jezek/xgb/xfixes"
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
	if err := damage.Init(conn); err != nil {
		return err
	}
	if err := xfixes.Init(conn); err != nil {
		return err
	}
	_, _ = xfixes.QueryVersion(conn, 5, 0).Reply()
	_, _ = damage.QueryVersion(conn, 1, 1).Reply()
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
	dmg, _ := damage.NewDamageId(conn)
	damage.Create(conn, dmg, xproto.Drawable(root), damage.ReportLevelNonEmpty)
	out, err := os.Create(path)
	if err != nil {
		return err
	}
	defer out.Close()
	interval := time.Second / maxFPS
	for i := 0; i < n; {
		event, xerr := conn.WaitForEvent()
		if xerr != nil || event == nil {
			return fmt.Errorf("x: %v", xerr)
		}
		if _, ok := event.(damage.NotifyEvent); !ok {
			continue
		}
		time.Sleep(interval)
		damage.Subtract(conn, dmg, 0, 0)
		pixels, err := read()
		if err != nil {
			return err
		}
		if _, err := out.Write(pixels); err != nil {
			return err
		}
		i++
	}
	return nil
}

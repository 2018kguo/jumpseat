package main

import (
	"encoding/json"
	"net/http"
	"strconv"
	"time"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/damage"
	"github.com/jezek/xgb/xproto"
)

// maxWait bounds ?timeout=: each wait holds an X connection open.
const maxWait = time.Minute

// getWait answers once the screen has been still for ?idle= ms (default
// 300, counted from the request so what an action just did has time to
// show), or at the first change with ?change. ?timeout= ms gives up (default
// 10 s). XDamage already says when the screen changes, so the agent needs
// no screenshots or sleeps to know a page has settled. A text caret blinks
// about twice a second: keep idle under 500 ms. After voxl's /wait.
func getWait(display string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		query := r.URL.Query()
		idle, ok := millis(query.Get("idle"), 300*time.Millisecond)
		timeout, ok2 := millis(query.Get("timeout"), 10*time.Second)
		if !ok || !ok2 {
			http.Error(w, "idle and timeout are whole milliseconds", http.StatusBadRequest)
			return
		}
		conn, err := xgb.NewConnDisplay(display)
		if err != nil {
			http.Error(w, "no X display", http.StatusServiceUnavailable)
			return
		}
		defer conn.Close()
		changes := make(chan struct{}, 1)
		if err := watchDamage(conn, changes); err != nil {
			http.Error(w, "no XDamage", http.StatusServiceUnavailable)
			return
		}
		start := time.Now()
		changed, timedOut := settle(r.Context().Done(), changes, query.Has("change"), idle, min(timeout, maxWait))
		w.Header().Set("content-type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{"changed": changed, "timedOut": timedOut, "waitedMs": time.Since(start).Milliseconds()})
	}
}

// watchDamage signals changes on every change to the root window.
func watchDamage(conn *xgb.Conn, changes chan<- struct{}) error {
	if err := damage.Init(conn); err != nil {
		return err
	}
	if _, err := damage.QueryVersion(conn, 1, 1).Reply(); err != nil {
		return err
	}
	dmg, err := damage.NewDamageId(conn)
	if err != nil {
		return err
	}
	root := xproto.Setup(conn).DefaultScreen(conn).Root
	if err := damage.CreateChecked(conn, dmg, xproto.Drawable(root), damage.ReportLevelNonEmpty).Check(); err != nil {
		return err
	}
	go func() {
		// The server reports the whole window damaged when the damage is
		// created (a still screen answered a change wait at 0 ms): not a change.
		first := true
		for {
			event, err := conn.WaitForEvent()
			if event == nil && err == nil {
				return
			}
			if _, ok := event.(damage.NotifyEvent); ok {
				// NonEmpty reports again only once the damage is emptied.
				damage.Subtract(conn, dmg, 0, 0)
				if first {
					first = false
					continue
				}
				select {
				case changes <- struct{}{}:
				default:
				}
			}
		}
	}()
	return nil
}

// settle returns at the first change (untilChange) or once no change has
// come for idle, whichever was asked, or after timeout.
func settle(done <-chan struct{}, changes <-chan struct{}, untilChange bool, idle, timeout time.Duration) (changed, timedOut bool) {
	deadline := time.After(timeout)
	quiet := time.NewTimer(idle)
	defer quiet.Stop()
	for {
		select {
		case <-changes:
			changed = true
			if untilChange {
				return true, false
			}
			quiet.Reset(idle)
		case <-quiet.C:
			if !untilChange {
				return changed, false
			}
		case <-deadline:
			return changed, true
		case <-done:
			return changed, false
		}
	}
}

func millis(value string, fallback time.Duration) (time.Duration, bool) {
	if value == "" {
		return fallback, true
	}
	ms, err := strconv.Atoi(value)
	return time.Duration(ms) * time.Millisecond, err == nil && ms >= 0
}

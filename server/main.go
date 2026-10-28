// agentview streams an X display to browsers as H.264 over a websocket, for
// watching and taking over a computer-use agent's desktop, and gives the
// agent a small HTTP API to say what it is doing, hand a step to the user,
// and know when the user is driving.
//
// A frame is captured only when XDamage reports a change, so an idle screen
// costs nothing; frames go through MIT-SHM, libyuv and OpenH264 (or libx264
// with -tags x264) in process. See PROTOCOL.md for the wire format.
package main

import (
	"crypto/subtle"
	"embed"
	"encoding/json"
	"flag"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"golang.org/x/net/websocket"
)

const (
	width, height = 1280, 800
	// Above Chromium's 60 Hz on purpose: a 60 cap paced off one grab's start
	// drops a frame whenever Chromium's lands a little early (57 fps measured;
	// 60.0 at this cap, same CPU).
	maxFPS = 90
	// How often the pointer is checked: it moves without damaging the screen,
	// and Xvfb draws no cursor into the frame.
	pointerPoll = 33 * time.Millisecond
	// The user counts as driving for this long after their last input.
	userLease = 5 * time.Second
)

//go:embed web
var webFiles embed.FS

func main() {
	listen := flag.String("listen", "unix:/run/agentview/stream.sock", `where to listen: "unix:/path" (trusted callers only, e.g. behind your proxy) or "host:port" (requires --token)`)
	token := flag.String("token", os.Getenv("AGENTVIEW_TOKEN"), "bearer token for every route on a TCP listener (or AGENTVIEW_TOKEN)")
	display := flag.String("display", ":0", "X display")
	dir := flag.String("dir", "/run/agentview", "directory for the agent status and user-input files")
	flag.Parse()

	if err := os.MkdirAll(*dir, 0o700); err != nil {
		log.Fatal(err)
	}
	agentStatusFile = filepath.Join(*dir, "agent-status")
	humanInputFile = filepath.Join(*dir, "human-input")

	var listener net.Listener
	var err error
	if path, ok := strings.CutPrefix(*listen, "unix:"); ok {
		_ = os.Remove(path)
		if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
			log.Fatal(err)
		}
		listener, err = net.Listen("unix", path)
		if err == nil {
			_ = os.Chmod(path, 0o600)
		}
		// Whoever reaches the socket is trusted: no token.
		*token = ""
	} else {
		if *token == "" {
			log.Fatal("a TCP listener needs --token (or AGENTVIEW_TOKEN): every page the agent's browser opens can reach local ports")
		}
		listener, err = net.Listen("tcp", *listen)
	}
	if err != nil {
		log.Fatal(err)
	}
	log.Printf("agentview on %s, display %s", *listen, *display)
	log.Fatal(http.Serve(listener, routes(*display, *token)))
}

func routes(display, token string) http.Handler {
	mux := http.NewServeMux()
	stream := websocket.Server{
		// The token is the gate; a browser always sends an Origin.
		Handshake: func(*websocket.Config, *http.Request) error { return nil },
		Handler:   func(ws *websocket.Conn) { serve(ws, display) },
	}
	mux.Handle("GET /stream", authed(token, stream))
	mux.Handle("POST /agent/status", authed(token, http.HandlerFunc(postStatus)))
	mux.Handle("GET /agent/lease", authed(token, http.HandlerFunc(getLease)))
	mux.Handle("POST /agent/answer", authed(token, http.HandlerFunc(postAnswer)))
	// The demo viewer: static files, no secrets; it asks for the token.
	web, _ := fs.Sub(webFiles, "web")
	mux.Handle("GET /", http.FileServerFS(web))
	return mux
}

// authed checks the bearer token, from the Authorization header or, for a
// browser's websocket which cannot set headers, a ?token= query.
func authed(token string, next http.Handler) http.Handler {
	if token == "" {
		return next
	}
	want := []byte(token)
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		got := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
		if got == "" {
			got = r.URL.Query().Get("token")
		}
		if subtle.ConstantTimeCompare([]byte(got), want) != 1 {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		next.ServeHTTP(w, r)
	})
}

// answer is the user's reply to the last handoff ("done" or "skip"), until
// the agent says something new.
var answer struct {
	sync.Mutex
	reply string
}

// postStatus records what the agent is doing: {"kind": "acting" | "waiting"
// | "handoff" | "idle", "text": "Opening LinkedIn"}. Every viewer shows it.
func postStatus(w http.ResponseWriter, r *http.Request) {
	var status agentStatus
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 4096)).Decode(&status); err != nil {
		http.Error(w, "body must be {\"kind\", \"text\"}", http.StatusBadRequest)
		return
	}
	switch status.Kind {
	case "acting", "waiting", "handoff", "idle":
	default:
		http.Error(w, "kind must be acting, waiting, handoff or idle", http.StatusBadRequest)
		return
	}
	if runes := []rune(status.Text); len(runes) > 120 {
		status.Text = string(runes[:119]) + "…"
	}
	encoded, _ := json.Marshal(status)
	tmp := agentStatusFile + ".tmp"
	if err := os.WriteFile(tmp, encoded, 0o600); err != nil || os.Rename(tmp, agentStatusFile) != nil {
		http.Error(w, "could not record the status", http.StatusInternalServerError)
		return
	}
	answer.Lock()
	answer.reply = ""
	answer.Unlock()
	w.WriteHeader(http.StatusNoContent)
}

// getLease tells the agent whether the user is driving (input in the last
// userLease), so its own clicks can wait, and how they answered a handoff.
func getLease(w http.ResponseWriter, _ *http.Request) {
	driving := false
	var lastInput int64
	if raw, err := os.ReadFile(humanInputFile); err == nil {
		var ms int64
		if json.Unmarshal(raw, &ms) == nil {
			lastInput = ms
			driving = time.Since(time.UnixMilli(ms)) < userLease
		}
	}
	answer.Lock()
	reply := answer.reply
	answer.Unlock()
	w.Header().Set("content-type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"driving": driving, "lastInputMs": lastInput, "answer": reply})
}

// postAnswer is the viewer's Done or Skip for a handoff: {"reply": "done"}.
func postAnswer(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Reply string `json:"reply"`
	}
	if json.NewDecoder(http.MaxBytesReader(w, r.Body, 1024)).Decode(&body) != nil || (body.Reply != "done" && body.Reply != "skip") {
		http.Error(w, "body must be {\"reply\": \"done\" | \"skip\"}", http.StatusBadRequest)
		return
	}
	answer.Lock()
	answer.reply = body.Reply
	answer.Unlock()
	w.WriteHeader(http.StatusNoContent)
}

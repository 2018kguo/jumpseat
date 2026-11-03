package main

import (
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
)

// Every route is behind the token on a TCP listener, the demo page aside.
func TestRoutesNeedTheToken(t *testing.T) {
	dir := t.TempDir()
	agentStatusFile, humanInputFile = filepath.Join(dir, "status"), filepath.Join(dir, "input")
	handler := routes(":0", "secret")
	for _, c := range []struct {
		method, path, auth string
		want               int
	}{
		{"GET", "/stream", "", http.StatusUnauthorized},
		{"GET", "/agent/lease", "", http.StatusUnauthorized},
		{"GET", "/agent/wait", "", http.StatusUnauthorized},
		{"POST", "/agent/status", "Bearer wrong", http.StatusUnauthorized},
		{"GET", "/agent/lease", "Bearer secret", http.StatusOK},
		{"GET", "/", "", http.StatusOK},
	} {
		req := httptest.NewRequest(c.method, c.path, strings.NewReader(`{"kind":"acting","text":"x"}`))
		if c.auth != "" {
			req.Header.Set("Authorization", c.auth)
		}
		rec := httptest.NewRecorder()
		handler.ServeHTTP(rec, req)
		if rec.Code != c.want {
			t.Errorf("%s %s (%q): %d, want %d", c.method, c.path, c.auth, rec.Code, c.want)
		}
	}
}

// A handoff answer lasts until the agent says something new.
func TestAnswerClearsOnTheNextStatus(t *testing.T) {
	dir := t.TempDir()
	agentStatusFile, humanInputFile = filepath.Join(dir, "status"), filepath.Join(dir, "input")
	handler := routes(":0", "")
	do := func(method, path, body string) string {
		rec := httptest.NewRecorder()
		handler.ServeHTTP(rec, httptest.NewRequest(method, path, strings.NewReader(body)))
		return rec.Body.String()
	}
	do("POST", "/agent/status", `{"kind":"handoff","text":"Sign in"}`)
	do("POST", "/agent/answer", `{"reply":"done"}`)
	if lease := do("GET", "/agent/lease", ""); !strings.Contains(lease, `"answer":"done"`) {
		t.Fatalf("lease %s", lease)
	}
	do("POST", "/agent/status", `{"kind":"acting","text":"Continuing"}`)
	if lease := do("GET", "/agent/lease", ""); !strings.Contains(lease, `"answer":""`) {
		t.Fatalf("lease kept a stale answer: %s", lease)
	}
}

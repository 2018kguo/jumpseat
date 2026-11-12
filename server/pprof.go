//go:build pprof

package main

// Built with -tags pprof (bench/: GOFLAGS=-tags=pprof), the Go profiler
// listens on 127.0.0.1:6060. Never in a release.
import (
	"log"
	"net/http"
	_ "net/http/pprof"
)

func init() {
	go func() { log.Println(http.ListenAndServe("127.0.0.1:6060", nil)) }()
}

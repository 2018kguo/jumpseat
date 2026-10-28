package main

import (
	"encoding/json"
	"os"
	"time"
)

// agentStatusFile holds what the agent is doing, for the viewer's caption,
// waiting hint and handoff banner: written by POST /agent/status, or by any
// process on the machine (JSON {"kind","text"}). Set from --dir.
var agentStatusFile = "/run/jumpseat/agent-status"

const statusPoll = 250 * time.Millisecond

type agentStatus struct {
	Kind string `json:"kind"`
	Text string `json:"text"`
}

// agentStatusWatch reports the agent's status when it differs from what
// this viewer was last sent. No file yet reads as idle.
type agentStatusWatch struct {
	sent *agentStatus
}

func (w *agentStatusWatch) changed() (agentStatus, bool) {
	status := agentStatus{Kind: "idle"}
	if raw, err := os.ReadFile(agentStatusFile); err == nil {
		var read agentStatus
		if json.Unmarshal(raw, &read) == nil && read.Kind != "" {
			status = read
		}
	}
	if w.sent != nil && *w.sent == status {
		return status, false
	}
	w.sent = &status
	return status, true
}

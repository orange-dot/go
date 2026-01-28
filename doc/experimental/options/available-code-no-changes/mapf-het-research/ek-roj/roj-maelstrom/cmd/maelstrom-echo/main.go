// maelstrom-echo implements the Maelstrom echo workload for infrastructure validation.
//
// This is a minimal Maelstrom node that responds to "echo" messages with "echo_ok",
// validating that the harness is working before adding ROJ consensus logic.
//
// Usage:
//
//	maelstrom test -w echo --bin ./maelstrom-echo --node-count 1 --time-limit 10
package main

import (
	"encoding/json"
	"log"

	maelstrom "github.com/jepsen-io/maelstrom/demo/go"
)

func main() {
	n := maelstrom.NewNode()

	// Handle init message (automatic)
	// The maelstrom library handles init internally

	// Handle echo messages
	n.Handle("echo", func(msg maelstrom.Message) error {
		// Parse the message body
		var body map[string]any
		if err := json.Unmarshal(msg.Body, &body); err != nil {
			return err
		}

		// Get the echo value
		echoValue := body["echo"]

		// Respond with echo_ok
		return n.Reply(msg, map[string]any{
			"type": "echo_ok",
			"echo": echoValue,
		})
	})

	// Run the node
	if err := n.Run(); err != nil {
		log.Fatal(err)
	}
}

// ROJ Node - Go Implementation
//
// Distributed consensus node for the ROJ protocol.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/elektrokombinacija/roj-node-go/consensus"
	"github.com/elektrokombinacija/roj-node-go/discovery"
	"github.com/elektrokombinacija/roj-node-go/transport"
)

func main() {
	name := flag.String("name", "", "Node name/identifier (required)")
	port := flag.Int("port", 9990, "UDP port to listen on")
	flag.Parse()

	if *name == "" {
		fmt.Println("Error: --name is required")
		flag.Usage()
		os.Exit(1)
	}

	log.SetFlags(log.Ltime)
	log.Printf("[INFO] ROJ node \"%s\" starting (go)", *name)

	// Initialize discovery
	disc := discovery.New(*name, "go", *port)
	if err := disc.Announce(); err != nil {
		log.Fatalf("[ERROR] Failed to announce: %v", err)
	}
	disc.Browse()

	// Initialize transport
	trans, err := transport.New(*port)
	if err != nil {
		log.Fatalf("[ERROR] Failed to create transport: %v", err)
	}
	log.Printf("[INFO] Listening on %s", trans.LocalAddr())
	trans.Start()

	// Initialize consensus
	cons := consensus.New(*name, disc.PeerCount)

	// Cleanup ticker
	cleanupTicker := time.NewTicker(5 * time.Second)
	defer cleanupTicker.Stop()

	// Signal handling
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	// Stdin handling for proposals
	proposeCh := make(chan [2]string, 16)
	go handleStdin(proposeCh)

	fmt.Println("\nCommands:")
	fmt.Println("  propose <key> <value>  - Propose a consensus value")
	fmt.Println("  state                  - Show committed state")
	fmt.Println("  peers                  - Show discovered peers")
	fmt.Println("  quit                   - Exit\n")

	// Main event loop
	for {
		select {
		case <-sigCh:
			log.Println("[INFO] Shutting down...")
			disc.Stop()
			trans.Stop()
			return

		case <-cleanupTicker.C:
			cons.CleanupExpired()

		case recvMsg := <-trans.Recv():
			handleMessage(*name, recvMsg, cons, disc, trans)

		case kv := <-proposeCh:
			key, valueStr := kv[0], kv[1]
			if key == "state" {
				state := cons.GetState()
				fmt.Println("Committed state:")
				for k, v := range state {
					fmt.Printf("  %s = %s\n", k, string(v))
				}
				if len(state) == 0 {
					fmt.Println("  (empty)")
				}
				continue
			}
			if key == "peers" {
				peers := disc.GetPeers()
				fmt.Println("Discovered peers:")
				for _, p := range peers {
					fmt.Printf("  %s (%s) at %s\n", p.NodeID, p.Lang, p.Addr)
				}
				if len(peers) == 0 {
					fmt.Println("  (none)")
				}
				continue
			}

			value, err := strconv.ParseInt(valueStr, 10, 64)
			if err != nil {
				fmt.Println("Invalid value (must be integer)")
				continue
			}

			proposeMsg := cons.CreateProposal(key, value)
			addrs := disc.GetPeerAddrs()
			if len(addrs) == 0 {
				log.Println("[INFO] No peers discovered yet")
			} else {
				trans.Broadcast(proposeMsg, addrs)
			}
		}
	}
}

func handleMessage(myName string, recv *transport.ReceivedMessage, cons *consensus.Consensus, disc *discovery.Discovery, trans *transport.Transport) {
	msg := recv.Msg

	switch msg.Type {
	case "ANNOUNCE":
		// mDNS handles discovery, but we could update last_seen here

	case "PROPOSE":
		if msg.From != myName {
			voteMsg := cons.HandleProposal(msg)
			if err := trans.Send(voteMsg, recv.From); err != nil {
				log.Printf("[WARN] Failed to send vote: %v", err)
			}
		}

	case "VOTE":
		if msg.From != myName {
			commitMsg := cons.HandleVote(msg)
			if commitMsg != nil {
				addrs := disc.GetPeerAddrs()
				trans.Broadcast(commitMsg, addrs)
			}
		}

	case "COMMIT":
		cons.HandleCommit(msg)
	}
}

func handleStdin(proposeCh chan<- [2]string) {
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		parts := strings.Fields(line)
		if len(parts) == 0 {
			continue
		}

		switch parts[0] {
		case "propose":
			if len(parts) >= 3 {
				proposeCh <- [2]string{parts[1], parts[2]}
			} else {
				fmt.Println("Usage: propose <key> <value>")
			}
		case "state":
			proposeCh <- [2]string{"state", ""}
		case "peers":
			proposeCh <- [2]string{"peers", ""}
		case "quit", "exit":
			os.Exit(0)
		default:
			fmt.Println("Unknown command. Try: propose <key> <value>")
		}
	}
}

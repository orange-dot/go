// Package transport provides UDP messaging for ROJ protocol
package transport

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
)

const (
	MaxMsgSize  = 65536
	DefaultPort = 9990
)

// Message represents a ROJ protocol message
type Message struct {
	Type       string          `json:"type"`
	NodeID     string          `json:"node_id,omitempty"`
	Lang       string          `json:"lang,omitempty"`
	Capabilities []string      `json:"capabilities,omitempty"`
	Version    string          `json:"version,omitempty"`
	ProposalID string          `json:"proposal_id,omitempty"`
	From       string          `json:"from,omitempty"`
	Key        string          `json:"key,omitempty"`
	Value      json.RawMessage `json:"value,omitempty"`
	Timestamp  int64           `json:"timestamp,omitempty"`
	Vote       string          `json:"vote,omitempty"`
	Voters     []string        `json:"voters,omitempty"`
}

// Transport handles UDP send/receive for ROJ messages
type Transport struct {
	conn      *net.UDPConn
	localAddr *net.UDPAddr
	recvCh    chan *ReceivedMessage
	stopCh    chan struct{}
}

// ReceivedMessage contains a message and its source address
type ReceivedMessage struct {
	Msg  *Message
	From *net.UDPAddr
}

// New creates a new Transport bound to the specified port
func New(port int) (*Transport, error) {
	addr := &net.UDPAddr{
		IP:   net.IPv4zero,
		Port: port,
	}

	conn, err := net.ListenUDP("udp4", addr)
	if err != nil {
		return nil, fmt.Errorf("failed to bind UDP: %w", err)
	}

	localAddr := conn.LocalAddr().(*net.UDPAddr)

	return &Transport{
		conn:      conn,
		localAddr: localAddr,
		recvCh:    make(chan *ReceivedMessage, 256),
		stopCh:    make(chan struct{}),
	}, nil
}

// LocalAddr returns the local address this transport is bound to
func (t *Transport) LocalAddr() *net.UDPAddr {
	return t.localAddr
}

// Start begins receiving messages in background
func (t *Transport) Start() {
	go t.receiveLoop()
}

func (t *Transport) receiveLoop() {
	buf := make([]byte, MaxMsgSize)

	for {
		select {
		case <-t.stopCh:
			return
		default:
		}

		n, src, err := t.conn.ReadFromUDP(buf)
		if err != nil {
			select {
			case <-t.stopCh:
				return
			default:
				log.Printf("[WARN] UDP receive error: %v", err)
				continue
			}
		}

		var msg Message
		if err := json.Unmarshal(buf[:n], &msg); err != nil {
			log.Printf("[WARN] Failed to parse message from %s: %v", src, err)
			continue
		}

		select {
		case t.recvCh <- &ReceivedMessage{Msg: &msg, From: src}:
		default:
			log.Printf("[WARN] Receive channel full, dropping message")
		}
	}
}

// Recv returns the channel for receiving messages
func (t *Transport) Recv() <-chan *ReceivedMessage {
	return t.recvCh
}

// Send sends a message to a specific address
func (t *Transport) Send(msg *Message, addr *net.UDPAddr) error {
	data, err := json.Marshal(msg)
	if err != nil {
		return fmt.Errorf("failed to marshal message: %w", err)
	}

	_, err = t.conn.WriteToUDP(data, addr)
	if err != nil {
		return fmt.Errorf("failed to send to %s: %w", addr, err)
	}

	return nil
}

// Broadcast sends a message to multiple addresses
func (t *Transport) Broadcast(msg *Message, addrs []*net.UDPAddr) {
	data, err := json.Marshal(msg)
	if err != nil {
		log.Printf("[WARN] Failed to marshal broadcast message: %v", err)
		return
	}

	for _, addr := range addrs {
		if _, err := t.conn.WriteToUDP(data, addr); err != nil {
			log.Printf("[WARN] Failed to send to %s: %v", addr, err)
		}
	}
}

// Stop shuts down the transport
func (t *Transport) Stop() {
	close(t.stopCh)
	t.conn.Close()
}

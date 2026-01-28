// Package discovery provides mDNS-based peer discovery for ROJ nodes
package discovery

import (
	"fmt"
	"log"
	"net"
	"sync"
	"time"

	"github.com/hashicorp/mdns"
)

const (
	ServiceName = "_roj._udp"
	DefaultPort = 9990
)

// PeerInfo contains information about a discovered peer
type PeerInfo struct {
	NodeID       string
	Lang         string
	Addr         *net.UDPAddr
	Capabilities []string
	Version      string
	LastSeen     time.Time
}

// Discovery handles mDNS-based peer discovery
type Discovery struct {
	nodeID   string
	lang     string
	port     int
	server   *mdns.Server
	peers    map[string]*PeerInfo
	peersMux sync.RWMutex
	stopCh   chan struct{}
}

// New creates a new Discovery instance
func New(nodeID, lang string, port int) *Discovery {
	return &Discovery{
		nodeID: nodeID,
		lang:   lang,
		port:   port,
		peers:  make(map[string]*PeerInfo),
		stopCh: make(chan struct{}),
	}
}

// Announce starts advertising this node via mDNS
func (d *Discovery) Announce() error {
	host, _ := net.LookupIP("localhost")
	var ips []net.IP

	// Get local IPs
	addrs, err := net.InterfaceAddrs()
	if err == nil {
		for _, addr := range addrs {
			if ipnet, ok := addr.(*net.IPNet); ok && !ipnet.IP.IsLoopback() {
				if ipnet.IP.To4() != nil {
					ips = append(ips, ipnet.IP)
				}
			}
		}
	}
	if len(ips) == 0 {
		ips = host
	}

	info := []string{
		fmt.Sprintf("lang=%s", d.lang),
		fmt.Sprintf("version=0.1.0"),
		fmt.Sprintf("caps=consensus"),
	}

	service, err := mdns.NewMDNSService(
		d.nodeID,
		ServiceName,
		"",
		"",
		d.port,
		ips,
		info,
	)
	if err != nil {
		return fmt.Errorf("failed to create mDNS service: %w", err)
	}

	server, err := mdns.NewServer(&mdns.Config{Zone: service})
	if err != nil {
		return fmt.Errorf("failed to start mDNS server: %w", err)
	}

	d.server = server
	log.Printf("[INFO] mDNS: Announcing %s on %s", d.nodeID, ServiceName)
	return nil
}

// Browse starts discovering other ROJ nodes
func (d *Discovery) Browse() {
	go func() {
		entriesCh := make(chan *mdns.ServiceEntry, 10)

		go func() {
			for entry := range entriesCh {
				d.handleEntry(entry)
			}
		}()

		ticker := time.NewTicker(2 * time.Second)
		defer ticker.Stop()

		for {
			select {
			case <-d.stopCh:
				return
			case <-ticker.C:
				params := mdns.DefaultParams(ServiceName)
				params.Entries = entriesCh
				params.Timeout = time.Second
				params.DisableIPv6 = true

				if err := mdns.Query(params); err != nil {
					log.Printf("[WARN] mDNS query failed: %v", err)
				}
			}
		}
	}()
}

func (d *Discovery) handleEntry(entry *mdns.ServiceEntry) {
	nodeID := entry.Name

	// Don't add ourselves
	if nodeID == d.nodeID {
		return
	}

	// Parse TXT records
	lang := "unknown"
	version := "0.1.0"
	var caps []string

	for _, txt := range entry.InfoFields {
		switch {
		case len(txt) > 5 && txt[:5] == "lang=":
			lang = txt[5:]
		case len(txt) > 8 && txt[:8] == "version=":
			version = txt[8:]
		case len(txt) > 5 && txt[:5] == "caps=":
			caps = append(caps, txt[5:])
		}
	}

	addr := &net.UDPAddr{
		IP:   entry.AddrV4,
		Port: entry.Port,
	}

	d.peersMux.Lock()
	existing, found := d.peers[nodeID]
	if !found {
		log.Printf("[INFO] mDNS: Discovered \"%s\" (%s) at %s", nodeID, lang, addr)
	}
	d.peers[nodeID] = &PeerInfo{
		NodeID:       nodeID,
		Lang:         lang,
		Addr:         addr,
		Capabilities: caps,
		Version:      version,
		LastSeen:     time.Now(),
	}
	if found && existing.Addr.String() != addr.String() {
		log.Printf("[INFO] mDNS: Updated \"%s\" address to %s", nodeID, addr)
	}
	d.peersMux.Unlock()
}

// GetPeers returns a copy of the current peer list
func (d *Discovery) GetPeers() map[string]*PeerInfo {
	d.peersMux.RLock()
	defer d.peersMux.RUnlock()

	result := make(map[string]*PeerInfo, len(d.peers))
	for k, v := range d.peers {
		peerCopy := *v
		result[k] = &peerCopy
	}
	return result
}

// GetPeerAddrs returns UDP addresses of all known peers
func (d *Discovery) GetPeerAddrs() []*net.UDPAddr {
	d.peersMux.RLock()
	defer d.peersMux.RUnlock()

	addrs := make([]*net.UDPAddr, 0, len(d.peers))
	for _, peer := range d.peers {
		addrs = append(addrs, peer.Addr)
	}
	return addrs
}

// PeerCount returns the number of known peers
func (d *Discovery) PeerCount() int {
	d.peersMux.RLock()
	defer d.peersMux.RUnlock()
	return len(d.peers)
}

// Stop shuts down the discovery service
func (d *Discovery) Stop() {
	close(d.stopCh)
	if d.server != nil {
		d.server.Shutdown()
	}
}

// Package adapter provides HTTP client for the Go simulator ROJ API.
package adapter

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"
)

// SimulatorClient is an HTTP client for the Go simulator ROJ API.
type SimulatorClient struct {
	baseURL    string
	httpClient *http.Client
}

// NewSimulatorClient creates a new simulator client.
func NewSimulatorClient(baseURL string) *SimulatorClient {
	return &SimulatorClient{
		baseURL: baseURL,
		httpClient: &http.Client{
			Timeout: 10 * time.Second,
		},
	}
}

// HealthCheck checks if the simulator is running.
func (c *SimulatorClient) HealthCheck() error {
	resp, err := c.httpClient.Get(c.baseURL + "/health")
	if err != nil {
		return fmt.Errorf("health check failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("health check returned %d", resp.StatusCode)
	}
	return nil
}

// ProposeRequest is the request body for /api/roj/propose.
type ProposeRequest struct {
	NodeID int    `json:"nodeId"`
	Key    string `json:"key"`
	Value  any    `json:"value"`
}

// ProposeResponse is the response from /api/roj/propose.
type ProposeResponse struct {
	Success    bool   `json:"success"`
	ProposalID string `json:"proposalId"`
}

// Propose submits a proposal via a specific node.
func (c *SimulatorClient) Propose(nodeID int, key string, value any) (*ProposeResponse, error) {
	body, err := json.Marshal(ProposeRequest{
		NodeID: nodeID,
		Key:    key,
		Value:  value,
	})
	if err != nil {
		return nil, err
	}

	resp, err := c.httpClient.Post(
		c.baseURL+"/api/roj/propose",
		"application/json",
		bytes.NewReader(body),
	)
	if err != nil {
		return nil, fmt.Errorf("propose failed: %w", err)
	}
	defer resp.Body.Close()

	var result ProposeResponse
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, err
	}
	return &result, nil
}

// StateResponse is the response from /api/roj/state.
type StateResponse struct {
	NodeID int            `json:"nodeId"`
	State  map[string]any `json:"state"`
}

// GetState gets the committed state of a node.
func (c *SimulatorClient) GetState(nodeID int) (*StateResponse, error) {
	resp, err := c.httpClient.Get(
		fmt.Sprintf("%s/api/roj/state?nodeId=%d", c.baseURL, nodeID),
	)
	if err != nil {
		return nil, fmt.Errorf("get state failed: %w", err)
	}
	defer resp.Body.Close()

	var result StateResponse
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, err
	}
	return &result, nil
}

// CrashNode crashes a node.
func (c *SimulatorClient) CrashNode(nodeID int) error {
	body, _ := json.Marshal(map[string]int{"nodeId": nodeID})
	resp, err := c.httpClient.Post(
		c.baseURL+"/api/roj/crash",
		"application/json",
		bytes.NewReader(body),
	)
	if err != nil {
		return fmt.Errorf("crash node failed: %w", err)
	}
	resp.Body.Close()
	return nil
}

// RecoverNode recovers a crashed node.
func (c *SimulatorClient) RecoverNode(nodeID int) error {
	body, _ := json.Marshal(map[string]int{"nodeId": nodeID})
	resp, err := c.httpClient.Post(
		c.baseURL+"/api/roj/recover",
		"application/json",
		bytes.NewReader(body),
	)
	if err != nil {
		return fmt.Errorf("recover node failed: %w", err)
	}
	resp.Body.Close()
	return nil
}

// PartitionRequest is the request body for /api/roj/partition.
type PartitionRequest struct {
	GroupA []int `json:"groupA"`
	GroupB []int `json:"groupB"`
}

// Partition creates a network partition between two groups.
func (c *SimulatorClient) Partition(groupA, groupB []int) error {
	body, _ := json.Marshal(PartitionRequest{
		GroupA: groupA,
		GroupB: groupB,
	})
	resp, err := c.httpClient.Post(
		c.baseURL+"/api/roj/partition",
		"application/json",
		bytes.NewReader(body),
	)
	if err != nil {
		return fmt.Errorf("partition failed: %w", err)
	}
	resp.Body.Close()
	return nil
}

// HealPartitions heals all network partitions.
func (c *SimulatorClient) HealPartitions() error {
	resp, err := c.httpClient.Post(
		c.baseURL+"/api/roj/heal",
		"application/json",
		bytes.NewReader([]byte("{}")),
	)
	if err != nil {
		return fmt.Errorf("heal partitions failed: %w", err)
	}
	resp.Body.Close()
	return nil
}

// GetHistory gets the Elle history as JSON.
func (c *SimulatorClient) GetHistory() ([]byte, error) {
	resp, err := c.httpClient.Get(c.baseURL + "/api/roj/history")
	if err != nil {
		return nil, fmt.Errorf("get history failed: %w", err)
	}
	defer resp.Body.Close()

	return io.ReadAll(resp.Body)
}

// ClearHistory clears the Elle history.
func (c *SimulatorClient) ClearHistory() error {
	resp, err := c.httpClient.Post(
		c.baseURL+"/api/roj/history/clear",
		"application/json",
		bytes.NewReader([]byte("{}")),
	)
	if err != nil {
		return fmt.Errorf("clear history failed: %w", err)
	}
	resp.Body.Close()
	return nil
}

// StatusResponse is the response from /api/roj/status.
type StatusResponse struct {
	Running          bool `json:"running"`
	Nodes            int  `json:"nodes"`
	ActiveNodes      int  `json:"activeNodes"`
	PartitionedPairs int  `json:"partitionedPairs"`
}

// GetStatus gets the cluster status.
func (c *SimulatorClient) GetStatus() (*StatusResponse, error) {
	resp, err := c.httpClient.Get(c.baseURL + "/api/roj/status")
	if err != nil {
		return nil, fmt.Errorf("get status failed: %w", err)
	}
	defer resp.Body.Close()

	var result StatusResponse
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, err
	}
	return &result, nil
}

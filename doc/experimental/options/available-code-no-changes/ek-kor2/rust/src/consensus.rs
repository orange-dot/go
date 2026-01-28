//! EK-KOR v2 - Threshold-Based Distributed Consensus
//!
//! # Novelty: Threshold Consensus for Mixed-Criticality Systems
//!
//! Modules vote on system-wide decisions using density-dependent threshold
//! mechanism. Supports supermajority for safety-critical decisions and
//! mutual inhibition for competing proposals.
//!
//! ## Patent Claims
//! 3. "A threshold-based consensus mechanism for mixed-criticality embedded
//!    systems using density-dependent activation functions"
//!
//! Dependent claims:
//! - Mutual inhibition signals for competing operational modes
//! - Weighted voting based on module health state

use crate::types::*;
use heapless::Vec;

// ============================================================================
// Proposal Types
// ============================================================================

/// Proposal types (application can extend via Custom variants)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ProposalType {
    /// Change operational mode
    ModeChange = 0,
    /// Set cluster power limit
    PowerLimit = 1,
    /// Graceful shutdown
    Shutdown = 2,
    /// Mesh reformation
    Reformation = 3,
    /// Application-defined
    Custom0 = 16,
    Custom1 = 17,
    Custom2 = 18,
    Custom3 = 19,
}

// ============================================================================
// Configuration
// ============================================================================

/// Consensus configuration
#[derive(Debug, Clone, Copy)]
pub struct ConsensusConfig {
    /// Timeout for vote collection (microseconds)
    pub vote_timeout: TimeUs,
    /// How long inhibition lasts (microseconds)
    pub inhibit_duration: TimeUs,
    /// Can proposer vote for own proposal
    pub allow_self_vote: bool,
    /// Require votes from all neighbors
    pub require_all_neighbors: bool,
}

impl Default for ConsensusConfig {
    fn default() -> Self {
        Self {
            vote_timeout: VOTE_TIMEOUT_US,
            inhibit_duration: 100_000, // 100ms
            allow_self_vote: true,
            require_all_neighbors: false,
        }
    }
}

// ============================================================================
// Ballot
// ============================================================================

/// Ballot (voting round)
#[derive(Debug, Clone)]
pub struct Ballot {
    /// Unique ballot ID
    pub id: BallotId,
    /// What we're voting on
    pub proposal_type: ProposalType,
    /// Who proposed it
    pub proposer: ModuleId,
    /// Proposal-specific data
    pub data: u32,
    /// Required approval threshold (Q16.16)
    pub threshold: Fixed,
    /// When voting ends
    pub deadline: TimeUs,

    /// Votes from neighbors (indexed by voter_id % K_NEIGHBORS for deduplication)
    votes: [VoteValue; K_NEIGHBORS],
    /// Number of votes received
    pub vote_count: u8,
    /// Number of approvals
    pub yes_count: u8,
    /// Number of rejections
    pub no_count: u8,

    /// Final result
    pub result: VoteResult,
    /// Voting finished
    pub completed: bool,
}

impl Ballot {
    /// Create new ballot
    pub fn new(
        id: BallotId,
        proposal_type: ProposalType,
        proposer: ModuleId,
        data: u32,
        threshold: Fixed,
        deadline: TimeUs,
    ) -> Self {
        Self {
            id,
            proposal_type,
            proposer,
            data,
            threshold,
            deadline,
            votes: [VoteValue::Abstain; K_NEIGHBORS],
            vote_count: 0,
            yes_count: 0,
            no_count: 0,
            result: VoteResult::Pending,
            completed: false,
        }
    }

    /// Record a vote from a specific voter (with deduplication)
    ///
    /// Uses voter_id % K_NEIGHBORS as slot index to detect duplicate votes.
    /// Returns true if vote was recorded, false if duplicate.
    pub fn record_vote_from(&mut self, voter_id: ModuleId, vote: VoteValue) -> bool {
        let slot = (voter_id as usize) % K_NEIGHBORS;

        // Check for duplicate vote
        if self.votes[slot] != VoteValue::Abstain {
            return false; // Already voted
        }

        // Record vote in slot
        self.votes[slot] = vote;
        self.vote_count += 1;

        match vote {
            VoteValue::Yes => self.yes_count += 1,
            VoteValue::No => self.no_count += 1,
            VoteValue::Inhibit => {
                self.result = VoteResult::Cancelled;
                self.completed = true;
            }
            VoteValue::Abstain => {}
        }

        true
    }

    /// Record a vote (legacy method for backwards compatibility)
    /// Note: This does not deduplicate - use record_vote_from for proper deduplication
    pub fn record_vote(&mut self, vote: VoteValue) {
        match vote {
            VoteValue::Yes => self.yes_count += 1,
            VoteValue::No => self.no_count += 1,
            VoteValue::Inhibit => {
                self.result = VoteResult::Cancelled;
                self.completed = true;
            }
            VoteValue::Abstain => {}
        }
        self.vote_count += 1;
    }

    /// Check if threshold is reached
    pub fn check_threshold(&mut self, total_voters: u8) {
        if self.completed {
            return;
        }

        let yes_ratio = if total_voters > 0 {
            Fixed::from_num(self.yes_count as f32 / total_voters as f32)
        } else {
            Fixed::ZERO
        };

        if yes_ratio >= self.threshold {
            self.result = VoteResult::Approved;
            self.completed = true;
        } else if self.vote_count >= total_voters {
            // All votes in, threshold not reached
            self.result = VoteResult::Rejected;
            self.completed = true;
        }
    }
}

// ============================================================================
// Inhibition Entry
// ============================================================================

#[derive(Debug, Clone, Copy, Default)]
struct Inhibition {
    ballot_id: BallotId,
    until: TimeUs,
}

// ============================================================================
// Consensus Engine
// ============================================================================

/// Consensus engine state
pub struct Consensus {
    /// This module's ID
    my_id: ModuleId,
    /// Active ballots
    ballots: Vec<Ballot, MAX_BALLOTS>,
    /// Inhibited ballot IDs
    inhibited: Vec<Inhibition, MAX_BALLOTS>,
    /// Next ballot ID to use
    next_ballot_id: BallotId,
    /// Configuration
    config: ConsensusConfig,

    /// Decision callback
    on_decide: Option<fn(&Ballot) -> VoteValue>,
    /// Completion callback
    on_complete: Option<fn(&Ballot, VoteResult)>,
}

impl Consensus {
    /// Create new consensus engine
    pub fn new(my_id: ModuleId, config: Option<ConsensusConfig>) -> Self {
        Self {
            my_id,
            ballots: Vec::new(),
            inhibited: Vec::new(),
            next_ballot_id: 1,
            config: config.unwrap_or_default(),
            on_decide: None,
            on_complete: None,
        }
    }

    /// Propose a vote to k-neighbors
    ///
    /// Returns ballot ID for tracking.
    pub fn propose(
        &mut self,
        proposal_type: ProposalType,
        data: u32,
        threshold: Fixed,
        now: TimeUs,
    ) -> Result<BallotId> {
        if self.ballots.len() >= MAX_BALLOTS {
            return Err(Error::Busy);
        }

        let ballot_id = self.next_ballot_id;
        self.next_ballot_id = self.next_ballot_id.wrapping_add(1);
        if self.next_ballot_id == INVALID_BALLOT_ID {
            self.next_ballot_id = 1;
        }

        let deadline = now + self.config.vote_timeout;
        let ballot = Ballot::new(ballot_id, proposal_type, self.my_id, data, threshold, deadline);

        self.ballots.push(ballot).map_err(|_| Error::NoMemory)?;

        Ok(ballot_id)
    }

    /// Cast vote in response to neighbor's proposal
    pub fn vote(&mut self, ballot_id: BallotId, vote: VoteValue) -> Result<()> {
        // Check if inhibited
        if self.is_inhibited(ballot_id) {
            return Err(Error::Inhibited);
        }

        // Find and update ballot
        for ballot in self.ballots.iter_mut() {
            if ballot.id == ballot_id && !ballot.completed {
                ballot.record_vote(vote);
                return Ok(());
            }
        }

        Err(Error::NotFound)
    }

    /// Inhibit a competing proposal
    pub fn inhibit(&mut self, ballot_id: BallotId, now: TimeUs) -> Result<()> {
        let inhibition = Inhibition {
            ballot_id,
            until: now + self.config.inhibit_duration,
        };

        self.inhibited.push(inhibition).map_err(|_| Error::NoMemory)?;

        // Mark ballot as cancelled if we have it
        for ballot in self.ballots.iter_mut() {
            if ballot.id == ballot_id {
                ballot.result = VoteResult::Cancelled;
                ballot.completed = true;
                break;
            }
        }

        Ok(())
    }

    /// Process incoming vote
    ///
    /// Uses voter_id % K_NEIGHBORS as slot index for deduplication.
    /// Duplicate votes from the same slot are ignored.
    pub fn on_vote(
        &mut self,
        voter_id: ModuleId,
        ballot_id: BallotId,
        vote: VoteValue,
        total_voters: u8,
    ) -> Result<()> {
        for ballot in self.ballots.iter_mut() {
            if ballot.id == ballot_id {
                // Use record_vote_from for proper deduplication
                if !ballot.record_vote_from(voter_id, vote) {
                    // Duplicate vote - ignore but don't error
                    return Ok(());
                }
                ballot.check_threshold(total_voters);

                if ballot.completed {
                    if let Some(callback) = self.on_complete {
                        callback(ballot, ballot.result);
                    }
                }
                return Ok(());
            }
        }

        Err(Error::NotFound)
    }

    /// Process incoming proposal
    pub fn on_proposal(
        &mut self,
        proposer_id: ModuleId,
        ballot_id: BallotId,
        proposal_type: ProposalType,
        data: u32,
        threshold: Fixed,
        now: TimeUs,
    ) -> Result<VoteValue> {
        // Check if inhibited
        if self.is_inhibited(ballot_id) {
            return Err(Error::Inhibited);
        }

        let deadline = now + self.config.vote_timeout;
        let ballot = Ballot::new(ballot_id, proposal_type, proposer_id, data, threshold, deadline);

        // Store ballot
        let _ = self.ballots.push(ballot.clone());

        // Get vote decision
        if let Some(decide) = self.on_decide {
            Ok(decide(&ballot))
        } else {
            // Default: approve everything
            Ok(VoteValue::Yes)
        }
    }

    /// Get result of a ballot
    pub fn get_result(&self, ballot_id: BallotId) -> VoteResult {
        for ballot in self.ballots.iter() {
            if ballot.id == ballot_id {
                return ballot.result;
            }
        }
        VoteResult::Pending
    }

    /// Periodic tick - check timeouts
    pub fn tick(&mut self, now: TimeUs) -> u32 {
        let mut completed = 0u32;

        // Check ballot timeouts
        for ballot in self.ballots.iter_mut() {
            if !ballot.completed && now >= ballot.deadline {
                ballot.result = VoteResult::Timeout;
                ballot.completed = true;
                completed += 1;

                if let Some(callback) = self.on_complete {
                    callback(ballot, ballot.result);
                }
            }
        }

        // Clean up expired inhibitions
        self.inhibited.retain(|i| i.until > now);

        // Clean up old completed ballots (keep last few)
        while self.ballots.len() > MAX_BALLOTS / 2 {
            if let Some(pos) = self.ballots.iter().position(|b| b.completed) {
                self.ballots.remove(pos);
            } else {
                break;
            }
        }

        completed
    }

    /// Check if ballot is inhibited
    fn is_inhibited(&self, ballot_id: BallotId) -> bool {
        self.inhibited.iter().any(|i| i.ballot_id == ballot_id)
    }

    /// Set decision callback
    pub fn set_on_decide(&mut self, callback: fn(&Ballot) -> VoteValue) {
        self.on_decide = Some(callback);
    }

    /// Set completion callback
    pub fn set_on_complete(&mut self, callback: fn(&Ballot, VoteResult)) {
        self.on_complete = Some(callback);
    }
}

// ============================================================================
// Messages
// ============================================================================

/// Vote message
#[derive(Debug, Clone, Copy)]
#[repr(C, packed)]
pub struct VoteMessage {
    pub voter_id: ModuleId,
    pub ballot_id: BallotId,
    pub vote: VoteValue,
    pub timestamp: u32,
}

/// Proposal message
#[derive(Debug, Clone, Copy)]
#[repr(C, packed)]
pub struct ProposalMessage {
    pub proposer_id: ModuleId,
    pub ballot_id: BallotId,
    pub proposal_type: ProposalType,
    pub data: u32,
    pub threshold: Fixed,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_consensus_propose() {
        let mut cons = Consensus::new(1, None);

        let ballot_id = cons
            .propose(ProposalType::ModeChange, 42, threshold::SIMPLE_MAJORITY, 1000)
            .unwrap();

        assert!(ballot_id != INVALID_BALLOT_ID);
        assert_eq!(cons.get_result(ballot_id), VoteResult::Pending);
    }

    #[test]
    fn test_consensus_voting() {
        let mut cons = Consensus::new(1, None);

        let ballot_id = cons
            .propose(ProposalType::ModeChange, 42, threshold::SIMPLE_MAJORITY, 1000)
            .unwrap();

        // Simulate 3 yes votes out of 5 total (60% > 50%)
        cons.on_vote(2, ballot_id, VoteValue::Yes, 5).unwrap();
        cons.on_vote(3, ballot_id, VoteValue::Yes, 5).unwrap();
        cons.on_vote(4, ballot_id, VoteValue::Yes, 5).unwrap();

        assert_eq!(cons.get_result(ballot_id), VoteResult::Approved);
    }

    #[test]
    fn test_consensus_inhibit() {
        let mut cons = Consensus::new(1, None);

        let ballot_id = cons
            .propose(ProposalType::ModeChange, 42, threshold::SIMPLE_MAJORITY, 1000)
            .unwrap();

        cons.inhibit(ballot_id, 1000).unwrap();

        assert_eq!(cons.get_result(ballot_id), VoteResult::Cancelled);
    }
}

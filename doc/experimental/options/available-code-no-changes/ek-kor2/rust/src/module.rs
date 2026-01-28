//! EK-KOR v2 - Module as First-Class Citizen
//!
//! # Design Philosophy
//!
//! In traditional RTOS, the Task is the primary unit. The scheduler decides
//! which task runs. In EK-KOR v2, the Module is primary. Each module:
//! - Maintains its own tasks internally
//! - Publishes coordination fields
//! - Tracks k-neighbors topologically
//! - Participates in threshold consensus
//! - Self-organizes based on gradient fields
//!
//! **There is NO global scheduler.** Each module decides locally what to do
//! based on its own state and the gradient fields from neighbors.

use crate::consensus::{Ballot, Consensus, ProposalType};
use crate::field::{FieldEngine, FieldRegion};
use crate::heartbeat::Heartbeat;
use crate::topology::Topology;
use crate::types::*;
use crate::types::{Capability, Deadline, can_perform, SLACK_NORMALIZE_US};
use heapless::Vec;

// ============================================================================
// Internal Task
// ============================================================================

/// Task state within a module
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum TaskState {
    /// Not running
    #[default]
    Idle,
    /// Ready to run
    Ready,
    /// Currently executing
    Running,
    /// Waiting for event
    Blocked,
}

/// Task function signature
pub type TaskFn = fn(*mut ());

/// Internal task (owned by module, not kernel)
#[derive(Clone)]
pub struct InternalTask {
    /// Task ID within module
    pub id: TaskId,
    /// Task name (for debugging)
    pub name: &'static str,
    /// Task function
    pub function: TaskFn,
    /// Task argument (raw pointer for flexibility)
    pub arg: *mut (),
    /// Current state
    pub state: TaskState,
    /// Local priority (0 = highest)
    pub priority: u8,
    /// Period for periodic tasks (0 = one-shot)
    pub period: TimeUs,
    /// Next scheduled run time
    pub next_run: TimeUs,
    /// Execution count
    pub run_count: u32,
    /// Total runtime in microseconds
    pub total_runtime: TimeUs,

    // MAPF-HET Integration: Deadline-aware scheduling
    /// Deadline information (None = no deadline)
    pub deadline: Option<Deadline>,
    /// Capabilities required to run this task
    pub required_caps: Capability,
}

impl Default for InternalTask {
    fn default() -> Self {
        Self {
            id: 0,
            name: "",
            function: |_| {},
            arg: core::ptr::null_mut(),
            state: TaskState::Idle,
            priority: 255,
            period: 0,
            next_run: 0,
            run_count: 0,
            total_runtime: 0,
            deadline: None,
            required_caps: 0,
        }
    }
}

// ============================================================================
// Module Callbacks
// ============================================================================

/// Module event callbacks
pub struct ModuleCallbacks {
    pub on_field_change: Option<fn(&Module)>,
    pub on_neighbor_lost: Option<fn(&Module, ModuleId)>,
    pub on_neighbor_found: Option<fn(&Module, ModuleId)>,
    pub on_vote_request: Option<fn(&Module, &Ballot) -> VoteValue>,
    pub on_consensus_complete: Option<fn(&Module, &Ballot, VoteResult)>,
    pub on_state_change: Option<fn(&Module, ModuleState)>,
}

impl Default for ModuleCallbacks {
    fn default() -> Self {
        Self {
            on_field_change: None,
            on_neighbor_lost: None,
            on_neighbor_found: None,
            on_vote_request: None,
            on_consensus_complete: None,
            on_state_change: None,
        }
    }
}

// ============================================================================
// Module Status
// ============================================================================

/// Module status summary
#[derive(Debug, Clone, Copy, Default)]
pub struct ModuleStatus {
    pub id: ModuleId,
    pub state: ModuleState,
    pub neighbor_count: u32,
    pub load_gradient: Fixed,
    pub thermal_gradient: Fixed,
    pub active_ballots: u32,
    pub ticks_total: u32,
}

// ============================================================================
// Module
// ============================================================================

/// Module - the first-class citizen of EK-KOR v2
///
/// Each module is a self-contained coordination unit that:
/// - Owns internal tasks
/// - Publishes its coordination field
/// - Maintains k-neighbor topology
/// - Participates in consensus voting
/// - Self-schedules based on gradient fields
pub struct Module {
    // Identity
    /// Module ID
    id: ModuleId,
    /// Module name (for debugging)
    name: &'static str,
    /// Current state
    state: ModuleState,

    // Coordination field
    /// My current field values
    my_field: Field,
    /// Aggregated neighbor fields
    neighbor_aggregate: Field,
    /// Current gradients
    gradients: [Fixed; FIELD_COUNT],
    /// Field engine
    field_engine: FieldEngine,

    // Topology
    /// Topological state
    topology: Topology,

    // Consensus
    /// Consensus engine
    consensus: Consensus,

    // Heartbeat
    /// Heartbeat engine
    heartbeat: Heartbeat,

    // Internal tasks
    /// Tasks owned by this module
    tasks: Vec<InternalTask, MAX_TASKS_PER_MODULE>,
    /// Currently running task (or None)
    active_task: Option<TaskId>,

    // Timing
    /// Last tick timestamp
    last_tick: TimeUs,
    /// Tick period
    tick_period: TimeUs,

    // Statistics
    /// Total ticks
    ticks_total: u32,
    /// Field updates
    field_updates: u32,
    /// Topology changes
    topology_changes: u32,
    /// Consensus rounds
    consensus_rounds: u32,

    // MAPF-HET Integration: Capability-based coordination
    /// This module's current capabilities
    capabilities: Capability,

    // Callbacks
    callbacks: ModuleCallbacks,
}

impl Module {
    /// Create a new module
    pub fn new(id: ModuleId, name: &'static str, position: Position) -> Self {
        Self {
            id,
            name,
            state: ModuleState::Init,
            my_field: Field::new(),
            neighbor_aggregate: Field::new(),
            gradients: [Fixed::ZERO; FIELD_COUNT],
            field_engine: FieldEngine::new(),
            topology: Topology::new(id, position, None),
            consensus: Consensus::new(id, None),
            heartbeat: Heartbeat::new(id, None),
            tasks: Vec::new(),
            active_task: None,
            last_tick: 0,
            tick_period: 1000, // 1ms default
            ticks_total: 0,
            field_updates: 0,
            topology_changes: 0,
            consensus_rounds: 0,
            capabilities: 0,
            callbacks: ModuleCallbacks::default(),
        }
    }

    /// Get module ID
    pub fn id(&self) -> ModuleId {
        self.id
    }

    /// Get module name
    pub fn name(&self) -> &'static str {
        self.name
    }

    /// Get current state
    pub fn state(&self) -> ModuleState {
        self.state
    }

    /// Start module operation
    pub fn start(&mut self) -> Result<()> {
        if self.state != ModuleState::Init {
            return Err(Error::InvalidArg);
        }

        self.set_state(ModuleState::Discovering);
        Ok(())
    }

    /// Stop module operation
    pub fn stop(&mut self) -> Result<()> {
        self.set_state(ModuleState::Shutdown);
        Ok(())
    }

    /// Main tick function
    ///
    /// This is the heart of the coordination loop.
    pub fn tick(&mut self, region: &mut FieldRegion, now: TimeUs) -> Result<()> {
        self.last_tick = now;
        self.ticks_total += 1;

        // 1. Update heartbeats
        let health_changed = self.heartbeat.tick(now);
        if health_changed > 0 {
            // Collect dead neighbors first to avoid borrow conflict
            let dead_neighbors: heapless::Vec<ModuleId, K_NEIGHBORS> = self.topology.neighbors()
                .iter()
                .filter(|n| self.heartbeat.get_health(n.id) == HealthState::Dead)
                .map(|n| n.id)
                .collect();

            // Then process them
            for dead_id in dead_neighbors {
                self.topology.on_neighbor_lost(dead_id).ok();
                self.topology_changes += 1;

                if let Some(cb) = self.callbacks.on_neighbor_lost {
                    cb(self, dead_id);
                }
            }
        }

        // 2. Sample neighbor fields
        self.neighbor_aggregate = self.field_engine.sample_neighbors(
            region,
            self.topology.neighbors(),
            now,
        );

        // 3. Compute gradients
        self.gradients = self.field_engine.gradient_all(&self.my_field, &self.neighbor_aggregate);

        // 4. Update consensus
        self.consensus.tick(now);

        // 5. Update topology
        if self.topology.tick(now) {
            self.topology_changes += 1;
        }

        // 6. Select and run task
        if let Some(task_id) = self.select_task() {
            self.run_task(task_id, now);
        }

        // 7. Publish updated field
        self.field_engine.publish(region, self.id, &self.my_field, now)?;
        self.field_updates += 1;

        // 8. Update state based on neighbor count
        self.update_state_from_topology();

        Ok(())
    }

    // ========================================================================
    // Task Management
    // ========================================================================

    /// Add internal task
    pub fn add_task(
        &mut self,
        name: &'static str,
        function: TaskFn,
        arg: *mut (),
        priority: u8,
        period: TimeUs,
    ) -> Result<TaskId> {
        if self.tasks.len() >= MAX_TASKS_PER_MODULE {
            return Err(Error::NoMemory);
        }

        let id = self.tasks.len() as TaskId;
        let task = InternalTask {
            id,
            name,
            function,
            arg,
            state: TaskState::Ready,
            priority,
            period,
            next_run: 0,
            run_count: 0,
            total_runtime: 0,
        };

        self.tasks.push(task).map_err(|_| Error::NoMemory)?;
        Ok(id)
    }

    /// Set task ready
    pub fn task_ready(&mut self, task_id: TaskId) -> Result<()> {
        if let Some(task) = self.tasks.get_mut(task_id as usize) {
            task.state = TaskState::Ready;
            Ok(())
        } else {
            Err(Error::NotFound)
        }
    }

    /// Block task
    pub fn task_block(&mut self, task_id: TaskId) -> Result<()> {
        if let Some(task) = self.tasks.get_mut(task_id as usize) {
            task.state = TaskState::Blocked;
            Ok(())
        } else {
            Err(Error::NotFound)
        }
    }

    /// Select task based on gradients, priority, deadlines, and capabilities
    ///
    /// MAPF-HET Integration: Task selection considers:
    /// 1. Capability matching (tasks require certain module capabilities)
    /// 2. Deadline criticality (critical tasks get priority boost)
    /// 3. Standard priority ordering
    fn select_task(&self) -> Option<TaskId> {
        let load_gradient = self.gradients[FieldComponent::Load as usize];

        // If we're significantly overloaded, throttle
        if load_gradient < Fixed::from_num(-0.2) {
            return None;
        }

        // Find best task considering capabilities, deadlines, and priority
        let mut best: Option<(TaskId, u8, bool)> = None; // (id, priority, is_critical)

        for task in self.tasks.iter() {
            if task.state != TaskState::Ready {
                continue;
            }

            // MAPF-HET: Check capability requirements
            if task.required_caps != 0 && !can_perform(self.capabilities, task.required_caps) {
                continue;
            }

            // MAPF-HET: Check if task has critical deadline
            let is_critical = task.deadline.as_ref().map_or(false, |d| d.critical);

            // Priority rules:
            // 1. Critical deadline tasks always beat non-critical
            // 2. Among critical tasks, lower priority number wins
            // 3. Among non-critical tasks, lower priority number wins
            let select_this_task = match best {
                None => true,
                Some((_, best_prio, best_critical)) => {
                    if is_critical && !best_critical {
                        true // Critical beats non-critical
                    } else if is_critical == best_critical {
                        task.priority < best_prio // Same criticality - compare priority
                    } else {
                        false
                    }
                }
            };

            if select_this_task {
                best = Some((task.id, task.priority, is_critical));
            }
        }

        best.map(|(id, _, _)| id)
    }

    /// Run a task
    fn run_task(&mut self, task_id: TaskId, now: TimeUs) {
        if let Some(task) = self.tasks.get_mut(task_id as usize) {
            task.state = TaskState::Running;
            self.active_task = Some(task_id);

            let start = now;
            (task.function)(task.arg);
            let elapsed = now.saturating_sub(start);

            task.total_runtime += elapsed;
            task.run_count += 1;

            // Handle periodic tasks
            if task.period > 0 {
                task.next_run = now + task.period;
                task.state = TaskState::Ready;
            } else {
                task.state = TaskState::Idle;
            }

            self.active_task = None;
        }
    }

    // ========================================================================
    // Field Operations
    // ========================================================================

    /// Update module's coordination field
    pub fn update_field(&mut self, load: Fixed, thermal: Fixed, power: Fixed) {
        self.my_field.set(FieldComponent::Load, load);
        self.my_field.set(FieldComponent::Thermal, thermal);
        self.my_field.set(FieldComponent::Power, power);

        if let Some(cb) = self.callbacks.on_field_change {
            cb(self);
        }
    }

    /// Get current gradient for a component
    pub fn get_gradient(&self, component: FieldComponent) -> Fixed {
        self.gradients[component as usize]
    }

    /// Get all gradients
    pub fn gradients(&self) -> &[Fixed; FIELD_COUNT] {
        &self.gradients
    }

    // ========================================================================
    // Deadline / Slack Operations (MAPF-HET)
    // ========================================================================

    /// Compute slack for all tasks with deadlines
    ///
    /// Updates the slack field for each task that has a deadline:
    /// - slack = deadline - (now + duration_estimate)
    /// - critical = slack < SLACK_THRESHOLD_US
    ///
    /// Also updates the module's FieldComponent::Slack with the minimum
    /// slack across all deadline-constrained tasks.
    pub fn compute_slack(&mut self, now: TimeUs) {
        let mut min_slack_us: i64 = SLACK_NORMALIZE_US as i64;
        let mut has_any_deadline = false;

        for task in self.tasks.iter_mut() {
            if let Some(ref mut deadline) = task.deadline {
                has_any_deadline = true;
                deadline.compute_slack(now);

                // Track minimum slack
                let slack_us = (deadline.deadline as i64)
                    .saturating_sub(now as i64)
                    .saturating_sub(deadline.duration_est as i64);

                if slack_us < min_slack_us {
                    min_slack_us = slack_us;
                }
            }
        }

        // Update slack field component
        if has_any_deadline {
            let slack_normalized = (min_slack_us as f32 / SLACK_NORMALIZE_US as f32)
                .clamp(0.0, 1.0);
            self.my_field.set(FieldComponent::Slack, Fixed::from_num(slack_normalized));
        } else {
            self.my_field.set(FieldComponent::Slack, Fixed::ONE);
        }
    }

    /// Set deadline for a task
    pub fn set_task_deadline(
        &mut self,
        task_id: TaskId,
        deadline: TimeUs,
        duration_est: TimeUs,
    ) -> Result<()> {
        if let Some(task) = self.tasks.get_mut(task_id as usize) {
            task.deadline = Some(Deadline::new(deadline, duration_est));
            Ok(())
        } else {
            Err(Error::NotFound)
        }
    }

    /// Clear deadline for a task
    pub fn clear_task_deadline(&mut self, task_id: TaskId) -> Result<()> {
        if let Some(task) = self.tasks.get_mut(task_id as usize) {
            task.deadline = None;
            Ok(())
        } else {
            Err(Error::NotFound)
        }
    }

    // ========================================================================
    // Capability Operations (MAPF-HET)
    // ========================================================================

    /// Set module capabilities
    pub fn set_capabilities(&mut self, caps: Capability) {
        self.capabilities = caps;
    }

    /// Get module capabilities
    pub fn capabilities(&self) -> Capability {
        self.capabilities
    }

    /// Set required capabilities for a task
    pub fn set_task_capabilities(&mut self, task_id: TaskId, caps: Capability) -> Result<()> {
        if let Some(task) = self.tasks.get_mut(task_id as usize) {
            task.required_caps = caps;
            Ok(())
        } else {
            Err(Error::NotFound)
        }
    }

    // ========================================================================
    // Consensus Shortcuts
    // ========================================================================

    /// Propose mode change
    pub fn propose_mode(&mut self, new_mode: u32, now: TimeUs) -> Result<BallotId> {
        self.consensus_rounds += 1;
        self.consensus.propose(
            ProposalType::ModeChange,
            new_mode,
            threshold::SUPERMAJORITY,
            now,
        )
    }

    /// Propose power limit
    pub fn propose_power_limit(&mut self, power_mw: u32, now: TimeUs) -> Result<BallotId> {
        self.consensus_rounds += 1;
        self.consensus.propose(
            ProposalType::PowerLimit,
            power_mw,
            threshold::SIMPLE_MAJORITY,
            now,
        )
    }

    /// Get consensus result
    pub fn get_consensus_result(&self, ballot_id: BallotId) -> VoteResult {
        self.consensus.get_result(ballot_id)
    }

    // ========================================================================
    // Topology Access
    // ========================================================================

    /// Get neighbor count
    pub fn neighbor_count(&self) -> usize {
        self.topology.neighbor_count()
    }

    /// Get neighbors
    pub fn neighbors(&self) -> &[Neighbor] {
        self.topology.neighbors()
    }

    // ========================================================================
    // Status
    // ========================================================================

    /// Get module status
    pub fn status(&self) -> ModuleStatus {
        ModuleStatus {
            id: self.id,
            state: self.state,
            neighbor_count: self.topology.neighbor_count() as u32,
            load_gradient: self.gradients[FieldComponent::Load as usize],
            thermal_gradient: self.gradients[FieldComponent::Thermal as usize],
            active_ballots: 0, // TODO
            ticks_total: self.ticks_total,
        }
    }

    /// Set callbacks
    pub fn set_callbacks(&mut self, callbacks: ModuleCallbacks) {
        self.callbacks = callbacks;
    }

    // ========================================================================
    // Private Helpers
    // ========================================================================

    fn set_state(&mut self, new_state: ModuleState) {
        let old_state = self.state;
        self.state = new_state;

        if let Some(cb) = self.callbacks.on_state_change {
            cb(self, old_state);
        }
    }

    fn update_state_from_topology(&mut self) {
        let count = self.topology.neighbor_count();

        match self.state {
            ModuleState::Discovering if count >= K_NEIGHBORS / 2 => {
                self.set_state(ModuleState::Active);
            }
            ModuleState::Active if count == 0 => {
                self.set_state(ModuleState::Isolated);
            }
            ModuleState::Active if count < K_NEIGHBORS / 2 => {
                self.set_state(ModuleState::Degraded);
            }
            ModuleState::Degraded if count >= K_NEIGHBORS / 2 => {
                self.set_state(ModuleState::Active);
            }
            ModuleState::Isolated if count > 0 => {
                self.set_state(ModuleState::Reforming);
            }
            ModuleState::Reforming if count >= K_NEIGHBORS / 2 => {
                self.set_state(ModuleState::Active);
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dummy_task(_: *mut ()) {}

    #[test]
    fn test_module_creation() {
        let module = Module::new(1, "test", Position::new(0, 0, 0));
        assert_eq!(module.id(), 1);
        assert_eq!(module.state(), ModuleState::Init);
    }

    #[test]
    fn test_module_tasks() {
        let mut module = Module::new(1, "test", Position::new(0, 0, 0));

        let task_id = module
            .add_task("task1", dummy_task, core::ptr::null_mut(), 0, 1000)
            .unwrap();

        assert_eq!(task_id, 0);
    }

    #[test]
    fn test_module_start() {
        let mut module = Module::new(1, "test", Position::new(0, 0, 0));
        module.start().unwrap();
        assert_eq!(module.state(), ModuleState::Discovering);
    }
}

//! EK-KOR v2 - Hardware Abstraction Layer
//!
//! HAL provides platform-independent interface for:
//! - Time measurement
//! - Message transmission
//! - Critical sections
//! - Platform-specific initialization
//!
//! # Implementing a HAL
//!
//! ```ignore
//! struct MyHal;
//!
//! impl Hal for MyHal {
//!     fn time_us(&self) -> TimeUs {
//!         // Read hardware timer
//!     }
//!
//!     fn send(&self, dest: ModuleId, msg_type: MsgType, data: &[u8]) -> Result<()> {
//!         // Send via CAN-FD
//!     }
//!
//!     // ... implement other methods
//! }
//! ```

use crate::types::*;

// ============================================================================
// Message Types
// ============================================================================

/// Message types for inter-module communication
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum MsgType {
    /// Liveness check
    Heartbeat = 0x01,
    /// Module discovery
    Discovery = 0x02,
    /// Coordination field update
    Field = 0x03,
    /// Consensus proposal
    Proposal = 0x04,
    /// Consensus vote
    Vote = 0x05,
    /// Proposal inhibition
    Inhibit = 0x06,
    /// Mesh reformation
    Reform = 0x07,
    /// Graceful shutdown
    Shutdown = 0x08,
    /// Application messages start here
    UserBase = 0x80,
}

// ============================================================================
// HAL Trait
// ============================================================================

/// Hardware Abstraction Layer trait
///
/// Implement this for your target platform (STM32, TriCore, etc.)
pub trait Hal {
    /// Get current time in microseconds
    ///
    /// Must be monotonically increasing.
    fn time_us(&self) -> TimeUs;

    /// Get current time in milliseconds
    fn time_ms(&self) -> u32 {
        (self.time_us() / 1000) as u32
    }

    /// Busy-wait delay
    fn delay_us(&self, us: u32);

    /// Send message to specific module
    fn send(&self, dest: ModuleId, msg_type: MsgType, data: &[u8]) -> Result<()>;

    /// Broadcast message to all modules
    fn broadcast(&self, msg_type: MsgType, data: &[u8]) -> Result<()>;

    /// Check for received message (non-blocking)
    ///
    /// Returns None if no message available.
    fn recv(&self, buffer: &mut [u8]) -> Option<ReceivedMessage>;

    /// Enter critical section (disable interrupts)
    ///
    /// Returns state to restore.
    fn critical_enter(&self) -> u32;

    /// Exit critical section
    fn critical_exit(&self, state: u32);

    /// Memory barrier
    fn memory_barrier(&self);

    /// Get this module's hardware ID
    fn get_module_id(&self) -> ModuleId;

    /// Get platform name
    fn platform_name(&self) -> &'static str;

    /// Debug print (optional)
    fn debug_print(&self, _msg: &str) {}
}

/// Received message info
#[derive(Debug, Clone)]
pub struct ReceivedMessage {
    pub sender_id: ModuleId,
    pub msg_type: MsgType,
    pub len: usize,
}

// ============================================================================
// Critical Section Guard
// ============================================================================

/// RAII guard for critical sections
pub struct CriticalSection<'a, H: Hal> {
    hal: &'a H,
    state: u32,
}

impl<'a, H: Hal> CriticalSection<'a, H> {
    /// Enter critical section
    pub fn new(hal: &'a H) -> Self {
        let state = hal.critical_enter();
        Self { hal, state }
    }
}

impl<'a, H: Hal> Drop for CriticalSection<'a, H> {
    fn drop(&mut self) {
        self.hal.critical_exit(self.state);
    }
}

// ============================================================================
// No-op HAL (for testing)
// ============================================================================

/// No-op HAL for testing and simulation
#[cfg(any(test, feature = "std"))]
pub struct NoopHal {
    time: core::sync::atomic::AtomicU64,
}

#[cfg(any(test, feature = "std"))]
impl NoopHal {
    /// Create new no-op HAL
    pub fn new() -> Self {
        Self {
            time: core::sync::atomic::AtomicU64::new(0),
        }
    }

    /// Advance time (for testing)
    pub fn advance_time(&self, us: u64) {
        self.time.fetch_add(us, core::sync::atomic::Ordering::SeqCst);
    }
}

#[cfg(any(test, feature = "std"))]
impl Default for NoopHal {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(any(test, feature = "std"))]
impl Hal for NoopHal {
    fn time_us(&self) -> TimeUs {
        self.time.load(core::sync::atomic::Ordering::SeqCst)
    }

    fn delay_us(&self, us: u32) {
        self.advance_time(us as u64);
    }

    fn send(&self, _dest: ModuleId, _msg_type: MsgType, _data: &[u8]) -> Result<()> {
        Ok(())
    }

    fn broadcast(&self, _msg_type: MsgType, _data: &[u8]) -> Result<()> {
        Ok(())
    }

    fn recv(&self, _buffer: &mut [u8]) -> Option<ReceivedMessage> {
        None
    }

    fn critical_enter(&self) -> u32 {
        0
    }

    fn critical_exit(&self, _state: u32) {}

    fn memory_barrier(&self) {}

    fn get_module_id(&self) -> ModuleId {
        1
    }

    fn platform_name(&self) -> &'static str {
        "noop"
    }

    fn debug_print(&self, msg: &str) {
        #[cfg(feature = "std")]
        println!("{}", msg);
        #[cfg(not(feature = "std"))]
        let _ = msg;
    }
}

// ============================================================================
// POSIX HAL (for std environments)
// ============================================================================

#[cfg(feature = "std")]
pub mod posix {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::time::Instant;

    /// POSIX-based HAL for desktop testing
    pub struct PosixHal {
        start: Instant,
        module_id: ModuleId,
    }

    impl PosixHal {
        /// Create new POSIX HAL
        pub fn new(module_id: ModuleId) -> Self {
            Self {
                start: Instant::now(),
                module_id,
            }
        }
    }

    impl Hal for PosixHal {
        fn time_us(&self) -> TimeUs {
            self.start.elapsed().as_micros() as TimeUs
        }

        fn delay_us(&self, us: u32) {
            std::thread::sleep(std::time::Duration::from_micros(us as u64));
        }

        fn send(&self, dest: ModuleId, msg_type: MsgType, data: &[u8]) -> Result<()> {
            // In real impl, would use UDP/shared memory
            println!(
                "[HAL] send to {} type {:?} len {}",
                dest,
                msg_type,
                data.len()
            );
            Ok(())
        }

        fn broadcast(&self, msg_type: MsgType, data: &[u8]) -> Result<()> {
            self.send(BROADCAST_ID, msg_type, data)
        }

        fn recv(&self, _buffer: &mut [u8]) -> Option<ReceivedMessage> {
            // Would poll socket/queue
            None
        }

        fn critical_enter(&self) -> u32 {
            // No-op on POSIX
            0
        }

        fn critical_exit(&self, _state: u32) {}

        fn memory_barrier(&self) {
            std::sync::atomic::fence(Ordering::SeqCst);
        }

        fn get_module_id(&self) -> ModuleId {
            self.module_id
        }

        fn platform_name(&self) -> &'static str {
            "posix"
        }

        fn debug_print(&self, msg: &str) {
            println!("{}", msg);
        }
    }
}

// ============================================================================
// STM32G474 HAL Skeleton
// ============================================================================

/// STM32G474 HAL (implement with actual HAL crate)
#[cfg(feature = "embedded")]
pub mod stm32g474 {
    use super::*;

    /// STM32G474 HAL implementation
    ///
    /// This is a skeleton - implement using stm32g4xx-hal crate.
    pub struct Stm32G474Hal {
        // Add peripherals here
        module_id: ModuleId,
    }

    impl Stm32G474Hal {
        /// Create new STM32G474 HAL
        ///
        /// # Safety
        /// Must only be called once, with valid peripheral access.
        pub unsafe fn new(module_id: ModuleId) -> Self {
            Self { module_id }
        }
    }

    impl Hal for Stm32G474Hal {
        fn time_us(&self) -> TimeUs {
            // Read TIM2 or DWT cycle counter
            0 // TODO: implement
        }

        fn delay_us(&self, _us: u32) {
            // Use cortex_m::asm::delay or timer
        }

        fn send(&self, _dest: ModuleId, _msg_type: MsgType, _data: &[u8]) -> Result<()> {
            // Use FDCAN peripheral
            Ok(())
        }

        fn broadcast(&self, _msg_type: MsgType, _data: &[u8]) -> Result<()> {
            // FDCAN broadcast
            Ok(())
        }

        fn recv(&self, _buffer: &mut [u8]) -> Option<ReceivedMessage> {
            // Poll FDCAN RX FIFO
            None
        }

        fn critical_enter(&self) -> u32 {
            // cortex_m::interrupt::disable() and save PRIMASK
            0
        }

        fn critical_exit(&self, _state: u32) {
            // Restore PRIMASK
        }

        fn memory_barrier(&self) {
            // cortex_m::asm::dmb()
        }

        fn get_module_id(&self) -> ModuleId {
            self.module_id
        }

        fn platform_name(&self) -> &'static str {
            "stm32g474"
        }
    }
}

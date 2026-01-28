# EK-KOR v2 Desktop GUI Plan

## Overview

Cross-platform desktop GUI for EK-KOR v2 using Electron + React.

**Requirements:**
- Use cases: Development/debugging, operator monitoring, demo/presentation
- Tech stack: Electron + React (reuses existing web demo patterns)
- Connection modes: In-process simulation, shared memory IPC, CAN bus adapter
- Scale: Up to 256 modules

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Electron Main Process                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ EKK Native  │  │ IPC Bridge  │  │ CAN Adapter Bridge  │  │
│  │ Module      │  │ (N-API)     │  │ (socketcan/slcan)   │  │
│  │ (NAPI)      │  │             │  │                     │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
│         │                │                     │             │
│         └────────────────┴─────────────────────┘             │
│                          │                                   │
│                    ┌─────┴─────┐                            │
│                    │ Data API  │                            │
│                    │ (preload) │                            │
│                    └─────┬─────┘                            │
├──────────────────────────┼──────────────────────────────────┤
│                          │      Electron Renderer Process   │
│                    ┌─────┴─────┐                            │
│                    │  React    │                            │
│                    │   App     │                            │
│                    └───────────┘                            │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │ Topology │ │  Field   │ │Consensus │ │Heartbeat │       │
│  │   View   │ │ Heatmap  │ │Dashboard │ │ Monitor  │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
└─────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
ek-kor2/
├── gui/                          # Desktop GUI
│   ├── package.json              # Electron + React deps
│   ├── electron/
│   │   ├── main.js               # Electron main process
│   │   ├── preload.js            # IPC bridge to renderer
│   │   └── native/
│   │       ├── binding.gyp       # N-API build config
│   │       ├── ekk_binding.c     # C → JS bindings
│   │       └── ekk_binding.h
│   ├── src/
│   │   ├── App.jsx               # Main React app
│   │   ├── main.jsx              # React entry point
│   │   ├── index.html
│   │   ├── stores/
│   │   │   └── ekkStore.js       # Zustand state management
│   │   ├── components/
│   │   │   ├── layout/
│   │   │   │   ├── Sidebar.jsx
│   │   │   │   ├── Toolbar.jsx
│   │   │   │   └── StatusBar.jsx
│   │   │   ├── views/
│   │   │   │   ├── TopologyView.jsx      # Force-directed graph
│   │   │   │   ├── FieldHeatmap.jsx      # Potential field heatmap
│   │   │   │   ├── ConsensusPanel.jsx    # Voting dashboard
│   │   │   │   ├── HeartbeatMonitor.jsx  # Neighbor health
│   │   │   │   ├── ModuleDetails.jsx     # Single module inspector
│   │   │   │   └── GradientView.jsx      # Gradient bars
│   │   │   └── common/
│   │   │       ├── StatusBadge.jsx       # Reuse from web demo
│   │   │       ├── MetricCard.jsx
│   │   │       └── HealthIndicator.jsx
│   │   └── hooks/
│   │       ├── useEkkData.js             # Real-time data hook
│   │       └── useSimulation.js          # Simulation control
│   ├── vite.config.js
│   └── tailwind.config.js
└── c/
    └── src/
        └── bindings/
            └── ekk_napi.c        # N-API wrapper for Electron
```

---

## Phase 1: Project Setup & Native Bindings

### 1.1 Initialize Electron + React Project

```bash
cd ek-kor2
mkdir gui && cd gui
npm init -y
npm install electron vite @vitejs/plugin-react react react-dom
npm install -D electron-builder
```

### 1.2 Create N-API Native Module

**File:** `gui/electron/native/ekk_binding.c`

Expose these functions to JavaScript:
```c
// Simulation control
napi_value ekk_gui_init(napi_env env, napi_callback_info info);
napi_value ekk_gui_tick(napi_env env, napi_callback_info info);
napi_value ekk_gui_shutdown(napi_env env, napi_callback_info info);

// Module data getters (return JSON-serializable objects)
napi_value ekk_gui_get_modules(napi_env env, napi_callback_info info);
napi_value ekk_gui_get_topology(napi_env env, napi_callback_info info);
napi_value ekk_gui_get_fields(napi_env env, napi_callback_info info);
napi_value ekk_gui_get_consensus(napi_env env, napi_callback_info info);
napi_value ekk_gui_get_heartbeats(napi_env env, napi_callback_info info);

// Configuration
napi_value ekk_gui_set_module_count(napi_env env, napi_callback_info info);
napi_value ekk_gui_set_tick_rate(napi_env env, napi_callback_info info);
```

### 1.3 Electron Main Process

**File:** `gui/electron/main.js`

```javascript
const { app, BrowserWindow, ipcMain } = require('electron');
const ekk = require('./native/ekk_binding.node');

let mainWindow;
let tickInterval = null;

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
    }
  });
  mainWindow.loadFile('dist/index.html');
}

// IPC handlers
ipcMain.handle('ekk:init', (_, moduleCount) => ekk.init(moduleCount));
ipcMain.handle('ekk:getModules', () => ekk.getModules());
ipcMain.handle('ekk:getTopology', () => ekk.getTopology());
ipcMain.handle('ekk:getFields', () => ekk.getFields());
ipcMain.handle('ekk:startSimulation', (_, tickRateMs) => {
  tickInterval = setInterval(() => {
    ekk.tick();
    mainWindow.webContents.send('ekk:tick', ekk.getModules());
  }, tickRateMs);
});
ipcMain.handle('ekk:stopSimulation', () => clearInterval(tickInterval));
```

---

## Phase 2: Core React Components

### 2.1 State Management (Zustand)

**File:** `gui/src/stores/ekkStore.js`

```javascript
import { create } from 'zustand';

export const useEkkStore = create((set) => ({
  // Connection state
  connectionMode: 'simulation', // 'simulation' | 'ipc' | 'can'
  connected: false,

  // Module data
  modules: [],           // Array of module status objects
  topology: {},          // { moduleId: [neighborIds] }
  fields: {},            // { moduleId: { components: [...], timestamp } }
  consensus: [],         // Active ballots
  heartbeats: {},        // { moduleId: { neighborId: health } }

  // Selected module for details panel
  selectedModuleId: null,

  // Simulation control
  isRunning: false,
  tickRate: 50,          // ms
  tickCount: 0,

  // Actions
  setModules: (modules) => set({ modules }),
  setTopology: (topology) => set({ topology }),
  setFields: (fields) => set({ fields }),
  selectModule: (id) => set({ selectedModuleId: id }),
  incrementTick: () => set((s) => ({ tickCount: s.tickCount + 1 })),
}));
```

### 2.2 Topology View (Force-Directed Graph)

**File:** `gui/src/components/views/TopologyView.jsx`

Using Cytoscape.js or react-force-graph:
- Nodes: 256 modules, colored by state
- Edges: k=7 neighbor connections per module
- Health: Edge color/style based on neighbor health
- Click: Select module for details panel
- Zoom: Mouse wheel, pinch gesture
- Pan: Drag background

### 2.3 Field Heatmap

**File:** `gui/src/components/views/FieldHeatmap.jsx`

- Grid layout based on module positions (x, y, z)
- Color gradient per field component (LOAD, THERMAL, POWER)
- Toggle between components
- Hover: Show exact values with decay

### 2.4 Consensus Dashboard

**File:** `gui/src/components/views/ConsensusPanel.jsx`

- Cards for active ballots (up to 4)
- Progress bars: votes received / expected
- Vote breakdown: YES / NO / ABSTAIN / INHIBIT
- Status badge: PENDING → APPROVED/REJECTED/TIMEOUT

### 2.5 Heartbeat Monitor

**File:** `gui/src/components/views/HeartbeatMonitor.jsx`

- Table or grid of neighbors per module
- Health indicators: ALIVE (green), SUSPECT (amber), DEAD (red)
- Missed heartbeat count with warning at >2
- Last seen timestamp

### 2.6 Module Details Panel

**File:** `gui/src/components/views/ModuleDetails.jsx`

Detailed view when module is selected:
- ID, name, state
- All 5 field components with bars
- All 5 gradients with +/- bars
- Neighbor list with health
- Active ballots involving this module
- Statistics: ticks, field updates, topology changes

---

## Phase 3: Connection Modes

### 3.1 In-Process Simulation Mode

- N-API bindings call EK-KOR C functions directly
- Create N virtual modules in memory
- Single-threaded tick loop in main process
- Fastest, no IPC overhead

### 3.2 Shared Memory IPC Mode

- Separate process runs EK-KOR simulation
- Shared memory region contains field data
- GUI polls via memory-mapped file
- More realistic, tests IPC patterns

### 3.3 CAN Bus Adapter Mode

- USB-CAN adapter (PCAN, Kvaser, SocketCAN)
- Parse EK-KOR wire protocol messages
- Real hardware connection
- Requires adapter library (node-can or similar)

### 3.4 Connection Manager UI

**File:** `gui/src/components/ConnectionManager.jsx`

Modal or settings panel:
- Radio buttons: Simulation / IPC / CAN
- Simulation: Module count slider (1-256)
- IPC: Shared memory path input
- CAN: Adapter selection, bitrate

---

## Phase 4: Visualization Features

### 4.1 Key Visualizations

| View | Data Source | Purpose |
|------|-------------|---------|
| Topology Graph | `ekk_topology_get_neighbors()` | Network structure |
| Field Heatmap | `ekk_field_sample()` | Load distribution |
| Gradient Bars | `ekk_field_gradient_all()` | Scheduling decisions |
| Consensus Cards | `ekk_consensus_get_result()` | Voting status |
| Heartbeat Table | `ekk_heartbeat_get_health()` | Neighbor liveness |
| Module Details | `ekk_module_get_status()` | Deep inspection |

### 4.2 Animations

- Framer Motion for component transitions
- Pulse animation for active heartbeats
- Smooth interpolation for field values
- Graph layout animation on topology changes

### 4.3 Theme

- Dark theme matching existing web demo
- Accent colors: cyan, purple
- Status colors: emerald (healthy), amber (warning), red (critical)
- Glass-morphism backgrounds

### 4.4 Performance Optimization

- Canvas rendering for 256-node topology
- WebGL heatmap for large field visualization
- Virtual scrolling for module lists
- Throttled updates (max 20 FPS for visuals)

---

## Phase 5: Build & Distribution

### 5.1 Electron Builder Config

**File:** `gui/package.json` (build section)

```json
{
  "build": {
    "appId": "com.elektrokombinacija.ekk-gui",
    "productName": "EK-KOR Monitor",
    "win": {
      "target": "nsis",
      "icon": "assets/icon.ico"
    },
    "linux": {
      "target": ["AppImage", "deb"],
      "icon": "assets/icon.png"
    },
    "mac": {
      "target": "dmg",
      "icon": "assets/icon.icns"
    }
  }
}
```

### 5.2 Native Module Prebuild

Use `prebuild` to create binaries for:
- Windows x64
- Linux x64
- macOS x64 / arm64

---

## Wireframe Layout

```
╔════════════════════════════════════════════════════════════════╗
║  EK-KOR v2 Desktop Monitor                    [_][□][X]        ║
╠════════════════════════════════════════════════════════════════╣
║ ┌─ Toolbar ──────────────────────────────────────────────────┐ ║
║ │ [▶ Start] [⏸ Pause] [⏹ Stop]  Modules: 42/256  Freq: 10ms │ ║
║ └────────────────────────────────────────────────────────────┘ ║
║                                                                ║
║ ┌─ Left Panel (Sidebar) ───────────┬─ Main Content Area ────┐ ║
║ │ Dashboard                        │                        │ ║
║ │  ├─ System Health                │  [Topology Graph]      │ ║
║ │  ├─ Module List                  │  (Force-directed)      │ ║
║ │  │  1: "charger-01" ACTIVE [7]   │                        │ ║
║ │  │  2: "charger-02" ACTIVE [7]   │  ◯ ─── ◯ ◯             │ ║
║ │  │  ... (scrollable)             │   \   /  \             │ ║
║ │  ├─ Heartbeat Monitor            │  ◯ ─ ◯ ─ ◯             │ ║
║ │  ├─ Consensus Ballots            │   / \   /              │ ║
║ │  ├─ Field Heatmap                │  ◯   ◯ ◯               │ ║
║ │  └─ Settings                     │                        │ ║
║ │                                  │  [Details Panel]       │ ║
║ │                                  │  Clicked Module: 42    │ ║
║ │                                  │  Name: charger-42      │ ║
║ │                                  │  State: ACTIVE         │ ║
║ │                                  │  Neighbors: 7/7        │ ║
║ │                                  │  Load Gradient: +0.34  │ ║
║ │                                  │  Active Ballots: 1     │ ║
║ └──────────────────────────────────┴─────────────────────────┘ ║
║                                                                ║
║ ┌─ Bottom Status Bar ────────────────────────────────────────┐ ║
║ │ Ticks: 12,847 | Field Updates: 3,402 | Topology Changes: 12│ ║
║ │ Consensus Rounds: 156 | Last Error: none                   │ ║
║ └────────────────────────────────────────────────────────────┘ ║
╚════════════════════════════════════════════════════════════════╝
```

---

## Key Files to Create

| File | Purpose |
|------|---------|
| `gui/package.json` | Project dependencies |
| `gui/electron/main.js` | Electron main process |
| `gui/electron/preload.js` | IPC bridge |
| `gui/electron/native/ekk_binding.c` | N-API C bindings |
| `gui/electron/native/binding.gyp` | Native build config |
| `gui/src/App.jsx` | Main React app layout |
| `gui/src/stores/ekkStore.js` | Zustand state store |
| `gui/src/components/views/TopologyView.jsx` | Network graph |
| `gui/src/components/views/FieldHeatmap.jsx` | Field visualization |
| `gui/src/components/views/ConsensusPanel.jsx` | Voting dashboard |
| `gui/src/components/views/HeartbeatMonitor.jsx` | Health monitor |
| `gui/src/components/views/ModuleDetails.jsx` | Module inspector |

---

## Dependencies

```json
{
  "dependencies": {
    "electron": "^28.0.0",
    "react": "^18.2.0",
    "react-dom": "^18.2.0",
    "zustand": "^4.4.0",
    "cytoscape": "^3.28.0",
    "react-cytoscapejs": "^2.0.0",
    "framer-motion": "^10.0.0",
    "lucide-react": "^0.300.0"
  },
  "devDependencies": {
    "vite": "^5.0.0",
    "@vitejs/plugin-react": "^4.0.0",
    "electron-builder": "^24.0.0",
    "node-gyp": "^10.0.0",
    "tailwindcss": "^3.4.0"
  }
}
```

---

## Verification

1. **Build native module:**
   ```bash
   cd gui/electron/native && node-gyp rebuild
   ```

2. **Start development:**
   ```bash
   cd gui && npm run dev
   ```

3. **Test simulation mode:**
   - Start simulation with 49 modules
   - Verify topology graph shows connections
   - Verify field heatmap updates

4. **Build distribution:**
   ```bash
   npm run build && npm run dist
   ```

---

## Data Types to Visualize

From EK-KOR v2 C headers:

| Type | Key Fields | Visualization |
|------|------------|---------------|
| `ekk_module_t` | id, state, name | Module list, badges |
| `ekk_topology_t` | neighbors[], neighbor_count | Graph nodes/edges |
| `ekk_neighbor_t` | id, health, last_seen, logical_distance | Edge styling |
| `ekk_field_t` | components[5], timestamp | Heatmap cells |
| `ekk_ballot_t` | id, type, votes[], result | Ballot cards |
| `ekk_heartbeat_neighbor_t` | id, health, missed_count | Health table |

---

## Related Documents

- Main implementation plan: `ek-kor2/` (see previous plan file)
- Web demo patterns: `web/src/components/`
- C API headers: `ek-kor2/c/include/ekk/`

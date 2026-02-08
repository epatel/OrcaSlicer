# OrcaSlicer Architecture Overview

> **Audience:** AI agents and developers working on the codebase.
> **Scope:** High-level architecture, module inventory, and data-flow diagrams.
> Each module listed here is a candidate for its own detailed doc in the future.

---

## Table of Contents

1. [System Layers](#1-system-layers)
2. [Source Tree Map](#2-source-tree-map)
3. [Core Engine — libslic3r](#3-core-engine--libslic3r)
4. [Slicing Pipeline](#4-slicing-pipeline)
5. [GUI Layer](#5-gui-layer)
6. [Background Processing & Threading](#6-background-processing--threading)
7. [Device & Network Communication](#7-device--network-communication)
8. [Configuration System](#8-configuration-system)
9. [File Format I/O](#9-file-format-io)
10. [Module Index](#10-module-index)

---

## 1. System Layers

OrcaSlicer is organized into four main layers. The core engine is platform-independent; the GUI, device, and utility layers depend on wxWidgets and platform APIs.

```mermaid
block-beta
  columns 1

  block:app["Application Layer"]
    columns 4
    A1["OrcaSlicer.cpp\n(entry point)"]
    A2["GUI_App\n(wxApp)"]
    A3["MainFrame\n(top window)"]
    A4["Plater\n(3D workspace)"]
  end

  block:gui["GUI & Interaction Layer"]
    columns 5
    G1["GLCanvas3D\n(OpenGL)"]
    G2["Gizmos"]
    G3["Sidebar\n(presets)"]
    G4["GCodeViewer"]
    G5["Widgets"]
  end

  block:services["Services Layer"]
    columns 4
    S1["Background\nSlicingProcess"]
    S2["DeviceManager"]
    S3["NetworkAgent"]
    S4["PresetBundle"]
  end

  block:engine["Core Engine — libslic3r"]
    columns 6
    E1["Print /\nPrintObject"]
    E2["Model /\nTriangleMesh"]
    E3["Layer /\nLayerRegion"]
    E4["Fill\nPatterns"]
    E5["Support\nGeneration"]
    E6["GCode\nExport"]
  end

  app --> gui
  gui --> services
  services --> engine
```

---

## 2. Source Tree Map

```mermaid
graph LR
  root["OrcaSlicer/"]

  root --> src["src/"]
  root --> deps["deps/"]
  root --> resources["resources/"]
  root --> tests["tests/"]
  root --> doc["doc/"]
  root --> scripts["scripts/"]

  src --> entry["OrcaSlicer.cpp — entry point"]
  src --> libslic3r["libslic3r/ — core engine"]
  src --> slic3r["slic3r/ — app framework"]

  libslic3r --> ls_gcode["GCode/ — G-code gen & post-processing"]
  libslic3r --> ls_fill["Fill/ — infill patterns"]
  libslic3r --> ls_support["Support/ — support generation"]
  libslic3r --> ls_arachne["Arachne/ — variable-width walls"]
  libslic3r --> ls_format["Format/ — file I/O (3MF, STL, …)"]
  libslic3r --> ls_geometry["Geometry/ — Voronoi, convex hull, …"]
  libslic3r --> ls_sla["SLA/ — stereolithography"]
  libslic3r --> ls_algo["Algorithm/ — region expansion, line split"]
  libslic3r --> ls_csg["CSGMesh/ — boolean mesh ops"]
  libslic3r --> ls_exec["Execution/ — TBB parallelization"]
  libslic3r --> ls_opt["Optimize/ — NLopt, brute-force"]
  libslic3r --> ls_shape["Shape/ — text shapes"]

  slic3r --> s_gui["GUI/ — wxWidgets UI"]
  slic3r --> s_utils["Utils/ — networking, utilities"]
  slic3r --> s_config["Config/ — config management"]

  s_gui --> sg_gizmos["Gizmos/ — 3D editing tools (53 files)"]
  s_gui --> sg_widgets["Widgets/ — custom controls (79 files)"]
  s_gui --> sg_jobs["Jobs/ — background job runners (37 files)"]
  s_gui --> sg_devcore["DeviceCore/ — device comms (47 files)"]
  s_gui --> sg_devtab["DeviceTab/ — device UI"]
  s_gui --> sg_printer["Printer/ — printer UI"]

  tests --> t_libslic3r["libslic3r/ — core tests (21 files)"]
  tests --> t_fff["fff_print/ — FDM tests (12 files)"]
  tests --> t_sla["sla_print/ — SLA tests (4 files)"]
  tests --> t_nest["libnest2d/ — nesting tests"]
  tests --> t_utils["slic3rutils/ — utility tests"]
```

---

## 3. Core Engine — libslic3r

### 3.1 Class Hierarchy

The core engine is organized around three main hierarchies: **Model** (input geometry), **Print** (slicing orchestration), and **Layer** (per-layer output).

```mermaid
classDiagram
  direction TB

  class Model {
    +ModelObjectPtrs objects
    +ModelMaterialMap materials
  }

  class ModelObject {
    +ModelVolumePtrs volumes
    +ModelInstancePtrs instances
    +ModelConfigObject config
    +LayerHeightProfile layer_height_profile
  }

  class ModelVolume {
    +ModelVolumeType type
    +TriangleMesh mesh
    +string material_id
    +ModelConfigObject config
  }

  class ModelInstance {
    +Transform3d transform
  }

  Model "1" --> "*" ModelObject
  ModelObject "1" --> "*" ModelVolume
  ModelObject "1" --> "*" ModelInstance

  class Print {
    +PrintObjectPtrs objects
    +PrintRegionPtrs regions
    +ToolOrdering tool_ordering
    +WipeTowerData wipe_tower_data
    +apply()
    +process()
    +export_gcode()
  }

  class PrintObject {
    +LayerPtrs layers
    +SupportLayerPtrs support_layers
    +PrintInstances instances
    +slice()
    +make_perimeters()
    +infill()
    +generate_support_material()
  }

  class PrintRegion {
    +PrintRegionConfig config
    +int region_id
  }

  Print "1" --> "*" PrintObject
  Print "1" --> "*" PrintRegion
  PrintObject ..> ModelObject : references

  class Layer {
    +size_t id
    +coordf_t print_z
    +coordf_t height
    +ExPolygons lslices
    +LayerRegionPtrs regions
    +make_perimeters()
    +make_fills()
    +make_ironing()
  }

  class LayerRegion {
    +SurfaceCollection slices
    +SurfaceCollection fill_surfaces
    +ExtrusionEntityCollection perimeters
    +ExtrusionEntityCollection fills
  }

  class SupportLayer {
    +ExPolygons support_islands
    +ExtrusionEntityCollection support_fills
    +SupportInnerType support_type
  }

  PrintObject "1" --> "*" Layer
  PrintObject "1" --> "*" SupportLayer
  Layer "1" --> "*" LayerRegion
  SupportLayer --|> Layer
```

### 3.2 Model Volume Types

`ModelVolume::type` controls how a mesh participates in slicing:

| Type | Purpose |
|------|---------|
| `NORMAL` | Standard printable geometry |
| `NEGATIVE` | Subtracted from model (boolean difference) |
| `MODIFIER` | Overrides settings in its region |
| `SUPPORT_ENFORCER` | Forces support generation in region |
| `SUPPORT_BLOCKER` | Prevents support generation in region |

---

## 4. Slicing Pipeline

### 4.1 Pipeline Overview

Slicing is orchestrated by `Print::process()`, which drives per-object steps followed by global steps. Each step is tracked by an enum-based state machine that supports incremental re-processing when settings change.

```mermaid
flowchart TD
  subgraph per_object["Per-Object Steps (PrintObject)"]
    direction TB
    S1["posSlice\nSlice mesh → 2D layers"]
    S2["posPerimeters\nGenerate wall extrusions"]
    S3["posEstimateCurledExtrusions\nWarp/curl detection"]
    S4["posPrepareInfill\nSurface classification,\nbridge detection, shell discovery"]
    S5["posInfill\nGenerate infill patterns"]
    S6["posIroning\nTop surface smoothing"]
    S7["posSupportMaterial\nTree / traditional supports"]
    S8["posSimplifyPath\nPath optimization"]
    S9["posDetectOverhangsForLift\nZ-hop overhang detection"]

    S1 --> S2 --> S3 --> S4 --> S5 --> S6 --> S7 --> S8 --> S9
  end

  subgraph global["Global Steps (Print)"]
    direction TB
    G1["psWipeTower / psToolOrdering\nMulti-material sequencing"]
    G2["psSkirtBrim\nBed adhesion structures"]
    G3["psConflictCheck\nCollision detection"]
    G4["psGCodeExport\nFinal G-code generation"]

    G1 --> G2 --> G3 --> G4
  end

  per_object --> global
```

### 4.2 Data Transformation Flow

```mermaid
flowchart LR
  A["TriangleMesh\n(3D indexed_triangle_set)"]
  B["ExPolygons per layer\n(2D polygons + holes)"]
  C["Typed Surfaces\n(top / bottom / internal)"]
  D["Perimeter\nExtrusionPaths"]
  E["Infill\nExtrusionPaths"]
  F["G-code\nstring output"]

  A -- "slice_mesh_ex()" --> B
  B -- "detect_surfaces_type()" --> C
  C -- "make_perimeters()" --> D
  C -- "make_fills()" --> E
  D & E -- "GCode::do_export()" --> F
```

### 4.3 Mesh Slicing Modes

`TriangleMeshSlicer` supports multiple slicing modes via `SlicingMode`:

| Mode | Rule | Use case |
|------|------|----------|
| `Regular` | Non-zero fill | Standard models |
| `EvenOdd` | Even-odd fill | Models with self-intersections |
| `Positive` | CCW contours only | Clean outer shells |
| `PositiveLargestContour` | Single largest CCW | Vase / spiral mode |

### 4.4 Infill Patterns

The `Fill` base class is subclassed for each pattern. Key patterns:

| Pattern | Class | Description |
|---------|-------|-------------|
| Rectilinear | `FillRectilinear` | Parallel lines, alternating angle |
| Gyroid | `FillGyroid` | TPMS-based, isotropic strength |
| Honeycomb | `FillHoneycomb` | Hex grid pattern |
| Lightning | `FillLightning` | Sparse tree-based infill |
| Concentric | `FillConcentric` | Inward-offset contours |
| Adaptive Cubic | `FillAdaptive` | Density varies near surfaces |
| Cross Hatch | `FillCrossHatch` | Crossed line patterns |
| 3D Honeycomb | `Fill3DHoneycomb` | Layer-shifted hex grid |

### 4.5 Support Generation

Two support strategies are available, selected by `SupportMaterialStyle`:

| Strategy | Classes | Description |
|----------|---------|-------------|
| Traditional | `SupportMaterial` | Grid/rectilinear pillars |
| Tree | `TreeSupport`, `TreeSupportData` | Organic branching structure with `SupportNode` tree |

Tree supports build a node graph (`SupportNode` with parent/child pointers) that branches from contact points down to the build plate, using collision avoidance fields computed per-layer.

---

## 5. GUI Layer

### 5.1 Component Hierarchy

```mermaid
flowchart TD
  App["GUI_App\n(wxApp)"]
  MF["MainFrame\n(DPIFrame)"]
  TopBar["BBLTopbar"]
  Tabs["Notebook\n(tab panel)"]
  Plater["Plater\n(wxPanel)"]
  Monitor["MonitorPanel"]
  MultiMachine["MultiMachinePage"]
  Calib["CalibrationPanel"]
  Project["ProjectPanel"]

  App --> MF
  MF --> TopBar
  MF --> Tabs
  Tabs --> Plater
  Tabs --> Monitor
  Tabs --> MultiMachine
  Tabs --> Calib
  Tabs --> Project

  subgraph plater_children["Plater internals"]
    Canvas["GLCanvas3D\n(OpenGL)"]
    Sidebar["Sidebar\n(presets & params)"]
    BGSlice["BackgroundSlicingProcess"]
    Worker["Worker\n(UI job queue)"]
    Plates["PartPlateList"]
  end

  Plater --> Canvas
  Plater --> Sidebar
  Plater --> BGSlice
  Plater --> Worker
  Plater --> Plates

  subgraph canvas_children["GLCanvas3D internals"]
    Camera["Camera"]
    Gizmos["GLGizmosManager"]
    GCodeV["GCodeViewer"]
    Bed["Bed3D"]
    Raycaster["SceneRaycaster"]
  end

  Canvas --> Camera
  Canvas --> Gizmos
  Canvas --> GCodeV
  Canvas --> Bed
  Canvas --> Raycaster

  subgraph sidebar_children["Sidebar internals"]
    PresetCombos["PresetComboBoxes\n(printer, filament)"]
    ParamsPanel["ParamsPanel"]
    Searcher["OptionsSearcher"]
  end

  Sidebar --> PresetCombos
  Sidebar --> ParamsPanel
  Sidebar --> Searcher
```

### 5.2 Main Tabs

| Tab enum | Panel | Purpose |
|----------|-------|---------|
| `tpHome` | Home | Start page, recent projects |
| `tp3DEditor` | Plater | 3D model editing and slicing |
| `tpPreview` | Plater (preview mode) | G-code visualization |
| `tpMonitor` | MonitorPanel | Device monitoring and control |
| `tpMultiDevice` | MultiMachinePage | Multi-printer management |
| `tpProject` | ProjectPanel | Project management |
| `tpCalibration` | CalibrationPanel | Calibration workflows |

### 5.3 Gizmos (3D Editing Tools)

The `GLGizmosManager` hosts interactive gizmo tools in the 3D viewport (53 source files in `GUI/Gizmos/`). These handle operations like move, rotate, scale, cut, measure, emboss text, paint-on supports, seam placement, and multi-material painting.

### 5.4 Event Flow

The GUI uses wxWidgets custom events for loose coupling between components:

```mermaid
sequenceDiagram
  participant User
  participant GLCanvas3D
  participant Plater
  participant BGSlice as BackgroundSlicingProcess
  participant Print

  User->>GLCanvas3D: Modify object / change setting
  GLCanvas3D->>Plater: EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS
  Plater->>Plater: on_config_change()
  Plater->>BGSlice: apply(model, config)
  Plater->>BGSlice: start()
  BGSlice->>Print: process() [background thread]
  Print-->>BGSlice: step completion
  BGSlice-->>Plater: SlicingStatusEvent (progress)
  BGSlice-->>Plater: SlicingProcessCompletedEvent
  Plater->>Plater: update UI, enable export
  Plater-->>User: Notification: slicing complete
```

---

## 6. Background Processing & Threading

### 6.1 Threading Model

```mermaid
flowchart LR
  subgraph main["Main Thread (UI)"]
    direction TB
    M1["Event loop"]
    M2["GLCanvas3D rendering"]
    M3["User input handling"]
    M4["Config propagation"]
  end

  subgraph bg["Background Thread"]
    direction TB
    B1["Print::process()"]
    B2["GCode::do_export()"]
  end

  subgraph tbb["TBB Thread Pool"]
    direction TB
    T1["Parallel layer processing"]
    T2["Parallel infill generation"]
    T3["Parallel support computation"]
  end

  subgraph jobs["Worker Job Queue"]
    direction TB
    J1["Arrange job"]
    J2["Orient job"]
    J3["Export job"]
    J4["Upload job"]
  end

  main -- "start()" --> bg
  bg -- "SlicingStatusEvent" --> main
  bg -- "TBB tasks" --> tbb
  main -- "schedule()" --> jobs
  jobs -- "completion callback" --> main
```

### 6.2 BackgroundSlicingProcess States

```mermaid
stateDiagram-v2
  [*] --> INITIAL
  INITIAL --> IDLE : reset()
  IDLE --> STARTED : start()
  STARTED --> RUNNING : thread begins
  RUNNING --> FINISHED : processing complete
  RUNNING --> CANCELED : stop()
  FINISHED --> IDLE : reset()
  CANCELED --> IDLE : reset()
  IDLE --> EXIT : destructor
  EXIT --> EXITED
  EXITED --> [*]
```

### 6.3 Synchronization

- **Mutex + condition variable** guards the `BackgroundSlicingProcess` state machine
- **TBB** is used internally by `libslic3r` for data-parallel operations (layer processing, infill)
- **`execute_ui_task()`** marshals results from background threads back to the main thread via wxWidgets event queue
- **Worker** is a separate job queue for non-slicing async tasks (arranging, exporting, uploading)

---

## 7. Device & Network Communication

### 7.1 Architecture Overview

```mermaid
flowchart TD
  subgraph app_layer["Application Layer"]
    DM["DeviceManager"]
    MO["MachineObject\n(per-device state)"]
  end

  subgraph network["Network Abstraction"]
    NA["NetworkAgent"]
    CA["ICloudServiceAgent"]
    PA["IPrinterAgent"]
  end

  subgraph cloud_impl["Cloud Implementations"]
    OCA["OrcaCloudServiceAgent"]
    BCA["BBLCloudServiceAgent"]
  end

  subgraph printer_impl["Printer Implementations"]
    OPA["OrcaPrinterAgent"]
    BPA["BBLPrinterAgent"]
    QPA["QidiPrinterAgent"]
    SPA["SnapmakerPrinterAgent"]
    MPA["MoonrakerPrinterAgent"]
  end

  subgraph hosts["Print Host Protocols"]
    OCT["OctoPrint"]
    REP["Repetier"]
    DUE["Duet"]
    FF["Flashforge"]
    MKS["MKS"]
    AST["AstroBox"]
    OBI["Obico"]
    SP["SimplyPrint"]
    CP["CrealityPrint"]
    EL["ElegooLink"]
    ESP["ESP3D"]
  end

  DM --> MO
  MO --> NA
  NA --> CA
  NA --> PA
  CA --> OCA
  CA --> BCA
  PA --> OPA
  PA --> BPA
  PA --> QPA
  PA --> SPA
  PA --> MPA
  DM --> hosts
```

### 7.2 Network Agent Design

`NetworkAgent` uses a **sub-agent composition** pattern:
- `ICloudServiceAgent` — interface for cloud account/fleet management
- `IPrinterAgent` — interface for direct printer communication (LAN or cloud-relayed)
- Agents are dynamically swappable at runtime via `set_printer_agent()`

### 7.3 Device Discovery

- **Bonjour/mDNS** (`Bonjour.*`) for LAN discovery
- **SSDP** for network device announcements
- **Cloud API** for account-linked devices
- Discovered devices are registered as `MachineObject` instances in `DeviceManager`

### 7.4 MachineObject Subsystems

Each `MachineObject` composes device subsystems:

| Subsystem | Class | Purpose |
|-----------|-------|---------|
| Lamp | `DevLamp` | Chamber light control |
| Nozzle | `DevNozzleSystem` | Nozzle configuration |
| Filament | `DevFilaSystem` | Filament/AMS management |
| Fan | `DevFan` | Fan speed control |
| Bed | `DevBed` | Bed temperature/type |
| HMS | `DevHMS` | Health Management System (errors) |
| Config | `DevConfig` | Device configuration |
| Extension | `DevExtensionTool` | Tool changer support |

---

## 8. Configuration System

### 8.1 Config Class Hierarchy

```mermaid
classDiagram
  direction TB

  class ConfigBase {
    <<abstract>>
    +option(key) ConfigOption*
    +set(key, value)
    +serialize()
  }

  class DynamicPrintConfig {
    +options map
    +apply(other)
    +diff(other)
  }

  class StaticPrintConfig {
    <<abstract>>
    +options fixed at compile time
  }

  class GCodeConfig {
    +gcode_flavor
    +nozzle_diameter
    +filament_diameter
    +extrusion_multiplier
  }

  class PrintConfig {
    +retraction settings
    +speed settings
    +travel settings
  }

  class PrintObjectConfig {
    +layer_height
    +perimeters
    +support settings
  }

  class PrintRegionConfig {
    +infill_density
    +fill_pattern
    +top/bottom layers
    +wall settings
  }

  class FullPrintConfig {
    +all settings combined
  }

  ConfigBase <|-- DynamicPrintConfig
  ConfigBase <|-- StaticPrintConfig
  StaticPrintConfig <|-- GCodeConfig
  GCodeConfig <|-- PrintConfig
  StaticPrintConfig <|-- PrintObjectConfig
  StaticPrintConfig <|-- PrintRegionConfig
  PrintConfig <|-- FullPrintConfig
  PrintObjectConfig <|-- FullPrintConfig
  PrintRegionConfig <|-- FullPrintConfig
```

### 8.2 Preset System

```mermaid
flowchart TD
  PB["PresetBundle"]
  PP["Preset (print)"]
  PM["Preset (material/filament)"]
  PR["Preset (printer)"]
  PU["PresetUpdater\n(sync from cloud)"]
  AC["AppConfig\n(app-level settings)"]

  PB --> PP
  PB --> PM
  PB --> PR
  PB --> PU
  PB --> AC

  subgraph override["Override Hierarchy"]
    direction TB
    O1["Printer defaults"]
    O2["Print preset overrides"]
    O3["Material preset overrides"]
    O4["Object config overrides\n(ModelObject.config)"]
    O5["Volume config overrides\n(ModelVolume.config)"]

    O1 --> O2 --> O3 --> O4 --> O5
  end
```

Presets are stored as INI-style files in the user data directory. Each preset contains a `DynamicPrintConfig`. The `PresetBundle` manages collections of print, material, and printer presets and resolves inheritance between them.

### 8.3 Key Config Enums

| Enum | Values (selection) | Controls |
|------|-------------------|----------|
| `InfillPattern` | Rectilinear, Honeycomb, Gyroid, Lightning, Concentric, … | Fill pattern selection |
| `SupportMaterialStyle` | Default, TreeSlim, TreeStrong, TreeHybrid, … | Support strategy |
| `SeamPosition` | Nearest, Aligned, Rear, Random | Seam placement |
| `GCodeFlavor` | MarlinLegacy, Klipper, RepRapFirmware, … | G-code dialect |
| `IroningType` | NoIroning, TopSurfaces, TopmostOnly, AllSolid | Ironing mode |
| `SlicingMode` | Regular, EvenOdd, CloseHoles | Mesh slicing rule |

---

## 9. File Format I/O

### 9.1 Import / Export Pipeline

```mermaid
flowchart LR
  subgraph import["Import Formats"]
    I1["STL"]
    I2["3MF / BBS 3MF"]
    I3["OBJ"]
    I4["AMF"]
    I5["STEP"]
  end

  subgraph core["Core Representation"]
    M["Model\n+ ModelObject\n+ TriangleMesh"]
  end

  subgraph export["Export Formats"]
    E1["G-code\n(via Print pipeline)"]
    E2["3MF / BBS 3MF\n(project save)"]
    E3["STL\n(mesh export)"]
    E4["SL1\n(SLA output)"]
  end

  I1 --> M
  I2 --> M
  I3 --> M
  I4 --> M
  I5 --> M

  M --> E1
  M --> E2
  M --> E3
  M --> E4
```

### 9.2 Format Details

| Format | Files | Read | Write | Notes |
|--------|-------|------|-------|-------|
| **3MF** | `3mf.*`, `bbs_3mf.*` | Yes | Yes | Native project format with BBS extensions for multi-material, plate layout, metadata |
| **STL** | `STL.*` | Yes | Yes | Binary and ASCII |
| **OBJ** | `OBJ.*`, `objparser.*` | Yes | No | With material/color support via `ObjColorUtils` |
| **AMF** | `AMF.*` | Yes | Yes | XML-based with color/material |
| **STEP** | `STEP.*` | Yes | No | CAD format via OpenCASCADE |
| **SL1** | `SL1.*` | Yes | Yes | SLA archive format |
| **SVG** | `svg.*` | No | Yes | For SLA layer export |

### 9.3 G-code Post-Processing Chain

After extrusion paths are generated, the G-code export pipeline applies several post-processors in sequence:

| Processor | Class | Purpose |
|-----------|-------|---------|
| Seam placement | `SeamPlacer` | Position layer-start seams |
| Wipe | `Wipe` | Wipe nozzle on retraction |
| Ooze prevention | `OozePrevention` | Multi-extruder ooze control |
| Wipe tower | `WipeTowerIntegration` | Color transition priming |
| Cooling | `CoolingBuffer` | Fan speed and slowdown |
| Fan mover | `FanMover` | Advance fan commands |
| Pressure equalization | `PressureEqualizer` | Pressure advance tuning |
| Adaptive PA | `AdaptivePAProcessor` | Dynamic pressure advance |
| Spiral vase | `SpiralVase` | Continuous Z for vase mode |
| Conflict checker | `ConflictChecker` | Toolhead collision detection |
| Arc fitting | `ArcWelder` | Convert segments to arcs |

---

## 10. Module Index

Every major module in the codebase, grouped by layer. Each is a candidate for a dedicated `doc/<module>.md`.

### Core Engine (`src/libslic3r/`)

| Module | Key Files | Description |
|--------|-----------|-------------|
| **Print orchestration** | `Print.*`, `PrintObject.*`, `PrintBase.*`, `PrintApply.*`, `PrintRegion.*` | Top-level slicing state machine and object management |
| **Model** | `Model.*`, `ModelArrange.*` | In-memory 3D model representation |
| **Layer** | `Layer.*`, `LayerRegion.*` | Per-layer slice data and region processing |
| **TriangleMesh** | `TriangleMesh.*`, `TriangleMeshSlicer.*`, `TriangleSelector.*` | Mesh data structure and slicing |
| **GCode** | `GCode/` (45 files) | G-code generation, post-processing, and analysis |
| **Fill** | `Fill/` (28 files) | Infill pattern implementations |
| **Support** | `Support/` (15 files) | Tree and traditional support generation |
| **Arachne** | `Arachne/` (8 files) | Variable-width wall generation via skeletal trapezoidation |
| **Format** | `Format/` (23 files) | File I/O for all supported formats |
| **Geometry** | `Geometry/` (18 files) | Voronoi diagrams, convex hulls, arc welding, medial axis |
| **SLA** | `SLA/` (32 files) | SLA-specific slicing, supports, hollowing, rasterization |
| **Clipper** | `clipper.*`, `ClipperUtils.*`, `Clipper2Utils.*` | 2D polygon clipping and offsetting |
| **Perimeter** | `PerimeterGenerator.*`, `VariableWidth.*` | Wall path generation |
| **Bridge detection** | `BridgeDetector.*` | Unsupported span detection and angle optimization |
| **Flow** | `Flow.*` | Extrusion width/height calculations |
| **Config** | `Config.*`, `PrintConfig.*`, `Preset.*`, `PresetBundle.*` | Settings definitions, presets, and bundles |
| **Extrusion** | `ExtrusionEntity.*`, `ExtrusionEntityCollection.*` | Extrusion path data structures |
| **Arrange** | `Arrange.*`, `Orient.*` | Auto-arrangement and orientation of objects on plate |
| **Brim** | `Brim.*` | Brim generation for bed adhesion |
| **Emboss** | `Emboss.*`, `EmbossShape.hpp` | Text and shape embossing |
| **CSGMesh** | `CSGMesh/` (7 files) | Constructive Solid Geometry boolean operations |
| **Algorithm** | `Algorithm/` (4 files) | Region expansion, line splitting |
| **Execution** | `Execution/` (3 files) | Parallel execution models (sequential, TBB) |
| **Optimize** | `Optimize/` (3 files) | NLopt and brute-force optimization |
| **Multi-material** | `MultiMaterialSegmentation.*`, `FlushVolCalc.*` | MMU segmentation and flush volumes |
| **Elephant foot** | `ElephantFootCompensation.*` | First-layer compensation |
| **Slicing** | `Slicing.*`, `SlicingAdaptive.*` | Layer height computation and adaptive slicing |
| **Spatial indexing** | `AABBTreeIndirect.hpp`, `KDTreeIndirect.hpp`, `AABBMesh.*` | Spatial search structures |
| **Pathfinding** | `ShortestPath.*`, `JumpPointSearch.*`, `AStar.hpp` | Path optimization algorithms |
| **Platform** | `Platform.*`, `MacUtils.hpp` | OS-specific abstractions |

### GUI Layer (`src/slic3r/GUI/`)

| Module | Key Files | Description |
|--------|-----------|-------------|
| **Application** | `GUI_App.*` | wxApp subclass, lifecycle, global services |
| **MainFrame** | `MainFrame.*` | Top-level window, menu bar, tab navigation |
| **Plater** | `Plater.*` | Central 3D workspace, model/plate management |
| **GLCanvas3D** | `GLCanvas3D.*` | OpenGL rendering, input handling, camera |
| **Gizmos** | `Gizmos/` (53 files) | Interactive 3D editing tools (move, rotate, cut, paint, …) |
| **Widgets** | `Widgets/` (79 files) | Custom wxWidgets controls and composite panels |
| **Jobs** | `Jobs/` (37 files) | Background job runners (arrange, orient, export, upload) |
| **GCodeViewer** | `GCodeViewer.*` | G-code preview visualization |
| **BackgroundSlicingProcess** | `BackgroundSlicingProcess.*` | Async slicing thread management |
| **Sidebar** | embedded in `Plater.*` | Preset selection, filament management, search |
| **ParamsPanel** | `ParamsPanel.*` | Setting tabs and parameter editing |
| **MonitorPanel** | `MonitorPanel.*` | Device status monitoring |
| **MultiMachinePage** | `MultiMachinePage.*` | Multi-device management |
| **CalibrationPanel** | `CalibrationPanel.*` | Printer calibration workflows |
| **Selection** | `Selection.*` | Object/instance selection management |
| **Camera** | `Camera.*` | 3D viewport camera control |
| **Bed3D** | `Bed3D.*` | Build plate visualization |
| **Notifications** | `NotificationManager.*` | Toast notification system |
| **Search** | `Search.*` | Settings search engine |
| **DeviceCore** | `DeviceCore/` (47 files) | Device communication protocols |
| **DeviceTab** | `DeviceTab/` (4 files) | Device management UI |
| **Printer** | `Printer/` (4 files) | Printer-specific UI components |

### Services & Utilities (`src/slic3r/Utils/`)

| Module | Key Files | Description |
|--------|-----------|-------------|
| **NetworkAgent** | `NetworkAgent.*`, `NetworkAgentFactory.*` | Network abstraction and agent factory |
| **Cloud agents** | `OrcaCloudServiceAgent.*`, `BBLCloudServiceAgent.*`, `ICloudServiceAgent.hpp` | Cloud service implementations |
| **Printer agents** | `OrcaPrinterAgent.*`, `BBLPrinterAgent.*`, `QidiPrinterAgent.*`, `SnapmakerPrinterAgent.*`, `MoonrakerPrinterAgent.*` | Per-vendor printer communication |
| **Print hosts** | `PrintHost.*`, `OctoPrint.*`, `Repetier.*`, `Duet.*`, `Flashforge.*`, `MKS.*`, … | Third-party print host protocols |
| **DeviceManager** | `GUI/DeviceManager.*` | Device lifecycle and state management |
| **Bonjour** | `Bonjour.*` | mDNS/Bonjour device discovery |
| **HTTP** | `Http.*`, `WebSocketClient.hpp` | HTTP and WebSocket client |
| **UndoRedo** | `UndoRedo.*` | Undo/redo stack implementation |
| **PresetUpdater** | `PresetUpdater.*` | Preset synchronization from cloud |
| **CalibUtils** | `CalibUtils.*` | Calibration utilities |

### Test Suites (`tests/`)

| Suite | Directory | Files | Covers |
|-------|-----------|-------|--------|
| **libslic3r** | `tests/libslic3r/` | 21 | Core geometry, algorithms, file formats |
| **fff_print** | `tests/fff_print/` | 12 | FDM slicing, G-code generation, fills |
| **sla_print** | `tests/sla_print/` | 4 | SLA processing and supports |
| **libnest2d** | `tests/libnest2d/` | — | 2D nesting algorithms |
| **slic3rutils** | `tests/slic3rutils/` | — | Utility functions |

---

## Appendix: Key Entry Points for Common Tasks

| Task | Start here |
|------|-----------|
| Understand the slicing pipeline | `Print::process()` in `libslic3r/Print.cpp` |
| Add a new print setting | `PrintConfig.cpp` → GUI in `ParamsPanel` or `Tab` |
| Add a new infill pattern | Subclass `Fill` in `libslic3r/Fill/` |
| Modify perimeter generation | `PerimeterGenerator.cpp`, `Arachne/WallToolPaths.cpp` |
| Change G-code output | `GCode.cpp`, post-processors in `GCode/` |
| Add support for a new printer | JSON profile in `resources/profiles/`, printer agent in `Utils/` |
| Add a new 3D gizmo | `GUI/Gizmos/`, register in `GLGizmosManager` |
| Add a new file format | Implement reader/writer in `libslic3r/Format/` |
| Modify the UI layout | `MainFrame.cpp`, `Plater.cpp`, `Sidebar` |
| Add a new background job | `GUI/Jobs/`, use `Worker` queue |
| Debug device communication | `DeviceManager`, relevant printer agent in `Utils/` |

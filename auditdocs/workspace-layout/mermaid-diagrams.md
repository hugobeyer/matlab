# Mermaid Diagrams

Decision flowcharts and state models for the workspace-layout and
global-variables work. See `decisions-log.md` for the same content as tables.

## 1. Global variables — decision flow

```mermaid
flowchart TD
    Start["Global variables cell"] --> Q1{"Parameter value types?"}
    Q1 -->|"v1"| C1["Float only"]
    Q1 -->|"later"| L1["Int / bool / enum"]

    C1 --> Q2{"Drive mechanism?"}
    Q2 -->|"chosen"| C2["Reuse FMixtormatParameterDriver + SourceVariableId field"]
    Q2 -->|"rejected"| R2a["New address owner type - large blast radius"]
    Q2 -->|"rejected"| R2b["New binding struct - duplicates the model"]

    C2 --> Q3{"Where is it resolved?"}
    Q3 -->|"chosen"| C3["CPU, in compositor layer copy, before hash + gather"]
    Q3 -->|"rejected"| R3a["GPU texture slots - only 2, spatial masks only"]
    Q3 -->|"rejected"| R3b["Editor-side - two resolution points, bake diverges"]

    C3 --> Q4{"Driver math?"}
    Q4 -->|"chosen"| C4["Match MixtormatDriver.ush chain exactly"]
    Q4 -->|"rejected"| R4["Invented CPU chain - preview and bake drift"]

    C4 --> Q5{"Reference and driver both set?"}
    Q5 -->|"editor"| C5["Mutually exclusive - enabling one disables the other"]
    Q5 -->|"malformed asset"| O5["OPEN - define explicit precedence"]

    C5 --> Q6{"Where does variable data live?"}
    Q6 -->|"chosen"| C6["UMixtormatMaterial asset UPROPERTY"]
    Q6 -->|"rejected"| R6["Authoring JSON - defaults and ranges only, never bindings"]

    C6 --> Q7{"Panel placement?"}
    Q7 -->|"recommended"| C7["Pinned GLOBAL group in the inspector"]
    Q7 -->|"fallback"| A7["Left tab, bottom drawer tab, or floating window"]

    C7 --> Q8{"Slider displays?"}
    Q8 -->|"recommended"| C8["Authored value - popover shows variable + computed result"]
    Q8 -->|"open"| O8["Effective value display"]

    C8 --> Q9{"Variable missing or deleted?"}
    Q9 -->|"chosen"| C9["Driver contributes nothing - authored value stays - broken state shown"]
    Q9 -->|"rejected"| R9["Silent wrong value or crash"]

    C9 --> Q10{"Import composition with driven layers?"}
    Q10 -->|"chosen"| C10["Import + remap IDs, or decline with a message"]
    Q10 -->|"rejected"| R10["Silent loss of the effect"]

    C10 --> P["Phases 1 to 4"]
```

## 2. Hybrid workspace — reconciliation flow

```mermaid
flowchart TD
    Start["Hybrid workspace redesign"] --> Q1{"Inspector placement?"}
    Q1 -->|"chosen"| C1["Placement mode: Overlay / Docked Right / Hidden / Auto"]
    Q1 -->|"rejected"| R1["Fixed right column only - current code"]

    C1 --> Q2{"Inspector chrome?"}
    Q2 -->|"needed"| C2["New header bar: name, Add, Pin / Dock / Hide"]
    Q2 -->|"open"| O2["Free drag position - medium lift, beyond resize"]

    C2 --> Q3{"Row layout?"}
    Q3 -->|"chosen"| C3["Two controls per row via MixtormatRow::MakePair"]

    C3 --> Q4{"Remember layout?"}
    Q4 -->|"chosen"| C4["UMixtormatEditorSettings - widths, heights, modes, collapsed"]
    Q4 -->|"rejected"| R4["Theme store - deliberately excludes layout"]

    C4 --> Q5{"Hotkeys?"}
    Q5 -->|"chosen"| C5["G exists - add L layers, P inspector"]

    C5 --> Q6{"Gallery form?"}
    Q6 -->|"recommended"| O6["Tabbed drawer - Materials / Masks"]
    Q6 -->|"option"| O6b["Vertical split - audit 2"]
    Q6 -->|"option"| O6c["Keep two-column splitter - current code"]

    C5 --> Q7{"Variables cell home?"}
    Q7 -->|"recommended"| C7["Pinned GLOBAL group in the inspector"]
    Q7 -->|"alternative"| A7["Left tab, bottom drawer tab, or floating window"]

    C7 --> P["Variables decisions - diagram 1"]
```

## 3. Inspector visibility state model (Auto mode)

```mermaid
stateDiagram-v2
    [*] --> Hidden
    Hidden --> ShownSelection: select layer / child / group
    Hidden --> ShownGlobals: press P
    ShownSelection --> ShownGlobals: deselect while manually opened
    ShownGlobals --> ShownSelection: select something
    ShownSelection --> Hidden: deselect (empty-space click)
    ShownGlobals --> Hidden: press P
    ShownSelection --> Pinned: Pin
    ShownGlobals --> Pinned: Pin
    Pinned --> ShownSelection: Unpin with selection
    Pinned --> ShownGlobals: Unpin without selection

    note right of ShownGlobals
        GLOBAL section = global variables cell
        (pinned, collapsible group)
    end note

    note right of Pinned
        Auto-hide suppressed
    end note
```

## 4. Inspector popover — collapse-to-top states

```mermaid
flowchart LR
    Bar["Collapsed bar - top of viewport\nselection name + badge\n~30px"] -->|"expand"| Panel["Expanded popover\nvariables + selection content\nresizable, draggable"]
    Panel -->|"collapse"| Bar
    Panel -->|"drag"| Moved["Free position\nclamped to viewport"]
    Moved -->|"collapse"| Bar
    Bar -->|"P"| Hidden["Hidden"]
    Hidden -->|"P"| Bar
```

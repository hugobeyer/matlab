
flowchart TD
    A["Right-click target generator"] --> B{"Add structural module"}
    B -->|Height Push| C["Insert Height Push<br/>before target"]
    B -->|Structural Warp| D["Insert Structural Warp<br/>before target"]

    C --> E["Assign explicit target GUID"]
    D --> E
    E --> F["Source chip: Set Source"]
    F --> G["Choose compatible earlier output"]
    G --> H{"Type, order and scope valid?"}

    H -->|Yes| I["Save Source reference<br/>Show connection in stack"]
    H -->|No| J["Show reason<br/>Keep existing reference"]
    F -->|Leave unset| K["Explicit unconnected no-op"]

    I --> L{"Reorder later?"}
    L -->|Still valid| M["Allow move"]
    L -->|Would invalidate link| N["Block move and explain"]
    J --> G

    classDef success fill:#254534,stroke:#8ace9a,color:#ffffff
    classDef warning fill:#583c25,stroke:#dfab65,color:#ffffff
    class I,M success
    class J,K,N warning

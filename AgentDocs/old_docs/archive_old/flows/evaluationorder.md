
flowchart TB
    subgraph Earlier["Optional earlier generator layer"]
        E["Completed compatible source output"]
    end

    subgraph Stack["Generator layer — authored top-to-bottom order"]
        direction TB
        A["Noise A<br/>Generator · signed Height"]
        B["Rock Formation<br/>Generator"]
        F["Flow effect<br/>Scoped to Rock · Flow / UVMap"]
        H["Height Push<br/>Structural module"]
        M["Mask<br/>Scoped to Height Push"]
        W["Structural Warp<br/>Structural module"]
        T["Strata Carver<br/>Target generator"]

        A --> B --> F --> H --> M --> W --> T
    end

    A -. "signed Height" .-> H
    F -. "typed Flow / UVMap" .-> W
    H -. "TargetChildId" .-> T
    W -. "TargetChildId" .-> T
    M -. "gates Push only" .-> H
    E -. "optional earlier source" .-> H
    E -. "optional earlier source" .-> W

    classDef module fill:#17364b,stroke:#62b6d9,color:#ffffff
    classDef target fill:#254534,stroke:#8ace9a,color:#ffffff
    class H,W module
    class T target

classDiagram
    class StreamTask {
        +string stream_id
        +string source_uri
        +SourceType source_type
        +Region region
        +BitrateClass bitrate_class
        +LatencyClass latency_class
        +ComputeClass compute_class
        +int width
        +int height
        +int fps
        +int target_bitrate_kbps
        +bool enable_ai
        +bool enable_overlay
        +bool enable_record
        +int analyze_fps
    }

    class Classifier {
        +Apply(StreamTask&)
    }

    class SchedulerPolicy {
        +double region_weight
        +double load_weight
        +double capability_weight
        +double latency_weight
        +double failover_weight
        +int max_queue_threshold
        +double busy_util_threshold
    }

    class Scheduler {
        +SelectNode(StreamTask, exclude_node) EdgeNode
    }

    class StreamManager {
        +Start()
        +StartStream(StreamTask) bool
        +StopStream(string)
        +StopAll()
    }

    class PipelineRunner {
        +Run(stop_flag) PipelineResult
    }

    class EdgeNodePool {
        +AddNode(EdgeNode)
        +AllNodes() vector~EdgeNode~
        +GetNode(string) EdgeNode
    }

    class EdgeNode {
        +Id() string
        +CanAccept(StreamTask, SchedulerPolicy) bool
        +Score(StreamTask, SchedulerPolicy) double
        +Submit(StreamTask, fn) bool
        +Stats() EdgeNodeStats
    }

    class MetricsRegistry {
        +SetGauge(scope, name, value)
        +IncCounter(scope, name, delta)
        +Snapshot(prefix) map
    }

    Classifier --> StreamTask
    Scheduler --> EdgeNodePool
    EdgeNodePool o-- EdgeNode
    StreamManager --> Scheduler
    StreamManager --> PipelineRunner
    StreamManager --> MetricsRegistry
    PipelineRunner --> StreamTask
    PipelineRunner --> MetricsRegistry

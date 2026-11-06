#ifndef MAP_NETWORK_TYPES_H
#define MAP_NETWORK_TYPES_H

// Network enums of the render contract shared by every map host (moved from
// the GUI's common/_enums_structs.h, which includes this header): the
// entity kinds the renderer draws and the colouring / heatmap metrics.

#include <cstdint>

enum class VisualNode
{
    None,
    Elevation,
    BaseDemand,
    TotalDemand,
    DemandDeficit,
    EmitterFlow,
    Leakage,
    Head,
    Pressure,
    Chlorine,
    RiverWater,
    LakeWater,
    WaterAge
};

enum class VisualLink
{
    None,
    Diameter,
    Length,
    Roughness,
    FlowRate,
    Velocity,
    HeadLoss,
    Leakage,
    Chlorine,
    RiverWater,
    LakeWater,
    WaterAge
};

enum class VisualHeatmap
{
    None,
    Elevation,
    BaseDemand,
    TotalDemand,
    DemandDeficit,
    EmitterFlow,
    Leakage,
    Head,
    Pressure,
    Chlorine,
    RiverWater,
    LakeWater,
    WaterAge
};

enum class InfrastructureEntity : std::uint8_t
{
    Unknown = 0,
    
    // Hydraulic / EPANET-style node entities
    Junction,
    Reservoir,
    Tank,
    
    // Hydraulic / EPANET-style link entities
    Pipe,
    Pump,
    Valve,
    
    // AOWIS hydraulic extensions
    DemandPoint,
    
    // Electric node/storage/generation entities
    ElectricJunction,
    Battery,
    Generator,
    SolarPanel,
    Inverter,
    Transformer,
    
    // Electric link/control entities
    Cable,
    Switch,
    Fuse,
    CircuitBreaker,
    
    // Non-simulation / UI entities
    Note
};

#endif // MAP_NETWORK_TYPES_H

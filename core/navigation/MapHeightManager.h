#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "../types/types.h"

namespace Navigation {

// Simple tile‐based height manager for TrinityCore .map files (terrain height maps).
// Only supports height queries; liquid/area data are ignored for now.
class MapHeightManager {
public:
    MapHeightManager() = default;
    ~MapHeightManager();

    // Initialise with the path that contains the "maps" directory produced by map_extractor.
    // (E.g. ".../maps" where files look like 0010304.map).
    bool Initialize(const std::string& mapsDirectory);

    void Shutdown();

    // Ensure the tile covering (mapId,x,y) is loaded.
    bool LoadTile(uint32_t mapId, uint32_t tileX, uint32_t tileY);

    // Retrieve ground height.  Returns -FLT_MAX if height not available.
    float GetHeight(uint32_t mapId, const Vector3& wowPos);

private:
    struct TileData {
        // V9 vertex grid 129×129 values (row‐major y-then-x) stored as float.
        std::vector<float> v9; // size 129*129 == 16641
        float minHeight = 0.f;
        float maxHeight = 0.f;
        bool flat = false;
    };

    struct TileKeyHash { size_t operator()(uint64_t k) const noexcept { return std::hash<uint64_t>{}(k);} };

    std::unordered_map<uint64_t, TileData, TileKeyHash> m_tiles; // key=(tileX<<32)|tileY|(mapId<<48)

    std::string m_mapsDir; // .../maps

    // Helpers
    static uint64_t MakeKey(uint32_t mapId, uint32_t tileX, uint32_t tileY) {
        return (static_cast<uint64_t>(mapId) << 40) | (static_cast<uint64_t>(tileX) << 20) | tileY;
    }
};

} // namespace Navigation 
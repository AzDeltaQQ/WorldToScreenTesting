#include "MapHeightManager.h"
#include "../logs/Logger.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <iomanip>

namespace Navigation {

namespace {
constexpr float TILE_SIZE = 533.33333f;
constexpr float UNIT_SIZE = TILE_SIZE / 128.0f; // each vertex spacing
constexpr float ZEROPOINT = 32.0f * TILE_SIZE;  // same as VMAP

#pragma pack(push,1)
struct map_fileheader {
    uint32_t mapMagic;
    uint32_t versionMagic;
    uint32_t buildMagic;
    uint32_t areaMapOffset;
    uint32_t areaMapSize;
    uint32_t heightMapOffset;
    uint32_t heightMapSize;
    uint32_t liquidMapOffset;
    uint32_t liquidMapSize;
    uint32_t holesOffset;
    uint32_t holesSize;
};
struct map_heightHeader {
    uint32_t fourcc;
    uint32_t flags;
    float gridHeight;
    float gridMaxHeight;
};
#pragma pack(pop)

static constexpr uint32_t MAP_MAGIC = 'SPAM'; // 'MAPS' little-endian
static constexpr uint32_t MAP_HEIGHT_MAGIC = 'TGHM'; // 'MHGT'
static constexpr uint32_t MAP_VERSION = 10;

inline float DecodeHeight(uint16_t val, float minH, float maxH) {
    float diff = maxH - minH;
    return minH + (static_cast<float>(val) / 65535.0f) * diff;
}
inline float DecodeHeight(uint8_t val, float minH, float maxH) {
    float diff = maxH - minH;
    return minH + (static_cast<float>(val) / 255.0f) * diff;
}
}

MapHeightManager::~MapHeightManager() { Shutdown(); }

bool MapHeightManager::Initialize(const std::string& mapsDirectory) {
    m_mapsDir = mapsDirectory;

    // If given path is the "mmaps" folder, adjust to sibling "maps" folder automatically.
    std::filesystem::path p(m_mapsDir);
    if (p.filename() == "mmaps") {
        std::filesystem::path candidate = p.parent_path() / "maps";
        if (std::filesystem::exists(candidate)) {
            m_mapsDir = candidate.string();
        }
    }

    if (!std::filesystem::exists(m_mapsDir)) {
        LOG_ERROR("MapHeightManager: maps directory not found: " + m_mapsDir);
        return false;
    }
    LOG_INFO("MapHeightManager initialized. maps path: " + m_mapsDir);
    return true;
}

void MapHeightManager::Shutdown() {
    m_tiles.clear();
}

bool MapHeightManager::LoadTile(uint32_t mapId, uint32_t tileX, uint32_t tileY) {
    uint64_t key = MakeKey(mapId, tileX, tileY);
    if (m_tiles.count(key)) return true; // already loaded

    // build filename AAAyyxx.map
    std::ostringstream fname;
    fname << std::setfill('0') << std::setw(3) << mapId
          << std::setw(2) << tileY
          << std::setw(2) << tileX << ".map";
    std::filesystem::path filePath = std::filesystem::path(m_mapsDir) / fname.str();

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        LOG_DEBUG("MapHeightManager: map tile not found: " + std::filesystem::weakly_canonical(filePath).string());
        return false;
    }

    map_fileheader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (header.mapMagic != MAP_MAGIC || header.versionMagic != MAP_VERSION) {
        LOG_WARNING("MapHeightManager: invalid map header: " + filePath.string());
        return false;
    }

    if (header.heightMapSize == 0) {
        LOG_DEBUG("MapHeightManager: tile has no height data: " + filePath.string());
        return false;
    }

    // seek to height header
    file.seekg(header.heightMapOffset, std::ios::beg);
    map_heightHeader hHdr{};
    file.read(reinterpret_cast<char*>(&hHdr), sizeof(hHdr));
    if (hHdr.fourcc != MAP_HEIGHT_MAGIC) {
        LOG_WARNING("MapHeightManager: missing height fourcc in " + filePath.string());
        return false;
    }

    bool noHeight = (hHdr.flags & 0x1) != 0; // MAP_HEIGHT_NO_HEIGHT
    if (noHeight) {
        TileData td; td.flat = true; td.minHeight = td.maxHeight = hHdr.gridHeight;
        m_tiles.emplace(key, std::move(td));
        return true;
    }

    bool asInt16 = (hHdr.flags & 0x2) != 0;  // MAP_HEIGHT_AS_INT16
    bool asInt8  = (hHdr.flags & 0x4) != 0;  // MAP_HEIGHT_AS_INT8

    constexpr size_t V9_SIZE = 129 * 129;
    size_t bytesPerEntry = asInt8 ? 1 : (asInt16 ? 2 : 4);

    std::vector<float> heights(V9_SIZE, hHdr.gridHeight);

    if (asInt8) {
        std::vector<uint8_t> buf(V9_SIZE);
        file.read(reinterpret_cast<char*>(buf.data()), buf.size());
        for (size_t i = 0; i < V9_SIZE; ++i)
            heights[i] = DecodeHeight(buf[i], hHdr.gridHeight, hHdr.gridMaxHeight);
        // skip V8 data
    } else if (asInt16) {
        std::vector<uint16_t> buf(V9_SIZE);
        file.read(reinterpret_cast<char*>(buf.data()), buf.size() * 2);
        for (size_t i = 0; i < V9_SIZE; ++i)
            heights[i] = DecodeHeight(buf[i], hHdr.gridHeight, hHdr.gridMaxHeight);
    } else {
        file.read(reinterpret_cast<char*>(heights.data()), V9_SIZE * 4);
    }

    TileData td;
    td.v9 = std::move(heights);
    td.minHeight = hHdr.gridHeight;
    td.maxHeight = hHdr.gridMaxHeight;
    td.flat = false;
    m_tiles.emplace(key, std::move(td));
    LOG_DEBUG("MapHeightManager: loaded tile " + filePath.string());
    return true;
}

float MapHeightManager::GetHeight(uint32_t mapId, const Vector3& pos) {
    // convert world to tile coordinates
    float worldX = pos.x;
    float worldY = pos.y;

    int tileX = static_cast<int>(std::floor((ZEROPOINT - worldY) / TILE_SIZE));
    int tileY = static_cast<int>(std::floor((ZEROPOINT - worldX) / TILE_SIZE));
    if (tileX < 0 || tileX >= 64 || tileY < 0 || tileY >= 64)
        return -FLT_MAX;

    if (!LoadTile(mapId, tileX, tileY))
        return -FLT_MAX;

    uint64_t key = MakeKey(mapId, tileX, tileY);
    auto it = m_tiles.find(key);
    if (it == m_tiles.end()) return -FLT_MAX;

    const TileData& td = it->second;
    if (td.flat) return td.minHeight;

    // local coordinates inside tile
    float localY = (ZEROPOINT - worldY) - tileX * TILE_SIZE; // along Y axis -> tileX
    float localX = (ZEROPOINT - worldX) - tileY * TILE_SIZE; // along X axis -> tileY

    // vertex indices
    float vx = localX / UNIT_SIZE;
    float vy = localY / UNIT_SIZE;

    int ix = static_cast<int>(std::floor(vx));
    int iy = static_cast<int>(std::floor(vy));

    float fx = vx - ix;
    float fy = vy - iy;

    if (ix < 0) { ix = 0; fx = 0; }
    if (iy < 0) { iy = 0; fy = 0; }
    if (ix >= 128) { ix = 127; fx = 1; }
    if (iy >= 128) { iy = 127; fy = 1; }

    auto sample = [&](int x,int y){ return td.v9[y*129 + x]; };

    float h00 = sample(ix,   iy);
    float h10 = sample(ix+1, iy);
    float h01 = sample(ix,   iy+1);
    float h11 = sample(ix+1, iy+1);

    float h = (1-fx)*(1-fy)*h00 + fx*(1-fy)*h10 + (1-fx)*fy*h01 + fx*fy*h11;
    return h;
}

} // namespace Navigation 
#pragma once

#include "../../types.h"
#include "WorldToScreenCore.h"
#include "RenderEngine.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

// BLP file format structures (matching C# reference implementation)
#pragma pack(push, 1)
struct BLPHeader {
    uint32_t magic;           // "BLP2" = 0x32504C42
    uint32_t type;            // DataType: 0=JPEG, 1=Uncompressed_DirectX
    uint8_t encoding;         // Encoding: 1=RAW1 (paletted), 2=DXT, 3=RAW3 (BGRA)
    uint8_t alphaDepth;       // Alpha bits (0, 1, 4, 8)
    uint8_t alphaEncoding;    // 0 for DXT1, 1 for DXT3, 7 for DXT5, 8 for RAW8BIT
    uint8_t hasMipmaps;       // 0 or 1
    uint32_t width;
    uint32_t height;
    uint32_t mipmapOffsets[16];
    uint32_t mipmapSizes[16];
    // Followed by palette if encoding is 1 (RAW1) and alphaDepth is 8
};
#pragma pack(pop)

// Simplified texture rendering data structure using direct D3D textures
struct TextureRenderData {
    IDirect3DTexture9* pD3DTexture; // Direct D3D texture for rendering
    D3DXVECTOR3 worldPos;
    D3DXVECTOR2 screenPos;
    D3DXVECTOR2 size;
    D3DCOLOR color;
    bool isVisible;
    int id;
    std::string label;
    std::string texturePath;
    float scale;
    bool billboarded;
};

// Texture search/filter settings
struct TextureSearchSettings {
    bool enableTextureRendering;
    bool showTextureLabels;
    bool onlyShowInRange;
    float maxRenderDistance;
    float defaultTextureSize;
    float defaultTextureScale;
    bool billboardTextures;
    
    // Search filters
    std::string searchFilter;
    bool showInterfaceTextures;
    bool showSpellTextures;
    bool showItemTextures;
    bool showEnvironmentTextures;
    
    TextureSearchSettings() {
        enableTextureRendering = true;
        showTextureLabels = true;
        onlyShowInRange = true;
        maxRenderDistance = 100.0f;
        defaultTextureSize = 32.0f;
        defaultTextureScale = 1.0f;
        billboardTextures = true;
        
        searchFilter = "";
        showInterfaceTextures = true;
        showSpellTextures = true;
        showItemTextures = true;
        showEnvironmentTextures = false;
    }
};

class TextureManager {
private:
    WorldToScreenCore* m_pWorldToScreen;
    RenderEngine* m_pRenderEngine;
    LPDIRECT3DDEVICE9 m_pDevice;
    
    // D3DX Sprite for texture rendering
    LPD3DXSPRITE m_pSprite;
    
    // Simplified texture cache using direct D3D textures
    std::unordered_map<std::string, IDirect3DTexture9*> m_textureCache;
    std::vector<TextureRenderData> m_renderTextures;
    int m_nextId;
    
    // Settings
    TextureSearchSettings m_settings;
    
    // Common texture paths for search suggestions
    std::vector<std::string> m_commonTexturePaths;
    
    // Internal helper methods
    bool InitializeSprite();
    IDirect3DTexture9* LoadTextureFromFile(const std::string& texturePath);
    IDirect3DTexture9* LoadBlpToDxTexture(const std::string& blpFilePath);
    IDirect3DTexture9* CreateFallbackTexture();
    void PopulateCommonTextures();
    bool PassesSearchFilter(const std::string& texturePath) const;
    void UpdateTextureVisibility();
    std::string GetTexturePath(const std::string& textureName);
    
public:
    TextureManager();
    ~TextureManager();
    
    // Initialization
    bool Initialize(WorldToScreenCore* pWorldToScreen, RenderEngine* pRenderEngine, LPDIRECT3DDEVICE9 pDevice);
    void Cleanup();
    
    // Texture management - renders actual textures from blps folder
    int AddTextureAtPosition(const std::string& texturePath, const D3DXVECTOR3& worldPos, 
                           float size = 32.0f, D3DCOLOR color = 0xFFFFFFFF, const std::string& label = "");
    void RemoveTexture(int id);
    void ClearAllTextures();
    
    // Texture loading and caching
    IDirect3DTexture9* GetTexture(const std::string& texturePath);
    void PreloadTexture(const std::string& texturePath);
    void ClearTextureCache();
    
    // Update and render
    void Update();
    void Render();
    
    // Settings management
    const TextureSearchSettings& GetSettings() const { return m_settings; }
    void SetSettings(const TextureSearchSettings& settings) { m_settings = settings; }
    
    // Search and discovery
    const std::vector<std::string>& GetCommonTexturePaths() const { return m_commonTexturePaths; }
    std::vector<std::string> SearchTextures(const std::string& filter) const;
    
    // Statistics
    size_t GetCacheSize() const { return m_textureCache.size(); }
    size_t GetRenderTextureCount() const { return m_renderTextures.size(); }
    
    // Access
    const std::vector<TextureRenderData>& GetRenderTextures() const { return m_renderTextures; }
    
    // Device management
    void OnDeviceLost();
    void OnDeviceReset();
}; 
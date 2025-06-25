#include "TextureManager.h"
#include "../../logs/Logger.h"
#include "../../memory/memory.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <windows.h>
#include <fstream>

// Helper to format pointers for logging
template <typename T>
std::string PtrToHex(T* ptr) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

// WoW's graphics device structure (simplified)
struct CGxDevice {
    char pad[0x296C];                    // Padding to reach the D3D device pointer
    IDirect3DDevice9* m_pD3DDevice;      // The actual D3D9 device at offset 0x296C
};

// WoW's global graphics device pointer (from disassembly)
constexpr uintptr_t CGxDevice_Ptr_Addr = 0xC5DF88;

// Function to get WoW's actual D3D device
IDirect3DDevice9* GetWowD3DDevice() {
    try {
        LOG_DEBUG("TextureManager: Attempting to get WoW's D3D device from 0x" + 
                  std::to_string(CGxDevice_Ptr_Addr));
        
        // Get the address of the global pointer to CGxDevice
        CGxDevice** ppDevice = reinterpret_cast<CGxDevice**>(CGxDevice_Ptr_Addr);
        
        // Validate the pointer to the pointer
        if (!Memory::IsValidAddress(reinterpret_cast<uintptr_t>(ppDevice))) {
            LOG_DEBUG("TextureManager: CGxDevice pointer address is not valid");
            return nullptr;
        }
        
        // Dereference to get the CGxDevice object pointer
        CGxDevice* pDevice = *ppDevice;
        LOG_DEBUG("TextureManager: CGxDevice object pointer: 0x" + 
                  std::to_string(reinterpret_cast<uintptr_t>(pDevice)));
        
        if (!pDevice || !Memory::IsValidAddress(reinterpret_cast<uintptr_t>(pDevice))) {
            LOG_DEBUG("TextureManager: CGxDevice object pointer is not valid");
            return nullptr;
        }
        
        // Get the D3D device from the CGxDevice object
        IDirect3DDevice9* pD3DDevice = pDevice->m_pD3DDevice;
        LOG_DEBUG("TextureManager: D3D device pointer: 0x" + 
                  std::to_string(reinterpret_cast<uintptr_t>(pD3DDevice)));
        
        if (!pD3DDevice || !Memory::IsValidAddress(reinterpret_cast<uintptr_t>(pD3DDevice))) {
            LOG_DEBUG("TextureManager: D3D device pointer is not valid");
            return nullptr;
        }
        
        LOG_DEBUG("TextureManager: Successfully retrieved WoW's D3D device");
        return pD3DDevice;
        
    } catch (...) {
        LOG_DEBUG("TextureManager: Exception while getting WoW's D3D device");
        return nullptr;
    }
}

// Simplified D3DX-based texture manager

TextureManager::TextureManager() 
    : m_pWorldToScreen(nullptr), m_pRenderEngine(nullptr), m_pDevice(nullptr), m_pSprite(nullptr), m_nextId(1) {
}

TextureManager::~TextureManager() {
    Cleanup();
}

bool TextureManager::Initialize(WorldToScreenCore* pWorldToScreen, RenderEngine* pRenderEngine, LPDIRECT3DDEVICE9 pDevice) {
    if (!pWorldToScreen || !pRenderEngine) {
        LOG_ERROR("TextureManager: Invalid parameters for initialization");
        return false;
    }
    
    m_pDevice = pDevice;
    if (!m_pDevice) {
        LOG_ERROR("TextureManager: D3D device is null");
        return false;
    }
    LOG_INFO("TextureManager: Using D3D device at: " + PtrToHex(m_pDevice));
    
    m_pWorldToScreen = pWorldToScreen;
    m_pRenderEngine = pRenderEngine;
    
    if (!InitializeSprite()) {
        LOG_ERROR("TextureManager: Failed to initialize D3DX Sprite");
        return false;
    }
    
    PopulateCommonTextures();
    LOG_INFO("TextureManager initialized successfully with custom BLP loader.");
    return true;
}

void TextureManager::Cleanup() {
    m_renderTextures.clear();
    
    // Clear texture cache manually
    for (auto& pair : m_textureCache) {
        if (pair.second) {
            pair.second->Release();
        }
    }
    m_textureCache.clear();
    
    if (m_pSprite) {
        m_pSprite->Release();
        m_pSprite = nullptr;
    }
    
    m_pWorldToScreen = nullptr;
    m_pRenderEngine = nullptr;
    m_pDevice = nullptr;
}

bool TextureManager::InitializeSprite() {
    if (!m_pDevice) {
        LOG_ERROR("TextureManager: Device is null, cannot create sprite");
        return false;
    }
    
    HRESULT hr = D3DXCreateSprite(m_pDevice, &m_pSprite);
    if (FAILED(hr) || !m_pSprite) {
        LOG_ERROR("TextureManager: Failed to create D3DX Sprite. HRESULT: " + std::to_string(hr));
        return false;
    }
    
    LOG_INFO("TextureManager: D3DX Sprite created successfully.");
    return true;
}

std::string TextureManager::GetTexturePath(const std::string& textureName) {
    // Get the current module's directory (where our DLL is located)
    char modulePath[MAX_PATH];
    HMODULE hModule = GetModuleHandle(L"WorldToScreenTesting.dll");
    if (!hModule) {
        LOG_DEBUG("TextureManager: Could not get handle to WorldToScreenTesting.dll, trying process module");
        // Fallback: get current process module
        hModule = GetModuleHandle(nullptr);
    }
    
    if (GetModuleFileNameA(hModule, modulePath, MAX_PATH)) {
        // Extract directory from full path
        std::string moduleDir(modulePath);
        LOG_DEBUG("TextureManager: Module full path: " + moduleDir);
        
        size_t lastSlash = moduleDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            moduleDir = moduleDir.substr(0, lastSlash);
        }
        
        LOG_DEBUG("TextureManager: Module directory: " + moduleDir);
        
        // Build path to blps folder: moduleDir/blps/textureName
        std::string texturePath = moduleDir + "\\blps\\" + textureName;
        LOG_DEBUG("TextureManager: Final texture path: " + texturePath);
        return texturePath;
    }
    
    LOG_WARNING("TextureManager: Could not resolve module path, using relative fallback");
    // Fallback: relative path
    return ".\\blps\\" + textureName;
}

IDirect3DTexture9* TextureManager::LoadBlpToDxTexture(const std::string& blpFilePath) {
    if (!m_pDevice) {
        LOG_ERROR("LoadBlpToDxTexture: D3D Device is null.");
        return nullptr;
    }

    LOG_INFO("LoadBlpToDxTexture: Attempting to load BLP file: " + blpFilePath);

    // 1. Read the entire file into a buffer
    std::ifstream file(blpFilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("LoadBlpToDxTexture: Failed to open file: " + blpFilePath);
        return nullptr;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        LOG_ERROR("LoadBlpToDxTexture: Failed to read file: " + blpFilePath);
        return nullptr;
    }
    file.close();

    LOG_DEBUG("LoadBlpToDxTexture: Read " + std::to_string(size) + " bytes from file");

    // 2. Parse the Header
    if (size < sizeof(BLPHeader)) {
        LOG_ERROR("LoadBlpToDxTexture: File too small to contain BLP header: " + blpFilePath);
        return nullptr;
    }

    BLPHeader* header = reinterpret_cast<BLPHeader*>(buffer.data());
    if (header->magic != 0x32504C42) { // "BLP2"
        LOG_ERROR("LoadBlpToDxTexture: Invalid BLP magic (expected BLP2): " + blpFilePath + 
                  " (got 0x" + std::to_string(header->magic) + ")");
        return nullptr;
    }

    LOG_INFO("LoadBlpToDxTexture: BLP Header - Width: " + std::to_string(header->width) + 
             ", Height: " + std::to_string(header->height) + 
             ", Type: " + std::to_string(header->type) + 
             ", Encoding: " + std::to_string(header->encoding) + 
             ", Alpha Depth: " + std::to_string(header->alphaDepth) +
             ", Alpha Encoding: " + std::to_string(header->alphaEncoding) + 
             ", Has Mipmaps: " + std::to_string(header->hasMipmaps));

    // 3. Determine DirectX Format based on encoding
    D3DFORMAT format = D3DFMT_UNKNOWN;
    if (header->type != 1) { // Must be Uncompressed_DirectX
        LOG_ERROR("LoadBlpToDxTexture: Unsupported BLP type: " + std::to_string(header->type));
        return nullptr;
    }
    
    if (header->encoding == 2) { // DXT Compression
        switch (header->alphaEncoding) {
            case 0: format = D3DFMT_DXT1; break;
            case 1: format = D3DFMT_DXT3; break;
            case 7: format = D3DFMT_DXT5; break;
            default:
                LOG_ERROR("LoadBlpToDxTexture: Unknown DXT alpha encoding: " + std::to_string(header->alphaEncoding));
                return nullptr;
        }
    } else if (header->encoding == 1 || header->encoding == 3) { // RAW1 (paletted) or RAW3 (BGRA)
        // For uncompressed BLP, we'll use ARGB format and handle color conversion
        format = D3DFMT_A8R8G8B8;
    } else {
        LOG_ERROR("LoadBlpToDxTexture: Unknown encoding type: " + std::to_string(header->encoding));
        return nullptr;
    }
    
    LOG_DEBUG("LoadBlpToDxTexture: Using D3D format: " + std::to_string(format));

    // 4. Load palette for indexed color modes
    uint32_t* palette = nullptr;
    if (header->encoding == 1 && header->alphaDepth == 8) {
        // Palette is located right after the header (256 colors * 4 bytes = 1024 bytes)
        palette = reinterpret_cast<uint32_t*>(buffer.data() + sizeof(BLPHeader));
        LOG_DEBUG("LoadBlpToDxTexture: Using palette for indexed color mode");
    }

    // 5. Create the DirectX Texture
    IDirect3DTexture9* pTexture = nullptr;
    UINT mipLevels = header->hasMipmaps ? 0 : 1; // 0 means generate all mipmaps

    HRESULT hr = m_pDevice->CreateTexture(
        header->width,
        header->height,
        mipLevels,
        0, // Usage
        format,
        D3DPOOL_MANAGED, // Let D3D manage memory, handles device lost/reset
        &pTexture,
        nullptr
    );

    if (FAILED(hr) || !pTexture) {
        LOG_ERROR("LoadBlpToDxTexture: CreateTexture failed. HRESULT: 0x" + std::to_string(hr));
        return nullptr;
    }

    LOG_DEBUG("LoadBlpToDxTexture: Created DirectX texture successfully");

    // 6. Lock & Copy Mipmap Data with proper color conversion
    UINT numMipsToCopy = header->hasMipmaps ? 16 : 1;
    for (UINT i = 0; i < numMipsToCopy; ++i) {
        if (header->mipmapOffsets[i] == 0 || header->mipmapSizes[i] == 0) {
            LOG_DEBUG("LoadBlpToDxTexture: No more mipmaps at level " + std::to_string(i));
            break; // No more mipmaps
        }

        LOG_DEBUG("LoadBlpToDxTexture: Processing mipmap level " + std::to_string(i) + 
                  " (offset: " + std::to_string(header->mipmapOffsets[i]) + 
                  ", size: " + std::to_string(header->mipmapSizes[i]) + ")");

        D3DLOCKED_RECT lockedRect;
        hr = pTexture->LockRect(i, &lockedRect, nullptr, 0);
        if (FAILED(hr)) {
            LOG_ERROR("LoadBlpToDxTexture: LockRect failed for mipmap " + std::to_string(i) + 
                      " HRESULT: 0x" + std::to_string(hr));
            pTexture->Release();
            return nullptr;
        }
        
        // Pointer to the start of the current mipmap's data in our buffer
        void* pSrcBits = buffer.data() + header->mipmapOffsets[i];
        
        // Handle compressed vs uncompressed data transfer
        if (header->encoding == 2) { // DXT (Compressed)
            memcpy(lockedRect.pBits, pSrcBits, header->mipmapSizes[i]);
            LOG_DEBUG("LoadBlpToDxTexture: Copied " + std::to_string(header->mipmapSizes[i]) + 
                      " bytes of DXT data for mipmap " + std::to_string(i));
        } else { // Uncompressed - needs color conversion and careful row-by-row copy
            UINT mipWidth = std::max(1u, header->width >> i);
            UINT mipHeight = std::max(1u, header->height >> i);
            
            if (header->alphaDepth == 8 && palette) {
                // Indexed color mode - convert palette indices to ARGB
                uint8_t* srcPixels = reinterpret_cast<uint8_t*>(pSrcBits);
                
                for (UINT y = 0; y < mipHeight; ++y) {
                    uint32_t* dstRow = reinterpret_cast<uint32_t*>((BYTE*)lockedRect.pBits + y * lockedRect.Pitch);
                    uint8_t* srcRow = srcPixels + y * mipWidth;
                    
                    for (UINT x = 0; x < mipWidth; ++x) {
                        uint8_t paletteIndex = srcRow[x];
                        uint32_t paletteColor = palette[paletteIndex];
                        
                        // Convert BGRA to ARGB (swap R and B channels)
                        uint8_t b = (paletteColor >> 0) & 0xFF;
                        uint8_t g = (paletteColor >> 8) & 0xFF;
                        uint8_t r = (paletteColor >> 16) & 0xFF;
                        uint8_t a = (paletteColor >> 24) & 0xFF;
                        
                        dstRow[x] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
                LOG_DEBUG("LoadBlpToDxTexture: Converted indexed color data for mipmap " + std::to_string(i) + 
                          " (" + std::to_string(mipWidth) + "x" + std::to_string(mipHeight) + ")");
            } else {
                // Direct color mode - assume BGRA and convert to ARGB
                UINT srcPitch = mipWidth * 4; // 4 bytes per pixel
                
                for (UINT y = 0; y < mipHeight; ++y) {
                    uint32_t* dstRow = reinterpret_cast<uint32_t*>((BYTE*)lockedRect.pBits + y * lockedRect.Pitch);
                    uint32_t* srcRow = reinterpret_cast<uint32_t*>((BYTE*)pSrcBits + y * srcPitch);
                    
                    for (UINT x = 0; x < mipWidth; ++x) {
                        uint32_t srcPixel = srcRow[x];
                        
                        // Convert BGRA to ARGB (swap R and B channels)
                        uint8_t b = (srcPixel >> 0) & 0xFF;
                        uint8_t g = (srcPixel >> 8) & 0xFF;
                        uint8_t r = (srcPixel >> 16) & 0xFF;
                        uint8_t a = (srcPixel >> 24) & 0xFF;
                        
                        dstRow[x] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
                LOG_DEBUG("LoadBlpToDxTexture: Converted direct color data for mipmap " + std::to_string(i) + 
                          " (" + std::to_string(mipWidth) + "x" + std::to_string(mipHeight) + ")");
            }
        }
        
        pTexture->UnlockRect(i);
    }
    
    LOG_INFO("Successfully created DirectX texture from BLP: " + blpFilePath);
    return pTexture;
}

IDirect3DTexture9* TextureManager::LoadTextureFromFile(const std::string& textureName) {
    if (!m_pDevice || textureName.empty()) {
        return nullptr;
    }

    // Get the full path to the texture in the blps folder
    std::string basePath = GetTexturePath(textureName);
    LOG_INFO("TextureManager: Attempting to load texture: " + textureName);
    LOG_INFO("TextureManager: Base path resolved to: " + basePath);
    
    IDirect3DTexture9* pTexture = nullptr;
    HRESULT hr;
    
    // Check if this is a BLP file and use our custom loader
    if (textureName.find(".BLP") != std::string::npos || textureName.find(".blp") != std::string::npos) {
        LOG_INFO("TextureManager: Detected BLP file, using custom BLP loader");
        pTexture = LoadBlpToDxTexture(basePath);
        if (pTexture) {
            LOG_INFO("Successfully loaded BLP texture: " + basePath);
            return pTexture;
        } else {
            LOG_WARNING("Failed to load BLP file, trying converted alternatives");
            
            // Try to find a converted version of the BLP file
            std::vector<std::string> blpAlternatives = { 
                basePath.substr(0, basePath.find_last_of('.')) + ".png",
                basePath.substr(0, basePath.find_last_of('.')) + ".dds",
                basePath.substr(0, basePath.find_last_of('.')) + ".bmp"
            };
            
            for (const auto& altPath : blpAlternatives) {
                LOG_DEBUG("TextureManager: Trying converted BLP alternative: " + altPath);
                hr = D3DXCreateTextureFromFileA(m_pDevice, altPath.c_str(), &pTexture);
                if (SUCCEEDED(hr) && pTexture) {
                    LOG_INFO("Successfully loaded converted BLP texture: " + altPath);
                    return pTexture;
                }
            }
        }
    } else {
        // Try loading the file as-is first (for files with extensions)
        LOG_DEBUG("TextureManager: Trying direct load: " + basePath);
        hr = D3DXCreateTextureFromFileA(m_pDevice, basePath.c_str(), &pTexture);
        
        if (SUCCEEDED(hr) && pTexture) {
            LOG_INFO("Successfully loaded texture: " + basePath);
            return pTexture;
        } else {
            LOG_DEBUG("TextureManager: Direct load failed, HRESULT: 0x" + std::to_string(hr));
        }
    }
    
    // If original file loading failed, try with common image extensions
    std::vector<std::string> extensions = { ".png", ".dds", ".bmp", ".jpg", ".tga" };
    
    for (const auto& ext : extensions) {
        std::string fullPath = basePath + ext;
        LOG_DEBUG("TextureManager: Trying with extension: " + fullPath);
        hr = D3DXCreateTextureFromFileA(m_pDevice, fullPath.c_str(), &pTexture);
        
        if (SUCCEEDED(hr) && pTexture) {
            LOG_INFO("Successfully loaded texture: " + fullPath);
            return pTexture;
        } else {
            LOG_DEBUG("TextureManager: Extension attempt failed, HRESULT: 0x" + std::to_string(hr));
        }
    }
    
    // If we can't load from file, create a simple colored texture as fallback
    LOG_WARNING("Could not load texture file for: " + textureName + " from any attempted path, creating fallback texture");
    return CreateFallbackTexture();
}

IDirect3DTexture9* TextureManager::CreateFallbackTexture() {
    if (!m_pDevice) return nullptr;
    
    IDirect3DTexture9* pTexture = nullptr;
    HRESULT hr = m_pDevice->CreateTexture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, nullptr);
    
    if (FAILED(hr) || !pTexture) {
        LOG_ERROR("Failed to create fallback texture. HRESULT: " + std::to_string(hr));
        return nullptr;
    }
    
    // Fill with a simple pattern
    D3DLOCKED_RECT lockedRect;
    hr = pTexture->LockRect(0, &lockedRect, nullptr, 0);
    if (SUCCEEDED(hr)) {
        DWORD* pixels = static_cast<DWORD*>(lockedRect.pBits);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                // Create a simple checkerboard pattern
                bool checker = ((x / 8) + (y / 8)) % 2;
                pixels[y * 32 + x] = checker ? 0xFFFF0000 : 0xFF00FF00; // Red/Green checkerboard
            }
        }
        pTexture->UnlockRect(0);
    }
    
    LOG_DEBUG("Created fallback checkerboard texture");
    return pTexture;
}

void TextureManager::PopulateCommonTextures() {
    m_commonTexturePaths.clear();
    
    std::string targetPath = GetTargetBlpsPath();
    if (targetPath.empty()) {
        LOG_ERROR("TextureManager: Could not get target blps path");
        return;
    }
    
    LOG_DEBUG("TextureManager: Scanning for textures in: " + targetPath);
    
    // Find all texture files in the target directory
    WIN32_FIND_DATAA findData;
    std::string searchPattern = targetPath + "\\*.*";
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        LOG_WARNING("TextureManager: Could not scan directory: " + targetPath + " (Error: " + std::to_string(error) + ")");
        return;
    }
    
    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue; // Skip directories
        }
        
        std::string fileName = findData.cFileName;
        if (fileName.find('.') == std::string::npos) {
            continue; // Skip files without extensions
        }
        
        std::string extension = fileName.substr(fileName.find_last_of('.'));
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        
        // Only include supported texture formats
        if (extension == ".blp" || extension == ".png" || extension == ".bmp" || 
            extension == ".dds" || extension == ".jpg" || extension == ".jpeg") {
            m_commonTexturePaths.push_back(fileName);
            LOG_DEBUG("TextureManager: Found texture file: " + fileName);
        }
        
    } while (FindNextFileA(hFind, &findData));
    
    FindClose(hFind);
    
    // Sort alphabetically for better UI experience
    std::sort(m_commonTexturePaths.begin(), m_commonTexturePaths.end());
    
    LOG_INFO("TextureManager: Found " + std::to_string(m_commonTexturePaths.size()) + " texture files in " + targetPath);
}

IDirect3DTexture9* TextureManager::GetTexture(const std::string& textureName) {
    auto it = m_textureCache.find(textureName);
    if (it != m_textureCache.end()) {
        return it->second;
    }
    
    IDirect3DTexture9* pTexture = LoadTextureFromFile(textureName);
    m_textureCache[textureName] = pTexture; // Cache even if nullptr to avoid repeated attempts
    
    return pTexture;
}

void TextureManager::PreloadTexture(const std::string& textureName) {
    GetTexture(textureName);
}

std::string TextureManager::GetSourceBlpsPath() {
    // Get the path to the source core/blps folder
    char modulePath[MAX_PATH];
    HMODULE hModule = GetModuleHandle(L"WorldToScreenTesting.dll");
    if (!hModule) {
        hModule = GetModuleHandle(nullptr);
    }
    
    if (!GetModuleFileNameA(hModule, modulePath, MAX_PATH)) {
        LOG_ERROR("TextureManager: Failed to get module path");
        return "";
    }
    
    std::string moduleDir(modulePath);
    size_t lastSlash = moduleDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        moduleDir = moduleDir.substr(0, lastSlash);
    }
    
    // Navigate up to project root and then to core/blps
    // From build/bin/Debug -> go up 3 levels to project root, then core/blps
    std::string projectRoot = moduleDir + "\\..\\..\\..";
    std::string sourceBlpsPath = projectRoot + "\\core\\blps";
    
    // Normalize the path
    char fullPath[MAX_PATH];
    if (!GetFullPathNameA(sourceBlpsPath.c_str(), MAX_PATH, fullPath, nullptr)) {
        LOG_ERROR("TextureManager: Failed to normalize source path: " + sourceBlpsPath);
        return "";
    }
    
    return std::string(fullPath);
}

std::string TextureManager::GetTargetBlpsPath() {
    // Get current module directory and append blps
    char modulePath[MAX_PATH];
    HMODULE hModule = GetModuleHandle(L"WorldToScreenTesting.dll");
    if (!hModule) {
        hModule = GetModuleHandle(nullptr);
    }
    
    if (!GetModuleFileNameA(hModule, modulePath, MAX_PATH)) {
        LOG_ERROR("TextureManager: Failed to get module path for target");
        return "";
    }
    
    std::string moduleDir(modulePath);
    size_t lastSlash = moduleDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        moduleDir = moduleDir.substr(0, lastSlash);
    }
    
    return moduleDir + "\\blps";
}

bool TextureManager::HotReloadTexturesFromSource() {
    std::string sourcePath = GetSourceBlpsPath();
    std::string targetPath = GetTargetBlpsPath();
    
    LOG_INFO("TextureManager: Hot reload - Source: " + sourcePath);
    LOG_INFO("TextureManager: Hot reload - Target: " + targetPath);
    
    // Create target directory if it doesn't exist
    CreateDirectoryA(targetPath.c_str(), nullptr);
    
    // Find all files in source directory
    WIN32_FIND_DATAA findData;
    std::string searchPattern = sourcePath + "\\*.*";
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_ERROR("TextureManager: Hot reload failed - Cannot access source directory: " + sourcePath);
        return false;
    }
    
    int copiedFiles = 0;
    int skippedFiles = 0;
    
    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue; // Skip directories
        }
        
        std::string fileName = findData.cFileName;
        std::string sourceFile = sourcePath + "\\" + fileName;
        std::string targetFile = targetPath + "\\" + fileName;
        
        // Check if file is a supported texture format
        std::string extension = fileName.substr(fileName.find_last_of('.'));
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        
        if (extension == ".blp" || extension == ".png" || extension == ".bmp" || 
            extension == ".dds" || extension == ".jpg" || extension == ".jpeg") {
            
            // Copy file
            if (CopyFileA(sourceFile.c_str(), targetFile.c_str(), FALSE)) {
                LOG_DEBUG("TextureManager: Hot reload copied: " + fileName);
                copiedFiles++;
                
                // Clear this texture from cache so it gets reloaded
                auto it = m_textureCache.find(fileName);
                if (it != m_textureCache.end()) {
                    if (it->second) {
                        it->second->Release();
                    }
                    m_textureCache.erase(it);
                }
            } else {
                LOG_WARNING("TextureManager: Hot reload failed to copy: " + fileName);
            }
        } else {
            skippedFiles++;
        }
        
    } while (FindNextFileA(hFind, &findData));
    
    FindClose(hFind);
    
    LOG_INFO("TextureManager: Hot reload complete - Copied: " + std::to_string(copiedFiles) + 
             ", Skipped: " + std::to_string(skippedFiles) + " files");
    
    // Update common texture paths to include newly found files
    PopulateCommonTextures();
    
    return copiedFiles > 0;
}

int TextureManager::AddTextureAtPosition(const std::string& textureName, const D3DXVECTOR3& worldPos, 
                                       float size, D3DCOLOR color, const std::string& label) {
    if (!m_settings.enableTextureRendering || !m_pWorldToScreen || !m_pSprite) {
        return -1;
    }
    
    IDirect3DTexture9* pTexture = GetTexture(textureName);
    
    TextureRenderData renderData;
    renderData.pD3DTexture = pTexture;
    renderData.worldPos = worldPos;
    renderData.size = D3DXVECTOR2(size, size);
    renderData.color = color;
    renderData.isVisible = true;
    renderData.id = m_nextId++;
    renderData.label = label.empty() ? ("Texture" + std::to_string(renderData.id)) : label;
    renderData.texturePath = textureName;
    renderData.scale = m_settings.defaultTextureScale;
    renderData.billboarded = m_settings.billboardTextures;
    
    if (pTexture) {
        LOG_DEBUG("TextureManager: Added texture for rendering: " + textureName);
    } else {
        LOG_DEBUG("TextureManager: Added texture placeholder (no file found): " + textureName);
    }
    
    m_renderTextures.push_back(renderData);
    return renderData.id;
}

void TextureManager::RemoveTexture(int id) {
    m_renderTextures.erase(
        std::remove_if(m_renderTextures.begin(), m_renderTextures.end(),
            [id](const TextureRenderData& texture) { return texture.id == id; }),
        m_renderTextures.end()
    );
}

void TextureManager::ClearAllTextures() {
    m_renderTextures.clear();
    LOG_DEBUG("TextureManager: All render textures cleared");
}

bool TextureManager::UpdateTextureScale(int id, float newScale) {
    for (auto& texture : m_renderTextures) {
        if (texture.id == id) {
            texture.scale = newScale;
            LOG_DEBUG("TextureManager: Updated texture " + std::to_string(id) + " scale to " + std::to_string(newScale));
            return true;
        }
    }
    return false;
}

bool TextureManager::UpdateTexturePosition(int id, const D3DXVECTOR3& newPos) {
    for (auto& texture : m_renderTextures) {
        if (texture.id == id) {
            texture.worldPos = newPos;
            LOG_DEBUG("TextureManager: Updated texture " + std::to_string(id) + " position");
            return true;
        }
    }
    return false;
}

bool TextureManager::UpdateTextureColor(int id, D3DCOLOR newColor) {
    for (auto& texture : m_renderTextures) {
        if (texture.id == id) {
            texture.color = newColor;
            LOG_DEBUG("TextureManager: Updated texture " + std::to_string(id) + " color");
            return true;
        }
    }
    return false;
}

bool TextureManager::PassesSearchFilter(const std::string& textureName) const {
    if (m_settings.searchFilter.empty()) {
        return true;
    }
    
    std::string lowerName = textureName;
    std::string lowerFilter = m_settings.searchFilter;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
    
    return lowerName.find(lowerFilter) != std::string::npos;
}

std::vector<std::string> TextureManager::SearchTextures(const std::string& filter) const {
    std::vector<std::string> results;
    
    for (const auto& textureName : m_commonTexturePaths) {
        if (filter.empty() || PassesSearchFilter(textureName)) {
            results.push_back(textureName);
        }
    }
    
    return results;
}

void TextureManager::UpdateTextureVisibility() {
    if (!m_pWorldToScreen) return;
    
    C3Vector playerPosC3;
    if (!m_pWorldToScreen->GetPlayerPositionSafe(playerPosC3)) return;
    D3DXVECTOR3 playerPos(playerPosC3.x, playerPosC3.y, playerPosC3.z);
    
    for (auto& texture : m_renderTextures) {
        D3DXVECTOR2 screenPos;
        texture.isVisible = m_pWorldToScreen->WorldToScreen(texture.worldPos, screenPos);
        
        if (texture.isVisible) {
            texture.screenPos = screenPos;
            if (m_settings.onlyShowInRange) {
                float distance = D3DXVec3Length(&(D3DXVECTOR3(texture.worldPos - playerPos)));
                if (distance > m_settings.maxRenderDistance) {
                    texture.isVisible = false;
                }
            }
        }
    }
}

void TextureManager::Update() {
    if (!m_settings.enableTextureRendering) return;
    
    UpdateTextureVisibility();
}

void TextureManager::Render() {
    if (!m_settings.enableTextureRendering || !m_pSprite || !m_pDevice || m_renderTextures.empty()) return;

    if (m_pDevice->TestCooperativeLevel() != D3D_OK) return;

    if (FAILED(m_pSprite->Begin(D3DXSPRITE_ALPHABLEND))) {
        LOG_WARNING("TextureManager: m_pSprite->Begin() failed.");
        return;
    }

    D3DXMATRIX identity;
    D3DXMatrixIdentity(&identity);
    m_pSprite->SetTransform(&identity);

    for (const auto& texture : m_renderTextures) {
        if (!texture.isVisible || !texture.pD3DTexture) continue;

        D3DSURFACE_DESC desc;
        if (FAILED(texture.pD3DTexture->GetLevelDesc(0, &desc))) continue;

        // Apply scaling to the texture dimensions
        float scaledWidth = desc.Width * texture.scale;
        float scaledHeight = desc.Height * texture.scale;

        RECT sourceRect = { 0, 0, (LONG)desc.Width, (LONG)desc.Height };
        
        // Use a simpler approach: scale around the center point
        D3DXMATRIX scaleMatrix;
        D3DXMatrixScaling(&scaleMatrix, texture.scale, texture.scale, 1.0f);
        m_pSprite->SetTransform(&scaleMatrix);
        
        // Calculate position to keep texture centered at the screen position
        // When scaling, we need to adjust the position to account for the scale
        D3DXVECTOR3 position(
            (texture.screenPos.x - (scaledWidth / 2.0f)) / texture.scale,
            (texture.screenPos.y - (scaledHeight / 2.0f)) / texture.scale,
            0.0f
        );

        m_pSprite->Draw(texture.pD3DTexture, &sourceRect, nullptr, &position, texture.color);

        // Reset transform for text rendering
        D3DXMATRIX identity;
        D3DXMatrixIdentity(&identity);
        m_pSprite->SetTransform(&identity);

        if (m_settings.showTextureLabels && !texture.label.empty() && m_pRenderEngine) {
            D3DXVECTOR2 labelPos = texture.screenPos;
            labelPos.y += (scaledHeight / 2.0f) + 5; // Use scaled height for label positioning
            m_pRenderEngine->DrawText(texture.label, labelPos, 0xFFFFFFFF, 1.0f);
        }
    }

    m_pSprite->End();
}

void TextureManager::OnDeviceLost() {
    if (m_pSprite) m_pSprite->OnLostDevice();
}

void TextureManager::OnDeviceReset() {
    if (m_pSprite) m_pSprite->OnResetDevice();
} 
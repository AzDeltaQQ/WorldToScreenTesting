#pragma once

#include "../../combat/CombatLogManager.h"
#include "../../combat/CombatLogAnalyzer.h"
#include "../../objects/ObjectManager.h"
#include "../../../dependencies/ImGui/imgui.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace GUI {

class CombatLogTab {
public:
    CombatLogTab();
    ~CombatLogTab();

    // Core tab interface
    void Render();
    void Update(float deltaTime);
    
    // Setup
    void SetObjectManager(ObjectManager* objManager) { m_objectManager = objManager; }
    
    // Tab state
    bool IsEnabled() const { return m_isEnabled; }
    void SetEnabled(bool enabled) { m_isEnabled = enabled; }

private:
    // Rendering sections
    void RenderControlPanel();
    void RenderSessionManagement();
    void RenderFilterControls();
    void RenderLogViewer();
    void RenderAnalysisPanel();
    void RenderStatistics();
    void RenderTimeline();
    void RenderExportOptions();
    void RenderSettings();
    
    // Analysis rendering
    void RenderOverallStats(const CombatAnalysis& analysis);
    void RenderParticipantBreakdown(const CombatAnalysis& analysis);
    void RenderSpellAnalysis();
    void RenderDamageChart();
    void RenderHealingChart();
    void RenderTimelineChart(const TimelineData& timeline);
    
    // Helper methods
    void UpdateAnalysis();
    void RefreshSessionData();
    void ApplyFilters();
    std::string FormatLogEntry(const CombatLogEntry& entry);
    ImVec4 GetEventTypeColor(CombatEventType eventType);
    void RenderTooltip(const std::string& text);
    
    // Data management
    void LoadSession(size_t sessionIndex);
    void ExportCurrentData();
    void ClearCurrentData();
    
    // UI state management
    struct UIState {
        // Tab sections
        bool showControlPanel = true;
        bool showLogViewer = true;
        bool showAnalysis = true;
        bool showStatistics = true;
        bool showTimeline = false;
        bool showSettings = false;
        
        // Analysis view modes
        int analysisViewMode = 0; // 0=Overview, 1=Participants, 2=Spells, 3=Timeline
        int selectedParticipant = -1;
        int selectedSpell = -1;
        
        // Log viewer
        bool autoScroll = true;
        bool showTimestamps = true;
        bool showGUIDs = false;
        int maxDisplayedEntries = 1000;
        float logTextScale = 1.0f;
        
        // Timeline
        bool showDpsLine = true;
        bool showHpsLine = true;
        bool showDamagePoints = false;
        bool showHealingPoints = false;
        
        // Filter panel
        bool showAdvancedFilters = false;
        
        // Export
        int exportFormat = 0; // 0=CSV, 1=JSON, 2=XML
        std::string exportPath = "combat_log_export";
    };
    
    // Filter management
    struct FilterState {
        CombatLogFilter filter;
        
        // UI controls
        char sourceNameBuffer[256] = "";
        char targetNameBuffer[256] = "";
        char spellNameBuffer[256] = "";
        
        // Event type checkboxes
        bool showDamage = true;
        bool showHealing = true;
        bool showCasts = false;
        bool showAuras = false;
        bool showMisses = false;
        bool showExperience = false;
        bool showHonor = false;
        
        // Amount filters
        bool useAmountFilter = false;
        int minAmount = 0;
        int maxAmount = 999999;
        
        // Time filters
        bool useTimeFilter = false;
        std::chrono::steady_clock::time_point filterStartTime;
        std::chrono::steady_clock::time_point filterEndTime;
        
        // Quick filters
        bool onlyLocalPlayer = false;
        bool onlyTargetedEntities = false;
        bool onlyCombatEvents = true;
    };
    
    // Chart data for ImPlot (if available) or simple line graphs
    struct ChartData {
        std::vector<float> timePoints;
        std::vector<float> dpsValues;
        std::vector<float> hpsValues;
        std::vector<float> damageValues;
        std::vector<float> healingValues;
        
        void Clear() {
            timePoints.clear();
            dpsValues.clear();
            hpsValues.clear();
            damageValues.clear();
            healingValues.clear();
        }
        
        void UpdateFromTimeline(const TimelineData& timeline);
    };
    
    // Session data cache
    struct SessionCache {
        std::shared_ptr<CombatSession> currentSession;
        std::vector<std::shared_ptr<CombatLogEntry>> filteredEntries;
        CombatAnalysis analysis;
        std::vector<SpellAnalysis> spellAnalyses;
        bool isAnalysisValid = false;
        bool needsRefresh = true;
        
        void Invalidate() {
            isAnalysisValid = false;
            needsRefresh = true;
        }
    };
    
    // Performance tracking
    struct PerformanceMetrics {
        float updateTimeMs = 0.0f;
        float renderTimeMs = 0.0f;
        float analysisTimeMs = 0.0f;
        size_t totalEntries = 0;
        size_t filteredEntries = 0;
        float memoryUsageMB = 0.0f;
        
        // Frame timing
        std::chrono::steady_clock::time_point lastUpdateTime;
        std::chrono::steady_clock::time_point lastRenderTime;
    };
    
    // Member variables
    ObjectManager* m_objectManager = nullptr;
    CombatLogManager* m_combatLogManager = nullptr;
    
    // UI state
    UIState m_uiState;
    FilterState m_filterState;
    SessionCache m_sessionCache;
    ChartData m_chartData;
    PerformanceMetrics m_performance;
    
    // Update timing
    float m_updateTimer = 0.0f;
    float m_refreshInterval = 0.5f; // Refresh every 500ms
    
    // Selection state
    std::vector<bool> m_selectedEntries;
    int m_selectedEntry = -1;
    
    // Scroll state
    bool m_shouldScrollToBottom = false;
    
    // Status
    bool m_isEnabled = true;
    bool m_isInitialized = false;
    std::string m_lastError;
    
    // Color themes for different elements
    static const ImVec4 COLOR_DAMAGE;
    static const ImVec4 COLOR_HEALING;
    static const ImVec4 COLOR_MISS;
    static const ImVec4 COLOR_CRITICAL;
    static const ImVec4 COLOR_EXPERIENCE;
    static const ImVec4 COLOR_HONOR;
    static const ImVec4 COLOR_AURA;
    static const ImVec4 COLOR_BACKGROUND_ALT;
    
    // Constants
    static const size_t MAX_LOG_ENTRIES_DISPLAY = 10000;
    static const float CHART_HEIGHT;
    static const float SIDEBAR_WIDTH;
};

} // namespace GUI 
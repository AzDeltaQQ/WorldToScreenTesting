#include "CombatLogTab.h"
#include "../../combat/CombatLogManager.h"
#include "../../combat/CombatLogAnalyzer.h"
#include "../../objects/ObjectManager.h"
#include <imgui.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace GUI {

// Static member definitions
const ImVec4 CombatLogTab::COLOR_DAMAGE = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
const ImVec4 CombatLogTab::COLOR_HEALING = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
const ImVec4 CombatLogTab::COLOR_MISS = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
const ImVec4 CombatLogTab::COLOR_CRITICAL = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
const ImVec4 CombatLogTab::COLOR_EXPERIENCE = ImVec4(0.4f, 0.4f, 1.0f, 1.0f);
const ImVec4 CombatLogTab::COLOR_HONOR = ImVec4(1.0f, 0.4f, 1.0f, 1.0f);
const ImVec4 CombatLogTab::COLOR_AURA = ImVec4(0.8f, 0.6f, 1.0f, 1.0f);
const ImVec4 CombatLogTab::COLOR_BACKGROUND_ALT = ImVec4(0.1f, 0.1f, 0.1f, 0.5f);
const float CombatLogTab::CHART_HEIGHT = 200.0f;
const float CombatLogTab::SIDEBAR_WIDTH = 300.0f;

CombatLogTab::CombatLogTab() {
    m_combatLogManager = &CombatLogManager::GetInstance();
    m_isInitialized = true;
    
    // Initialize filter defaults
    m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_DAMAGE);
    m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_HEAL);
    m_filterState.filter.allowedEventTypes.insert(CombatEventType::MELEE_DAMAGE);
    m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_CAST_SUCCESS);
    m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_CAST_START);
    m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_CAST_FAILED);
    m_filterState.filter.allowedEventTypes.insert(CombatEventType::UNKNOWN); // For debugging
    
    LOG_INFO("CombatLogTab initialized");
}

CombatLogTab::~CombatLogTab() {
    LOG_INFO("CombatLogTab destroyed");
}

void CombatLogTab::Update(float deltaTime) {
    if (!m_isEnabled || !m_isInitialized) {
        return;
    }
    
    auto updateStart = std::chrono::steady_clock::now();
    
    m_updateTimer += deltaTime;
    
    // Trigger automatic combat log reading if capture is active (but throttled)
    static float combatLogUpdateTimer = 0.0f;
    static const float COMBAT_LOG_UPDATE_INTERVAL = 0.1f; // Read every 100ms instead of every frame
    
    if (m_combatLogManager->IsCapturing() && m_combatLogManager->IsWowMemoryReadingActive()) {
        combatLogUpdateTimer += deltaTime;
        
        if (combatLogUpdateTimer >= COMBAT_LOG_UPDATE_INTERVAL) {
            try {
                m_combatLogManager->UpdateCombatLogReading();
                combatLogUpdateTimer = 0.0f;
            } catch (const std::exception& e) {
                LOG_ERROR("CombatLogTab::Update: Exception during automatic reading: " + std::string(e.what()));
                // Stop automatic reading on repeated failures
                static int consecutiveFailures = 0;
                consecutiveFailures++;
                if (consecutiveFailures >= 5) {
                    LOG_ERROR("CombatLogTab::Update: Too many consecutive failures, stopping automatic reading");
                    m_combatLogManager->StopWowMemoryReading();
                    consecutiveFailures = 0;
                }
            }
        }
    } else {
        combatLogUpdateTimer = 0.0f; // Reset timer when not capturing
    }
    
    // Update session cache periodically
    if (m_updateTimer >= m_refreshInterval || m_sessionCache.needsRefresh) {
        RefreshSessionData();
        m_updateTimer = 0.0f;
    }
    
    // Update performance metrics
    auto updateEnd = std::chrono::steady_clock::now();
    m_performance.updateTimeMs = std::chrono::duration<float, std::milli>(updateEnd - updateStart).count();
    m_performance.lastUpdateTime = updateEnd;
}

void CombatLogTab::Render() {
    if (!m_isEnabled || !m_isInitialized) {
        return;
    }
    
    auto renderStart = std::chrono::steady_clock::now();
    
    // Remove the nested tab - we're already inside the Combat Log tab from GUI.cpp
    // Main layout with splitter
    ImGui::BeginChild("CombatLogMain", ImVec2(0, 0), false);
        
        // Top control panel
        if (m_uiState.showControlPanel) {
            RenderControlPanel();
            ImGui::Separator();
        }
        
        // Main content area with horizontal split
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        
        // Use a static variable to maintain sidebar width between frames
        static float sidebarWidth = std::min(SIDEBAR_WIDTH, contentSize.x * 0.3f);
        
        // Clamp sidebar width to reasonable bounds
        sidebarWidth = std::max(200.0f, std::min(sidebarWidth, contentSize.x - 200.0f));
        
        // Left sidebar - Session management and filters
        ImGui::BeginChild("CombatLogSidebar", ImVec2(sidebarWidth, 0), true);
        {
            RenderSessionManagement();
            ImGui::Separator();
            RenderFilterControls();
            ImGui::Separator();
            RenderSettings();
        }
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Splitter
        ImGui::Button("##splitter", ImVec2(8.0f, -1));
        if (ImGui::IsItemActive()) {
            sidebarWidth += ImGui::GetIO().MouseDelta.x;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        
        ImGui::SameLine();
        
        // Right main area - Log viewer and analysis
        ImGui::BeginChild("CombatLogContent", ImVec2(0, 0), false);
        {
            // Tab bar for different views
            if (ImGui::BeginTabBar("CombatLogViews")) {
                
                if (ImGui::BeginTabItem("Live Log")) {
                    RenderLogViewer();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Analysis")) {
                    RenderAnalysisPanel();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Statistics")) {
                    RenderStatistics();
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Timeline")) {
                    m_uiState.showTimeline = true;
                    RenderTimeline();
                    ImGui::EndTabItem();
                } else {
                    m_uiState.showTimeline = false;
                }
                
                if (ImGui::BeginTabItem("Export")) {
                    RenderExportOptions();
                    ImGui::EndTabItem();
                }
                
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
    
    ImGui::EndChild();
    
    // Update performance metrics
    auto renderEnd = std::chrono::steady_clock::now();
    m_performance.renderTimeMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
    m_performance.lastRenderTime = renderEnd;
}

void CombatLogTab::RenderControlPanel() {
    ImGui::Text("Combat Log Control Panel");
    
    // Capture controls
    ImGui::BeginGroup();
    {
        bool isCapturing = m_combatLogManager->IsCapturing();
        if (isCapturing) {
            if (ImGui::Button("Stop Capture", ImVec2(100, 0))) {
                m_combatLogManager->StopCapture();
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "CAPTURING");
        } else {
            if (ImGui::Button("Start Capture", ImVec2(100, 0))) {
                m_combatLogManager->StartCapture();
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "STOPPED");
        }
        
        ImGui::SameLine();
        if (ImGui::Button("New Session", ImVec2(100, 0))) {
            m_combatLogManager->StartNewSession();
            m_sessionCache.Invalidate();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Clear All", ImVec2(100, 0))) {
            m_combatLogManager->ClearAllSessions();
            m_sessionCache.Invalidate();
            // Clear the live log display as well
            m_sessionCache.filteredEntries.clear();
            m_sessionCache.currentSession.reset();
            m_sessionCache.needsRefresh = true;
        }
    }
    ImGui::EndGroup();
    
    // Debug controls
    ImGui::Separator();
    ImGui::Text("Debug Controls");
    ImGui::BeginGroup();
    {        
        if (ImGui::Button("Manual Read", ImVec2(100, 0))) {
            m_combatLogManager->TriggerManualRead();
        }
        
        // Logger level control
        ImGui::SameLine();
        static int logLevel = static_cast<int>(LogLevel::INFO);
        const char* logLevelNames[] = { "DEBUG", "INFO", "WARNING", "ERROR" };
        ImGui::SetNextItemWidth(80);
        if (ImGui::Combo("Log Level", &logLevel, logLevelNames, 4)) {
            Logger::GetInstance()->SetLogLevel(static_cast<LogLevel>(logLevel));
        }
    }
    ImGui::EndGroup();
    
    ImGui::SameLine();
    
    // Status information
    ImGui::BeginGroup();
    {
        bool isInCombat = m_combatLogManager->IsInCombat();
        if (isInCombat) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "IN COMBAT");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Out of Combat");
        }
        
        ImGui::Text("Total Entries: %zu", m_combatLogManager->GetTotalEntryCount());
        ImGui::Text("Sessions: %zu", m_combatLogManager->GetSessionCount());
        
        if (m_sessionCache.currentSession) {
            double duration = m_sessionCache.currentSession->GetDurationSeconds();
            ImGui::Text("Session Duration: %.1fs", duration);
        }
    }
    ImGui::EndGroup();
    
    ImGui::SameLine();
    
    // Performance metrics
    ImGui::BeginGroup();
    {
        ImGui::Text("Performance:");
        ImGui::Text("Update: %.2fms", m_performance.updateTimeMs);
        ImGui::Text("Render: %.2fms", m_performance.renderTimeMs);
        ImGui::Text("Memory: %.1fMB", m_performance.memoryUsageMB);
    }
    ImGui::EndGroup();
}

void CombatLogTab::RenderSessionManagement() {
    if (ImGui::CollapsingHeader("Sessions", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        size_t sessionCount = m_combatLogManager->GetSessionCount();
        
        if (sessionCount == 0) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No sessions");
            return;
        }
        
        // Session list
        ImGui::BeginChild("SessionList", ImVec2(0, 150), true);
        
        for (size_t i = 0; i < sessionCount; ++i) {
            auto session = m_combatLogManager->GetSession(i);
            if (!session) continue;
            
            bool isSelected = (m_sessionCache.currentSession == session);
            bool isCurrent = session->isActive;
            
            std::string sessionName = "Session " + std::to_string(i + 1);
            if (isCurrent) {
                sessionName += " (Active)";
            }
            
            ImGui::PushID(static_cast<int>(i));
            
            if (ImGui::Selectable(sessionName.c_str(), isSelected)) {
                LoadSession(i);
            }
            
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Entries: %zu", session->entries.size());
                ImGui::Text("Duration: %.1fs", session->GetDurationSeconds());
                ImGui::Text("Participants: %zu", session->participantNames.size());
                ImGui::EndTooltip();
            }
            
            ImGui::PopID();
        }
        
        ImGui::EndChild();
    }
}

void CombatLogTab::RenderFilterControls() {
    if (ImGui::CollapsingHeader("Filters", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        bool filtersChanged = false;
        
        // Event type filters
        ImGui::Text("Event Types:");
        filtersChanged |= ImGui::Checkbox("Damage", &m_filterState.showDamage);
        ImGui::SameLine();
        filtersChanged |= ImGui::Checkbox("Healing", &m_filterState.showHealing);
        filtersChanged |= ImGui::Checkbox("Casts", &m_filterState.showCasts);
        ImGui::SameLine();
        filtersChanged |= ImGui::Checkbox("Auras", &m_filterState.showAuras);
        filtersChanged |= ImGui::Checkbox("Misses", &m_filterState.showMisses);
        ImGui::SameLine();
        filtersChanged |= ImGui::Checkbox("XP/Honor", &m_filterState.showExperience);
        
        ImGui::Separator();
        
        // Name filters
        ImGui::Text("Name Filters:");
        if (ImGui::InputText("Source", m_filterState.sourceNameBuffer, sizeof(m_filterState.sourceNameBuffer))) {
            filtersChanged = true;
        }
        if (ImGui::InputText("Target", m_filterState.targetNameBuffer, sizeof(m_filterState.targetNameBuffer))) {
            filtersChanged = true;
        }
        if (ImGui::InputText("Spell", m_filterState.spellNameBuffer, sizeof(m_filterState.spellNameBuffer))) {
            filtersChanged = true;
        }
        
        ImGui::Separator();
        
        // Amount filters
        filtersChanged |= ImGui::Checkbox("Amount Filter", &m_filterState.useAmountFilter);
        if (m_filterState.useAmountFilter) {
            ImGui::SetNextItemWidth(100);
            filtersChanged |= ImGui::InputInt("Min", &m_filterState.minAmount);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            filtersChanged |= ImGui::InputInt("Max", &m_filterState.maxAmount);
        }
        
        ImGui::Separator();
        
        // Quick filters
        ImGui::Text("Quick Filters:");
        filtersChanged |= ImGui::Checkbox("Only Local Player", &m_filterState.onlyLocalPlayer);
        filtersChanged |= ImGui::Checkbox("Only Combat Events", &m_filterState.onlyCombatEvents);
        
        if (filtersChanged) {
            ApplyFilters();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Reset Filters", ImVec2(-1, 0))) {
            // Reset filter state
            m_filterState = FilterState();
            ApplyFilters();
        }
    }
}

void CombatLogTab::RenderLogViewer() {
    ImGui::BeginChild("LogViewerContent", ImVec2(0, 0), false);
    
    // Log viewer controls
    ImGui::BeginGroup();
    {
        ImGui::Checkbox("Auto Scroll", &m_uiState.autoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Show Timestamps", &m_uiState.showTimestamps);
        ImGui::SameLine();
        ImGui::Checkbox("Show GUIDs", &m_uiState.showGUIDs);
        
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Text Scale", &m_uiState.logTextScale, 0.5f, 2.0f, "%.1f");
        
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::SliderInt("Max Entries", &m_uiState.maxDisplayedEntries, 100, 5000);
    }
    ImGui::EndGroup();
    
    ImGui::Separator();
    
    // Log entries
    ImGui::BeginChild("LogEntries", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    if (m_sessionCache.filteredEntries.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No entries to display");
    } else {
        
        // Calculate visible range
        size_t totalEntries = m_sessionCache.filteredEntries.size();
        size_t maxDisplay = static_cast<size_t>(m_uiState.maxDisplayedEntries);
        size_t startIndex = totalEntries > maxDisplay ? totalEntries - maxDisplay : 0;
        
        // Render entries
        for (size_t i = startIndex; i < totalEntries; ++i) {
            const auto& entry = m_sessionCache.filteredEntries[i];
            if (!entry) {
                LOG_DEBUG("Skipping null entry at index " + std::to_string(i));
                continue;
            }
            
            // Comprehensive entry validation
            if (entry->sourceName.empty()) {
                LOG_DEBUG("Skipping entry with empty source name at index " + std::to_string(i));
                continue;
            }
            
            // Validate source name isn't garbage
            if (entry->sourceName.length() > 50) {
                LOG_DEBUG("Skipping entry with too long source name at index " + std::to_string(i));
                continue;
            }
            
            // Check for non-printable characters
            bool hasInvalidChars = false;
            for (char c : entry->sourceName) {
                if (c < 32 || c > 126) { // Not printable ASCII
                    hasInvalidChars = true;
                    break;
                }
            }
            if (hasInvalidChars) {
                LOG_DEBUG("Skipping entry with invalid characters in source name at index " + std::to_string(i));
                continue;
            }
            
            // Same validation for target name if it exists
            if (!entry->targetName.empty()) {
                if (entry->targetName.length() > 50) {
                    LOG_DEBUG("Skipping entry with too long target name at index " + std::to_string(i));
                    continue;
                }
                for (char c : entry->targetName) {
                    if (c < 32 || c > 126) {
                        hasInvalidChars = true;
                        break;
                    }
                }
                if (hasInvalidChars) {
                    LOG_DEBUG("Skipping entry with invalid characters in target name at index " + std::to_string(i));
                    continue;
                }
            }
            
            ImVec4 color = GetEventTypeColor(entry->eventType);
            std::string logLine;
            
            // Safe string formatting with error handling
            try {
                logLine = FormatLogEntry(*entry);
                
                // Final validation of the formatted string
                if (logLine.empty()) {
                    logLine = "Empty log line";
                }
                
                // Check for null characters or other issues
                if (logLine.find('\0') != std::string::npos) {
                    logLine = "Log line contains null characters";
                }
                
                // Truncate if too long
                if (logLine.length() > 500) {
                    logLine = logLine.substr(0, 497) + "...";
                }
                
            } catch (const std::exception& e) {
                logLine = "Error formatting entry: " + std::string(e.what());
                color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // Red for errors
            } catch (...) {
                logLine = "Unknown error formatting entry";
                color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // Red for errors
            }
            
            // Alternating background
            if (i % 2 == 0) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, COLOR_BACKGROUND_ALT);
            }
            
            // Safe ImGui text rendering
            try {
                if (!logLine.empty() && logLine.c_str() != nullptr) {
                    ImGui::TextColored(color, "%s", logLine.c_str());
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid log line");
                }
            } catch (...) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error rendering log line");
            }
            
            if (i % 2 == 0) {
                ImGui::PopStyleColor();
            }
            
            // Context menu for individual entries
            // Provide a unique ID for the popup so ImGui has a valid identifier even when the last item had no ID (Text items don't push an ID)
            std::string popupId = "LogEntryPopup##" + std::to_string(i);
            if (ImGui::BeginPopupContextItem(popupId.c_str())) {
                if (ImGui::MenuItem("Copy Entry")) {
                    ImGui::SetClipboardText(logLine.c_str());
                }
                if (ImGui::MenuItem("Filter by Source")) {
                    strncpy_s(m_filterState.sourceNameBuffer, entry->sourceName.c_str(), sizeof(m_filterState.sourceNameBuffer) - 1);
                    ApplyFilters();
                }
                if (ImGui::MenuItem("Filter by Target")) {
                    strncpy_s(m_filterState.targetNameBuffer, entry->targetName.c_str(), sizeof(m_filterState.targetNameBuffer) - 1);
                    ApplyFilters();
                }
                ImGui::EndPopup();
            }
        }
        
        // Auto-scroll to bottom
        if (m_uiState.autoScroll && m_shouldScrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            m_shouldScrollToBottom = false;
        }
    }
    
    ImGui::EndChild();
    ImGui::EndChild();
}

void CombatLogTab::RenderStatistics() {
    if (!m_sessionCache.currentSession) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No active session");
        return;
    }

    if (ImGui::CollapsingHeader("Session Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto session = m_sessionCache.currentSession;
        
        // Overall session stats
        ImGui::Text("Session Overview:");
        ImGui::BulletText("Duration: %.1f seconds", session->GetDurationSeconds());
        ImGui::BulletText("Total Entries: %zu", session->entries.size());
        ImGui::BulletText("Participants: %zu", session->participantNames.size());
        ImGui::BulletText("Status: %s", session->isActive ? "Active" : "Completed");
        
        ImGui::Separator();
        
        // Damage statistics
        if (!session->damageStats.empty()) {
            ImGui::Text("Damage Statistics:");
            
            // Calculate totals
            uint64_t totalDamage = 0;
            uint64_t totalHits = 0;
            for (const auto& pair : session->damageStats) {
                totalDamage += pair.second.totalDamage;
                totalHits += pair.second.totalHits;
            }
            
            ImGui::BulletText("Total Damage: %s", CombatLogAnalyzer::FormatNumber(totalDamage).c_str());
            ImGui::BulletText("Total Hits: %s", CombatLogAnalyzer::FormatNumber(totalHits).c_str());
            if (session->GetDurationSeconds() > 0) {
                double dps = static_cast<double>(totalDamage) / session->GetDurationSeconds();
                ImGui::BulletText("Overall DPS: %s", CombatLogAnalyzer::FormatDps(dps).c_str());
            }
        }
        
        ImGui::Separator();
        
        // Healing statistics  
        if (!session->healingStats.empty()) {
            ImGui::Text("Healing Statistics:");
            
            // Calculate totals
            uint64_t totalHealing = 0;
            uint64_t totalHealHits = 0;
            for (const auto& pair : session->healingStats) {
                totalHealing += pair.second.totalHealing;
                totalHealHits += pair.second.totalHits;
            }
            
            ImGui::BulletText("Total Healing: %s", CombatLogAnalyzer::FormatNumber(totalHealing).c_str());
            ImGui::BulletText("Total Heals: %s", CombatLogAnalyzer::FormatNumber(totalHealHits).c_str());
            if (session->GetDurationSeconds() > 0) {
                double hps = static_cast<double>(totalHealing) / session->GetDurationSeconds();
                ImGui::BulletText("Overall HPS: %s", CombatLogAnalyzer::FormatDps(hps).c_str());
            }
        }
        
        ImGui::Separator();
        
        // Participant breakdown
        if (ImGui::TreeNode("Participants")) {
            for (const auto& pair : session->participantNames) {
                const WGUID& guid = pair.first;
                const std::string& name = pair.second;
                
                if (ImGui::TreeNode(name.c_str())) {
                    // Show damage stats for this participant
                    auto damageIt = session->damageStats.find(guid);
                    if (damageIt != session->damageStats.end()) {
                        const auto& stats = damageIt->second;
                        ImGui::Text("Damage: %s (%.1f DPS)", 
                                  CombatLogAnalyzer::FormatNumber(stats.totalDamage).c_str(),
                                  stats.dps);
                        ImGui::Text("Hits: %s (%.1f%% crit)", 
                                  CombatLogAnalyzer::FormatNumber(stats.totalHits).c_str(),
                                  stats.critRate * 100.0);
                    }
                    
                    // Show healing stats for this participant
                    auto healingIt = session->healingStats.find(guid);
                    if (healingIt != session->healingStats.end()) {
                        const auto& stats = healingIt->second;
                        ImGui::Text("Healing: %s (%.1f HPS)", 
                                  CombatLogAnalyzer::FormatNumber(stats.totalHealing).c_str(),
                                  stats.hps);
                        ImGui::Text("Heals: %s (%.1f%% crit)", 
                                  CombatLogAnalyzer::FormatNumber(stats.totalHits).c_str(),
                                  stats.critRate * 100.0);
                    }
                    
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
}

void CombatLogTab::RenderAnalysisPanel() {
    if (!m_sessionCache.isAnalysisValid) {
        UpdateAnalysis();
    }
    
    // Analysis mode selector
    const char* analysisMode[] = { "Overview", "Participants", "Spells", "Timeline" };
    ImGui::Combo("Analysis Mode", &m_uiState.analysisViewMode, analysisMode, IM_ARRAYSIZE(analysisMode));
    
    ImGui::Separator();
    
    switch (m_uiState.analysisViewMode) {
        case 0: // Overview
            RenderOverallStats(m_sessionCache.analysis);
            break;
        case 1: // Participants
            RenderParticipantBreakdown(m_sessionCache.analysis);
            break;
        case 2: // Spells
            RenderSpellAnalysis();
            break;
        case 3: // Timeline
            RenderTimelineChart(m_sessionCache.analysis.timeline);
            break;
    }
}

void CombatLogTab::RenderOverallStats(const CombatAnalysis& analysis) {
    ImGui::BeginChild("OverallStats", ImVec2(0, 0), false);
    
    // Session summary
    ImGui::Text("Session Summary");
    ImGui::Separator();
    
    ImGui::Text("Duration: %s", CombatLogAnalyzer::FormatDuration(analysis.duration).c_str());
    ImGui::Text("Participants: %zu", analysis.participants.size());
    ImGui::Text("Total Damage: %s", CombatLogAnalyzer::FormatNumber(analysis.totalDamage).c_str());
    ImGui::Text("Total Healing: %s", CombatLogAnalyzer::FormatNumber(analysis.totalHealing).c_str());
    ImGui::Text("Average DPS: %s", CombatLogAnalyzer::FormatDps(analysis.averageDps).c_str());
    ImGui::Text("Average HPS: %s", CombatLogAnalyzer::FormatDps(analysis.averageHps).c_str());
    
    ImGui::Separator();
    
    // Top performers
    if (!analysis.topDamageDealer.empty()) {
        ImGui::Text("Top Damage Dealers:");
        for (size_t i = 0; i < std::min(size_t(5), analysis.topDamageDealer.size()); ++i) {
            const auto& entry = analysis.topDamageDealer[i];
            ImGui::Text("  %zu. %s: %s DPS", i + 1, entry.first.c_str(), 
                       CombatLogAnalyzer::FormatDps(entry.second).c_str());
        }
    }
    
    if (!analysis.topHealer.empty()) {
        ImGui::Separator();
        ImGui::Text("Top Healers:");
        for (size_t i = 0; i < std::min(size_t(5), analysis.topHealer.size()); ++i) {
            const auto& entry = analysis.topHealer[i];
            ImGui::Text("  %zu. %s: %s HPS", i + 1, entry.first.c_str(), 
                       CombatLogAnalyzer::FormatDps(entry.second).c_str());
        }
    }
    
    ImGui::EndChild();
}

void CombatLogTab::RenderParticipantBreakdown(const CombatAnalysis& analysis) {
    ImGui::BeginChild("ParticipantBreakdown", ImVec2(0, 0), false);
    
    // Participant selector
    if (ImGui::BeginCombo("Select Participant", 
                         m_uiState.selectedParticipant >= 0 && 
                         m_uiState.selectedParticipant < static_cast<int>(analysis.participants.size()) ?
                         analysis.participants[m_uiState.selectedParticipant].second.c_str() : "Select...")) {
        
        for (int i = 0; i < static_cast<int>(analysis.participants.size()); ++i) {
            bool isSelected = (m_uiState.selectedParticipant == i);
            if (ImGui::Selectable(analysis.participants[i].second.c_str(), isSelected)) {
                m_uiState.selectedParticipant = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    // Display selected participant's stats
    if (m_uiState.selectedParticipant >= 0 && 
        m_uiState.selectedParticipant < static_cast<int>(analysis.participants.size())) {
        
        const auto& participant = analysis.participants[m_uiState.selectedParticipant];
        WGUID participantGUID = participant.first;
        
        ImGui::Separator();
        ImGui::Text("Stats for: %s", participant.second.c_str());
        ImGui::Separator();
        
        // Damage stats
        auto damageIt = analysis.damageByParticipant.find(participantGUID);
        if (damageIt != analysis.damageByParticipant.end()) {
            const auto& damageStats = damageIt->second;
            ImGui::Text("Damage Done:");
            ImGui::Text("  Total: %s", CombatLogAnalyzer::FormatNumber(damageStats.totalDamage).c_str());
            ImGui::Text("  DPS: %s", CombatLogAnalyzer::FormatDps(damageStats.dps).c_str());
            ImGui::Text("  Crit Rate: %s", CombatLogAnalyzer::FormatPercent(damageStats.critRate).c_str());
            ImGui::Text("  Accuracy: %s", CombatLogAnalyzer::FormatPercent(damageStats.accuracy).c_str());
        }
        
        // Healing stats
        auto healingIt = analysis.healingByParticipant.find(participantGUID);
        if (healingIt != analysis.healingByParticipant.end()) {
            const auto& healingStats = healingIt->second;
            ImGui::Separator();
            ImGui::Text("Healing Done:");
            ImGui::Text("  Total: %s", CombatLogAnalyzer::FormatNumber(healingStats.totalHealing).c_str());
            ImGui::Text("  HPS: %s", CombatLogAnalyzer::FormatDps(healingStats.hps).c_str());
            ImGui::Text("  Crit Rate: %s", CombatLogAnalyzer::FormatPercent(healingStats.critRate).c_str());
            ImGui::Text("  Efficiency: %s", CombatLogAnalyzer::FormatPercent(healingStats.efficiency).c_str());
        }
    }
    
    ImGui::EndChild();
}

void CombatLogTab::RenderSpellAnalysis() {
    ImGui::BeginChild("SpellAnalysis", ImVec2(0, 0), false);
    
    if (m_sessionCache.spellAnalyses.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No spell data available");
        ImGui::EndChild();
        return;
    }
    
    // Spell table
    if (ImGui::BeginTable("SpellTable", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Spell");
        ImGui::TableSetupColumn("Casts");
        ImGui::TableSetupColumn("Total Damage/Heal");
        ImGui::TableSetupColumn("Avg Damage/Heal");
        ImGui::TableSetupColumn("Crit Rate");
        ImGui::TableSetupColumn("DPS/HPS");
        ImGui::TableHeadersRow();
        
        for (const auto& spellStats : m_sessionCache.spellAnalyses) {
            ImGui::TableNextRow();
            
            ImGui::TableNextColumn();
            ImGui::Text("%s (%u)", spellStats.spellName.c_str(), spellStats.spellId);
            
            ImGui::TableNextColumn();
            ImGui::Text("%llu", spellStats.totalCasts);
            
            ImGui::TableNextColumn();
            if (spellStats.totalDamage > 0) {
                ImGui::Text("%s", CombatLogAnalyzer::FormatNumber(spellStats.totalDamage).c_str());
            } else if (spellStats.totalHealing > 0) {
                ImGui::Text("%s", CombatLogAnalyzer::FormatNumber(spellStats.totalHealing).c_str());
            } else {
                ImGui::Text("-");
            }
            
            ImGui::TableNextColumn();
            if (spellStats.totalDamage > 0) {
                ImGui::Text("%.0f", spellStats.averageDamage);
            } else if (spellStats.totalHealing > 0) {
                ImGui::Text("%.0f", spellStats.averageHeal);
            } else {
                ImGui::Text("-");
            }
            
            ImGui::TableNextColumn();
            ImGui::Text("%s", CombatLogAnalyzer::FormatPercent(spellStats.critRate).c_str());
            
            ImGui::TableNextColumn();
            if (spellStats.dps > 0) {
                ImGui::Text("%s", CombatLogAnalyzer::FormatDps(spellStats.dps).c_str());
            } else if (spellStats.hps > 0) {
                ImGui::Text("%s", CombatLogAnalyzer::FormatDps(spellStats.hps).c_str());
            } else {
                ImGui::Text("-");
            }
        }
        
        ImGui::EndTable();
    }
    
    ImGui::EndChild();
}

void CombatLogTab::RenderTimeline() {
    ImGui::BeginChild("TimelineView", ImVec2(0, 0), false);
    
    // Timeline controls
    ImGui::Checkbox("Show DPS", &m_uiState.showDpsLine);
    ImGui::SameLine();
    ImGui::Checkbox("Show HPS", &m_uiState.showHpsLine);
    ImGui::SameLine();
    ImGui::Checkbox("Show Damage Points", &m_uiState.showDamagePoints);
    ImGui::SameLine();
    ImGui::Checkbox("Show Healing Points", &m_uiState.showHealingPoints);
    
    ImGui::Separator();
    
    // Simple timeline chart using ImGui drawing
    RenderTimelineChart(m_sessionCache.analysis.timeline);
    
    ImGui::EndChild();
}

void CombatLogTab::RenderTimelineChart(const TimelineData& timeline) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y = CHART_HEIGHT;
    
    if (canvasSize.x < 100 || canvasSize.y < 50) {
        ImGui::Text("Timeline area too small");
        return;
    }
    
    // Background
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), 
                           IM_COL32(20, 20, 20, 255));
    
    // TODO: Implement actual timeline chart rendering
    // For now, just show placeholder
    ImVec2 textPos = ImVec2(canvasPos.x + canvasSize.x * 0.5f - 50, canvasPos.y + canvasSize.y * 0.5f);
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), "Timeline Chart");
    drawList->AddText(ImVec2(textPos.x - 30, textPos.y + 20), IM_COL32(180, 180, 180, 255), "(Coming Soon)");
    
    // Reserve space
    ImGui::Dummy(canvasSize);
}

void CombatLogTab::RenderExportOptions() {
    ImGui::BeginChild("ExportOptions", ImVec2(0, 0), false);
    
    ImGui::Text("Export Combat Log Data");
    ImGui::Separator();
    
    // Export format selection
    const char* formats[] = { "CSV", "JSON", "XML" };
    ImGui::Combo("Format", &m_uiState.exportFormat, formats, IM_ARRAYSIZE(formats));
    
    // Export path
    ImGui::InputText("File Path", &m_uiState.exportPath[0], m_uiState.exportPath.capacity());
    
    ImGui::Separator();
    
    // Export current session
    if (ImGui::Button("Export Current Session", ImVec2(-1, 0))) {
        ExportCurrentData();
    }
    
    // Export filtered data
    if (ImGui::Button("Export Filtered Data", ImVec2(-1, 0))) {
        // TODO: Implement filtered export
    }
    
    ImGui::Separator();
    
    // Export statistics
    ImGui::Text("Include in Export:");
    static bool includeRawData = true;
    static bool includeStatistics = true;
    static bool includeAnalysis = false;
    
    ImGui::Checkbox("Raw Log Data", &includeRawData);
    ImGui::Checkbox("Statistics", &includeStatistics);
    ImGui::Checkbox("Analysis Results", &includeAnalysis);
    
    ImGui::EndChild();
}

void CombatLogTab::RenderSettings() {
    if (ImGui::CollapsingHeader("Settings")) {
        auto& settings = m_combatLogManager->GetSettings();
        bool settingsChanged = false;
        
        settingsChanged |= ImGui::Checkbox("Auto Start on Combat", &settings.autoStartOnCombat);
        settingsChanged |= ImGui::Checkbox("Auto End on Combat End", &settings.autoEndOnCombatEnd);
        settingsChanged |= ImGui::Checkbox("Capture All Events", &settings.captureAllEvents);
        
        ImGui::SetNextItemWidth(-1);
        int maxEntries = static_cast<int>(settings.maxEntriesPerSession);
        if (ImGui::SliderInt("Max Entries", &maxEntries, 1000, 50000)) {
            settings.maxEntriesPerSession = static_cast<size_t>(maxEntries);
            settingsChanged = true;
        }
        
        ImGui::SetNextItemWidth(-1);
        float timeoutFloat = static_cast<float>(settings.combatTimeoutSeconds);
        if (ImGui::SliderFloat("Combat Timeout", &timeoutFloat, 1.0f, 30.0f, "%.1fs")) {
            settings.combatTimeoutSeconds = static_cast<double>(timeoutFloat);
            settingsChanged = true;
        }
        
        if (settingsChanged) {
            m_combatLogManager->ApplySettings(settings);
        }
    }
}

// Helper methods
void CombatLogTab::UpdateAnalysis() {
    if (!m_sessionCache.currentSession) {
        return;
    }
    
    auto analysisStart = std::chrono::steady_clock::now();
    
    // Perform analysis
    m_sessionCache.analysis = CombatLogAnalyzer::AnalyzeSession(*m_sessionCache.currentSession, m_filterState.filter);
    m_sessionCache.spellAnalyses = CombatLogAnalyzer::AnalyzeAllSpells(m_sessionCache.filteredEntries);
    m_sessionCache.isAnalysisValid = true;
    
    auto analysisEnd = std::chrono::steady_clock::now();
    m_performance.analysisTimeMs = std::chrono::duration<float, std::milli>(analysisEnd - analysisStart).count();
}

void CombatLogTab::RefreshSessionData() {
    m_sessionCache.currentSession = m_combatLogManager->GetCurrentSession();
    
    if (m_sessionCache.currentSession) {
        m_sessionCache.filteredEntries = m_combatLogManager->GetFilteredEntries(m_filterState.filter);
        m_performance.totalEntries = m_sessionCache.currentSession->entries.size();
        m_performance.filteredEntries = m_sessionCache.filteredEntries.size();
        
        // Estimate memory usage
        m_performance.memoryUsageMB = (m_performance.totalEntries * sizeof(CombatLogEntry)) / (1024.0f * 1024.0f);
        
        m_shouldScrollToBottom = true;
    }
    
    m_sessionCache.needsRefresh = false;
    m_sessionCache.Invalidate(); // Invalidate analysis
}

void CombatLogTab::ApplyFilters() {
    // Update filter from UI state
    m_filterState.filter.allowedEventTypes.clear();
    
    if (m_filterState.showDamage) {
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_DAMAGE);
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::MELEE_DAMAGE);
    }
    if (m_filterState.showHealing) {
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_HEAL);
    }
    if (m_filterState.showCasts) {
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_CAST_START);
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_CAST_SUCCESS);
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_CAST_FAILED);
    }
    
    // Always include UNKNOWN events for debugging
    m_filterState.filter.allowedEventTypes.insert(CombatEventType::UNKNOWN);
    if (m_filterState.showAuras) {
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_AURA_APPLIED);
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_AURA_REMOVED);
    }
    if (m_filterState.showMisses) {
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::SPELL_MISS);
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::MELEE_MISS);
    }
    if (m_filterState.showExperience) {
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::EXPERIENCE_GAIN);
        m_filterState.filter.allowedEventTypes.insert(CombatEventType::HONOR_GAIN);
    }
    
    // Apply amount filter
    if (m_filterState.useAmountFilter) {
        m_filterState.filter.minAmount = static_cast<uint32_t>(m_filterState.minAmount);
        m_filterState.filter.maxAmount = static_cast<uint32_t>(m_filterState.maxAmount);
    } else {
        m_filterState.filter.minAmount = 0;
        m_filterState.filter.maxAmount = UINT32_MAX;
    }
    
    m_sessionCache.needsRefresh = true;
}

std::string CombatLogTab::FormatLogEntry(const CombatLogEntry& entry) {
    std::stringstream ss;
    
    // Timestamp
    if (m_uiState.showTimestamps) {
        auto elapsed = std::chrono::steady_clock::now() - entry.timestamp;
        auto systemTime = std::chrono::system_clock::now() - std::chrono::duration_cast<std::chrono::system_clock::duration>(elapsed);
        auto timeT = std::chrono::system_clock::to_time_t(systemTime);
        ss << "[" << std::put_time(std::localtime(&timeT), "%H:%M:%S") << "] ";
    }
    
    // Event type and main content
    switch (entry.eventType) {
        case CombatEventType::SPELL_DAMAGE:
            ss << entry.sourceName << " -> " << entry.targetName << ": " 
               << entry.amount << " " << GetSchoolMaskName(entry.spellSchoolMask) << " damage";
            if (entry.spellId > 0) {
                // Always try to get the latest spell name from the manager
                std::string currentSpellName = m_combatLogManager->GetSpellNameById(entry.spellId);
                if (currentSpellName.find("Spell ") != 0) {
                    // We have a real spell name (not a fallback)
                    ss << " (" << currentSpellName << ")";
                } else {
                    // Still using fallback, show spell ID
                    ss << " (Spell " << entry.spellId << ")";
                }
            }
            
            // Add mitigation info
            if (entry.absorbed > 0) ss << " [" << entry.absorbed << " absorbed]";
            if (entry.resisted > 0) ss << " [" << entry.resisted << " resisted]";
            if (entry.blocked > 0) ss << " [" << entry.blocked << " blocked]";
            if (entry.overAmount > 0) ss << " [" << entry.overAmount << " overkill]";
            break;
            
        case CombatEventType::SPELL_HEAL:
            ss << entry.sourceName << " heals " << entry.targetName << " for " << entry.amount;
            if (entry.overAmount > 0) {
                uint32_t totalHeal = entry.amount + entry.overAmount;
                float efficiency = (static_cast<float>(entry.amount) / static_cast<float>(totalHeal)) * 100.0f;
                ss << " (+" << entry.overAmount << " overheal, " << std::fixed << std::setprecision(1) << efficiency << "% efficiency)";
            }
            if (entry.spellId > 0) {
                // Always try to get the latest spell name from the manager
                std::string currentSpellName = m_combatLogManager->GetSpellNameById(entry.spellId);
                if (currentSpellName.find("Spell ") != 0) {
                    // We have a real spell name (not a fallback)
                    ss << " (" << currentSpellName << ")";
                } else {
                    // Still using fallback, show spell ID
                    ss << " (Spell " << entry.spellId << ")";
                }
            }
            break;
            
        case CombatEventType::MELEE_DAMAGE:
            ss << entry.sourceName << " hits " << entry.targetName << " for " << entry.amount << " melee damage";
            
            // Add mitigation info for melee
            if (entry.absorbed > 0) ss << " [" << entry.absorbed << " absorbed]";
            if (entry.resisted > 0) ss << " [" << entry.resisted << " resisted]";
            if (entry.blocked > 0) ss << " [" << entry.blocked << " blocked]";
            if (entry.overAmount > 0) ss << " [" << entry.overAmount << " overkill]";
            break;
            
        case CombatEventType::EXPERIENCE_GAIN:
            ss << "You gain " << entry.amount << " experience";
            break;
            
        case CombatEventType::HONOR_GAIN:
            ss << "You gain " << entry.amount << " honor";
            break;
            
        case CombatEventType::SPELL_CAST_START:
        case CombatEventType::SPELL_CAST_SUCCESS:
        case CombatEventType::SPELL_CAST_FAILED:
            ss << entry.sourceName;
            if (entry.eventType == CombatEventType::SPELL_CAST_START) ss << " begins casting";
            else if (entry.eventType == CombatEventType::SPELL_CAST_SUCCESS) ss << " casts";
            else ss << " failed to cast";
            
            if (entry.spellId > 0) {
                // Always try to get the latest spell name from the manager
                std::string currentSpellName = m_combatLogManager->GetSpellNameById(entry.spellId);
                if (currentSpellName.find("Spell ") != 0) {
                    // We have a real spell name (not a fallback)
                    ss << " " << currentSpellName;
                } else {
                    // Still using fallback, show spell ID
                    ss << " (Spell ID: " << entry.spellId << ")";
                }
            } else {
                ss << " something";
            }
            
            if (!entry.targetName.empty()) ss << " on " << entry.targetName;
            break;
            
        default:
            ss << CombatLogAnalyzer::GetEventTypeName(entry.eventType) << ": " << entry.sourceName;
            if (!entry.targetName.empty()) {
                ss << " -> " << entry.targetName;
            }
            break;
    }
    
    // Hit flags
    uint32_t flags = static_cast<uint32_t>(entry.hitFlags);
    if (flags & static_cast<uint32_t>(HitFlags::CRITICAL)) ss << " (Critical)";
    if (flags & static_cast<uint32_t>(HitFlags::GLANCING)) ss << " (Glancing)";
    if (flags & static_cast<uint32_t>(HitFlags::CRUSHING)) ss << " (Crushing)";
    if (flags & static_cast<uint32_t>(HitFlags::MISS)) ss << " (Miss)";
    if (flags & static_cast<uint32_t>(HitFlags::DODGE)) ss << " (Dodge)";
    if (flags & static_cast<uint32_t>(HitFlags::PARRY)) ss << " (Parry)";
    if (flags & static_cast<uint32_t>(HitFlags::DEFLECT)) ss << " (Deflect)";
    if (flags & static_cast<uint32_t>(HitFlags::IMMUNE)) ss << " (Immune)";
    if (flags & static_cast<uint32_t>(HitFlags::BLOCK)) ss << " (Blocked)";
    if (flags & static_cast<uint32_t>(HitFlags::RESIST)) ss << " (Resisted)";
    if (flags & static_cast<uint32_t>(HitFlags::ABSORB)) ss << " (Absorbed)";
    
    // GUIDs if enabled
    if (m_uiState.showGUIDs && (entry.sourceGUID.IsValid() || entry.targetGUID.IsValid())) {
        try {
            std::stringstream guidSS;
            guidSS << " [0x" << std::hex << entry.sourceGUID.ToUint64();
            if (entry.targetGUID.ToUint64() != 0) {
                guidSS << " -> 0x" << entry.targetGUID.ToUint64();
            }
            guidSS << std::dec << "]";
            ss << guidSS.str();
        } catch (...) {
            ss << " [GUID Error]";
        }
    }
    
    return ss.str();
}

ImVec4 CombatLogTab::GetEventTypeColor(CombatEventType eventType) {
    switch (eventType) {
        case CombatEventType::SPELL_DAMAGE:
        case CombatEventType::MELEE_DAMAGE:
            return COLOR_DAMAGE;
        case CombatEventType::SPELL_HEAL:
            return COLOR_HEALING;
        case CombatEventType::SPELL_MISS:
        case CombatEventType::MELEE_MISS:
            return COLOR_MISS;
        case CombatEventType::EXPERIENCE_GAIN:
            return COLOR_EXPERIENCE;
        case CombatEventType::HONOR_GAIN:
            return COLOR_HONOR;
        case CombatEventType::SPELL_AURA_APPLIED:
        case CombatEventType::SPELL_AURA_REMOVED:
            return COLOR_AURA;
        default:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White
    }
}

void CombatLogTab::LoadSession(size_t sessionIndex) {
    m_sessionCache.currentSession = m_combatLogManager->GetSession(sessionIndex);
    m_sessionCache.needsRefresh = true;
}

void CombatLogTab::ExportCurrentData() {
    if (!m_sessionCache.currentSession) {
        LOG_ERROR("No session to export");
        return;
    }
    
    std::string filename = m_uiState.exportPath;
    
    switch (m_uiState.exportFormat) {
        case 0: // CSV
            filename += ".csv";
            m_combatLogManager->ExportToCSV(filename, m_filterState.filter);
            break;
        case 1: // JSON
            filename += ".json";
            m_combatLogManager->ExportToJSON(filename, m_filterState.filter);
            break;
        case 2: // XML
            filename += ".xml";
            // TODO: Implement XML export
            break;
    }
}

void CombatLogTab::ChartData::UpdateFromTimeline(const TimelineData& timeline) {
    Clear();
    
    // Convert timeline data to chart format
    for (const auto& point : timeline.dpsOverTime) {
        auto duration = point.first.time_since_epoch();
        float timeSeconds = std::chrono::duration<float>(duration).count();
        timePoints.push_back(timeSeconds);
        dpsValues.push_back(static_cast<float>(point.second));
    }
    
    for (const auto& point : timeline.hpsOverTime) {
        hpsValues.push_back(static_cast<float>(point.second));
    }
}

} // namespace GUI 
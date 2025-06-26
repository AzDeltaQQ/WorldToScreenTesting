#pragma once

#include "CombatLogEntry.h"
#include <map>
#include <vector>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

// Advanced analysis results
struct DamageBreakdown {
    std::map<uint32_t, uint64_t> damageBySpell;
    std::map<std::string, uint64_t> damageBySpellName;
    std::map<CombatSpellSchool, uint64_t> damageBySchool;
    std::map<std::string, uint64_t> damageByTarget;
    
    uint64_t totalDamage = 0;
    uint64_t totalHits = 0;
    uint64_t criticalHits = 0;
    uint64_t normalHits = 0;
    uint64_t totalMisses = 0;
    uint64_t totalDodges = 0;
    uint64_t totalParries = 0;
    uint64_t totalBlocks = 0;
    uint64_t totalBlocked = 0;
    uint64_t totalAbsorbed = 0;
    uint64_t totalResisted = 0;
    uint64_t totalOverkill = 0;
    
    double averageDamage = 0.0;
    double dps = 0.0;
    double critRate = 0.0;
    double accuracy = 0.0;
    double overkillPercent = 0.0;
};

struct HealingBreakdown {
    std::map<uint32_t, uint64_t> healingBySpell;
    std::map<std::string, uint64_t> healingBySpellName;
    std::map<std::string, uint64_t> healingByTarget;
    
    uint64_t totalHealing = 0;
    uint64_t totalHits = 0;
    uint64_t criticalHits = 0;
    uint64_t normalHits = 0;
    uint64_t totalOverheal = 0;
    
    double averageHeal = 0.0;
    double hps = 0.0;
    double critRate = 0.0;
    double overhealPercent = 0.0;
    double efficiency = 0.0;  // (totalHealing - totalOverheal) / totalHealing
};

struct TimelineData {
    struct DataPoint {
        std::chrono::steady_clock::time_point timestamp;
        double value;
        CombatEventType eventType;
        std::string description;
    };
    
    std::vector<DataPoint> damagePoints;
    std::vector<DataPoint> healingPoints;
    std::vector<DataPoint> dpsPoints;
    std::vector<DataPoint> hpsPoints;
    
    // Calculated over time windows
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> dpsOverTime;
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> hpsOverTime;
};

struct CombatAnalysis {
    std::chrono::duration<double> duration;
    std::vector<std::pair<WGUID, std::string>> participants;
    
    // Overall statistics
    uint64_t totalDamage = 0;
    uint64_t totalHealing = 0;
    double averageDps = 0.0;
    double averageHps = 0.0;
    
    // Per-participant breakdowns
    std::map<WGUID, DamageBreakdown> damageByParticipant;
    std::map<WGUID, HealingBreakdown> healingByParticipant;
    
    // Timeline data
    TimelineData timeline;
    
    // Top performers
    std::vector<std::pair<std::string, double>> topDamageDealer;
    std::vector<std::pair<std::string, double>> topHealer;
    std::vector<std::pair<std::string, uint64_t>> mostDamageTaken;
    std::vector<std::pair<std::string, uint64_t>> mostHealingReceived;
};

struct SpellAnalysis {
    uint32_t spellId;
    std::string spellName;
    CombatSpellSchool school;
    
    // Usage statistics
    uint64_t totalCasts = 0;
    uint64_t totalHits = 0;
    uint64_t totalCrits = 0;
    uint64_t totalMisses = 0;
    
    // Damage statistics (if applicable)
    uint64_t totalDamage = 0;
    uint64_t minDamage = UINT64_MAX;
    uint64_t maxDamage = 0;
    double averageDamage = 0.0;
    uint64_t totalOverkill = 0;
    
    // Healing statistics (if applicable)
    uint64_t totalHealing = 0;
    uint64_t minHeal = UINT64_MAX;
    uint64_t maxHeal = 0;
    double averageHeal = 0.0;
    uint64_t totalOverheal = 0;
    
    // Performance metrics
    double hitRate = 0.0;
    double critRate = 0.0;
    double dps = 0.0;
    double hps = 0.0;
    
    // Targets
    std::map<WGUID, uint64_t> damageByTarget;
    std::map<WGUID, uint64_t> healingByTarget;
};

class CombatLogAnalyzer {
public:
    // Comprehensive session analysis
    static CombatAnalysis AnalyzeSession(const CombatSession& session, const CombatLogFilter& filter = CombatLogFilter());
    
    // Individual participant analysis
    static DamageBreakdown AnalyzeDamage(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, const WGUID& entityGUID);
    static HealingBreakdown AnalyzeHealing(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, const WGUID& entityGUID);
    
    // Spell-specific analysis
    static SpellAnalysis AnalyzeSpell(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, uint32_t spellId);
    static std::vector<SpellAnalysis> AnalyzeAllSpells(const std::vector<std::shared_ptr<CombatLogEntry>>& entries);
    
    // Timeline analysis
    static TimelineData GenerateTimeline(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, 
                                        std::chrono::seconds windowSize = std::chrono::seconds(5));
    
    // Performance over time calculations
    static std::vector<std::pair<std::chrono::steady_clock::time_point, double>> 
        CalculateDpsOverTime(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, 
                           const WGUID& entityGUID, 
                           std::chrono::seconds windowSize = std::chrono::seconds(5));
    
    static std::vector<std::pair<std::chrono::steady_clock::time_point, double>> 
        CalculateHpsOverTime(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, 
                           const WGUID& entityGUID, 
                           std::chrono::seconds windowSize = std::chrono::seconds(5));
    
    // Comparative analysis
    static std::vector<std::pair<std::string, double>> RankParticipantsByDps(const CombatAnalysis& analysis);
    static std::vector<std::pair<std::string, double>> RankParticipantsByHps(const CombatAnalysis& analysis);
    static std::vector<std::pair<std::string, uint64_t>> RankParticipantsByDamageTaken(const CombatAnalysis& analysis);
    
    // Statistical calculations
    static double CalculateStandardDeviation(const std::vector<uint64_t>& values);
    static std::pair<double, double> CalculateConfidenceInterval(const std::vector<double>& values, double confidence = 0.95);
    
    // Utility functions
    static std::string FormatDuration(std::chrono::duration<double> duration);
    static std::string FormatNumber(uint64_t number);
    static std::string FormatDps(double dps);
    static std::string FormatPercent(double percent);
    static std::string GetSpellSchoolName(CombatSpellSchool school);
    static std::string GetEventTypeName(CombatEventType eventType);
    
    // Advanced filtering
    static std::vector<std::shared_ptr<CombatLogEntry>> FilterByTimeWindow(
        const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
        std::chrono::steady_clock::time_point start,
        std::chrono::steady_clock::time_point end);
    
    static std::vector<std::shared_ptr<CombatLogEntry>> FilterByEntity(
        const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
        const WGUID& entityGUID,
        bool asSource = true);
    
    static std::vector<std::shared_ptr<CombatLogEntry>> FilterByEventType(
        const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
        CombatEventType eventType);
    
    static std::vector<std::shared_ptr<CombatLogEntry>> FilterBySpell(
        const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
        uint32_t spellId);
    
    // Data export helpers
    static std::string ToCsvRow(const CombatLogEntry& entry);
    static std::string ToJsonObject(const CombatLogEntry& entry);
    static std::string AnalysisToJson(const CombatAnalysis& analysis);
    static std::string AnalysisToXml(const CombatAnalysis& analysis);
    
private:
    // Internal calculation helpers
    static double CalculateDps(uint64_t totalDamage, std::chrono::duration<double> duration);
    static double CalculateHps(uint64_t totalHealing, std::chrono::duration<double> duration);
    static double CalculateCritRate(uint64_t crits, uint64_t totalHits);
    static double CalculateAccuracy(uint64_t hits, uint64_t totalAttempts);
    
    // Timeline processing
    static void ProcessTimelineWindow(const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
                                    std::chrono::steady_clock::time_point windowStart,
                                    std::chrono::steady_clock::time_point windowEnd,
                                    TimelineData& timeline);
}; 
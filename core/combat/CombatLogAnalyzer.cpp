#include "CombatLogAnalyzer.h"
#include "../logs/Logger.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <unordered_set>

CombatAnalysis CombatLogAnalyzer::AnalyzeSession(const CombatSession& session, const CombatLogFilter& filter) {
    CombatAnalysis analysis;
    
    if (session.entries.empty()) {
        return analysis;
    }
    
    // Calculate session duration
    if (session.isActive) {
        analysis.duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - session.startTime);
    } else {
        analysis.duration = std::chrono::duration<double>(session.endTime - session.startTime);
    }
    
    // Build participant list
    for (const auto& pair : session.participantNames) {
        analysis.participants.emplace_back(pair.first, pair.second);
    }
    
    // Filter entries based on filter criteria
    std::vector<std::shared_ptr<CombatLogEntry>> filteredEntries;
    for (const auto& entry : session.entries) {
        if (!entry) continue;
        
        // Apply filter logic (simplified version of manager's filtering)
        if (!filter.allowedEventTypes.empty() && 
            filter.allowedEventTypes.find(entry->eventType) == filter.allowedEventTypes.end()) {
            continue;
        }
        
        if (filter.useTimeFilter) {
            if (entry->timestamp < filter.startTime || entry->timestamp > filter.endTime) {
                continue;
            }
        }
        
        if (entry->amount < filter.minAmount || entry->amount > filter.maxAmount) {
            continue;
        }
        
        filteredEntries.push_back(entry);
    }
    
    // Analyze damage for each participant
    for (const auto& participant : analysis.participants) {
        DamageBreakdown damageStats = AnalyzeDamage(filteredEntries, participant.first);
        if (damageStats.totalDamage > 0) {
            analysis.damageByParticipant[participant.first] = damageStats;
            analysis.totalDamage += damageStats.totalDamage;
        }
        
        HealingBreakdown healingStats = AnalyzeHealing(filteredEntries, participant.first);
        if (healingStats.totalHealing > 0) {
            analysis.healingByParticipant[participant.first] = healingStats;
            analysis.totalHealing += healingStats.totalHealing;
        }
    }
    
    // Calculate overall averages
    if (analysis.duration.count() > 0) {
        analysis.averageDps = static_cast<double>(analysis.totalDamage) / analysis.duration.count();
        analysis.averageHps = static_cast<double>(analysis.totalHealing) / analysis.duration.count();
    }
    
    // Rank participants
    analysis.topDamageDealer = RankParticipantsByDps(analysis);
    analysis.topHealer = RankParticipantsByHps(analysis);
    
    // Generate timeline
    analysis.timeline = GenerateTimeline(filteredEntries);
    
    return analysis;
}

DamageBreakdown CombatLogAnalyzer::AnalyzeDamage(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, const WGUID& entityGUID) {
    DamageBreakdown breakdown;
    
    for (const auto& entry : entries) {
        if (!entry || entry->sourceGUID != entityGUID) continue;
        
        bool isDamageEvent = (entry->eventType == CombatEventType::SPELL_DAMAGE || 
                             entry->eventType == CombatEventType::MELEE_DAMAGE);
        
        if (!isDamageEvent) continue;
        
        // Count hits and damage
        breakdown.totalHits++;
        breakdown.totalDamage += entry->amount;
        breakdown.totalOverkill += entry->overAmount;
        breakdown.totalAbsorbed += entry->absorbed;
        breakdown.totalResisted += entry->resisted;
        breakdown.totalBlocked += entry->blocked;
        
        // Check for critical hits
        uint32_t flags = static_cast<uint32_t>(entry->hitFlags);
        if (flags & static_cast<uint32_t>(HitFlags::CRITICAL)) {
            breakdown.criticalHits++;
        } else {
            breakdown.normalHits++;
        }
        
        // Check for misses
        if (flags & static_cast<uint32_t>(HitFlags::MISS)) {
            breakdown.totalMisses++;
        }
        
        if (flags & static_cast<uint32_t>(HitFlags::DODGE)) {
            breakdown.totalDodges++;
        }
        
        if (flags & static_cast<uint32_t>(HitFlags::PARRY)) {
            breakdown.totalParries++;
        }
        
        if (flags & static_cast<uint32_t>(HitFlags::BLOCK)) {
            breakdown.totalBlocks++;
        }
        
        // Damage by spell
        if (entry->spellId > 0) {
            breakdown.damageBySpell[entry->spellId] += entry->amount;
            if (!entry->spellName.empty()) {
                breakdown.damageBySpellName[entry->spellName] += entry->amount;
            }
        }
        
        // Damage by school
        breakdown.damageBySchool[entry->spellSchool] += entry->amount;
        
        // Damage by target
        if (!entry->targetName.empty()) {
            breakdown.damageByTarget[entry->targetName] += entry->amount;
        }
    }
    
    // Calculate derived statistics
    if (breakdown.totalHits > 0) {
        breakdown.averageDamage = static_cast<double>(breakdown.totalDamage) / breakdown.totalHits;
        breakdown.critRate = static_cast<double>(breakdown.criticalHits) / breakdown.totalHits;
        
        uint64_t totalAttempts = breakdown.totalHits + breakdown.totalMisses + breakdown.totalDodges + breakdown.totalParries;
        if (totalAttempts > 0) {
            breakdown.accuracy = static_cast<double>(breakdown.totalHits) / totalAttempts;
        }
    }
    
    if (breakdown.totalDamage > 0) {
        breakdown.overkillPercent = static_cast<double>(breakdown.totalOverkill) / breakdown.totalDamage;
    }
    
    return breakdown;
}

HealingBreakdown CombatLogAnalyzer::AnalyzeHealing(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, const WGUID& entityGUID) {
    HealingBreakdown breakdown;
    
    for (const auto& entry : entries) {
        if (!entry || entry->sourceGUID != entityGUID) continue;
        
        if (entry->eventType != CombatEventType::SPELL_HEAL) continue;
        
        // Count heals
        breakdown.totalHits++;
        breakdown.totalHealing += entry->amount;
        breakdown.totalOverheal += entry->overAmount;
        
        // Check for critical heals
        uint32_t flags = static_cast<uint32_t>(entry->hitFlags);
        if (flags & static_cast<uint32_t>(HitFlags::CRITICAL)) {
            breakdown.criticalHits++;
        } else {
            breakdown.normalHits++;
        }
        
        // Healing by spell
        if (entry->spellId > 0) {
            breakdown.healingBySpell[entry->spellId] += entry->amount;
            if (!entry->spellName.empty()) {
                breakdown.healingBySpellName[entry->spellName] += entry->amount;
            }
        }
        
        // Healing by target
        if (!entry->targetName.empty()) {
            breakdown.healingByTarget[entry->targetName] += entry->amount;
        }
    }
    
    // Calculate derived statistics
    if (breakdown.totalHits > 0) {
        breakdown.averageHeal = static_cast<double>(breakdown.totalHealing) / breakdown.totalHits;
        breakdown.critRate = static_cast<double>(breakdown.criticalHits) / breakdown.totalHits;
    }
    
    if (breakdown.totalHealing > 0) {
        breakdown.overhealPercent = static_cast<double>(breakdown.totalOverheal) / breakdown.totalHealing;
        breakdown.efficiency = static_cast<double>(breakdown.totalHealing - breakdown.totalOverheal) / breakdown.totalHealing;
    }
    
    return breakdown;
}

SpellAnalysis CombatLogAnalyzer::AnalyzeSpell(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, uint32_t spellId) {
    SpellAnalysis analysis;
    analysis.spellId = spellId;
    
    for (const auto& entry : entries) {
        if (!entry || entry->spellId != spellId) continue;
        
        // Set spell name if we find it
        if (analysis.spellName.empty() && !entry->spellName.empty()) {
            analysis.spellName = entry->spellName;
            analysis.school = entry->spellSchool;
        }
        
        // Count casts (for cast events)
        if (entry->eventType == CombatEventType::SPELL_CAST_START || 
            entry->eventType == CombatEventType::SPELL_CAST_SUCCESS) {
            analysis.totalCasts++;
        }
        
        // Analyze damage events
        if (entry->eventType == CombatEventType::SPELL_DAMAGE) {
            analysis.totalHits++;
            analysis.totalDamage += entry->amount;
            analysis.totalOverkill += entry->overAmount;
            
            if (entry->amount < analysis.minDamage) analysis.minDamage = entry->amount;
            if (entry->amount > analysis.maxDamage) analysis.maxDamage = entry->amount;
            
            uint32_t flags = static_cast<uint32_t>(entry->hitFlags);
            if (flags & static_cast<uint32_t>(HitFlags::CRITICAL)) {
                analysis.totalCrits++;
            }
            if (flags & static_cast<uint32_t>(HitFlags::MISS)) {
                analysis.totalMisses++;
            }
            
            analysis.damageByTarget[entry->targetGUID] += entry->amount;
        }
        
        // Analyze healing events
        if (entry->eventType == CombatEventType::SPELL_HEAL) {
            analysis.totalHits++;
            analysis.totalHealing += entry->amount;
            analysis.totalOverheal += entry->overAmount;
            
            if (entry->amount < analysis.minHeal) analysis.minHeal = entry->amount;
            if (entry->amount > analysis.maxHeal) analysis.maxHeal = entry->amount;
            
            uint32_t flags = static_cast<uint32_t>(entry->hitFlags);
            if (flags & static_cast<uint32_t>(HitFlags::CRITICAL)) {
                analysis.totalCrits++;
            }
            
            analysis.healingByTarget[entry->targetGUID] += entry->amount;
        }
    }
    
    // Calculate derived statistics
    if (analysis.totalHits > 0) {
        if (analysis.totalDamage > 0) {
            analysis.averageDamage = static_cast<double>(analysis.totalDamage) / analysis.totalHits;
        }
        if (analysis.totalHealing > 0) {
            analysis.averageHeal = static_cast<double>(analysis.totalHealing) / analysis.totalHits;
        }
        analysis.critRate = static_cast<double>(analysis.totalCrits) / analysis.totalHits;
    }
    
    uint64_t totalAttempts = analysis.totalHits + analysis.totalMisses;
    if (totalAttempts > 0) {
        analysis.hitRate = static_cast<double>(analysis.totalHits) / totalAttempts;
    }
    
    return analysis;
}

std::vector<SpellAnalysis> CombatLogAnalyzer::AnalyzeAllSpells(const std::vector<std::shared_ptr<CombatLogEntry>>& entries) {
    std::unordered_set<uint32_t> spellIds;
    
    // Collect all unique spell IDs
    for (const auto& entry : entries) {
        if (entry && entry->spellId > 0) {
            spellIds.insert(entry->spellId);
        }
    }
    
    std::vector<SpellAnalysis> results;
    for (uint32_t spellId : spellIds) {
        SpellAnalysis analysis = AnalyzeSpell(entries, spellId);
        if (analysis.totalCasts > 0 || analysis.totalHits > 0) {
            results.push_back(analysis);
        }
    }
    
    // Sort by total damage/healing
    std::sort(results.begin(), results.end(), [](const SpellAnalysis& a, const SpellAnalysis& b) {
        return (a.totalDamage + a.totalHealing) > (b.totalDamage + b.totalHealing);
    });
    
    return results;
}

TimelineData CombatLogAnalyzer::GenerateTimeline(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, std::chrono::seconds windowSize) {
    TimelineData timeline;
    
    if (entries.empty()) {
        return timeline;
    }
    
    // Find time bounds
    auto startTime = entries.front()->timestamp;
    auto endTime = entries.back()->timestamp;
    
    // Generate data points for individual events
    for (const auto& entry : entries) {
        if (!entry) continue;
        
        TimelineData::DataPoint point;
        point.timestamp = entry->timestamp;
        point.eventType = entry->eventType;
        point.value = static_cast<double>(entry->amount);
        
        std::stringstream desc;
        desc << entry->sourceName << " -> " << entry->targetName << ": " << entry->amount;
        point.description = desc.str();
        
        if (entry->eventType == CombatEventType::SPELL_DAMAGE || 
            entry->eventType == CombatEventType::MELEE_DAMAGE) {
            timeline.damagePoints.push_back(point);
        } else if (entry->eventType == CombatEventType::SPELL_HEAL) {
            timeline.healingPoints.push_back(point);
        }
    }
    
    // Calculate DPS and HPS over time windows
    auto currentTime = startTime;
    while (currentTime < endTime) {
        auto windowEnd = currentTime + windowSize;
        
        uint64_t windowDamage = 0;
        uint64_t windowHealing = 0;
        
        for (const auto& entry : entries) {
            if (!entry) continue;
            
            if (entry->timestamp >= currentTime && entry->timestamp < windowEnd) {
                if (entry->eventType == CombatEventType::SPELL_DAMAGE || 
                    entry->eventType == CombatEventType::MELEE_DAMAGE) {
                    windowDamage += entry->amount;
                } else if (entry->eventType == CombatEventType::SPELL_HEAL) {
                    windowHealing += entry->amount;
                }
            }
        }
        
        double dps = static_cast<double>(windowDamage) / windowSize.count();
        double hps = static_cast<double>(windowHealing) / windowSize.count();
        
        timeline.dpsOverTime.emplace_back(currentTime, dps);
        timeline.hpsOverTime.emplace_back(currentTime, hps);
        
        currentTime = windowEnd;
    }
    
    return timeline;
}

std::vector<std::pair<std::chrono::steady_clock::time_point, double>> 
CombatLogAnalyzer::CalculateDpsOverTime(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, 
                                      const WGUID& entityGUID, 
                                      std::chrono::seconds windowSize) {
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> result;
    
    if (entries.empty()) {
        return result;
    }
    
    auto startTime = entries.front()->timestamp;
    auto endTime = entries.back()->timestamp;
    
    auto currentTime = startTime;
    while (currentTime < endTime) {
        auto windowEnd = currentTime + windowSize;
        
        uint64_t windowDamage = 0;
        
        for (const auto& entry : entries) {
            if (!entry || entry->sourceGUID != entityGUID) continue;
            
            if (entry->timestamp >= currentTime && entry->timestamp < windowEnd) {
                if (entry->eventType == CombatEventType::SPELL_DAMAGE || 
                    entry->eventType == CombatEventType::MELEE_DAMAGE) {
                    windowDamage += entry->amount;
                }
            }
        }
        
        double dps = static_cast<double>(windowDamage) / windowSize.count();
        result.emplace_back(currentTime, dps);
        
        currentTime = windowEnd;
    }
    
    return result;
}

std::vector<std::pair<std::chrono::steady_clock::time_point, double>> 
CombatLogAnalyzer::CalculateHpsOverTime(const std::vector<std::shared_ptr<CombatLogEntry>>& entries, 
                                      const WGUID& entityGUID, 
                                      std::chrono::seconds windowSize) {
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> result;
    
    if (entries.empty()) {
        return result;
    }
    
    auto startTime = entries.front()->timestamp;
    auto endTime = entries.back()->timestamp;
    
    auto currentTime = startTime;
    while (currentTime < endTime) {
        auto windowEnd = currentTime + windowSize;
        
        uint64_t windowHealing = 0;
        
        for (const auto& entry : entries) {
            if (!entry || entry->sourceGUID != entityGUID) continue;
            
            if (entry->timestamp >= currentTime && entry->timestamp < windowEnd) {
                if (entry->eventType == CombatEventType::SPELL_HEAL) {
                    windowHealing += entry->amount;
                }
            }
        }
        
        double hps = static_cast<double>(windowHealing) / windowSize.count();
        result.emplace_back(currentTime, hps);
        
        currentTime = windowEnd;
    }
    
    return result;
}

std::vector<std::pair<std::string, double>> CombatLogAnalyzer::RankParticipantsByDps(const CombatAnalysis& analysis) {
    std::vector<std::pair<std::string, double>> rankings;
    
    for (const auto& participant : analysis.participants) {
        auto damageIt = analysis.damageByParticipant.find(participant.first);
        if (damageIt != analysis.damageByParticipant.end()) {
            rankings.emplace_back(participant.second, damageIt->second.dps);
        }
    }
    
    std::sort(rankings.begin(), rankings.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    
    return rankings;
}

std::vector<std::pair<std::string, double>> CombatLogAnalyzer::RankParticipantsByHps(const CombatAnalysis& analysis) {
    std::vector<std::pair<std::string, double>> rankings;
    
    for (const auto& participant : analysis.participants) {
        auto healingIt = analysis.healingByParticipant.find(participant.first);
        if (healingIt != analysis.healingByParticipant.end()) {
            rankings.emplace_back(participant.second, healingIt->second.hps);
        }
    }
    
    std::sort(rankings.begin(), rankings.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    
    return rankings;
}

std::vector<std::pair<std::string, uint64_t>> CombatLogAnalyzer::RankParticipantsByDamageTaken(const CombatAnalysis& analysis) {
    std::unordered_map<std::string, uint64_t> damageTaken;
    
    // This would require analyzing entries where participants are targets
    // For now, return empty vector as it requires more complex analysis
    std::vector<std::pair<std::string, uint64_t>> rankings;
    return rankings;
}

double CombatLogAnalyzer::CalculateStandardDeviation(const std::vector<uint64_t>& values) {
    if (values.empty()) return 0.0;
    
    double mean = static_cast<double>(std::accumulate(values.begin(), values.end(), 0ULL)) / values.size();
    
    double variance = 0.0;
    for (uint64_t value : values) {
        double diff = static_cast<double>(value) - mean;
        variance += diff * diff;
    }
    variance /= values.size();
    
    return std::sqrt(variance);
}

std::pair<double, double> CombatLogAnalyzer::CalculateConfidenceInterval(const std::vector<double>& values, double confidence) {
    if (values.empty()) return {0.0, 0.0};
    
    // Simplified confidence interval calculation
    double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    
    double variance = 0.0;
    for (double value : values) {
        double diff = value - mean;
        variance += diff * diff;
    }
    variance /= values.size();
    
    double stdError = std::sqrt(variance / values.size());
    double margin = 1.96 * stdError; // Approximate 95% confidence
    
    return {mean - margin, mean + margin};
}

std::string CombatLogAnalyzer::FormatDuration(std::chrono::duration<double> duration) {
    auto seconds = static_cast<int>(duration.count());
    int minutes = seconds / 60;
    seconds %= 60;
    
    std::stringstream ss;
    if (minutes > 0) {
        ss << minutes << "m " << seconds << "s";
    } else {
        ss << std::fixed << std::setprecision(1) << duration.count() << "s";
    }
    return ss.str();
}

std::string CombatLogAnalyzer::FormatNumber(uint64_t number) {
    if (number >= 1000000000) {
        return std::to_string(number / 1000000000) + "." + std::to_string((number / 100000000) % 10) + "B";
    } else if (number >= 1000000) {
        return std::to_string(number / 1000000) + "." + std::to_string((number / 100000) % 10) + "M";
    } else if (number >= 1000) {
        return std::to_string(number / 1000) + "." + std::to_string((number / 100) % 10) + "K";
    }
    return std::to_string(number);
}

std::string CombatLogAnalyzer::FormatDps(double dps) {
    return FormatNumber(static_cast<uint64_t>(dps));
}

std::string CombatLogAnalyzer::FormatPercent(double percent) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << (percent * 100.0) << "%";
    return ss.str();
}

std::string CombatLogAnalyzer::GetSpellSchoolName(CombatSpellSchool school) {
    switch (school) {
        case SPELL_SCHOOL_NORMAL: return "Physical";
        case SPELL_SCHOOL_HOLY: return "Holy";
        case SPELL_SCHOOL_FIRE: return "Fire";
        case SPELL_SCHOOL_NATURE: return "Nature";
        case SPELL_SCHOOL_FROST: return "Frost";
        case SPELL_SCHOOL_SHADOW: return "Shadow";
        case SPELL_SCHOOL_ARCANE: return "Arcane";
        default: return "Unknown";
    }
}

std::string CombatLogAnalyzer::GetEventTypeName(CombatEventType eventType) {
    switch (eventType) {
        case CombatEventType::SPELL_DAMAGE: return "Spell Damage";
        case CombatEventType::SPELL_HEAL: return "Spell Heal";
        case CombatEventType::SPELL_MISS: return "Spell Miss";
        case CombatEventType::SPELL_CAST_START: return "Cast Start";
        case CombatEventType::SPELL_CAST_SUCCESS: return "Cast Success";
        case CombatEventType::SPELL_CAST_FAILED: return "Cast Failed";
        case CombatEventType::SPELL_AURA_APPLIED: return "Aura Applied";
        case CombatEventType::SPELL_AURA_REMOVED: return "Aura Removed";
        case CombatEventType::MELEE_DAMAGE: return "Melee Damage";
        case CombatEventType::MELEE_MISS: return "Melee Miss";
        case CombatEventType::EXPERIENCE_GAIN: return "Experience Gain";
        case CombatEventType::HONOR_GAIN: return "Honor Gain";
        default: return "Unknown";
    }
}

std::vector<std::shared_ptr<CombatLogEntry>> CombatLogAnalyzer::FilterByTimeWindow(
    const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    
    std::vector<std::shared_ptr<CombatLogEntry>> result;
    
    for (const auto& entry : entries) {
        if (entry && entry->timestamp >= start && entry->timestamp <= end) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<CombatLogEntry>> CombatLogAnalyzer::FilterByEntity(
    const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
    const WGUID& entityGUID,
    bool asSource) {
    
    std::vector<std::shared_ptr<CombatLogEntry>> result;
    
    for (const auto& entry : entries) {
        if (!entry) continue;
        
        if (asSource && entry->sourceGUID == entityGUID) {
            result.push_back(entry);
        } else if (!asSource && entry->targetGUID == entityGUID) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<CombatLogEntry>> CombatLogAnalyzer::FilterByEventType(
    const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
    CombatEventType eventType) {
    
    std::vector<std::shared_ptr<CombatLogEntry>> result;
    
    for (const auto& entry : entries) {
        if (entry && entry->eventType == eventType) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<std::shared_ptr<CombatLogEntry>> CombatLogAnalyzer::FilterBySpell(
    const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
    uint32_t spellId) {
    
    std::vector<std::shared_ptr<CombatLogEntry>> result;
    
    for (const auto& entry : entries) {
        if (entry && entry->spellId == spellId) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::string CombatLogAnalyzer::ToCsvRow(const CombatLogEntry& entry) {
    std::stringstream ss;
    
    // Format timestamp - convert steady_clock to system_clock
    auto timeSinceEpoch = entry.timestamp.time_since_epoch();
    auto timeT = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(timeSinceEpoch)));
    
    ss << std::put_time(std::gmtime(&timeT), "%Y-%m-%d %H:%M:%S") << ",";
    ss << GetEventTypeName(entry.eventType) << ",";
    ss << entry.sourceGUID.ToUint64() << ",";
    ss << "\"" << entry.sourceName << "\",";
    ss << entry.targetGUID.ToUint64() << ",";
    ss << "\"" << entry.targetName << "\",";
    ss << entry.spellId << ",";
    ss << "\"" << entry.spellName << "\",";
    ss << entry.amount << ",";
    ss << entry.overAmount << ",";
    ss << entry.absorbed << ",";
    ss << entry.resisted << ",";
    ss << entry.blocked << ",";
    ss << static_cast<uint32_t>(entry.hitFlags);
    
    return ss.str();
}

std::string CombatLogAnalyzer::ToJsonObject(const CombatLogEntry& entry) {
    std::stringstream ss;
    ss << "{";
    ss << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(entry.timestamp.time_since_epoch()).count() << ",";
    ss << "\"eventType\":\"" << GetEventTypeName(entry.eventType) << "\",";
    ss << "\"sourceGUID\":" << entry.sourceGUID.ToUint64() << ",";
    ss << "\"sourceName\":\"" << entry.sourceName << "\",";
    ss << "\"targetGUID\":" << entry.targetGUID.ToUint64() << ",";
    ss << "\"targetName\":\"" << entry.targetName << "\",";
    ss << "\"spellId\":" << entry.spellId << ",";
    ss << "\"spellName\":\"" << entry.spellName << "\",";
    ss << "\"amount\":" << entry.amount << ",";
    ss << "\"overAmount\":" << entry.overAmount << ",";
    ss << "\"absorbed\":" << entry.absorbed << ",";
    ss << "\"resisted\":" << entry.resisted << ",";
    ss << "\"blocked\":" << entry.blocked << ",";
    ss << "\"hitFlags\":" << static_cast<uint32_t>(entry.hitFlags);
    ss << "}";
    return ss.str();
}

std::string CombatLogAnalyzer::AnalysisToJson(const CombatAnalysis& analysis) {
    std::stringstream ss;
    ss << "{";
    ss << "\"duration\":" << analysis.duration.count() << ",";
    ss << "\"totalDamage\":" << analysis.totalDamage << ",";
    ss << "\"totalHealing\":" << analysis.totalHealing << ",";
    ss << "\"averageDps\":" << analysis.averageDps << ",";
    ss << "\"averageHps\":" << analysis.averageHps << ",";
    ss << "\"participantCount\":" << analysis.participants.size();
    ss << "}";
    return ss.str();
}

std::string CombatLogAnalyzer::AnalysisToXml(const CombatAnalysis& analysis) {
    std::stringstream ss;
    ss << "<CombatAnalysis>";
    ss << "<Duration>" << analysis.duration.count() << "</Duration>";
    ss << "<TotalDamage>" << analysis.totalDamage << "</TotalDamage>";
    ss << "<TotalHealing>" << analysis.totalHealing << "</TotalHealing>";
    ss << "<AverageDps>" << analysis.averageDps << "</AverageDps>";
    ss << "<AverageHps>" << analysis.averageHps << "</AverageHps>";
    ss << "<ParticipantCount>" << analysis.participants.size() << "</ParticipantCount>";
    ss << "</CombatAnalysis>";
    return ss.str();
}

// Private helper methods
double CombatLogAnalyzer::CalculateDps(uint64_t totalDamage, std::chrono::duration<double> duration) {
    if (duration.count() <= 0) return 0.0;
    return static_cast<double>(totalDamage) / duration.count();
}

double CombatLogAnalyzer::CalculateHps(uint64_t totalHealing, std::chrono::duration<double> duration) {
    if (duration.count() <= 0) return 0.0;
    return static_cast<double>(totalHealing) / duration.count();
}

double CombatLogAnalyzer::CalculateCritRate(uint64_t crits, uint64_t totalHits) {
    if (totalHits == 0) return 0.0;
    return static_cast<double>(crits) / totalHits;
}

double CombatLogAnalyzer::CalculateAccuracy(uint64_t hits, uint64_t totalAttempts) {
    if (totalAttempts == 0) return 0.0;
    return static_cast<double>(hits) / totalAttempts;
}

void CombatLogAnalyzer::ProcessTimelineWindow(const std::vector<std::shared_ptr<CombatLogEntry>>& entries,
                                            std::chrono::steady_clock::time_point windowStart,
                                            std::chrono::steady_clock::time_point windowEnd,
                                            TimelineData& timeline) {
    // Helper function for timeline processing
    // Implementation would depend on specific timeline requirements
} 
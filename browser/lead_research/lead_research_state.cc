// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/lead_research/lead_research_state.h"

#include <optional>
#include <utility>

#include "base/json/values_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace lead_research {

namespace {
constexpr char kCampaignsPref[] = "brave.lead_research.campaigns";
constexpr char kLeadsPref[] = "brave.lead_research.leads";

constexpr char kIdKey[] = "id";
constexpr char kGoalKey[] = "goal";
constexpr char kLocationsKey[] = "locations";
constexpr char kIndustriesKey[] = "industries";
constexpr char kServicesKey[] = "services";
constexpr char kExclusionsKey[] = "exclusions";
constexpr char kTargetCountKey[] = "target_count";
constexpr char kCreatedAtKey[] = "created_at";

constexpr char kCampaignIdKey[] = "campaign_id";
constexpr char kNameKey[] = "name";
constexpr char kDomainKey[] = "domain";
constexpr char kCityKey[] = "city";
constexpr char kServiceFitKey[] = "service_fit";
constexpr char kNotesKey[] = "notes";
constexpr char kStageKey[] = "stage";
constexpr char kScoreKey[] = "score";
constexpr char kEvidenceCoverageKey[] = "evidence_coverage_percent";
constexpr char kScoreRationaleKey[] = "score_rationale";

std::vector<std::string> ToStringVector(const base::ListValue* list) {
  std::vector<std::string> result;
  if (!list) {
    return result;
  }
  for (const base::Value& item : *list) {
    if (item.is_string()) {
      result.push_back(item.GetString());
    }
  }
  return result;
}

base::ListValue ToValueList(const std::vector<std::string>& values) {
  base::ListValue list;
  for (const std::string& value : values) {
    list.Append(value);
  }
  return list;
}

}  // namespace

LeadResearchState::Campaign::Campaign() = default;
LeadResearchState::Campaign::Campaign(std::string id,
                                      std::string goal,
                                      std::vector<std::string> locations,
                                      std::vector<std::string> industries,
                                      std::vector<std::string> services,
                                      std::vector<std::string> exclusions,
                                      int target_count,
                                      base::Time created_at)
    : id(std::move(id)),
      goal(std::move(goal)),
      locations(std::move(locations)),
      industries(std::move(industries)),
      services(std::move(services)),
      exclusions(std::move(exclusions)),
      target_count(target_count),
      created_at(created_at) {}
LeadResearchState::Campaign::Campaign(const Campaign&) = default;
LeadResearchState::Campaign& LeadResearchState::Campaign::operator=(
    const Campaign&) = default;
LeadResearchState::Campaign::~Campaign() = default;

LeadResearchState::Lead::Lead() = default;
LeadResearchState::Lead::Lead(std::string id,
                              std::string campaign_id,
                              std::string name,
                              std::string domain,
                              std::string city,
                              std::string service_fit,
                              std::string notes,
                              std::string stage,
                              base::Time created_at)
    : id(std::move(id)),
      campaign_id(std::move(campaign_id)),
      name(std::move(name)),
      domain(std::move(domain)),
      city(std::move(city)),
      service_fit(std::move(service_fit)),
      notes(std::move(notes)),
      stage(std::move(stage)),
      created_at(created_at) {}
LeadResearchState::Lead::Lead(const Lead&) = default;
LeadResearchState::Lead& LeadResearchState::Lead::operator=(const Lead&) =
    default;
LeadResearchState::Lead::~Lead() = default;

LeadResearchState::LeadResearchState(PrefService* prefs) : prefs_(prefs) {}

LeadResearchState::~LeadResearchState() = default;

// static
void LeadResearchState::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kCampaignsPref);
  registry->RegisterListPref(kLeadsPref);
}

std::string LeadResearchState::CreateCampaign(
    const std::string& goal,
    std::vector<std::string> locations,
    std::vector<std::string> industries,
    std::vector<std::string> services,
    std::vector<std::string> exclusions,
    int target_count) {
  std::string id =
      base::StrCat({"campaign_", base::NumberToString(next_campaign_number_++)});

  base::DictValue entry;
  entry.Set(kIdKey, id);
  entry.Set(kGoalKey, goal);
  entry.Set(kLocationsKey, ToValueList(locations));
  entry.Set(kIndustriesKey, ToValueList(industries));
  entry.Set(kServicesKey, ToValueList(services));
  entry.Set(kExclusionsKey, ToValueList(exclusions));
  entry.Set(kTargetCountKey, target_count);
  entry.Set(kCreatedAtKey, base::TimeToValue(base::Time::Now()));

  ScopedListPrefUpdate update(prefs_, kCampaignsPref);
  update->Insert(update->begin(), base::Value(std::move(entry)));
  return id;
}

std::vector<LeadResearchState::Campaign> LeadResearchState::GetCampaigns()
    const {
  std::vector<Campaign> campaigns;
  for (const base::Value& item : prefs_->GetList(kCampaignsPref)) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString(kIdKey);
    const std::string* goal = dict->FindString(kGoalKey);
    const base::Value* created_at_value = dict->Find(kCreatedAtKey);
    if (!id || !goal || !created_at_value) {
      continue;
    }
    std::optional<base::Time> created_at =
        base::ValueToTime(created_at_value);
    if (!created_at) {
      continue;
    }
    campaigns.emplace_back(
        *id, *goal, ToStringVector(dict->FindList(kLocationsKey)),
        ToStringVector(dict->FindList(kIndustriesKey)),
        ToStringVector(dict->FindList(kServicesKey)),
        ToStringVector(dict->FindList(kExclusionsKey)),
        dict->FindInt(kTargetCountKey).value_or(0), *created_at);
  }
  return campaigns;
}

std::optional<LeadResearchState::Campaign> LeadResearchState::GetCampaign(
    const std::string& campaign_id) const {
  for (Campaign& campaign : GetCampaigns()) {
    if (campaign.id == campaign_id) {
      return std::move(campaign);
    }
  }
  return std::nullopt;
}

std::string LeadResearchState::SaveLead(const std::string& campaign_id,
                                        const std::string& name,
                                        const std::string& domain,
                                        const std::string& city,
                                        const std::string& service_fit,
                                        const std::string& notes) {
  if (campaign_id.empty()) {
    return std::string();
  }
  std::string id =
      base::StrCat({"lead_", base::NumberToString(next_lead_number_++)});

  base::DictValue entry;
  entry.Set(kIdKey, id);
  entry.Set(kCampaignIdKey, campaign_id);
  entry.Set(kNameKey, name);
  entry.Set(kDomainKey, domain);
  entry.Set(kCityKey, city);
  entry.Set(kServiceFitKey, service_fit);
  entry.Set(kNotesKey, notes);
  entry.Set(kStageKey, "New");
  entry.Set(kCreatedAtKey, base::TimeToValue(base::Time::Now()));

  ScopedListPrefUpdate update(prefs_, kLeadsPref);
  update->Insert(update->begin(), base::Value(std::move(entry)));
  return id;
}

std::vector<LeadResearchState::Lead> LeadResearchState::GetLeads(
    const std::string& campaign_id) const {
  std::vector<Lead> leads;
  for (const base::Value& item : prefs_->GetList(kLeadsPref)) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* lead_campaign_id = dict->FindString(kCampaignIdKey);
    if (!lead_campaign_id || *lead_campaign_id != campaign_id) {
      continue;
    }
    const std::string* id = dict->FindString(kIdKey);
    const base::Value* created_at_value = dict->Find(kCreatedAtKey);
    if (!id || !created_at_value) {
      continue;
    }
    std::optional<base::Time> created_at =
        base::ValueToTime(created_at_value);
    if (!created_at) {
      continue;
    }
    Lead& lead = leads.emplace_back(
        *id, *lead_campaign_id,
        dict->FindString(kNameKey) ? *dict->FindString(kNameKey)
                                   : std::string(),
        dict->FindString(kDomainKey) ? *dict->FindString(kDomainKey)
                                     : std::string(),
        dict->FindString(kCityKey) ? *dict->FindString(kCityKey)
                                   : std::string(),
        dict->FindString(kServiceFitKey) ? *dict->FindString(kServiceFitKey)
                                         : std::string(),
        dict->FindString(kNotesKey) ? *dict->FindString(kNotesKey)
                                    : std::string(),
        dict->FindString(kStageKey) ? *dict->FindString(kStageKey)
                                    : std::string(),
        *created_at);
    lead.score = dict->FindInt(kScoreKey).value_or(-1);
    lead.evidence_coverage_percent =
        dict->FindInt(kEvidenceCoverageKey).value_or(0);
    lead.score_rationale = dict->FindString(kScoreRationaleKey)
                               ? *dict->FindString(kScoreRationaleKey)
                               : std::string();
  }
  return leads;
}

bool LeadResearchState::UpdateLeadScore(const std::string& lead_id,
                                        int score,
                                        int evidence_coverage_percent,
                                        const std::string& rationale) {
  ScopedListPrefUpdate update(prefs_, kLeadsPref);
  for (base::Value& item : *update) {
    base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString(kIdKey);
    if (!id || *id != lead_id) {
      continue;
    }
    dict->Set(kScoreKey, score);
    dict->Set(kEvidenceCoverageKey, evidence_coverage_percent);
    dict->Set(kScoreRationaleKey, rationale);
    return true;
  }
  return false;
}

}  // namespace lead_research

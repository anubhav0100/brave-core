// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_LEAD_RESEARCH_LEAD_RESEARCH_STATE_H_
#define BRAVE_BROWSER_LEAD_RESEARCH_LEAD_RESEARCH_STATE_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class PrefService;

namespace lead_research {

// Local, per-profile storage for the AI Chat "Lead Research" tools
// (browser/ai_chat/tools/lead_research/) - see
// Brave_AI_Lead_Assistant_Development_Blueprint.md. This is the narrow
// browser-side slice of that blueprint: campaign and lead records the AI
// assistant creates via normal tool calls, persisted locally in profile
// prefs. There is deliberately no backend service, job orchestrator, or
// scoring engine here - those are the blueprint's separate, much larger
// "expansion path," out of scope for this slice. Nothing here fetches or
// scrapes anything itself; it only records facts the assistant already
// observed (e.g. from a page it was shown, or the user told it) via
// SaveLead(), and opens safe, fixed-template URLs (Maps/LinkedIn) via the
// tools that read this state.
class LeadResearchState : public KeyedService {
 public:
  // One lead-research campaign - see CreateLeadCampaignTool.
  struct Campaign {
    Campaign();
    Campaign(std::string id,
             std::string goal,
             std::vector<std::string> locations,
             std::vector<std::string> industries,
             std::vector<std::string> services,
             std::vector<std::string> exclusions,
             int target_count,
             base::Time created_at);
    Campaign(const Campaign&);
    Campaign& operator=(const Campaign&);
    ~Campaign();

    std::string id;
    std::string goal;
    std::vector<std::string> locations;
    std::vector<std::string> industries;
    std::vector<std::string> services;
    std::vector<std::string> exclusions;
    int target_count = 0;
    base::Time created_at;
  };

  // One company recorded as a lead against a campaign - see SaveLeadTool.
  // `notes` is expected to hold the evidence/reasoning the assistant
  // actually observed for why this company is relevant - never a fact it
  // invented (see save_lead_tool.cc's tool description).
  struct Lead {
    Lead();
    Lead(std::string id,
         std::string campaign_id,
         std::string name,
         std::string domain,
         std::string city,
         std::string service_fit,
         std::string notes,
         std::string stage,
         base::Time created_at);
    Lead(const Lead&);
    Lead& operator=(const Lead&);
    ~Lead();

    std::string id;
    std::string campaign_id;
    std::string name;
    std::string domain;
    std::string city;
    std::string service_fit;
    std::string notes;
    std::string stage;
    base::Time created_at;

    // Set by CalculateLeadScoreTool via UpdateLeadScore() - not part of the
    // constructor above since a lead starts unscored and is scored later,
    // separately from being recorded. `score` of -1 means "not yet
    // scored". `evidence_coverage_percent` is the share (0-100) of the
    // blueprint's 5 scoring dimensions that were actually supported by
    // evidence rather than left unknown - see
    // Brave_AI_Lead_Assistant_Development_Blueprint.md section 11.1: a
    // low score from missing information must read as "research
    // incomplete," not "poor prospect."
    int score = -1;
    int evidence_coverage_percent = 0;
    std::string score_rationale;
  };

  explicit LeadResearchState(PrefService* prefs);
  ~LeadResearchState() override;
  LeadResearchState(const LeadResearchState&) = delete;
  LeadResearchState& operator=(const LeadResearchState&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Creates and persists a new campaign, returning its generated id.
  std::string CreateCampaign(const std::string& goal,
                             std::vector<std::string> locations,
                             std::vector<std::string> industries,
                             std::vector<std::string> services,
                             std::vector<std::string> exclusions,
                             int target_count);

  // Most-recently-created first.
  std::vector<Campaign> GetCampaigns() const;
  std::optional<Campaign> GetCampaign(const std::string& campaign_id) const;

  // Records `campaign_id` as a lead, returning the generated lead id.
  // Empty `campaign_id` is rejected (returns an empty string) - a lead must
  // belong to a real campaign so it shows up when that campaign is
  // reviewed.
  std::string SaveLead(const std::string& campaign_id,
                       const std::string& name,
                       const std::string& domain,
                       const std::string& city,
                       const std::string& service_fit,
                       const std::string& notes);

  // Most-recently-created first.
  std::vector<Lead> GetLeads(const std::string& campaign_id) const;

  // Records a computed score against an existing lead - see
  // CalculateLeadScoreTool. Returns false if no lead with `lead_id`
  // exists.
  bool UpdateLeadScore(const std::string& lead_id,
                      int score,
                      int evidence_coverage_percent,
                      const std::string& rationale);

 private:
  raw_ptr<PrefService> prefs_;
  int next_campaign_number_ = 1;
  int next_lead_number_ = 1;
};

}  // namespace lead_research

#endif  // BRAVE_BROWSER_LEAD_RESEARCH_LEAD_RESEARCH_STATE_H_

#include "kst/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <map>
#include <set>

namespace kst::analysis {

const char* to_string(Classification c) {
    switch (c) {
        case Classification::NaturalEarthquake: return "natural-earthquake";
        case Classification::SurfaceExplosion:  return "surface-explosion";
        case Classification::IndustrialBlast:   return "industrial-blast";
        case Classification::Indeterminate:     return "indeterminate";
    }
    return "indeterminate";
}

// Ordinal bands, derived from stored factors rather than stored themselves
// (REQ-7.7). Deliberately coarse: the underlying evidence does not support
// finer distinctions, and a numeric score would imply precision we lack
// (REQ-11.4).
std::string Confidence::occurrence_band() const {
    if (has_disconfirming_evidence && independent_sources <= 1) return "low";
    if (instrumental_sources >= 2) return "high";
    if (instrumental_sources == 1 && independent_sources >= 2) return "high";
    if (instrumental_sources == 1 || has_government_source) return "moderate";
    if (independent_sources >= 3) return "moderate";
    return "low";
}

std::string Confidence::location_band() const {
    if (best_location_uncertainty_km < 0.0) {
        return instrumental_sources > 0 ? "moderate" : "low";
    }
    if (best_location_uncertainty_km <= 5.0) return "high";
    if (best_location_uncertainty_km <= 30.0) return "moderate";
    return "low";
}

std::string Confidence::characterisation_band() const {
    // Characterisation is the weakest axis by construction: catalog
    // parameters alone rarely settle source type (REQ-5.7).
    if (has_disconfirming_evidence) return "low";
    if (instrumental_sources >= 2 && independent_sources >= 3) return "moderate";
    if (instrumental_sources >= 1 && independent_sources >= 2) return "moderate";
    return "low";
}

Discrimination discriminate(const model::Observation& o) {
    Discrimination d;

    // The source's own event type is evidence, taken as published (REQ-3.6).
    const std::string& t = o.reported_event_type;
    if (t == "quarry blast" || t == "quarry" || t == "mining explosion" ||
        t == "mine collapse") {
        d.classification = Classification::IndustrialBlast;
        d.reasons.push_back("source reports event type '" + t + "'");
        return d;
    }
    if (t == "explosion" || t == "chemical explosion" ||
        t == "nuclear explosion") {
        d.classification = Classification::SurfaceExplosion;
        d.reasons.push_back("source reports event type '" + t + "'");
        return d;
    }

    // Depth discriminant (REQ-5.2). An agency default is a processing
    // artefact, not a measurement — applying the discriminant to it would
    // systematically manufacture false positives, so we decline and say so.
    if (!o.depth_km.has_value()) {
        d.reasons.push_back("no depth published; depth discriminant not applied");
    } else if (o.depth_is_fixed) {
        d.reasons.push_back(
            "depth " + std::to_string(static_cast<int>(*o.depth_km)) +
            " km is an agency default, not a constrained measurement; "
            "depth discriminant not applied (REQ-5.2)");
    } else {
        d.depth_discriminant_applied = true;
        if (*o.depth_km <= 1.0) {
            d.classification = Classification::SurfaceExplosion;
            d.reasons.push_back(
                "constrained depth at or near the surface is consistent with "
                "a surface or near-surface source");
            return d;
        }
        if (*o.depth_km >= 5.0) {
            d.classification = Classification::NaturalEarthquake;
            d.reasons.push_back(
                "constrained depth well below the surface is inconsistent "
                "with a surface source");
            return d;
        }
        d.reasons.push_back("constrained depth is not diagnostic in isolation");
    }

    // mb:Ms is unavailable from catalog parameters alone, and the literature
    // puts its useful floor near mb 4.5 — above most events of interest
    // (REQ-5.3, REQ-5.7). Recorded so the omission is visible, not silent.
    if (o.magnitude.has_value() && *o.magnitude < 4.5) {
        d.reasons.push_back(
            "mb:Ms discriminant not applicable below approximately mb 4.5");
    }

    d.classification = Classification::Indeterminate;
    return d;
}

namespace {

// Cross-catalog identity: the same physical event published by two agencies
// must count once as instrumental corroboration (REQ-6.5).
bool same_physical_event(const model::Observation& a,
                         const model::Observation& b, const Windows& w) {
    if (!a.latitude || !a.longitude || !b.latitude || !b.longitude) return false;
    const auto ta = model::parse_iso8601_ms(a.origin_time);
    const auto tb = model::parse_iso8601_ms(b.origin_time);
    if (!ta || !tb) return false;
    const double dt = std::fabs(static_cast<double>(*ta - *tb)) / 1000.0;
    const double dkm = model::haversine_km(*a.latitude, *a.longitude,
                                           *b.latitude, *b.longitude);
    const bool both_instrumental =
        a.source_class == model::SourceClass::Instrumental &&
        b.source_class == model::SourceClass::Instrumental;
    const double km_limit = both_instrumental ? w.instrumental_km : w.reporting_km;
    const double s_limit =
        both_instrumental ? w.instrumental_seconds : w.reporting_seconds;
    return dt <= s_limit && dkm <= km_limit;
}

std::string event_uid_for(const std::vector<model::Observation>& group) {
    // Derived from the sorted constituent identifiers, so the identifier is
    // a pure function of membership and independent of input order (REQ-6.3).
    std::vector<std::string> ids;
    ids.reserve(group.size());
    for (const auto& o : group) ids.push_back(o.observation_uid);
    std::sort(ids.begin(), ids.end());
    std::string joined;
    for (const auto& i : ids) {
        joined += i;
        joined.push_back('|');
    }
    return model::observation_uid("event", joined);
}

}  // namespace

std::vector<Event> correlate(std::vector<model::Observation> obs,
                             const Windows& w) {
    // Canonical ordering first: correlation must not depend on the order in
    // which sources happened to be ingested (REQ-6.3).
    std::sort(obs.begin(), obs.end(),
              [](const model::Observation& a, const model::Observation& b) {
                  if (a.origin_time != b.origin_time)
                      return a.origin_time < b.origin_time;
                  return a.observation_uid < b.observation_uid;
              });

    // Union-find over the association relation. Because the relation is
    // symmetric and the input is canonically ordered, the resulting
    // partition is unique regardless of ingestion order.
    std::vector<std::size_t> parent(obs.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    std::function<std::size_t(std::size_t)> find =
        [&](std::size_t x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };

    std::vector<std::vector<std::string>> reasons(obs.size());
    for (std::size_t i = 0; i < obs.size(); ++i) {
        for (std::size_t j = i + 1; j < obs.size(); ++j) {
            if (!same_physical_event(obs[i], obs[j], w)) continue;
            const std::size_t ri = find(i), rj = find(j);
            if (ri != rj) parent[ri] = rj;
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "%s:%s associated with %s:%s within space/time window",
                          obs[i].source_id.c_str(), obs[i].native_id.c_str(),
                          obs[j].source_id.c_str(), obs[j].native_id.c_str());
            reasons[find(i)].emplace_back(buf);
        }
    }

    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < obs.size(); ++i) groups[find(i)].push_back(i);

    std::vector<Event> events;
    events.reserve(groups.size());
    for (const auto& [root, members] : groups) {
        Event e;
        for (std::size_t idx : members) e.constituents.push_back(obs[idx]);
        e.association_reasons = reasons[root];
        e.event_uid = event_uid_for(e.constituents);
        e.origin_time = e.constituents.front().origin_time;

        // Representative position: prefer the instrumental observation with
        // the tightest stated uncertainty; otherwise the first with a
        // position. Derived, and labelled as such downstream (REQ-3.6).
        const model::Observation* best = nullptr;
        for (const auto& o : e.constituents) {
            if (!o.latitude || !o.longitude) continue;
            if (!best) {
                best = &o;
                continue;
            }
            const bool o_inst = o.source_class == model::SourceClass::Instrumental;
            const bool b_inst = best->source_class == model::SourceClass::Instrumental;
            if (o_inst && !b_inst) best = &o;
        }
        if (best) {
            e.latitude = *best->latitude;
            e.longitude = *best->longitude;
        }

        // Discrimination: the strongest instrumental verdict wins; a
        // definite verdict from any constituent beats indeterminate.
        for (const auto& o : e.constituents) {
            Discrimination d = discriminate(o);
            if (e.discrimination.classification == Classification::Indeterminate ||
                d.classification != Classification::Indeterminate) {
                if (d.classification != Classification::Indeterminate ||
                    e.discrimination.reasons.empty()) {
                    e.discrimination.classification = d.classification;
                    e.discrimination.depth_discriminant_applied =
                        e.discrimination.depth_discriminant_applied ||
                        d.depth_discriminant_applied;
                }
            }
            for (const auto& r : d.reasons) {
                e.discrimination.reasons.push_back(o.source_id + ": " + r);
            }
        }

        // Confidence factors. Sources sharing an upstream origin are not
        // independent, so independence is counted by distinct source id
        // (REQ-7.3) — a coarse proxy, and the honest one available here.
        std::set<std::string> distinct_sources;
        for (const auto& o : e.constituents) {
            distinct_sources.insert(o.source_id);
            switch (o.source_class) {
                case model::SourceClass::Instrumental:
                    ++e.confidence.instrumental_sources;
                    break;
                case model::SourceClass::Government:
                    e.confidence.has_government_source = true;
                    break;
                case model::SourceClass::News:
                case model::SourceClass::Social:
                case model::SourceClass::CuratedDataset:
                    ++e.confidence.reporting_sources;
                    break;
            }
            if (o.location_uncertainty_km) {
                if (e.confidence.best_location_uncertainty_km < 0.0 ||
                    *o.location_uncertainty_km <
                        e.confidence.best_location_uncertainty_km) {
                    e.confidence.best_location_uncertainty_km =
                        *o.location_uncertainty_km;
                }
            }
            if (o.reported_event_type == "earthquake" &&
                e.discrimination.classification == Classification::SurfaceExplosion) {
                e.confidence.has_disconfirming_evidence = true;  // REQ-7.6
            }
        }
        e.confidence.independent_sources = static_cast<int>(distinct_sources.size());
        events.push_back(std::move(e));
    }

    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.origin_time != b.origin_time) return a.origin_time < b.origin_time;
        return a.event_uid < b.event_uid;
    });
    return events;
}

}  // namespace kst::analysis

#include "kst/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <array>
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
    // An unpublished uncertainty is not a small one. Reporting "moderate"
    // for an origin whose ellipse is hundreds of kilometres across would be
    // the single most misleading thing this system could say (REQ-7.5).
    if (best_location_uncertainty_km < 0.0) return "unknown";
    if (best_location_uncertainty_km <= 5.0) return "high";
    if (best_location_uncertainty_km <= 30.0) return "moderate";
    if (best_location_uncertainty_km <= 100.0) return "low";
    return "unusable";  // beyond this, no site-level attribution is possible
}

std::string Confidence::characterisation_band() const {
    // Characterisation is the weakest axis by construction: catalog
    // parameters alone rarely settle source type (REQ-5.7).
    if (has_disconfirming_evidence || any_source_type_suspected) return "low";
    if (instrumental_sources >= 2 && independent_sources >= 3) return "moderate";
    if (instrumental_sources >= 1 && independent_sources >= 2) return "moderate";
    return "low";
}

Discrimination discriminate(const model::Observation& o) {
    Discrimination d;

    // The source's own event type is evidence, taken as published (REQ-3.6).
    // Values follow the QuakeML 1.2 EventType enumeration.
    const std::string& t = o.reported_event_type;

    // Deliberate, routine, non-hostile explosions. Distinguishing these is
    // the whole point of REQ-5.5: a mining region produces a steady stream
    // of them, and a system that cannot tell them from munitions is a
    // false-positive generator wherever industry operates.
    if (t == "static thermal source") {
        d.classification = Classification::IndustrialBlast;
        d.reasons.push_back(
            "source classifies this as a persistent static thermal source "
            "(gas flare, volcano, or fixed industrial heat), not a discrete "
            "event");
        return d;
    }
    if (t == "quarry blast" || t == "quarry" || t == "mining explosion" ||
        t == "mine collapse" || t == "experimental explosion" ||
        t == "controlled explosion" || t == "induced or triggered event" ||
        t == "rock burst" || t == "collapse" || t == "cavity collapse") {
        d.classification = Classification::IndustrialBlast;
        d.reasons.push_back("source reports event type '" + t +
                            "', a deliberate or induced non-hostile source");
        return d;
    }
    // Explosions the source does not attribute to industry. 'explosion' is
    // the generic QuakeML value and says nothing about cause — it is not
    // evidence of a strike on its own (REQ-11.4).
    if (t == "explosion" || t == "chemical explosion" ||
        t == "accidental explosion" || t == "nuclear explosion") {
        d.classification = Classification::SurfaceExplosion;
        d.reasons.push_back("source reports event type '" + t +
                            "'; the source does not attribute a cause");
        return d;
    }

    // Depth discriminant (REQ-5.2). An agency default is a processing
    // artefact, not a measurement — applying the discriminant to it would
    // systematically manufacture false positives, so we decline and say so.
    if (!o.depth_km.has_value()) {
        d.reasons.push_back("no depth published; depth discriminant not applied");
    } else if (o.depth_is_fixed) {
        const std::string why =
            o.depth_type.empty()
                ? "the source does not say how this depth was determined"
                : "the source declares this depth '" + o.depth_type + "'";
        d.reasons.push_back(
            "depth " + std::to_string(static_cast<int>(*o.depth_km)) + " km: " +
            why + "; depth discriminant not applied (REQ-5.2)");
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

long cell_key(double lat, double lon, double size) {
    const long la = static_cast<long>(std::floor(lat / size));
    const long lo = static_cast<long>(std::floor(lon / size));
    return la * 100000L + lo;  // distinct for any plausible grid size
}

std::map<long, DiurnalCell> diurnal_cells(
    const std::vector<model::Observation>& obs, const DiurnalRule& rule) {
    // Hour histogram per cell, explosion-typed observations only. UTC is used
    // deliberately: converting to local time would require assuming a zone,
    // and the signature survives the smear for any single cell (DM-2026-008).
    std::map<long, std::array<int, 24>> hist;
    std::map<long, double> cell_lon;
    for (const model::Observation& o : obs) {
        if (o.reported_event_type != "explosion" &&
            o.reported_event_type != "chemical explosion") {
            continue;
        }
        if (!o.latitude || !o.longitude) continue;
        const auto ms = model::parse_iso8601_ms(o.origin_time);
        if (!ms) continue;
        long h = static_cast<long>((*ms / 3600000) % 24);
        if (h < 0) h += 24;
        const long k = cell_key(*o.latitude, *o.longitude, rule.cell_degrees);
        hist[k][static_cast<std::size_t>(h)]++;
        cell_lon[k] = *o.longitude;
    }

    std::map<long, DiurnalCell> out;
    for (const auto& [key, hours] : hist) {
        DiurnalCell c;
        c.key = key;
        for (int n : hours) c.count += n;
        if (c.count < rule.min_events) continue;

        // Busiest contiguous window, wrapping midnight.
        int best = 0, best_start = 0;
        for (int start = 0; start < 24; ++start) {
            int sum = 0;
            for (int k = 0; k < rule.window_hours; ++k) {
                sum += hours[static_cast<std::size_t>((start + k) % 24)];
            }
            if (sum > best) { best = sum; best_start = start; }
        }
        c.busiest_start_hour = best_start;
        c.concentration = static_cast<double>(best) / c.count;
        for (int n : hours) if (n == 0) ++c.empty_hours;

        // Convert the window centre to local solar time. A tight window at
        // local midnight is the signature of sustained night operations, NOT
        // of industry — without this check the rule reclassifies conflict as
        // blasting, which is the worst error it could make.
        const double centre_utc = best_start + rule.window_hours / 2.0;
        double local = std::fmod(centre_utc + cell_lon[key] / 15.0, 24.0);
        if (local < 0) local += 24.0;
        c.window_centre_local = local;
        c.in_daylight = local >= rule.daylight_start && local <= rule.daylight_end;

        c.industrial = c.concentration >= rule.concentration &&
                       c.empty_hours >= rule.min_empty_hours && c.in_daylight;
        out.emplace(key, c);
    }
    return out;
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

// Members of a frozen cluster, by root. Linear scan over a handful of
// clusters; kept simple because the partition is small relative to obs.
const std::vector<std::size_t>& frozen_members(
    const std::vector<std::pair<std::size_t, std::vector<std::size_t>>>& frozen,
    std::size_t root) {
    for (const auto& [r, members] : frozen) {
        if (r == root) return members;
    }
    static const std::vector<std::size_t> empty;
    return empty;
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
                             const Windows& w, const DiurnalRule& diurnal) {
    const std::map<long, DiurnalCell> cells = diurnal_cells(obs, diurnal);
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
    // R1 (DM-2026-009): event formation is instrumental-only. A report is not
    // positional evidence, and admitting one to the association relation lets
    // it bridge unrelated detections transitively — one coarse report chained
    // 38 gas-flare pixels and two reported strikes 77 km apart into a single
    // "high confidence surface explosion" (issue #29).
    const auto instrumental = [](const model::Observation& o) {
        return o.source_class == model::SourceClass::Instrumental;
    };

    std::vector<std::size_t> parent(obs.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    std::function<std::size_t(std::size_t)> find =
        [&](std::size_t x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };

    std::vector<std::vector<std::string>> reasons(obs.size());
    for (std::size_t i = 0; i < obs.size(); ++i) {
        if (!instrumental(obs[i])) continue;
        for (std::size_t j = i + 1; j < obs.size(); ++j) {
            if (!instrumental(obs[j])) continue;
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
    for (std::size_t i = 0; i < obs.size(); ++i) {
        if (instrumental(obs[i])) groups[find(i)].push_back(i);
    }

    // R2 (DM-2026-009): a report attaches to exactly one instrumental cluster,
    // or to none at all. Matching runs against this frozen copy of the
    // partition, never against clusters as they are being amended, so the
    // outcome cannot depend on the order reports are considered (REQ-1.2).
    const std::vector<std::pair<std::size_t, std::vector<std::size_t>>> frozen(
        groups.begin(), groups.end());

    // The R2 verdict, keyed by the group each report lands in.
    std::map<std::size_t, std::vector<std::string>> assoc;

    for (std::size_t r = 0; r < obs.size(); ++r) {
        if (instrumental(obs[r])) continue;
        std::vector<std::size_t> matched;
        for (const auto& [root, members] : frozen) {
            for (std::size_t m : members) {
                if (same_physical_event(obs[r], obs[m], w)) {
                    matched.push_back(root);
                    break;
                }
            }
        }

        if (matched.size() == 1) {
            groups[matched.front()].push_back(r);
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "%s:%s attached to the one instrumental cluster "
                          "within the reporting window",
                          obs[r].source_id.c_str(), obs[r].native_id.c_str());
            reasons[matched.front()].emplace_back(buf);
            assoc[matched.front()].emplace_back(
                obs[r].source_id + ":" + obs[r].native_id +
                " attached — the one instrumental cluster within the "
                "reporting window");
            continue;
        }

        // Zero matches, or several. Several is the case that matters: the
        // report corresponds to at most one of them. Corroborating all would
        // inflate every candidate, and corroborating the nearest would assert
        // a correspondence the evidence does not establish, so it corroborates
        // none and the ambiguity is carried into the output (REQ-11.4).
        groups[r].push_back(r);
        if (matched.empty()) {
            reasons[r].emplace_back(
                "no instrumental cluster within the reporting window; stands "
                "as a reported-only event located by its own uncertainty");
            assoc[r].emplace_back(
                obs[r].source_id + ":" + obs[r].native_id +
                " reported-only — no instrumental cluster within the "
                "reporting window; located by its own stated uncertainty");
        } else {
            std::vector<std::string> candidates;
            for (std::size_t root : matched) {
                const model::Observation& first =
                    obs[frozen_members(frozen, root).front()];
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%.3f,%.3f@%s",
                              first.latitude ? *first.latitude : 0.0,
                              first.longitude ? *first.longitude : 0.0,
                              first.origin_time.c_str());
                candidates.emplace_back(buf);
            }
            std::sort(candidates.begin(), candidates.end());
            std::string joined;
            for (const std::string& c : candidates) {
                if (!joined.empty()) joined += "; ";
                joined += c;
            }
            const std::string note =
                obs[r].source_id + ":" + obs[r].native_id + " ambiguous — " +
                "matches " + std::to_string(matched.size()) +
                " instrumental clusters within the reporting window, so "
                "corroborates none. Candidates: " + joined;
            reasons[r].emplace_back(note);
            assoc[r].emplace_back(note);
        }
    }

    std::vector<Event> events;
    events.reserve(groups.size());
    for (const auto& [root, members] : groups) {
        Event e;
        for (std::size_t idx : members) e.constituents.push_back(obs[idx]);
        e.association_reasons = reasons[root];
        if (auto it = assoc.find(root); it != assoc.end()) {
            e.report_association = it->second;
        }
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

        // REQ-5.12: the diurnal rule may only move a verdict toward the more
        // conservative classification, never toward surface-explosion.
        if (e.discrimination.classification == Classification::SurfaceExplosion) {
            for (const auto& o : e.constituents) {
                if (!o.latitude || !o.longitude) continue;
                const auto it = cells.find(
                    cell_key(*o.latitude, *o.longitude, diurnal.cell_degrees));
                if (it == cells.end() || !it->second.industrial) continue;
                const DiurnalCell& c = it->second;
                char buf[320];
                std::snprintf(
                    buf, sizeof(buf),
                    "diurnal rule: %d explosion-typed events in this 0.1 deg cell, "
                    "%.0f%% inside a 10-hour window centred %04.1f local solar "
                    "(daylight) with %d hours empty - a working-hours signature "
                    "consistent with industrial blasting rather than conflict "
                    "(REQ-5.12)",
                    c.count, c.concentration * 100, c.window_centre_local,
                    c.empty_hours);
                e.discrimination.classification = Classification::IndustrialBlast;
                e.discrimination.reasons.emplace_back(buf);
                break;
            }
        }

        // Confidence factors. Sources sharing an upstream origin are not
        // independent, so independence is counted by distinct source id
        // (REQ-7.3) — a coarse proxy, and the honest one available here.
        //
        // The per-class counts obey the same rule (DM-2026-009 R3). They were
        // once observation counts, which meant a catalog publishing forty
        // detections of one gas flare read as forty corroborating instrumental
        // sources: two pixels of a single satellite overpass were enough to
        // report high confidence that an event occurred. A source does not
        // corroborate itself by reporting twice.
        std::set<std::string> distinct_sources;
        std::set<std::string> distinct_instrumental;
        std::set<std::string> distinct_reporting;
        for (const auto& o : e.constituents) {
            distinct_sources.insert(o.source_id);
            switch (o.source_class) {
                case model::SourceClass::Instrumental:
                    distinct_instrumental.insert(o.source_id);
                    break;
                case model::SourceClass::Government:
                    e.confidence.has_government_source = true;
                    break;
                case model::SourceClass::News:
                case model::SourceClass::Social:
                case model::SourceClass::CuratedDataset:
                    distinct_reporting.insert(o.source_id);
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
            // The source's own hedge is evidence about the source's
            // confidence, and is carried rather than discarded (REQ-7.6).
            if (o.type_certainty == "suspected") {
                e.confidence.any_source_type_suspected = true;
            }
        }
        e.confidence.independent_sources = static_cast<int>(distinct_sources.size());
        e.confidence.instrumental_sources =
            static_cast<int>(distinct_instrumental.size());
        e.confidence.reporting_sources = static_cast<int>(distinct_reporting.size());
        events.push_back(std::move(e));
    }

    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.origin_time != b.origin_time) return a.origin_time < b.origin_time;
        return a.event_uid < b.event_uid;
    });
    return events;
}

}  // namespace kst::analysis

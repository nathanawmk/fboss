/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmStatUpdater.h"

#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmFieldProcessorFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmIngressFieldProcessorFlexCounter.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"

#include "fboss/agent/hw/CounterUtils.h"
#include "fboss/agent/hw/HwResourceStatsPublisher.h"
#include "fboss/agent/hw/bcm/BcmPortUtils.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/Port.h"

#include "fboss/lib/config/PlatformConfigUtils.h"

#include <boost/container/flat_map.hpp>

#include <thrift/lib/cpp/util/EnumUtils.h>

extern "C" {
#include <bcm/field.h>
}

namespace {

struct LaneRateMapKey {
  LaneRateMapKey(uint32 speed, uint32 numLanes, bcm_port_phy_fec_t fecType)
      : speed_(speed), numLanes_(numLanes), fecType_(fecType) {}

  bool operator<(const LaneRateMapKey& other) const {
    return speed_ != other.speed_
        ? (speed_ < other.speed_)
        : (numLanes_ != other.numLanes_ ? (numLanes_ < other.numLanes_)
                                        : (fecType_ < other.fecType_));
  }

  uint32 speed_; /* port speed in Mbps */
  uint32 numLanes_; /* number of lanes */
  bcm_port_phy_fec_t fecType_; /* FEC type */
};
using LaneRateMap = std::map<LaneRateMapKey, double>;

static const LaneRateMap kLaneRateMap = {
    {{10000, 1, bcmPortPhyFecNone}, 10.3125},
    {{10000, 1, bcmPortPhyFecBaseR}, 10.3125},
    {{20000, 1, bcmPortPhyFecNone}, 10.3125},
    {{20000, 1, bcmPortPhyFecBaseR}, 10.3125},
    {{40000, 4, bcmPortPhyFecNone}, 10.3125},
    {{40000, 4, bcmPortPhyFecBaseR}, 10.3125},
    {{40000, 2, bcmPortPhyFecNone}, 10.3125},
    {{25000, 1, bcmPortPhyFecNone}, 25.78125},
    {{25000, 1, bcmPortPhyFecBaseR}, 25.78125},
    {{25000, 1, bcmPortPhyFecRsFec}, 25.7812},
    {{50000, 1, bcmPortPhyFecNone}, 51.5625},
    {{50000, 1, bcmPortPhyFecRsFec}, 51.5625},
    {{50000, 1, bcmPortPhyFecRs544}, 53.125},
    {{50000, 1, bcmPortPhyFecRs272}, 53.125},
    {{50000, 2, bcmPortPhyFecNone}, 25.78125},
    {{50000, 2, bcmPortPhyFecRsFec}, 25.78125},
    {{50000, 2, bcmPortPhyFecRs544}, 26.5625},
    {{100000, 2, bcmPortPhyFecNone}, 51.5625},
    {{100000, 2, bcmPortPhyFecRsFec}, 51.5625},
    {{100000, 2, bcmPortPhyFecRs544}, 53.125},
    {{100000, 2, bcmPortPhyFecRs272}, 53.125},
    {{100000, 4, bcmPortPhyFecNone}, 25.78125},
    {{100000, 4, bcmPortPhyFecRsFec}, 25.78125},
    {{100000, 4, bcmPortPhyFecRs544}, 26.5625},
    {{200000, 4, bcmPortPhyFecNone}, 51.5625},
    {{200000, 4, bcmPortPhyFecRs272}, 53.125},
    {{200000, 4, bcmPortPhyFecRs544}, 53.125},
    {{200000, 4, bcmPortPhyFecRs544_2xN}, 53.125},
    {{400000, 8, bcmPortPhyFecRs544_2xN}, 53.125}};
} // namespace

namespace facebook::fboss {

using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::system_clock;

using facebook::fboss::bcmCheckError;

BcmStatUpdater::BcmStatUpdater(BcmSwitch* hw)
    : hw_(hw),
      bcmTableStatsManager_(std::make_unique<BcmHwTableStatManager>(hw)) {}

void BcmStatUpdater::toBeAddedAclStat(
    BcmAclStatHandle handle,
    const std::string& aclStatName,
    const std::vector<cfg::CounterType>& counterTypes) {
  for (auto type : counterTypes) {
    toBeAddedAclStats_.emplace(BcmAclStatDescriptor(handle, aclStatName), type);
  }
}

void BcmStatUpdater::toBeRemovedAclStat(BcmAclStatHandle handle) {
  toBeRemovedAclStats_.emplace(handle);
}

void BcmStatUpdater::toBeAddedRouteCounter(
    BcmRouteCounterID id,
    const std::string& routeStatName) {
  toBeProcessedRouteCounters_.emplace(id, routeStatName, true);
}

void BcmStatUpdater::toBeRemovedRouteCounter(BcmRouteCounterID id) {
  toBeProcessedRouteCounters_.emplace(id, "", false);
}

void BcmStatUpdater::refreshPostBcmStateChange(const StateDelta& delta) {
  refreshHwTableStats(delta);
  refreshAclStats();
  refreshPrbsStats(delta);
  refreshRouteCounters();
}

void BcmStatUpdater::updateStats() {
  updateAclStats();
  updateHwTableStats();
  updatePrbsStats();
  updateRouteCounters();
}

void BcmStatUpdater::updateAclStats() {
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  auto lockedAclStats = aclStats_.wlock();
  for (auto& entry : *lockedAclStats) {
    // Fetch all counter types at once
    std::vector<cfg::CounterType> counters;
    for (auto& counterItr : entry.second) {
      counters.push_back(counterItr.first);
    }
    for (auto& stat : getAclTrafficStats(entry.first, counters)) {
      entry.second[stat.first]->updateValue(now, stat.second);
    }
  }
}

void BcmStatUpdater::updateRouteCounters() {
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  auto lockedRouteCounters = routeStats_.wlock();
  for (auto& entry : *lockedRouteCounters) {
    entry.second->updateValue(now, getRouteTrafficStats(entry.first));
  }
}

uint64_t BcmStatUpdater::getRouteTrafficStats(BcmRouteCounterID id) {
  uint32 entry = 0;
  bcm_stat_value_t routeCounter;
  auto rc = bcm_stat_flex_counter_get(
      hw_->getUnit(), id, bcmStatFlexStatBytes, 1, &entry, &routeCounter);
  // SDK returns error if counter is not attached to any route
  return rc ? 0 : routeCounter.bytes;
}

void BcmStatUpdater::refreshRouteCounters() {
  if (toBeProcessedRouteCounters_.empty()) {
    return;
  }

  auto lockedRouteCounters = routeStats_.wlock();

  while (!toBeProcessedRouteCounters_.empty()) {
    // Check whether route stat already exists
    auto id = toBeProcessedRouteCounters_.front().id;
    const auto& routeStatName =
        toBeProcessedRouteCounters_.front().routeStatName;
    auto addCounter = toBeProcessedRouteCounters_.front().addCounter;
    auto itr = lockedRouteCounters->find(id);
    if (addCounter) {
      if (itr != lockedRouteCounters->end()) {
        throw FbossError("Duplicate Route stat, id=", id);
      } else {
        lockedRouteCounters->operator[](id) =
            std::make_unique<MonotonicCounter>(
                routeStatName, fb303::SUM, fb303::RATE);
      }
    } else {
      if (itr != lockedRouteCounters->end()) {
        lockedRouteCounters->erase(itr++);
      } else {
        throw FbossError("Cannot find Route stat, id=", id);
      }
    }
    toBeProcessedRouteCounters_.pop();
  }
}

void BcmStatUpdater::updateHwTableStats() {
  HwResourceStatsPublisher().publish(*resourceStats_.rlock());
}

void BcmStatUpdater::updatePrbsStats() {
  uint32 status;
  auto lockedAsicPrbsStats = portAsicPrbsStats_.wlock();
  for (auto& entry : *lockedAsicPrbsStats) {
    auto& lanePrbsStatsTable = entry.second;

    for (auto& lanePrbsStatsEntry : lanePrbsStatsTable) {
      bcm_gport_t gport = lanePrbsStatsEntry.getGportId();
      bcm_port_phy_control_get(
          hw_->getUnit(), gport, BCM_PORT_PHY_CONTROL_PRBS_RX_STATUS, &status);
      if ((int32_t)status == -1) {
        lanePrbsStatsEntry.lossOfLock();
      } else if ((int32_t)status == -2) {
        lanePrbsStatsEntry.locked();
      } else {
        lanePrbsStatsEntry.updateLaneStats(status);
      }
    }
  }
}

double BcmStatUpdater::calculateLaneRate(std::shared_ptr<Port> swPort) {
  auto profileID = swPort->getProfileID();
  const auto& platformPortEntry = hw_->getPlatform()
                                      ->getPlatformPort(swPort->getID())
                                      ->getPlatformPortEntry();
  auto platformPortConfig =
      platformPortEntry.supportedProfiles_ref()->find(profileID);
  if (platformPortConfig == platformPortEntry.supportedProfiles_ref()->end()) {
    throw FbossError(
        "No speed profile with id ",
        apache::thrift::util::enumNameSafe(profileID),
        " found in PlatformPortEntry for ",
        swPort->getName());
  }

  const auto portProfileConfig = hw_->getPlatform()->getPortProfileConfig(
      PlatformPortProfileConfigMatcher(profileID, swPort->getID()));
  if (!portProfileConfig) {
    throw FbossError(
        "Platform doesn't support speed profile: ",
        apache::thrift::util::enumNameSafe(profileID));
  }

  auto portSpeed = static_cast<int>((*portProfileConfig).get_speed());
  auto fecType = utility::phyFecModeToBcmPortPhyFec(
      (*portProfileConfig).get_iphy().get_fec());
  auto numLanes = platformPortConfig->second.pins_ref()->iphy_ref()->size();

  double laneRateGb;
  auto laneRateGbIter =
      kLaneRateMap.find(LaneRateMapKey(portSpeed, numLanes, fecType));
  if (laneRateGbIter != kLaneRateMap.end()) {
    laneRateGb = laneRateGbIter->second;
  } else {
    laneRateGb = portSpeed / 1000 / numLanes;
  }

  double laneRate = laneRateGb * 1024. * 1024. * 1024.;
  return laneRate;
}

size_t BcmStatUpdater::getAclStatCounterCount() const {
  size_t count = 0;
  auto lockedAclStats = aclStats_.rlock();
  for (auto& iter : *lockedAclStats) {
    count += iter.second.size();
  }
  return count;
}

MonotonicCounter* FOLLY_NULLABLE BcmStatUpdater::getAclStatCounterIf(
    BcmAclStatHandle handle,
    cfg::CounterType counterType) {
  auto lockedAclStats = aclStats_.rlock();
  if (auto iter = lockedAclStats->find(handle); iter != lockedAclStats->end()) {
    auto counterIter = iter->second.find(counterType);
    return counterIter != iter->second.end() ? counterIter->second.get()
                                             : nullptr;
  }
  return nullptr;
}

std::vector<cfg::CounterType> BcmStatUpdater::getAclStatCounterType(
    BcmAclStatHandle handle) const {
  std::vector<cfg::CounterType> counterTypes;

  auto lockedAclStats = aclStats_.rlock();
  for (auto& iter : *lockedAclStats) {
    if (iter.first == handle) {
      for (auto& innerIter : iter.second) {
        counterTypes.push_back(innerIter.first);
      }
      break;
    }
  }

  return counterTypes;
}

void BcmStatUpdater::clearPortStats(
    const std::unique_ptr<std::vector<int32_t>>& ports) {
  // XXX clear per queue stats and, BST stats as well.
  for (auto port : *ports) {
    auto ret = bcm_stat_clear(hw_->getUnit(), port);
    if (BCM_FAILURE(ret)) {
      XLOG(ERR) << "Clear Failed for port " << port << " :" << bcm_errmsg(ret);
      return;
    }
  }
}

std::vector<PrbsLaneStats> BcmStatUpdater::getPortAsicPrbsStats(
    int32_t portId) {
  std::vector<PrbsLaneStats> prbsStats;
  auto lockedPortAsicPrbsStats = portAsicPrbsStats_.rlock();
  auto portAsicPrbsStatIter = lockedPortAsicPrbsStats->find(portId);
  if (portAsicPrbsStatIter == lockedPortAsicPrbsStats->end()) {
    throw FbossError(
        "Asic prbs lane error map not initialized for port ", portId);
  }
  auto lanePrbsStatsTable = portAsicPrbsStatIter->second;
  XLOG(DBG3) << "lanePrbsStatsMap size: " << lanePrbsStatsTable.size();

  for (const auto& lanePrbsStats : lanePrbsStatsTable) {
    prbsStats.push_back(lanePrbsStats.getPrbsLaneStats());
  }
  return prbsStats;
}

void BcmStatUpdater::clearPortAsicPrbsStats(int32_t portId) {
  auto lockedPortAsicPrbsStats = portAsicPrbsStats_.wlock();
  auto portAsicPrbsStatIter = lockedPortAsicPrbsStats->find(portId);
  if (portAsicPrbsStatIter == lockedPortAsicPrbsStats->end()) {
    XLOG(ERR) << "Asic prbs lane error map not initialized for port " << portId;
    return;
  }
  auto& lanePrbsStatsTable = portAsicPrbsStatIter->second;
  for (auto& lanePrbsStats : lanePrbsStatsTable) {
    lanePrbsStats.clearLaneStats();
  }
}

void BcmStatUpdater::refreshHwTableStats(const StateDelta& delta) {
  auto stats = resourceStats_.wlock();
  bcmTableStatsManager_->refresh(delta, &(*stats));
}

void BcmStatUpdater::refreshAclStats() {
  if (toBeRemovedAclStats_.empty() && toBeAddedAclStats_.empty()) {
    return;
  }

  auto lockedAclStats = aclStats_.wlock();

  while (!toBeRemovedAclStats_.empty()) {
    auto handle = toBeRemovedAclStats_.front();
    auto itr = lockedAclStats->begin();
    while (itr != lockedAclStats->end()) {
      if (itr->first == handle) {
        lockedAclStats->erase(itr++);
      } else {
        ++itr;
      }
    }
    toBeRemovedAclStats_.pop();
  }

  while (!toBeAddedAclStats_.empty()) {
    // Check whether acl stat already exists
    auto handle = toBeAddedAclStats_.front().first.handle;
    const auto& aclStatName = toBeAddedAclStats_.front().first.aclStatName;
    auto counterType = toBeAddedAclStats_.front().second;
    auto itr = lockedAclStats->find(handle);
    if (itr != lockedAclStats->end()) {
      auto counterItr = itr->second.find(counterType);
      if (counterItr != itr->second.end()) {
        throw FbossError(
            "Duplicate ACL stat, handle=",
            handle,
            ", type=",
            apache::thrift::util::enumNameSafe(counterType));
      }
      // counter name exists, but counter type doesn't
      itr->second[counterType] = std::make_unique<MonotonicCounter>(
          utility::statNameFromCounterType(aclStatName, counterType),
          fb303::SUM,
          fb303::RATE);
    } else {
      lockedAclStats->operator[](handle)[counterType] =
          std::make_unique<MonotonicCounter>(
              utility::statNameFromCounterType(aclStatName, counterType),
              fb303::SUM,
              fb303::RATE);
    }
    toBeAddedAclStats_.pop();
  }
}

void BcmStatUpdater::refreshPrbsStats(const StateDelta& delta) {
  // Add or remove ports into PrbsStats.
  DeltaFunctions::forEachChanged(
      delta.getPortsDelta(),
      [&](const std::shared_ptr<Port>& oldPort,
          const std::shared_ptr<Port>& newPort) {
        if (oldPort->getAsicPrbs() == newPort->getAsicPrbs()) {
          // nothing changed
          return;
        }

        auto lockedPortAsicPrbsStats = portAsicPrbsStats_.wlock();
        if (!(*newPort->getAsicPrbs().enabled_ref())) {
          lockedPortAsicPrbsStats->erase(oldPort->getID());
          return;
        }

        // Find how many lanes does the port associate with.
        auto profileID = newPort->getProfileID();
        if (profileID == cfg::PortProfileID::PROFILE_DEFAULT) {
          XLOG(WARNING)
              << newPort->getName()
              << " has default profile, skip refreshPrbsStats for now";
          return;
        }

        const auto& platformPortEntry = hw_->getPlatform()
                                            ->getPlatformPort(newPort->getID())
                                            ->getPlatformPortEntry();
        const auto& platformPortConfig =
            platformPortEntry.supportedProfiles_ref()->find(profileID);
        if (platformPortConfig ==
            platformPortEntry.supportedProfiles_ref()->end()) {
          throw FbossError(
              "No speed profile with id ",
              apache::thrift::util::enumNameSafe(profileID),
              " found in PlatformPortEntry for ",
              newPort->getName());
        }

        const auto& portProfileConfig =
            hw_->getPlatform()->getPortProfileConfig(
                PlatformPortProfileConfigMatcher(profileID, newPort->getID()));
        if (!portProfileConfig.has_value()) {
          throw FbossError(
              "No port profile with id ",
              apache::thrift::util::enumNameSafe(profileID),
              " found in PlatformConfig for ",
              newPort->getName());
        }

        auto lanePrbsStatsTable = LanePrbsStatsTable();
        for (int lane = 0;
             lane < platformPortConfig->second.pins_ref()->iphy_ref()->size();
             lane++) {
          bcm_gport_t gport;
          BCM_PHY_GPORT_LANE_PORT_SET(gport, lane, newPort->getID());
          lanePrbsStatsTable.push_back(
              LanePrbsStatsEntry(lane, gport, calculateLaneRate(newPort)));
        }
        (*lockedPortAsicPrbsStats)[newPort->getID()] =
            std::move(lanePrbsStatsTable);
      });
}

BcmTrafficCounterStats BcmStatUpdater::getAclTrafficStats(
    BcmAclStatHandle handle,
    const std::vector<cfg::CounterType>& counters) {
  if (hw_->getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_FIELD_PROCESSOR_FLEX_COUNTER)) {
    return BcmIngressFieldProcessorFlexCounter::getAclTrafficFlexCounterStats(
        hw_->getUnit(), handle, counters);
  }
  BcmTrafficCounterStats stats;
  for (auto counterType : counters) {
    uint64_t value;
    auto rv = bcm_field_stat_get(
        hw_->getUnit(),
        handle,
        utility::cfgCounterTypeToBcmCounterType(counterType),
        &value);
    bcmCheckError(rv, "Failed to get bcm_field_stat, handle=", handle);
    stats[counterType] = value;
  }
  return stats;
}
} // namespace facebook::fboss

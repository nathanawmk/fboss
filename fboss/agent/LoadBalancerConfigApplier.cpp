/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/LoadBalancerConfigApplier.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/Platform.h"
#include "fboss/agent/state/NodeBase-defs.h"

#include <utility>

namespace facebook::fboss {

LoadBalancerID LoadBalancerConfigParser::parseLoadBalancerID(
    const cfg::LoadBalancer& loadBalancerConfig) const {
  // LoadBalancerID is an alias for the type of loadBalancerConfig.id so no
  // validation is necessary
  return *loadBalancerConfig.id_ref();
}

std::tuple<
    LoadBalancer::IPv4Fields,
    LoadBalancer::IPv6Fields,
    LoadBalancer::TransportFields,
    LoadBalancer::MPLSFields>
LoadBalancerConfigParser::parseFields(
    const cfg::LoadBalancer& loadBalancer) const {
  LoadBalancer::IPv4Fields v4Fields(
      loadBalancer.fieldSelection_ref()->ipv4Fields_ref()->begin(),
      loadBalancer.fieldSelection_ref()->ipv4Fields_ref()->end());
  LoadBalancer::IPv6Fields v6Fields(
      loadBalancer.fieldSelection_ref()->ipv6Fields_ref()->begin(),
      loadBalancer.fieldSelection_ref()->ipv6Fields_ref()->end());
  LoadBalancer::TransportFields transportFields(
      loadBalancer.fieldSelection_ref()->transportFields_ref()->begin(),
      loadBalancer.fieldSelection_ref()->transportFields_ref()->end());
  LoadBalancer::MPLSFields mplsFields(
      loadBalancer.fieldSelection_ref()->mplsFields_ref()->begin(),
      loadBalancer.fieldSelection_ref()->mplsFields_ref()->end());

  return std::make_tuple(v4Fields, v6Fields, transportFields, mplsFields);
}

std::shared_ptr<LoadBalancer> LoadBalancerConfigParser::parse(
    const cfg::LoadBalancer& cfg) const {
  auto loadBalancerID = parseLoadBalancerID(cfg);
  auto fields = parseFields(cfg);
  auto algorithm = *cfg.algorithm_ref(); // TODO(samank): handle not being set
  auto seed = cfg.seed_ref() ? *cfg.seed_ref()
                             : LoadBalancer::generateDeterministicSeed(
                                   loadBalancerID, platform_->getLocalMac());

  return std::make_shared<LoadBalancer>(
      loadBalancerID,
      algorithm,
      seed,
      std::get<0>(fields),
      std::get<1>(fields),
      std::get<2>(fields),
      std::get<3>(fields));
}

LoadBalancerConfigApplier::LoadBalancerConfigApplier(
    const std::shared_ptr<LoadBalancerMap>& originalLoadBalancers,
    const std::vector<cfg::LoadBalancer>& loadBalancersConfig,
    const Platform* platform)
    : originalLoadBalancers_(originalLoadBalancers),
      loadBalancersConfig_(loadBalancersConfig),
      platform_(platform) {}

LoadBalancerConfigApplier::~LoadBalancerConfigApplier() {}

void LoadBalancerConfigApplier::appendToLoadBalancerContainer(
    LoadBalancerMap::NodeContainer* loadBalancerContainer,
    std::shared_ptr<LoadBalancer> loadBalancer) const {
  bool inserted = false;
  auto loadBalancerID = loadBalancer->getID();
  std::tie(std::ignore, inserted) = loadBalancerContainer->emplace(
      std::make_pair(loadBalancerID, std::move(loadBalancer)));
  CHECK(inserted);
}

std::shared_ptr<LoadBalancerMap>
LoadBalancerConfigApplier::updateLoadBalancers() {
  LoadBalancerMap::NodeContainer newLoadBalancers;
  bool changed = false;

  // This contains the set of LoadBalancerIDs for which loadBalancersConfig has
  // declared a LoadBalancer Thrift config structure. It is used to check that
  // each LoadBalancerID has no more than one such config structure.
  boost::container::flat_set<LoadBalancerID> loadBalancerIDs;
  size_t numExistingProcessed = 0;
  for (const auto& loadBalancerConfig : loadBalancersConfig_) {
    auto newLoadBalancer =
        LoadBalancerConfigParser(platform_).parse(loadBalancerConfig);

    auto rtn = loadBalancerIDs.insert(newLoadBalancer->getID());
    if (!rtn.second) {
      throw FbossError(
          "LoadBalancer ",
          newLoadBalancer->getID(),
          " configured more than once");
    }

    auto origLoadBalancer =
        originalLoadBalancers_->getLoadBalancerIf(newLoadBalancer->getID());

    if (origLoadBalancer) {
      // The LoadBalancer existed in the previous configuration
      ++numExistingProcessed;

      if (*newLoadBalancer == *origLoadBalancer) {
        // It is easy to miss that newLoadBalancer can not be used in place of
        // origLoadBalancer and so warrants an explanation.
        // When two Nodes have the same key, NodeMapDelta unfortunately uses
        // object identity, rather than object equality, to determine if the two
        // Nodes are different.
        // As a result, if newLoadBalancer were passed to
        // appendToLoadBalancerContainer() instead of origLoadBalancer,
        // NodeMapDelta would return them as having changed across the
        // SwitchState because they are different objects.
        newLoadBalancer = origLoadBalancer;
      } else {
        // The LoadBalancer has been modified between the previous config and
        // the current config
        changed |= true;
      }
    } else {
      // The LoadBalancer has been newly added
      changed |= true;
    }

    appendToLoadBalancerContainer(
        &newLoadBalancers, std::move(newLoadBalancer));
  }

  if (numExistingProcessed != originalLoadBalancers_->size()) {
    // Some existing LoadBalancers were removed.
    CHECK_LT(numExistingProcessed, originalLoadBalancers_->size());
    changed = true;
  }

  if (!changed) {
    return nullptr;
  }

  return originalLoadBalancers_->clone(newLoadBalancers);
}

} // namespace facebook::fboss

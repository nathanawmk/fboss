/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/AclMap.h"

#include "fboss/agent/state/NodeMap-defs.h"
#include "fboss/agent/state/NodeMapDelta-defs.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

AclMap::AclMap() {}

AclMap::~AclMap() {}

AclMap* AclMap::modify(std::shared_ptr<SwitchState>* state) {
  if (!isPublished()) {
    CHECK(!(*state)->isPublished());
    return this;
  }

  SwitchState::modify(state);
  auto newAcls = clone();
  auto* ptr = newAcls.get();
  (*state)->resetAcls(std::move(newAcls));
  return ptr;
}

std::set<cfg::AclTableQualifier> PrioAclMap::requiredQualifiers() const {
  std::set<cfg::AclTableQualifier> qualifiers{};
  for (const auto& entry : *this) {
    for (auto qualifier : entry->getRequiredAclTableQualifiers()) {
      qualifiers.insert(qualifier);
    }
  }
  return qualifiers;
}

FBOSS_INSTANTIATE_NODE_MAP(AclMap, AclMapTraits);
FBOSS_INSTANTIATE_NODE_MAP(PrioAclMap, PrioAclMapTraits);

template class NodeMapDelta<
    PrioAclMap,
    DeltaValue<PrioAclMap::Node>,
    MapUniquePointerTraits<PrioAclMap>>;

} // namespace facebook::fboss

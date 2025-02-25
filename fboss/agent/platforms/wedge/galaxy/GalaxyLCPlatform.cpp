/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/platforms/wedge/galaxy/GalaxyLCPlatform.h"

#include "fboss/agent/platforms/common/PlatformProductInfo.h"
#include "fboss/agent/platforms/common/galaxy/GalaxyLCPlatformMapping.h"
#include "fboss/agent/platforms/wedge/WedgePortMapping.h"
#include "fboss/agent/platforms/wedge/galaxy/GalaxyPort.h"

namespace facebook::fboss {

GalaxyLCPlatform::GalaxyLCPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac)
    : GalaxyPlatform(
          std::move(productInfo),
          std::make_unique<GalaxyLCPlatformMapping>(
              GalaxyLCPlatformMapping::getLinecardName()),
          localMac) {}

std::unique_ptr<WedgePortMapping> GalaxyLCPlatform::createPortMapping() {
  return WedgePortMapping::createFromConfig<
      WedgePortMappingT<GalaxyPlatform, GalaxyPort>>(this);
}

} // namespace facebook::fboss

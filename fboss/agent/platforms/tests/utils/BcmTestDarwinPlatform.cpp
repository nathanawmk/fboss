/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/platforms/tests/utils/BcmTestDarwinPlatform.h"
#include "fboss/agent/platforms/common/PlatformProductInfo.h"
#include "fboss/agent/platforms/common/darwin/DarwinPlatformMapping.h"
#include "fboss/agent/platforms/tests/utils/BcmTestDarwinPort.h"

namespace facebook::fboss {

BcmTestDarwinPlatform::BcmTestDarwinPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo)
    : BcmTestWedgeTomahawk3Platform(
          std::move(productInfo),
          std::make_unique<DarwinPlatformMapping>()) {}

std::unique_ptr<BcmTestPort> BcmTestDarwinPlatform::createTestPort(PortID id) {
  return std::make_unique<BcmTestDarwinPort>(id, this);
}

} // namespace facebook::fboss

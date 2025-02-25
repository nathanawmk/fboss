/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/test/HwPortUtils.h"

#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortUtils.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/HwSwitchEnsemble.h"
#include "fboss/agent/hw/test/HwTestPortUtils.h"
#include "fboss/agent/platforms/common/utils/GalaxyLedUtils.h"
#include "fboss/agent/platforms/common/utils/Wedge100LedUtils.h"
#include "fboss/agent/platforms/common/utils/Wedge400LedUtils.h"
#include "fboss/agent/platforms/common/utils/Wedge40LedUtils.h"
#include "fboss/agent/platforms/sai/SaiBcmPlatformPort.h"

#include "fboss/agent/FbossError.h"

#include <gtest/gtest.h>

namespace facebook::fboss::utility {

namespace {
SaiPortTraits::AdapterKey getPortAdapterKey(const HwSwitch* hw, PortID port) {
  auto saiSwitch = static_cast<const SaiSwitch*>(hw);
  auto handle = saiSwitch->managerTable()->portManager().getPortHandle(port);
  CHECK(handle);
  return handle->port->adapterKey();
}
} // namespace
bool portEnabled(const HwSwitch* hw, PortID port) {
  auto key = getPortAdapterKey(hw, port);
  SaiPortTraits::Attributes::AdminState state;
  SaiApiTable::getInstance()->portApi().getAttribute(key, state);
  return state.value();
}

cfg::PortSpeed curPortSpeed(const HwSwitch* hw, PortID port) {
  auto key = getPortAdapterKey(hw, port);
  SaiPortTraits::Attributes::Speed speed;
  SaiApiTable::getInstance()->portApi().getAttribute(key, speed);
  return cfg::PortSpeed(speed.value());
}

void assertPort(
    const HwSwitch* hw,
    PortID port,
    bool enabled,
    cfg::PortSpeed speed) {
  CHECK_EQ(enabled, portEnabled(hw, port));
  if (enabled) {
    // Only verify speed on enabled ports
    CHECK_EQ(
        static_cast<int>(speed),
        static_cast<int>(utility::curPortSpeed(hw, port)));
  }
}

void assertPortStatus(const HwSwitch* hw, PortID port) {
  CHECK(portEnabled(hw, port));
}

void assertPortsLoopbackMode(
    const HwSwitch* hw,
    const std::map<PortID, int>& port2LoopbackMode) {
  for (auto portAndLoopBackMode : port2LoopbackMode) {
    assertPortLoopbackMode(
        hw, portAndLoopBackMode.first, portAndLoopBackMode.second);
  }
}

void assertPortSampleDestination(
    const HwSwitch* /*hw*/,
    PortID /*port*/,
    int /*expectedSampleDestination*/) {
  throw FbossError("sampling is unsupported for SAI");
}

void assertPortsSampleDestination(
    const HwSwitch* /*hw*/,
    const std::map<PortID, int>& /*port2SampleDestination*/) {
  throw FbossError("sampling is unsupported for SAI");
}

void assertPortLoopbackMode(
    const HwSwitch* hw,
    PortID port,
    int expectedLoopbackMode) {
  auto key = getPortAdapterKey(hw, port);
  SaiPortTraits::Attributes::InternalLoopbackMode loopbackMode;
  SaiApiTable::getInstance()->portApi().getAttribute(key, loopbackMode);
  CHECK_EQ(expectedLoopbackMode, loopbackMode.value());
}

void cleanPortConfig(
    cfg::SwitchConfig* config,
    std::vector<PortID> allPortsinGroup) {
  // remove portCfg not in allPortsinGroup
  auto removed = std::remove_if(
      config->ports_ref()->begin(),
      config->ports_ref()->end(),
      [&allPortsinGroup](auto portCfg) {
        auto portID = static_cast<PortID>(*portCfg.logicalID_ref());
        for (auto id : allPortsinGroup) {
          if (portID == id) {
            return false;
          }
        }
        return true;
      });
  config->ports_ref()->erase(removed, config->ports_ref()->end());
}

void assertQUADMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    std::vector<PortID> allPortsinGroup) {
  utility::assertPort(hw, allPortsinGroup[0], true, enabledLaneSpeed);
}

void assertDUALMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    cfg::PortSpeed /*disabledLaneSpeed*/,
    std::vector<PortID> allPortsinGroup) {
  bool odd_lane;
  auto portItr = allPortsinGroup.begin();
  for (; portItr != allPortsinGroup.end(); portItr++) {
    odd_lane = (*portItr - allPortsinGroup.front()) % 2 == 0 ? true : false;
    if (odd_lane) {
      utility::assertPort(hw, *portItr, true, enabledLaneSpeed);
    }
  }
}
void assertSINGLEMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    cfg::PortSpeed /*disabledLaneSpeed*/,
    std::vector<PortID> allPortsinGroup) {
  utility::assertPort(hw, allPortsinGroup[0], true, enabledLaneSpeed);
}

void verifyInterfaceMode(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const phy::ProfileSideConfig& expectedProfileConfig) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  if (!saiPlatform->getAsic()->isSupported(
          HwAsic::Feature::PORT_INTERFACE_TYPE) ||
      !saiPlatform->supportInterfaceType()) {
    return;
  }
  auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto* saiPortHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(portID);

  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto speed = portApi.getAttribute(
      saiPortHandle->port->adapterKey(), SaiPortTraits::Attributes::Speed{});

  if (!expectedProfileConfig.medium_ref()) {
    throw FbossError(
        "Missing medium info in profile ",
        apache::thrift::util::enumNameSafe(profileID));
  }
  auto transmitterTech = *expectedProfileConfig.medium_ref();
  auto expectedInterfaceType = saiPlatform->getInterfaceType(
      transmitterTech, static_cast<cfg::PortSpeed>(speed));
  auto programmedInterfaceType = portApi.getAttribute(
      saiPortHandle->port->adapterKey(),
      SaiPortTraits::Attributes::InterfaceType{});
  EXPECT_EQ(expectedInterfaceType.value(), programmedInterfaceType);
}

void verifyTxSettting(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const std::vector<phy::PinConfig>& expectedPinConfigs) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  if (!saiPlatform->isSerdesApiSupported()) {
    return;
  }

  auto numExpectedTxLanes = 0;
  for (const auto& pinConfig : expectedPinConfigs) {
    if (auto tx = pinConfig.tx_ref()) {
      ++numExpectedTxLanes;
    }
  }
  if (numExpectedTxLanes == 0) {
    return;
  }

  auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto* saiPortHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(portID);
  // Prepare expected SaiPortSerdesTraits::CreateAttributes
  SaiPortSerdesTraits::CreateAttributes expectedTx =
      saiSwitch->managerTable()->portManager().serdesAttributesFromSwPinConfigs(
          saiPortHandle->port->adapterKey(), expectedPinConfigs);

  auto serdes = saiPortHandle->serdes;

  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto pre = portApi.getAttribute(
      serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirPre1{});
  auto main = portApi.getAttribute(
      serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirMain{});
  auto post = portApi.getAttribute(
      serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirPost1{});

  EXPECT_EQ(pre, GET_OPT_ATTR(PortSerdes, TxFirPre1, expectedTx));
  EXPECT_EQ(main, GET_OPT_ATTR(PortSerdes, TxFirMain, expectedTx));
  EXPECT_EQ(post, GET_OPT_ATTR(PortSerdes, TxFirPost1, expectedTx));
  if (auto expectedDriveCurrent =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::IDriver>>(
              expectedTx)) {
    auto driverCurrent = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::IDriver{});
    EXPECT_EQ(driverCurrent, expectedDriveCurrent->value());
  }

  // Also need to check Preemphasis is set correctly
  if (saiPlatform->getAsic()->getPortSerdesPreemphasis().has_value()) {
    EXPECT_EQ(
        portApi.getAttribute(
            serdes->adapterKey(),
            SaiPortSerdesTraits::Attributes::Preemphasis{}),
        GET_OPT_ATTR(PortSerdes, Preemphasis, expectedTx));
  }
}

void verifyLedStatus(HwSwitchEnsemble* ensemble, PortID port, bool up) {
  SaiPlatform* platform = static_cast<SaiPlatform*>(ensemble->getPlatform());
  SaiPlatformPort* platformPort = platform->getPort(port);
  uint32_t currentVal = platformPort->getCurrentLedState();
  uint32_t expectedVal = 0;
  switch (platform->getMode()) {
    case PlatformMode::WEDGE: {
      expectedVal =
          static_cast<uint32_t>(Wedge40LedUtils::getExpectedLEDState(up, up));
    } break;
    case PlatformMode::WEDGE100: {
      expectedVal = static_cast<uint32_t>(Wedge100LedUtils::getExpectedLEDState(
          platform->getLaneCount(platformPort->getCurrentProfile()), up, up));
    } break;
    case PlatformMode::GALAXY_FC:
    case PlatformMode::GALAXY_LC: {
      expectedVal = GalaxyLedUtils::getExpectedLEDState(up, up);
    } break;
    case PlatformMode::WEDGE400:
    case PlatformMode::WEDGE400C: {
      expectedVal = static_cast<uint32_t>(Wedge400LedUtils::getLedState(
          platform->getLaneCount(platformPort->getCurrentProfile()), up, up));
    } break;
    default:
      return;
  }
  EXPECT_EQ(currentVal, expectedVal);
}

void verifyRxSettting(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const std::vector<phy::PinConfig>& expectedPinConfigs) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  if (!saiPlatform->isSerdesApiSupported()) {
    return;
  }

  auto numExpectedRxLanes = 0;
  for (const auto& pinConfig : expectedPinConfigs) {
    if (auto tx = pinConfig.rx_ref()) {
      ++numExpectedRxLanes;
    }
  }

  auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto* saiPortHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(portID);
  auto serdes = saiPortHandle->serdes;

  if (!serdes) {
    EXPECT_TRUE(numExpectedRxLanes == 0);
    return;
  }
  if (numExpectedRxLanes == 0) {
    // not all platforms may have these settings
    return;
  }

  // Prepare expected SaiPortSerdesTraits::CreateAttributes
  SaiPortSerdesTraits::CreateAttributes expectedSerdes =
      saiSwitch->managerTable()->portManager().serdesAttributesFromSwPinConfigs(
          saiPortHandle->port->adapterKey(), expectedPinConfigs);

  auto& portApi = SaiApiTable::getInstance()->portApi();
  if (auto expectedRxCtlCode =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::RxCtleCode>>(
              expectedSerdes)) {
    auto rxCtlCode = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::RxCtleCode{});
    EXPECT_EQ(rxCtlCode, expectedRxCtlCode->value());
  }
  if (auto expectedRxDspMode =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::RxDspMode>>(
              expectedSerdes)) {
    auto rxDspMode = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::RxDspMode{});
    EXPECT_EQ(rxDspMode, expectedRxDspMode->value());
  }
  if (auto expectedRxAfeTrim =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::RxAfeTrim>>(
              expectedSerdes)) {
    auto rxAafeTrim = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::RxAfeTrim{});
    EXPECT_EQ(rxAafeTrim, expectedRxAfeTrim->value());
  }
  if (auto expectedRxAcCouplingBypass = std::get<
          std::optional<SaiPortSerdesTraits::Attributes::RxAcCouplingByPass>>(
          expectedSerdes)) {
    auto rxAcCouplingBypass = portApi.getAttribute(
        serdes->adapterKey(),
        SaiPortSerdesTraits::Attributes::RxAcCouplingByPass{});
    EXPECT_EQ(rxAcCouplingBypass, expectedRxAcCouplingBypass->value());
  }
}

void verifyFec(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const phy::ProfileSideConfig& expectedProfileConfig) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto* saiPortHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(portID);

  // retrive configured fec.
  auto expectedFec =
      utility::getSaiPortFecMode(*expectedProfileConfig.fec_ref());

  // retrive programmed fec.
  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto programmedFec = portApi.getAttribute(
      saiPortHandle->port->adapterKey(), SaiPortTraits::Attributes::FecMode{});

  EXPECT_EQ(expectedFec, programmedFec);
}
} // namespace facebook::fboss::utility

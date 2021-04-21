/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/cli/fboss2/CmdHandler.h"

namespace facebook::fboss {

struct CmdShowNdpTraits {
  using ClientType = facebook::fboss::FbossCtrlAsyncClient;
  using RetType = std::vector<facebook::fboss::NdpEntryThrift>;
};

class CmdShowNdp : public CmdHandler<CmdShowNdp, CmdShowNdpTraits> {
 public:
  using ClientType = CmdShowNdpTraits::ClientType;
  using RetType = CmdShowNdpTraits::RetType;

  RetType queryClient(
      const std::unique_ptr<ClientType>& client) {
    RetType retVal;

    client->sync_getNdpTable(retVal);
    return retVal;
  }

  void printOutput(const RetType& ndpEntries) {
    std::string fmtString =
        "{:<45}{:<19}{:<12}{:<19}{:<14}{:<9}{:<12}\n";

    std::cout << fmt::format(
        fmtString,
        "IP Address",
        "MAC Address",
        "Port",
        "VLAN",
        "State",
        "TTL",
        "CLASSID");

    for (auto const& ndpEntry : ndpEntries) {
      auto ip = folly::IPAddress::fromBinary(
          folly::ByteRange(folly::StringPiece(ndpEntry.get_ip().get_addr())));
      auto vlan = folly::to<std::string>(
          ndpEntry.get_vlanName(), " (", ndpEntry.get_vlanID(), ")");

      std::cout << fmt::format(
          fmtString,
          ip.str(),
          ndpEntry.get_mac(),
          ndpEntry.get_port(),
          vlan,
          ndpEntry.get_state(),
          ndpEntry.get_ttl(),
          ndpEntry.get_classID());
    }
    std::cout << std::endl;
  }

};

} // namespace facebook::fboss
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
#include "fboss/cli/fboss2/commands/show/mka/gen-cpp2/model_types.h"
#include "fboss/mka_service/if/gen-cpp2/MKAService.h"

namespace facebook::fboss {

struct CmdShowMkaTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::ShowMkaModel;
};

class CmdShowMka : public CmdHandler<CmdShowMka, CmdShowMkaTraits> {
 private:
  void cachePortInfo(const HostInfo& hostInfo) {
    auto client =
        utils::createClient<facebook::fboss::FbossCtrlAsyncClient>(hostInfo);
    client->sync_getAllPortInfo(portId2Info_);
  }

  std::string getPortName(const std::string& inPort) const {
    try {
      auto portId = folly::to<int32_t>(inPort);
      return *portId2Info_.find(portId)->second.name_ref();
    } catch (const std::exception& ) {
      return inPort;
    }
  }
  static std::string strTime(int64_t secsSinceEpoch) {
    if (!secsSinceEpoch) {
      return "--";
    }
    time_t t(secsSinceEpoch);
    std::string timeStr = std::ctime(&t);
    // Trim trailing newline from ctime output
    auto it = std::find_if(timeStr.rbegin(), timeStr.rend(), [](char c) {
      return !std::isspace(c);
    });
    timeStr.erase(it.base(), timeStr.end());
    return timeStr;
  };

 public:
  using RetType = CmdShowMkaTraits::RetType;
  RetType queryClient(const HostInfo& hostInfo) {
    cachePortInfo(hostInfo);
    auto client =
        utils::createClient<facebook::fboss::mka::MKAServiceAsyncClient>(
            hostInfo);

    std::vector<::facebook::fboss::mka::MKASessionInfo> mkaEntries;
    client->sync_getSessions(mkaEntries);
    return createModel(mkaEntries);
  }
  RetType createModel(
      std::vector<facebook::fboss::mka::MKASessionInfo> mkaEntries) {
    RetType model;
    auto makeMkaProfile = [](const auto& participantCtx,
                             const auto& activePeers,
                             const auto& potentialPeers) {
      cli::MkaProfile profile;
      profile.srcMac_ref() = *participantCtx.srcMac_ref();
      profile.ckn_ref() = *participantCtx.cak_ref()->ckn_ref();
      profile.keyServerElected_ref() = *participantCtx.elected_ref();
      profile.sakRxInstalledSince_ref() =
          strTime(*participantCtx.sakEnabledRxSince_ref());
      profile.sakTxInstalledSince_ref() =
          strTime(*participantCtx.sakEnabledTxSince_ref());
      profile.sakRotatedAt_ref() =
          strTime(*participantCtx.sakRotatedAt_ref());
      profile.activePeers_ref() = activePeers;
      profile.potentialPeers_ref() = potentialPeers;
      return profile;
    };
    for (const auto& entry : mkaEntries) {
      auto& participantCtx = *entry.participantCtx_ref();
      cli::MkaEntry modelEntry;
      modelEntry.primaryProfile_ref() = makeMkaProfile(
          participantCtx,
          *entry.activePeersPrimary_ref(),
          *entry.potentialPeersPrimary_ref());
      if (entry.secondaryParticipantCtx_ref()) {
        modelEntry.secondaryProfile_ref() = makeMkaProfile(
            participantCtx,
            *entry.activePeersSecondary_ref(),
            *entry.potentialPeersSecondary_ref());
      }
      modelEntry.encryptedSak_ref() = *entry.encryptedSak_ref();
      model.portToMkaEntry_ref()[*participantCtx.l2Port_ref()] = modelEntry;
    }
    return model;
  }
  void printOutput(const RetType& model, std::ostream& out = std::cout) {
    for (auto const& portAndEntry : model.get_portToMkaEntry()) {
      out << "Port: " << getPortName(portAndEntry.first) << std::endl;
      out << std::string(20, '=') << std::endl;

      auto printProfile = [&out](const auto& profile, bool isPrimary) {
        out << " MAC: " << profile.get_srcMac() << std::endl;
        out << " CKN: " << profile.get_ckn() << " ("
            << (isPrimary ? "Primary" : " Secondary") << ")" << std::endl;
        out << " Keyserver elected: "
            << (profile.get_keyServerElected() ? "Y" : "N") << std::endl;
        out << " SAK installed since: "
            << " rx: " << profile.get_sakRxInstalledSince()
            << " tx: " << profile.get_sakTxInstalledSince() << std::endl;
        out << " SAK rotated at: " << profile.get_sakRotatedAt() << std::endl;
        auto printPeers = [&out](const auto& type, const auto& peers) {
          if (!peers.size()) {
            return;
          }
          out << type << std::endl;
          for (const auto& peer : peers) {
            out << "\t"
                << " id: " << *peer.id_ref() << std::endl;
            out << "\t"
                << " live since: " << strTime(*peer.liveSince_ref()) << std::endl;
            out << "\t"
                << " priority: " << *peer.priority_ref() << std::endl;
            out << "\t"
                << " sakUsed: " << *peer.sakUsed_ref() << std::endl;
            out << "\t"
                << " isKeyServer: " << *peer.isKeyServer_ref() << std::endl;
            out << "\t"
                << " Secure Channel Identifier: "
                << *peer.secureChannelIdentifier_ref() << std::endl;
            out << "\t"
                << "Message number: " << *peer.messageNumber_ref() << std::endl;
          }
        };
        printPeers(" Active peers ", profile.get_activePeers());
        printPeers(" Potential peers ", profile.get_potentialPeers());
      };
      auto& entry = portAndEntry.second;
      printProfile(*entry.primaryProfile_ref(), true);
      if (entry.secondaryProfile_ref()) {
        printProfile(*entry.secondaryProfile_ref(), false);
      }
      out << " Encrypted SAK: " << entry.get_encryptedSak() << std::endl;
    }
  }

 private:
  std::map<int32_t, facebook::fboss::PortInfoThrift> portId2Info_;
};

} // namespace facebook::fboss
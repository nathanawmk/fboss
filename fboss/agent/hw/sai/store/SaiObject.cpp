// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/api/AclApi.h"

namespace facebook {
namespace fboss {

template <>
folly::dynamic
SaiObject<SaiNextHopGroupTraits>::adapterHostKeyToFollyDynamic() {
  folly::dynamic json = folly::dynamic::array;
  for (auto& ahk : adapterHostKey_) {
    folly::dynamic object = folly::dynamic::object;
    object[AttributeName<SaiIpNextHopTraits::Attributes::Type>::value] =
        folly::to<std::string>(ahk.index());

    if (auto ipAhk = std::get_if<SaiIpNextHopTraits::AdapterHostKey>(&ahk)) {
      object[AttributeName<
          SaiIpNextHopTraits::Attributes::RouterInterfaceId>::value] =
          folly::to<std::string>(
              std::get<SaiIpNextHopTraits::Attributes::RouterInterfaceId>(
                  *ipAhk)
                  .value());
      object[AttributeName<SaiIpNextHopTraits::Attributes::Ip>::value] =
          std::get<SaiIpNextHopTraits::Attributes::Ip>(*ipAhk).value().str();
    } else if (
        auto mplsAhk =
            std::get_if<SaiMplsNextHopTraits::AdapterHostKey>(&ahk)) {
      object[AttributeName<
          SaiMplsNextHopTraits::Attributes::RouterInterfaceId>::value] =
          folly::to<std::string>(
              std::get<SaiMplsNextHopTraits::Attributes::RouterInterfaceId>(
                  *mplsAhk)
                  .value());
      object[AttributeName<SaiMplsNextHopTraits::Attributes::Ip>::value] =
          std::get<SaiMplsNextHopTraits::Attributes::Ip>(*mplsAhk)
              .value()
              .str();
      object
          [AttributeName<SaiMplsNextHopTraits::Attributes::LabelStack>::value] =
              folly::dynamic::array;
      for (auto label :
           std::get<SaiMplsNextHopTraits::Attributes::LabelStack>(*mplsAhk)
               .value()) {
        object
            [AttributeName<SaiMplsNextHopTraits::Attributes::LabelStack>::value]
                .push_back(folly::to<std::string>(label));
      }
    }
    json.push_back(object);
  }
  return json;
}

template <>
typename SaiNextHopGroupTraits::AdapterHostKey
SaiObject<SaiNextHopGroupTraits>::follyDynamicToAdapterHostKey(
    folly::dynamic json) {
  SaiNextHopGroupTraits::AdapterHostKey key;
  for (auto object : json) {
    auto type =
        object[AttributeName<SaiIpNextHopTraits::Attributes::Type>::value]
            .asInt();
    switch (type) {
      case SAI_NEXT_HOP_TYPE_IP: {
        SaiIpNextHopTraits::AdapterHostKey ipAhk;

        std::get<SaiIpNextHopTraits::Attributes::RouterInterfaceId>(ipAhk) =
            folly::to<sai_object_id_t>(
                object[AttributeName<SaiIpNextHopTraits::Attributes::
                                         RouterInterfaceId>::value]
                    .asString());
        std::get<SaiIpNextHopTraits::Attributes::Ip>(ipAhk) = folly::IPAddress(
            object[AttributeName<SaiIpNextHopTraits::Attributes::Ip>::value]
                .asString());
        key.insert(ipAhk);
      } break;

      case SAI_NEXT_HOP_TYPE_MPLS: {
        SaiMplsNextHopTraits::AdapterHostKey mplsAhk;

        std::get<SaiMplsNextHopTraits::Attributes::RouterInterfaceId>(mplsAhk) =
            folly::to<sai_object_id_t>(
                object[AttributeName<SaiMplsNextHopTraits::Attributes::
                                         RouterInterfaceId>::value]
                    .asString());
        std::get<SaiMplsNextHopTraits::Attributes::Ip>(mplsAhk) =
            folly::IPAddress(
                object
                    [AttributeName<SaiMplsNextHopTraits::Attributes::Ip>::value]
                        .asString());
        std::vector<sai_uint32_t> stack;
        for (auto label : object[AttributeName<
                 SaiMplsNextHopTraits::Attributes::LabelStack>::value]) {
          stack.push_back(

              label.asInt());
        }
        std::get<SaiMplsNextHopTraits::Attributes::LabelStack>(mplsAhk) = stack;
        key.insert(mplsAhk);
      } break;

      default:
        XLOG(FATAL) << "unsupported next hop type " << type;
    }
  }
  return key;
}

template <>
folly::dynamic SaiObject<SaiLagTraits>::adapterHostKeyToFollyDynamic() {
  const auto& value = adapterHostKey_.value();
  std::string label{std::begin(value), std::end(value)};
  return label;
}

template <>
typename SaiLagTraits::AdapterHostKey
SaiObject<SaiLagTraits>::follyDynamicToAdapterHostKey(folly::dynamic json) {
  std::string label = json.asString();
  SaiLagTraits::AdapterHostKey::ValueType key{};
  std::copy(std::begin(label), std::end(label), std::begin(key));
  return key;
}

template <>
folly::dynamic SaiObject<SaiAclTableTraits>::adapterHostKeyToFollyDynamic() {
  return adapterHostKey_;
}

template <>
typename SaiAclTableTraits::AdapterHostKey
SaiObject<SaiAclTableTraits>::follyDynamicToAdapterHostKey(
    folly::dynamic json) {
  return json.asString();
}
} // namespace fboss
} // namespace facebook

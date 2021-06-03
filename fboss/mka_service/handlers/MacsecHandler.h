// Copyright 2004-present Facebook.  All rights reserved.
#pragma once

#include <fboss/mka_service/if/gen-cpp2/mka_types.h>

namespace facebook {
namespace fboss {
namespace mka {

// Handler that configures the macsec interfaces and other macsec options
class MacsecHandler {
 public:
  MacsecHandler() = default;
  virtual ~MacsecHandler() = default;

  /**
   * Install SAK in Rx
   **/
  virtual bool sakInstallRx(const MKASak& sak, const MKASci& sci) = 0;

  /**
   * Install SAK in Tx
   **/
  virtual bool sakInstallTx(const MKASak& sak) = 0;

  /**
   * Delete SAK in Rx
   **/
  virtual bool sakDeleteRx(const MKASak& sak, const MKASci& sci) = 0;

  /**
   * Clear macsec on this interface.
   */
  virtual bool sakDelete(const MKASak& sak) = 0;

  /*
   * Check if the interface is healthy. If the return value is false,
   * then re-negotiation will start.
   */

  virtual MKASakHealthResponse sakHealthCheck(const MKASak& sak) = 0;

 private:
  MacsecHandler(const MacsecHandler&) = delete;
  MacsecHandler& operator=(const MacsecHandler&) = delete;
};

} // namespace mka
} // namespace fboss
} // namespace facebook
// Copyright 2004-present Facebook. All Rights Reserved.

#include <thrift/lib/cpp2/server/ThriftServer.h>

#include <folly/experimental/FunctionScheduler.h>
#include <folly/logging/Init.h>

#include "fboss/qsfp_service/QsfpServer.h"
#include "fboss/qsfp_service/QsfpServiceHandler.h"
#include "fboss/qsfp_service/StatsPublisher.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManagerInit.h"

using namespace facebook;
using namespace facebook::fboss;

DEFINE_int32(
    stats_publish_interval,
    300,
    "Interval (in seconds) for publishing stats");
DEFINE_int32(
    loop_interval,
    5,
    "Interval (in seconds) to run the main loop that determines "
    "if we need to change or fetch data for transceivers");
DEFINE_int32(
    xphy_stats_loop_interval,
    60,
    "Interval (in seconds) to run the loop that updates all xphy ports stats");

DECLARE_int32(port);

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG2; default:async=true");

void initFlagDefaults(int argc, char** argv) {
  // one pass over flags, but don't clear argc/argv. We only do this
  // to extract the 'qsfp_config' arg.
  gflags::ParseCommandLineFlags(&argc, &argv, false);
  auto qsfpConfig = QsfpConfig::fromDefaultFile();
  for (auto item : *qsfpConfig->thrift.defaultCommandLineArgs_ref()) {
    // logging not initialized yet, need to use std::cerr
    std::cerr << "Overriding default flag from config: " << item.first.c_str()
              << "=" << item.second.c_str() << std::endl;
    gflags::SetCommandLineOptionWithMode(
        item.first.c_str(), item.second.c_str(), gflags::SET_FLAGS_DEFAULT);
  }
}

int main(int argc, char** argv) {
  // Read the config and set default command line arguments
  initFlagDefaults(argc, argv);

  qsfpServiceInit(&argc, &argv);

  auto transceiverManager = createWedgeManager();
  StatsPublisher publisher(transceiverManager.get());

  auto [server, handler] = setupThriftServer(std::move(transceiverManager));
  // Create Platform specific FbossMacsecHandler object
  folly::FunctionScheduler scheduler;
  // init after handler has been initted - this ensures everything is setup
  // before we try to retrieve stats for it
  publisher.init();
  scheduler.addFunction(
      [&publisher, server = server]() {
        publisher.publishStats(
            server->getEventBaseManager()->getEventBase(),
            FLAGS_stats_publish_interval);
      },
      std::chrono::seconds(FLAGS_stats_publish_interval),
      "statsPublish");
  scheduler.addFunction(
      [mgr = handler->getTransceiverManager()]() {
        mgr->refreshTransceivers();
      },
      std::chrono::seconds(FLAGS_loop_interval),
      "refreshTransceivers");

  // Schedule the function to periodically send the I2c transaction
  // stats to the ServiceData object which gets pulled by FBagent.
  // The function is called from abstract base class TransceiverManager
  // which gets implemented by platfdorm aware class inheriting
  // this class
  scheduler.addFunction(
      [mgr = handler->getTransceiverManager()]() {
        mgr->publishI2cTransactionStats();
      },
      std::chrono::seconds(FLAGS_loop_interval),
      "publishI2cTransactionStats");

  // Schedule the function to periodically collect xphy stats if there's a
  // PhyManager
  if (auto* mgr = handler->getTransceiverManager(); mgr->getPhyManager()) {
    scheduler.addFunction(
        [mgr]() { mgr->updateAllXphyPortsStats(); },
        std::chrono::seconds(FLAGS_xphy_stats_loop_interval),
        "updateAllXphyPortsStats");
  }

  // Note: This doesn't block, this merely starts it's own thread
  scheduler.start();

  // Start the server loop
  doServerLoop(server, handler);

  return 0;
}

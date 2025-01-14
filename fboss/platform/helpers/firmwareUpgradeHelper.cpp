// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/platform/helpers/firmwareUpgradeHelper.h"

namespace facebook::fboss::platform::helpers {
/*
 *checkCmdStatus
 *Validate the command exit status of system() for errors
 */

void checkCmdStatus(const std::string& cmd, int exitStatus) {
  if (exitStatus < 0) {
    throw std::runtime_error(
        "Running command " + cmd + " failed with errno " +
        folly::errnoStr(errno));
  } else if (!WIFEXITED(exitStatus)) {
    throw std::runtime_error(
        "Running Command " + cmd + " exited abnormally " +
        std::to_string(WEXITSTATUS(exitStatus)));
  }
}

bool isFilePresent(std::string file) {
  return std::filesystem::exists(file);
}

void i2cRegWrite(
    std::string bus,
    std::string devAddress,
    std::string regAddress,
    uint8_t value = 0x00) {
  int out = 0;
  const std::string cmd = folly::to<std::string>(
      "i2cset -f -y ",
      bus,
      " ",
      devAddress,
      " ",
      regAddress,
      " ",
      std::to_string(value));
  execCommand(cmd, out);
  if (out) {
    LOG(ERROR) << "Error running " << cmd;
  }
}

int runCmd(const std::string& cmd) {
  auto subProc = folly::Subprocess(folly::shellify(cmd));
  auto ret = subProc.wait().exitStatus();
  return ret;
}

void jamUpgrade(std::string fpga, std::string action, std::string fpgaFile) {
  if (action == "read") {
    std::cout << action << " not supported for " << fpga << std::endl;
  }
  const std::string cmd = folly::to<std::string>(
      "/usr/bin/jam -a", action, " -f", fpga, " -v ", fpgaFile);
  int ret = runCmd(cmd);
  if (ret < 0) {
    throw std::runtime_error(folly::to<std::string>("Error running ", cmd));
  }
}

void xappUpgrade(std::string fpga, std::string action, std::string fpgaFile) {
  if (action == "verify" || action == "read") {
    std::cout << action << " not supported for " << fpga << std::endl;
    exit(0);
  }
  const std::string cmd =
      folly::to<std::string>("/usr/bin/xapp -f ", fpga, " ", fpgaFile);
  int ret = runCmd(cmd);
  if (ret < 0) {
    throw std::runtime_error(folly::to<std::string>("Error running ", cmd));
  }
}

void flashromBiosUpgrade(
    std::string action,
    std::string biosFile,
    std::string chip,
    std::string layout) {
  const std::string readBiosCmd = folly::to<std::string>(
      "flashrom -p internal ",
      chip,
      " -l ",
      layout,
      " -i normal -i fallback -r ",
      biosFile);
  const std::string verifyBiosCmd = folly::to<std::string>(
      "flashrom -p internal ",
      chip,
      " -l ",
      layout,
      " -i normal -i fallback -v ",
      biosFile);
  const std::string writeBiosCmd = folly::to<std::string>(
      "flashrom -p internal ",
      chip,
      " -l ",
      layout,
      " -i normal -i fallback --noverify-all -w ",
      biosFile);
  int exitStatus = 0;

  if (action == std::string("read")) {
    exitStatus = runCmd(readBiosCmd);
    checkCmdStatus(readBiosCmd, exitStatus);
  } else if (action == std::string("verify")) {
    exitStatus = runCmd(verifyBiosCmd);
    checkCmdStatus(verifyBiosCmd, exitStatus);
  } else if (action == std::string("program")) {
    exitStatus = runCmd(writeBiosCmd);
    checkCmdStatus(writeBiosCmd, exitStatus);
    exitStatus = runCmd(verifyBiosCmd);
    checkCmdStatus(verifyBiosCmd, exitStatus);
  }
}

} // namespace facebook::fboss::platform::helpers

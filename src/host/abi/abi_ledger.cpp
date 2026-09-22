#include "tgles/host/abi_ledger.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tgles {
namespace host {
namespace {

// Insertion order matters: a failing CTS case should report the same first
// missing gap on every run, so the vector keeps discovery order and the map
// only serves the call counter.
struct Ledger {
  std::mutex mutex;
  std::vector<std::string> order;
  std::unordered_map<std::string, unsigned long long> calls;
};

Ledger& Instance() {
  static Ledger ledger;
  return ledger;
}

}  // namespace

void LedgerRecord(const char* name) {
  if (name == nullptr) return;
  Ledger& ledger = Instance();
  std::lock_guard<std::mutex> lock(ledger.mutex);
  auto it = ledger.calls.find(name);
  if (it == ledger.calls.end()) {
    ledger.order.emplace_back(name);
    ledger.calls.emplace(name, 1);
  } else {
    ++it->second;
  }
}

std::size_t LedgerSize() {
  Ledger& ledger = Instance();
  std::lock_guard<std::mutex> lock(ledger.mutex);
  return ledger.order.size();
}

const char* LedgerNameAt(std::size_t index) {
  Ledger& ledger = Instance();
  std::lock_guard<std::mutex> lock(ledger.mutex);
  return index < ledger.order.size() ? ledger.order[index].c_str() : nullptr;
}

unsigned long long LedgerCalls(const char* name) {
  if (name == nullptr) return 0;
  Ledger& ledger = Instance();
  std::lock_guard<std::mutex> lock(ledger.mutex);
  auto it = ledger.calls.find(name);
  return it == ledger.calls.end() ? 0 : it->second;
}

void LedgerReset() {
  Ledger& ledger = Instance();
  std::lock_guard<std::mutex> lock(ledger.mutex);
  ledger.order.clear();
  ledger.calls.clear();
}

}  // namespace host
}  // namespace tgles

#include "job/download_concurrency.h"

#include <QThread>

#include <algorithm>
#include <cstdint>

namespace xgs::job {

int concurrencyForCores(int cores) {
    // Kotlin's Int multiplication wraps around; the unsigned product converted back does the same.
    const int product = static_cast<int>(static_cast<std::uint32_t>(cores) * 7u);
    return std::max(product / 10, 1);
}

int automaticConcurrency() {
    return concurrencyForCores(std::max(QThread::idealThreadCount(), 1));
}

}  // namespace xgs::job

#pragma once

// Port of engine/.../job/DownloadConcurrency.kt: how many items of a job are downloaded at the same time, 70% of the
// processors of the machine, at least one.
namespace xgs::job {

int concurrencyForCores(int cores);   // (cores * 7 / 10) rounded down, at least 1
int automaticConcurrency();           // for QThread::idealThreadCount() (the logical processors, hyper-threads included)

}  // namespace xgs::job

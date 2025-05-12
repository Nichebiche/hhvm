/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <thrift/conformance/stresstest/util/IoUringUtil.h>

DEFINE_bool(use_iouring_event_eventfd, true, "");
DEFINE_int32(io_capacity, 0, "");
DEFINE_int32(io_submit_sqe, 0, "");
DEFINE_int32(io_max_get, 0, "");
DEFINE_bool(set_iouring_defer_taskrun, true, "");
DEFINE_int32(io_max_submit, 0, "");
DEFINE_int32(io_registers, 2048, "");
DEFINE_int32(io_prov_buffs_size, 2048, "");
DEFINE_int32(io_prov_buffs, 2000, "");
DEFINE_bool(io_zcrx, false, "");
DEFINE_int32(io_zcrx_num_pages, 16384, "");
DEFINE_int32(io_zcrx_refill_entries, 16384, "");
DEFINE_string(io_zcrx_ifname, "eth0", "");
DEFINE_int32(io_zcrx_queue_id, 0, "");
#if FOLLY_HAS_LIBURING

namespace apache::thrift::stress {
#if FOLLY_HAVE_WEAK_SYMBOLS
FOLLY_ATTR_WEAK int resolve_napi_callback(
    int /*ifindex*/, uint32_t /*queueId*/);
#else
static int resolve_napi_callback(int /*ifindex*/, uint32_t /*queueId*/) {
  return -1;
}
#endif

folly::IoUringBackend::Options getIoUringOptions() {
  folly::IoUringBackend::Options options;
  options.setRegisterRingFd(FLAGS_use_iouring_event_eventfd);

  if (FLAGS_io_prov_buffs_size > 0 && FLAGS_io_prov_buffs > 0) {
    options.setInitialProvidedBuffers(
        FLAGS_io_prov_buffs_size, FLAGS_io_prov_buffs);
  }

  if (FLAGS_io_registers > 0) {
    options.setUseRegisteredFds(static_cast<size_t>(FLAGS_io_registers));
  }

  if (FLAGS_io_capacity > 0) {
    options.setCapacity(static_cast<size_t>(FLAGS_io_capacity));
  }

  if (FLAGS_io_submit_sqe > 0) {
    options.setSqeSize(FLAGS_io_submit_sqe);
  }

  if (FLAGS_io_max_get > 0) {
    options.setMaxGet(static_cast<size_t>(FLAGS_io_max_get));
  }

  if (FLAGS_io_max_submit > 0) {
    options.setMaxSubmit(static_cast<size_t>(FLAGS_io_max_submit));
  }

  if (FLAGS_set_iouring_defer_taskrun) {
    if (folly::IoUringBackend::kernelSupportsDeferTaskrun()) {
      options.setDeferTaskRun(FLAGS_set_iouring_defer_taskrun);
    } else {
      LOG(ERROR) << "not setting DeferTaskRun as not supported on this kernel";
    }
  }

  static std::atomic<int32_t> currQueueId{FLAGS_io_zcrx_queue_id};
  if (FLAGS_io_zcrx) {
    options.setZeroCopyRx(true)
        .setZeroCopyRxInterface(FLAGS_io_zcrx_ifname)
        .setZeroCopyRxQueue(currQueueId.fetch_add(1))
        .setZeroCopyRxNumPages(FLAGS_io_zcrx_num_pages)
        .setZeroCopyRxRefillEntries(FLAGS_io_zcrx_refill_entries)
        .setResolveNapiCallback(resolve_napi_callback);
  }

  return options;
}
} // namespace apache::thrift::stress

#endif

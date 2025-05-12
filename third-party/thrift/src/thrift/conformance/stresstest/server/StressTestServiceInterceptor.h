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

#pragma once

#include <thrift/lib/cpp2/server/ServiceInterceptor.h>

namespace apache::thrift::stress {

class StressTestServiceInterceptor final
    : public ServiceInterceptor<folly::Unit, folly::Unit> {
  using ConnectionState = folly::Unit;
  using RequestState = folly::Unit;

 public:
  std::string getName() const final;

  folly::coro::Task<std::optional<RequestState>> onRequest(
      ConnectionState*, RequestInfo) final;

  folly::coro::Task<void> onResponse(
      RequestState*, ConnectionState*, ResponseInfo) final;

  std::optional<ConnectionState> onConnection(ConnectionInfo) final;

  void onConnectionClosed(ConnectionState*, ConnectionInfo) noexcept final;
};

} // namespace apache::thrift::stress

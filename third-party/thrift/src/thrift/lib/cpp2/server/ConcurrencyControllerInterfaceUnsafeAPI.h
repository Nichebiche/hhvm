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

#include <thrift/lib/cpp2/server/ConcurrencyControllerInterface.h>

namespace apache::thrift {

class ConcurrencyControllerInterfaceUnsafeAPI {
 public:
  ConcurrencyControllerInterfaceUnsafeAPI(
      ConcurrencyControllerInterface& concurrencyController)
      : concurrencyController_(concurrencyController) {}

  bool getExecutionLimitRequestsHasBeenEnforced() const {
    return concurrencyController_.getExecutionLimitRequestsHasBeenEnforced();
  }

  bool getQpsLimitHasBeenEnforced() const {
    return concurrencyController_.getQpsLimitHasBeenEnforced();
  }

 private:
  ConcurrencyControllerInterface& concurrencyController_;
};

} // namespace apache::thrift

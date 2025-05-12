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

#include <thrift/lib/cpp2/async/InterceptorFrameworkMetadata.h>

namespace apache::thrift::detail {

/*

ContextPropClientInterceptor will be widely installed to all Meta services,
and defines an implementation for initializeInterceptorFrameworkMetadataStorage.
Unfortunately, Cinder tests in Meta are commonly compiled in
@mode/dev may load C++ libraries dynamically in different orders and may defer
loading some libraries until they are used. Thrift pluggable functions expects
that all libraries are loaded immediately before any functions in the program
are called. We relax this requirement by allowing the function be registered
late.

*/
THRIFT_PLUGGABLE_FUNC_REGISTER_ALLOW_LATE_OVERRIDE(
    InterceptorFrameworkMetadataStorage,
    initializeInterceptorFrameworkMetadataStorage) {
  return InterceptorFrameworkMetadataStorage{};
}

THRIFT_PLUGGABLE_FUNC_REGISTER(
    void,
    postProcessFrameworkMetadata,
    InterceptorFrameworkMetadataStorage&,
    const RpcOptions&) {}

/*

ContextPropClientInterceptor (WDL) will also define an implementation for
serializeFrameworkMetadata.

*/
THRIFT_PLUGGABLE_FUNC_REGISTER_ALLOW_LATE_OVERRIDE(
    std::unique_ptr<folly::IOBuf>,
    serializeFrameworkMetadata,
    InterceptorFrameworkMetadataStorage&& /* unused */) {
  return nullptr;
}

THRIFT_PLUGGABLE_FUNC_REGISTER(
    InterceptorFrameworkMetadataStorage,
    deserializeFrameworkMetadata,
    const folly::IOBuf& /* unused */) {
  return InterceptorFrameworkMetadataStorage{};
}

} // namespace apache::thrift::detail

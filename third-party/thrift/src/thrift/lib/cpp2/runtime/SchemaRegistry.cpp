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

#include <thrift/lib/cpp2/runtime/SchemaRegistry.h>

#ifdef THRIFT_SCHEMA_AVAILABLE

#include <folly/Indestructible.h>
#include <thrift/lib/cpp2/schema/SyntaxGraph.h>
#include <thrift/lib/cpp2/schema/detail/Merge.h>

namespace apache::thrift {

SchemaRegistry::SchemaRegistry(BaseSchemaRegistry& base) : base_(base) {
  auto resolver = std::make_unique<schema::detail::IncrementalResolver>();
  resolver_ = resolver.get();
  syntaxGraph_ = std::make_unique<schema::SyntaxGraph>(std::move(resolver));
}
SchemaRegistry::~SchemaRegistry() = default;

SchemaRegistry& SchemaRegistry::get() {
  static folly::Indestructible<SchemaRegistry> self(BaseSchemaRegistry::get());
  return *self;
}

SchemaRegistry::Ptr SchemaRegistry::getMergedSchema() {
  std::shared_lock rlock(base_.mutex_);
  if (mergedSchema_) {
    mergedSchemaAccessed_ = true;
    return mergedSchema_;
  }
  rlock.unlock();

  std::unique_lock wlock(base_.mutex_);
  if (mergedSchema_) {
    mergedSchemaAccessed_ = true;
    return mergedSchema_;
  }

  mergedSchema_ = std::make_shared<type::Schema>();
  for (auto& [name, data] : base_.rawSchemas_) {
    if (auto schema = schema::detail::readSchema(data.data)) {
      schema::detail::mergeInto(
          *mergedSchema_,
          std::move(*schema),
          includedPrograms_,
          /*allowDuplicateDefinitionKeys*/ false);
    }
  }

  base_.insertCallback_ = [this](std::string_view data) {
    // The caller is holding a write lock.

    // If no one else has a reference yet we can reuse the storage.
    if (mergedSchemaAccessed_.exchange(false)) {
      mergedSchema_ = std::make_shared<type::Schema>(*mergedSchema_);
    }

    if (auto schema = schema::detail::readSchema(data)) {
      schema::detail::mergeInto(
          *mergedSchema_,
          std::move(*schema),
          includedPrograms_,
          /*allowDuplicateDefinitionKeys*/ false);
    }
  };

  mergedSchemaAccessed_ = true;
  return mergedSchema_;
}

} // namespace apache::thrift

#endif

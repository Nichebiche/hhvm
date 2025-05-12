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

#include <thrift/lib/cpp2/schema/SchemaV1.h>
#include <thrift/lib/cpp2/schema/detail/Resolver.h>

#include <folly/Synchronized.h>

#ifdef THRIFT_SCHEMA_AVAILABLE

namespace apache::thrift::schema::detail {
namespace type = apache::thrift::type;
class SchemaIndex;

folly::not_null_unique_ptr<Resolver> createResolverfromSchema(type::Schema&&);
folly::not_null_unique_ptr<Resolver> createResolverfromSchemaRef(
    const type::Schema&);

class IncrementalResolver : public Resolver {
 public:
  IncrementalResolver();
  ~IncrementalResolver() override;

  /**
   * Gets node for given definition, or throws `std::out_of_range` if not
   * present in schema.
   */
  template <typename T>
  const DefinitionNode& getDefinitionNode() const;

  const ProgramNode& programOf(const type::ProgramId& id) const override;
  const protocol::Value& valueOf(const type::ValueId& id) const override;
  const DefinitionNode* definitionOf(
      const type::DefinitionKey& key) const override;
  std::vector<folly::not_null<const ProgramNode*>> programs() const override;

 private:
  const DefinitionNode& getDefinitionNode(
      const type::DefinitionKey& key,
      type::ProgramId programId,
      std::string_view name,
      ::folly::Range<const ::std::string_view*> (*bundle)()) const;

  mutable folly::Synchronized<type::Schema> schema_;
  folly::not_null_unique_ptr<SchemaIndex> index_;
};

template <typename T>
const DefinitionNode& IncrementalResolver::getDefinitionNode() const {
  using ::apache::thrift::detail::TSchemaAssociation;
  return getDefinitionNode(
      type::DefinitionKey{TSchemaAssociation<T>::definitionKey},
      type::ProgramId{TSchemaAssociation<T>::programId},
      folly::pretty_name<T>(),
      TSchemaAssociation<T>::bundle);
}

} // namespace apache::thrift::schema::detail
#endif

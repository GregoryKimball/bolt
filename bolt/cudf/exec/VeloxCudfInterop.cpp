/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include "bolt/cudf/CudfConfig.h"
#include "bolt/cudf/CudfNoDefaults.h"
#include "bolt/cudf/exec/VeloxCudfInterop.h"

#include "bolt/common/memory/Memory.h"
#include "bolt/type/Type.h"
#include "bolt/vector/BaseVector.h"
#include "bolt/vector/ComplexVector.h"
#include "bolt/vector/arrow/Bridge.h"

#include <cudf/interop.hpp>
#include <cudf/table/table.hpp>
#include <cudf/types.hpp>
#include <cudf/utilities/error.hpp>

#include <cuda/stream>

#include <arrow/c/bridge.h>
#include <arrow/io/interfaces.h>
#include <arrow/table.h>

#include <cstdlib>
#include <cstring>
#include <string_view>

namespace bytedance::bolt::cudf_velox {

std::optional<cudf::data_type> tryVeloxToCudfDataType(const TypePtr& type) {
  if (false) {
    return std::nullopt;
  }
  switch (type->kind()) {
    case TypeKind::BOOLEAN:
      return cudf::data_type{cudf::type_id::BOOL8};
    case TypeKind::TINYINT:
      return cudf::data_type{cudf::type_id::INT8};
    case TypeKind::SMALLINT:
      return cudf::data_type{cudf::type_id::INT16};
    case TypeKind::INTEGER:
      if (type->isIntervalYearMonth()) {
        return std::nullopt;
      }
      if (type->isDate()) {
        return cudf::data_type{cudf::type_id::TIMESTAMP_DAYS};
      }
      return cudf::data_type{cudf::type_id::INT32};
    case TypeKind::BIGINT:
      if (type->isDecimal()) {
        auto const decimalType =
            std::dynamic_pointer_cast<const ShortDecimalType>(type);
        if (!decimalType) {
          return std::nullopt;
        }
        auto const cudfScale = numeric::scale_type{-decimalType->scale()};
        return cudf::data_type{cudf::type_id::DECIMAL64, cudfScale};
      }
      return cudf::data_type{cudf::type_id::INT64};
    case TypeKind::HUGEINT: {
      // HUGEINT is used only for DECIMAL128
      // per facebookincubator/velox PR 4434 (May 2, 2023)
      if (!type->isDecimal()) {
        return std::nullopt;
      }
      auto const decimalType =
          std::dynamic_pointer_cast<const LongDecimalType>(type);
      if (!decimalType) {
        return std::nullopt;
      }
      auto const cudfScale = numeric::scale_type{-decimalType->scale()};
      return cudf::data_type{cudf::type_id::DECIMAL128, cudfScale};
    }
    case TypeKind::REAL:
      return cudf::data_type{cudf::type_id::FLOAT32};
    case TypeKind::DOUBLE:
      return cudf::data_type{cudf::type_id::FLOAT64};
    case TypeKind::VARCHAR:
      return cudf::data_type{cudf::type_id::STRING};
    case TypeKind::VARBINARY:
      return cudf::data_type{cudf::type_id::STRING};
    case TypeKind::TIMESTAMP:
      return cudf::data_type{CudfConfig::getInstance().timestampUnit};
    case TypeKind::ARRAY:
      if (!tryVeloxToCudfDataType(type->childAt(0))) {
        return std::nullopt;
      }
      return cudf::data_type{cudf::type_id::LIST};
    case TypeKind::ROW:
      for (size_t i = 0; i < type->size(); ++i) {
        if (!tryVeloxToCudfDataType(type->childAt(i))) {
          return std::nullopt;
        }
      }
      return cudf::data_type{cudf::type_id::STRUCT};
    case TypeKind::MAP:
    case TypeKind::UNKNOWN:
    case TypeKind::FUNCTION:
    case TypeKind::OPAQUE:
    case TypeKind::INVALID:
    default:
      return std::nullopt;
  }
}

cudf::data_type veloxToCudfDataType(const TypePtr& type) {
  auto result = tryVeloxToCudfDataType(type);
  if (!result) {
    CUDF_FAIL(
        "Unsupported Velox type: " +
        std::string(mapTypeKindToName(type->kind())));
  }
  return *result;
}

bool isTypeSupportedByCudf(const TypePtr& type) {
  return tryVeloxToCudfDataType(type).has_value();
}

namespace with_arrow {

std::unique_ptr<cudf::table> toCudfTable(
    const bytedance::bolt::RowVectorPtr& veloxTable,
    bytedance::bolt::memory::MemoryPool* pool,
    cuda::stream_ref stream,
    rmm::device_async_resource_ref mr,
    std::optional<std::string> timestampTimeZone) {
  TimestampUnit unit;
  switch (CudfConfig::getInstance().timestampUnit) {
    case cudf::type_id::TIMESTAMP_NANOSECONDS:
      unit = TimestampUnit::kNano;
      break;
    case cudf::type_id::TIMESTAMP_MICROSECONDS:
      unit = TimestampUnit::kMicro;
      break;
    case cudf::type_id::TIMESTAMP_MILLISECONDS:
      unit = TimestampUnit::kMilli;
      break;
    case cudf::type_id::TIMESTAMP_SECONDS:
      unit = TimestampUnit::kSecond;
      break;
    default:
      BOLT_UNSUPPORTED();
  }
  // Need to flattenDictionary and flattenConstant, otherwise we observe issues
  // in the null mask. Also, libcudf does not support Arrow binary, so we export
  // VARBINARY as UTF-8.
  ArrowOptions arrowOptions{
      .flattenDictionary = true,
      .flattenConstant = true,
      .timestampUnit = unit,
      .timestampTimeZone = timestampTimeZone};
  ArrowArray arrowArray;
  exportToArrow(
      std::dynamic_pointer_cast<bytedance::bolt::BaseVector>(veloxTable),
      arrowArray,
      pool,
      arrowOptions);
  ArrowSchema arrowSchema;
  exportToArrow(
      std::dynamic_pointer_cast<bytedance::bolt::BaseVector>(veloxTable),
      arrowSchema,
      arrowOptions);
  auto tbl = cudf::from_arrow(&arrowSchema, &arrowArray, stream, mr);

  // Synchronize before releasing Arrow resources.  cudf::from_arrow uses
  // cudaMemcpyBatchAsync (CUDA 13.0+) with cudaMemcpySrcAccessOrderStream,
  // which defers reading the host source buffers until the stream reaches
  // each copy.  The Arrow arrays must therefore stay alive until the stream
  // has executed those copies.
  stream.sync();

  // Release Arrow resources
  if (arrowArray.release) {
    arrowArray.release(&arrowArray);
  }
  if (arrowSchema.release) {
    arrowSchema.release(&arrowSchema);
  }
  return tbl;
}

namespace {

void setArrowFormatBackToVarbinary(ArrowSchema* schema, const TypePtr& type) {
  switch (type->kind()) {
    case TypeKind::ROW: {
      if (schema->n_children != static_cast<int64_t>(type->size())) {
        break;
      }
      for (size_t i = 0; i < type->size(); ++i) {
        setArrowFormatBackToVarbinary(schema->children[i], type->childAt(i));
      }
      break;
    }
    case TypeKind::ARRAY: {
      if (schema->n_children == 1) {
        setArrowFormatBackToVarbinary(schema->children[0], type->childAt(0));
      }
      break;
    }
    case TypeKind::VARBINARY: {
      // Each Arrow string format has a binary twin that uses the same buffers
      // ("u"/"z" for 32-bit offsets, "U"/"Z" for 64-bit offsets and "vu"/"vz"
      // for string views). The importer reads the buffers according to the
      // format, so the offset layout must be preserved when relabelling the
      // string format as binary. Binary formats are accepted as is.
      BOLT_CHECK_NOT_NULL(
          schema->format, "Missing Arrow format for VARBINARY column");
      const std::string_view format{schema->format};
      const char* varbinaryArrowFormat = nullptr;
      if (format == "u" || format == "z") {
        varbinaryArrowFormat = "z";
      } else if (format == "U" || format == "Z") {
        varbinaryArrowFormat = "Z";
      } else if (format == "vu" || format == "vz") {
        varbinaryArrowFormat = "vz";
      } else {
        BOLT_FAIL("Unexpected Arrow format for VARBINARY column: {}", format);
      }
      std::free(const_cast<char*>(schema->format));
      schema->format = nullptr;
      const size_t bufferLen = std::strlen(varbinaryArrowFormat) + 1;
      auto* buffer = static_cast<char*>(std::malloc(bufferLen));
      BOLT_CHECK_NOT_NULL(buffer);
      std::memcpy(buffer, varbinaryArrowFormat, bufferLen);
      schema->format = buffer;
      break;
    }
    default:
      break;
  }
}

RowVectorPtr toVeloxColumn(
    const cudf::table_view& table,
    memory::MemoryPool* pool,
    const std::vector<cudf::column_metadata>& metadata,
    const RowTypePtr* outputType,
    cuda::stream_ref stream,
    rmm::device_async_resource_ref mr) {
  // To avoid ownership issues, we make copies of the Arrow objects
  // returned from cuDF as unique_ptrs, then mark the originals as
  // released so their destructors don't try to free the resources.
  //
  // A better solution would be alternative versions of the cuDF
  // to_arrow_host functions that return Arrow objects by value
  // or populate objects passed by reference, but that would require
  // changes to cuDF.
  //
  // seves 1/17/26

  auto arrowDeviceArray = cudf::to_arrow_host(table, stream, mr);
  ArrowArray arrayCopy = arrowDeviceArray->array;
  arrowDeviceArray->array.release = nullptr;

  auto arrowSchema = cudf::to_arrow_schema(table, metadata);
  ArrowSchema schemaCopy = *arrowSchema;
  arrowSchema->release = nullptr;

  // Override schema type recursively with outputType if provided. This is
  // needed for types like VARBINARY, which cuDF does not have: toCudfTable
  // exports them as strings (exportVarbinaryAsString), so cuDF holds them as
  // STRING columns and to_arrow_schema reports a string format ("u", or "U"
  // for 64-bit offsets). This code implements the other side of that
  // conversion, restoring the matching binary format ("z" or "Z") so that the
  // data re-imports as VARBINARY.
  if (outputType) {
    setArrowFormatBackToVarbinary(&schemaCopy, *outputType);
  }

  auto veloxTable = importFromArrowAsOwner(schemaCopy, arrayCopy, ArrowOptions{}, pool);

  // BaseVector to RowVector
  auto castedPtr =
      std::dynamic_pointer_cast<bytedance::bolt::RowVector>(veloxTable);
  BOLT_CHECK_NOT_NULL(castedPtr);
  return castedPtr;
}

template <typename Iterator>
std::vector<cudf::column_metadata>
getMetadata(Iterator begin, Iterator end, const std::string& namePrefix) {
  std::vector<cudf::column_metadata> metadata;
  int i = 0;
  for (auto c = begin; c < end; c++) {
    metadata.push_back(cudf::column_metadata(namePrefix + std::to_string(i)));
    metadata.back().children_meta = getMetadata(
        c->child_begin(), c->child_end(), namePrefix + std::to_string(i));
    i++;
  }
  return metadata;
}

// Recursively generate metadata using exact names from Velox RowType.
cudf::column_metadata getMetadataWithName(
    const bytedance::bolt::TypePtr& type,
    const std::string& name) {
  cudf::column_metadata meta(name);
  if (type->kind() == bytedance::bolt::TypeKind::ROW) {
    auto rowType =
        std::dynamic_pointer_cast<const bytedance::bolt::RowType>(type);
    for (size_t i = 0; i < rowType->size(); ++i) {
      meta.children_meta.push_back(
          getMetadataWithName(rowType->childAt(i), rowType->nameOf(i)));
    }
  } else if (type->kind() == bytedance::bolt::TypeKind::ARRAY) {
    // cudf::lists_column_view::child_column_index is 1, the first metadata is
    // offsets
    meta.children_meta.emplace_back(cudf::column_metadata(name + "_offsets"));
    meta.children_meta.push_back(
        getMetadataWithName(type->childAt(0), "element"));
  }
  return meta;
}

std::vector<cudf::column_metadata> getMetadataWithName(
    const RowTypePtr& rowType) {
  std::vector<cudf::column_metadata> metadata;
  for (size_t i = 0; i < rowType->size(); ++i) {
    metadata.push_back(
        getMetadataWithName(rowType->childAt(i), rowType->nameOf(i)));
  }
  return metadata;
}

} // namespace

bytedance::bolt::RowVectorPtr toVeloxColumn(
    const cudf::table_view& table,
    bytedance::bolt::memory::MemoryPool* pool,
    std::string namePrefix,
    cuda::stream_ref stream,
    rmm::device_async_resource_ref mr) {
  auto metadata = getMetadata(table.begin(), table.end(), namePrefix);
  return toVeloxColumn(table, pool, metadata, nullptr, stream, mr);
}

bytedance::bolt::RowVectorPtr toVeloxColumn(
    const cudf::table_view& table,
    bytedance::bolt::memory::MemoryPool* pool,
    const bytedance::bolt::RowTypePtr& outputType,
    std::string namePrefix,
    cuda::stream_ref stream,
    rmm::device_async_resource_ref mr) {
  auto metadata = getMetadata(table.begin(), table.end(), namePrefix);
  return toVeloxColumn(table, pool, metadata, &outputType, stream, mr);
}

// New overload: Accepts a Velox TypePtr for recursive metadata construction.
RowVectorPtr toVeloxColumn(
    const cudf::table_view& table,
    memory::MemoryPool* pool,
    const TypePtr& type,
    cuda::stream_ref stream,
    rmm::device_async_resource_ref mr) {
  // Recursively generate metadata using Velox type names for all columns.
  // This assumes 'type' is a RowType and its children match the cudf table
  // columns.
  auto rowType =
      std::dynamic_pointer_cast<const bytedance::bolt::RowType>(type);
  BOLT_CHECK_NOT_NULL(rowType);
  auto metadata = getMetadataWithName(rowType);
  return toVeloxColumn(table, pool, metadata, &rowType, stream, mr);
}

} // namespace with_arrow

} // namespace bytedance::bolt::cudf_velox

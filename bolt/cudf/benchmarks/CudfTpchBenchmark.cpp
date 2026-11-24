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


#include "bolt/cudf/benchmarks/CudfTpchBenchmark.h"
#include "bolt/cudf/CudfConfig.h"
#include "bolt/cudf/connectors/hive/CudfHiveConfig.h"
#include "bolt/cudf/connectors/hive/CudfHiveConnector.h"
#include "bolt/cudf/exec/CudfConversion.h"
#include "bolt/cudf/exec/ToCudf.h"
#include "bolt/cudf/expression/PrestoFunctions.h"

#include "bolt/connectors/Connector.h"
#include "bolt/exec/tests/utils/HiveConnectorTestBase.h"

DECLARE_string(bolt_benchmark_max_coalesced_distance_bytes);
DECLARE_int64(bolt_benchmark_max_coalesced_bytes);
DECLARE_int32(bolt_benchmark_parquet_prefetch_rowgroups);

using namespace bytedance::bolt;
using namespace bytedance::bolt::exec;
using namespace bytedance::bolt::exec::test;

DEFINE_uint64(
    cudf_chunk_read_limit,
    0,
    "Output table chunk read limit for cudf::parquet_chunked_reader.");

DEFINE_uint64(
    cudf_pass_read_limit,
    0,
    "Pass read limit for cudf::parquet_chunked_reader.");

DEFINE_int32(
    cudf_gpu_batch_size_rows,
    100000,
    "Preferred output batch size in rows for cudf operators.");

DEFINE_uint64(
    cudf_local_exchange_buffer_size,
    1UL << 30,
    "Maximum buffered bytes per local exchange before applying backpressure.");

DEFINE_bool(bolt_cudf_table_scan, true, "Enable cuDF table scan");

DEFINE_bool(
    cudf_hive_use_buffered_input,
    true,
    "Use BufferedInputDataSource instead of KvikIO for cuDF Hive reads");

DEFINE_bool(
    cudf_hive_preload_column_chunks,
    false,
    "Load column chunks while preloading cuDF Hive splits");

DEFINE_bool(
    cudf_hive_serialize_io_requests,
    false,
    "Serialize I/O submissions to prevent interleaving between concurrent splits");

DEFINE_string(
    cudf_properties,
    "",
    "Path to a properties file for CudfConfig. Each line should be key=value "
    "(e.g. cudf.memory_resource=async). See CudfConfig for available keys.");

void CudfTpchBenchmark::initialize() {
  if (!FLAGS_cudf_properties.empty()) {
    cudf_velox::CudfConfig::getInstance().initialize(
        cudf_velox::loadPropertiesFile(FLAGS_cudf_properties));
  }

  TpchBenchmark::initialize();

  if (FLAGS_bolt_cudf_table_scan) {
    using CudfHiveCfg = cudf_velox::connector::hive::CudfHiveConfig;
    std::unordered_map<std::string, std::string> values;
    values[connector::hive::HiveConfig::kMaxCoalescedBytes] =
        std::to_string(FLAGS_bolt_benchmark_max_coalesced_bytes);
    values[connector::hive::HiveConfig::kMaxCoalescedDistance] =
        FLAGS_bolt_benchmark_max_coalesced_distance_bytes;
    values[connector::hive::HiveConfig::kPrefetchRowGroups] =
        std::to_string(FLAGS_bolt_benchmark_parquet_prefetch_rowgroups);
    values[CudfHiveCfg::kMaxChunkReadLimit] =
        std::to_string(FLAGS_cudf_chunk_read_limit);
    values[CudfHiveCfg::kMaxPassReadLimit] =
        std::to_string(FLAGS_cudf_pass_read_limit);
    values[CudfHiveCfg::kAllowMismatchedCudfHiveSchemas] = "true";
    values[CudfHiveCfg::kUseBufferedInput] =
        std::to_string(FLAGS_cudf_hive_use_buffered_input);
    values[CudfHiveCfg::kPreloadColumnChunks] =
        std::to_string(FLAGS_cudf_hive_preload_column_chunks);
    values[CudfHiveCfg::kSerializeIoRequests] =
        std::to_string(FLAGS_cudf_hive_serialize_io_requests);

    connector::unregisterConnector(kHiveConnectorId);
    cudf_velox::connector::hive::CudfHiveConnectorFactory factory;
    connector::registerConnector(factory.newConnector(
        kHiveConnectorId,
        std::make_shared<const config::ConfigBase>(std::move(values)),
        ioExecutor_.get()));
  }

  cudf_velox::registerCudf();
  cudf_velox::registerPrestoFunctions(
      cudf_velox::CudfConfig::getInstance().functionNamePrefix);

  queryConfigs_[cudf_velox::CudfFromVelox::kGpuBatchSizeRows] =
      std::to_string(FLAGS_cudf_gpu_batch_size_rows);
  queryConfigs_[core::QueryConfig::kMaxLocalExchangeBufferSize] =
      std::to_string(FLAGS_cudf_local_exchange_buffer_size);
}

std::vector<std::shared_ptr<connector::ConnectorSplit>>
CudfTpchBenchmark::listSplits(
    const std::string& path,
    int32_t numSplitsPerFile,
    const TpchPlan& plan) {
  // The cuDF Parquet reader reads a whole file per split.
  return TpchBenchmark::listSplits(
      path, FLAGS_bolt_cudf_table_scan ? 1 : numSplitsPerFile, plan);
}

void CudfTpchBenchmark::shutdown() {
  cudf_velox::unregisterCudf();
  TpchBenchmark::shutdown();
}

int main(int argc, char** argv) {
  std::string kUsage(
      "This program benchmarks TPC-H queries on the GPU with cuDF. Run "
      "'bolt_cudf_tpch_benchmark -helpon=TpchBenchmark' for available options.\n");
  gflags::SetUsageMessage(kUsage);
  folly::Init init{&argc, &argv, false};
  benchmark = std::make_unique<CudfTpchBenchmark>();
  tpchBenchmarkMain();
}

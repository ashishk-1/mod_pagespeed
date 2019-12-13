/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "envoy_fetch.h"

#include <algorithm>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>

#include "net/instaweb/http/public/async_fetch.h"
#include "net/instaweb/http/public/inflating_fetch.h"
#include "pagespeed/kernel/base/basictypes.h"
#include "pagespeed/kernel/base/condvar.h"
#include "pagespeed/kernel/base/message_handler.h"
#include "pagespeed/kernel/base/pool.h"
#include "pagespeed/kernel/base/pool_element.h"
#include "pagespeed/kernel/base/statistics.h"
#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/base/thread_system.h"
#include "pagespeed/kernel/base/timer.h"
#include "pagespeed/kernel/base/writer.h"
#include "pagespeed/kernel/http/request_headers.h"
#include "pagespeed/kernel/http/response_headers.h"
#include "pagespeed/kernel/http/response_headers_parser.h"
#include "pagespeed_remote_data_fetcher.h"
#include "pagespeed/envoy/header_utils.h"

namespace net_instaweb {

// Default keepalive 60s.
const int64 keepalive_timeout_ms = 60000;
static const char cluster_str[] = "cluster1";

PagespeedDataFetcherCallback::PagespeedDataFetcherCallback(EnvoyFetch* fetch) { 
  fetch_ = fetch;
}

void PagespeedDataFetcherCallback::onSuccess(Envoy::Http::MessagePtr& response) {
  fetch_->setResponse(response->headers(), response->body());
}

void PagespeedDataFetcherCallback::onFailure(FailureReason reason) {
  std::cout << "PagespeedDataFetcherCallback::onFailure\n";
  std::cout.flush();
}

EnvoyFetch::EnvoyFetch(const GoogleString& url,
                   AsyncFetch* async_fetch,
                   MessageHandler* message_handler,
                   EnvoyClusterManager& cluster_manager)
    : str_url_(url),
      fetcher_(NULL),
      async_fetch_(async_fetch),
      message_handler_(message_handler),
      cluster_manager(cluster_manager),
      done_(false),
      content_length_(-1),
      content_length_known_(false) {
}

EnvoyFetch::~EnvoyFetch() {
}

void EnvoyFetch::FetchWithEnvoy() {
  UriImpl uri(str_url_);
  try {
    uri.resolve(*cluster_manager.getDispatcher(), Envoy::Network::DnsLookupFamily::Auto);
  } catch (UriException) {
    std::cout << "UriException \n";
    std::cout.flush();
  }
  Envoy::Upstream::ClusterManagerPtr cm_{};
  cm_ = cluster_manager.getClusterManagerFactory().clusterManagerFromProto(
      createBootstrapConfiguration(uri));
  cm_->setInitializedCb([this]() -> void {
    cluster_manager.getInitManager().initialize(cluster_manager.getInitWatcher());
  });

  envoy::api::v2::core::HttpUri http_uri;
  http_uri.set_uri(str_url_);
  // dummy response content hash as we want to keep
  // PagespeedRemoteDataFetcher structure same as
  // envoy RemoteDataFetcher
  std::string response_content_hash("DUMMY_HASH");

  http_uri.set_cluster(cluster_str);
  cb_ptr_ = std::make_unique<PagespeedDataFetcherCallback>(this);
  std::unique_ptr<PagespeedRemoteDataFetcher> PagespeedRemoteDataFetcherPtr =
      std::make_unique<PagespeedRemoteDataFetcher>(*cm_, http_uri, response_content_hash, *cb_ptr_);

  PagespeedRemoteDataFetcherPtr->fetch();
  cluster_manager.getDispatcher()->run(Envoy::Event::Dispatcher::RunType::Block);
}

const envoy::config::bootstrap::v2::Bootstrap
EnvoyFetch::createBootstrapConfiguration(const Uri& uri) const {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
  cluster->set_name(cluster_str);
  cluster->mutable_connect_timeout()->set_seconds(15);
  cluster->set_type(envoy::api::v2::Cluster::DiscoveryType::Cluster_DiscoveryType_STATIC);
  auto* host = cluster->add_hosts();
  auto* socket_address = host->mutable_socket_address();
  socket_address->set_address(uri.address()->ip()->addressAsString());
  socket_address->set_port_value(uri.port());

  // ENVOY_LOG(info, "Computed configuration: {}", bootstrap.DebugString());

  return bootstrap;
}


// This function is called by EnvoyUrlAsyncFetcher::StartFetch.
void EnvoyFetch::Start() {
  std::function<void()> fetch_fun_ptr =
      std::bind(&EnvoyFetch::FetchWithEnvoy, this);
  cluster_manager.getDispatcher()->post(fetch_fun_ptr);
  cluster_manager.getDispatcher()->run(Envoy::Event::Dispatcher::RunType::NonBlock);
}


bool EnvoyFetch::Init() {
  return true;
}


// This function should be called only once. The only argument is sucess or
// not.
void EnvoyFetch::CallbackDone(bool success) {
}

void EnvoyFetch::setResponse(Envoy::Http::HeaderMap& headers,
                             Envoy::Buffer::InstancePtr& response_body) {

  ResponseHeaders* res_header = async_fetch_->response_headers();
  std::unique_ptr<ResponseHeaders> response_headers_ptr = HeaderUtils::toPageSpeedResponseHeaders(headers);
  res_header->CopyFrom(*response_headers_ptr);

  async_fetch_->response_headers()->SetOriginalContentLength(response_body->length());
  if (async_fetch_->response_headers()->Has(HttpAttributes::kXOriginalContentLength)) {
    async_fetch_->extra_response_headers()->SetOriginalContentLength(response_body->length());
  }

  async_fetch_->Write(StringPiece(response_body->toString()), message_handler());

  async_fetch_->Done(true);
}

MessageHandler* EnvoyFetch::message_handler() {
  return message_handler_;
}

void EnvoyFetch::FixUserAgent() {
}

// Prepare the request data for this fetch, and hook the write event.
int EnvoyFetch::InitRequest() {
  return 0;
}

int EnvoyFetch::Connect() {
  return 0;
}

}  // namespace net_instaweb

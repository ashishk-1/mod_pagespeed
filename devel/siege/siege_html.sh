#!/bin/bash
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
# Runs 'siege' on a HTML file.
#
# Usage:
#   devel/siege/siege_html.sh

this_dir=$(dirname "${BASH_SOURCE[0]}")
source "$this_dir/siege_helper.sh" || exit 1

# TODO(jmarantz): There appears to be no better way to turn all
# filters off via query-param.  Though you might think that
# PageSpeedRewriteLevel=PassThrough should work, it does not.  There
# is special handling for PageSpeedFilters=core but not for
# PassThrough.
URL="http://localhost:8080/mod_pagespeed_example/collapse_whitespace.html?PageSpeedFilters=rewrite_domains"
run_siege "$URL"

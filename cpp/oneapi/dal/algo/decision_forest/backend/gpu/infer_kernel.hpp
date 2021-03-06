/*******************************************************************************
* Copyright 2020 Intel Corporation
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
*******************************************************************************/

#pragma once

#include "oneapi/dal/algo/decision_forest/infer_types.hpp"
#include "oneapi/dal/backend/dispatcher_dpc.hpp"

namespace oneapi::dal::decision_forest::backend {

template <typename Float, typename Task, typename Method>
struct infer_kernel_gpu {
    infer_result<Task> operator()(const dal::backend::context_gpu& ctx,
                                  const descriptor_base<Task>& params,
                                  const infer_input<Task>& input) const;
};

} // namespace oneapi::dal::decision_forest::backend

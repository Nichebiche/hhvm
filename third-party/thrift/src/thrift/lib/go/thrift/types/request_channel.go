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

package types

import (
	"context"
	"io"
)

// RequestChannel is an API that implements the most minimal surface for
// generated client code. An implementation:
//   - Must be thread-safe
//   - May implement request pipelining
//   - May implement reconnection logic
//   - May implement connection pooling
//   - Hides implementation details of the protocol and transport.
type RequestChannel interface {
	io.Closer

	SendRequestResponse(ctx context.Context, method string, request WritableStruct, response ReadableStruct) error
	SendRequestNoResponse(ctx context.Context, method string, request WritableStruct) error
}

// DO NOT USE: temporary migration workaround.
type DO_NOT_USE_ChannelWrapper interface {
	DO_NOT_USE_WrapChannel() RequestChannel
}

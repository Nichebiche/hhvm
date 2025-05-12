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

package format

import (
	"io"

	"github.com/facebook/fbthrift/thrift/lib/go/thrift/types"
)

func flush(writer io.Writer) error {
	flusher, ok := writer.(types.Flusher)
	if !ok {
		return nil
	}
	return types.NewProtocolException(flusher.Flush())
}

func readByte(reader io.Reader) (byte, error) {
	if byteReader, ok := reader.(io.ByteReader); ok {
		return byteReader.ReadByte()
	}
	v := [1]byte{0}
	n, err := reader.Read(v[0:1])
	if n > 0 && (err == nil || err == io.EOF) {
		return v[0], nil
	}
	if n > 0 && err != nil {
		return v[0], err
	}
	if err != nil {
		return 0, err
	}
	return v[0], nil
}

func writeByte(writer io.Writer, c byte) error {
	if byteWriter, ok := writer.(io.ByteWriter); ok {
		return byteWriter.WriteByte(c)
	}
	v := [1]byte{c}
	_, err := writer.Write(v[0:1])
	return err
}

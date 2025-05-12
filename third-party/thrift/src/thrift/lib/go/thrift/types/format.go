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
	"bytes"
	"errors"
	"fmt"
	"io"
	"math"
)

// ProtocolID is the type of protocol used to serialize/deserialize data.
type ProtocolID uint16

const (
	ProtocolIDBinary     ProtocolID = 0
	ProtocolIDJSON       ProtocolID = 1
	ProtocolIDCompact    ProtocolID = 2
	ProtocolIDDebug      ProtocolID = 3
	ProtocolIDVirtual    ProtocolID = 4
	ProtocolIDSimpleJSON ProtocolID = 5
)

func (p ProtocolID) String() string {
	switch p {
	case ProtocolIDBinary:
		return "binary"
	case ProtocolIDJSON:
		return "json"
	case ProtocolIDCompact:
		return "compact"
	case ProtocolIDDebug:
		return "debug"
	case ProtocolIDVirtual:
		return "virtual"
	case ProtocolIDSimpleJSON:
		return "simplejson"
	default:
		return "unknown"
	}
}

const (
	VERSION_MASK = 0xffff0000
	VERSION_1    = 0x80010000
	// Used by SimpleJSON protocol to denote that no field ID is available.
	NO_FIELD_ID = math.MinInt16
)

// Format is the interface that must be implemented by all serialization formats.
type Format interface {
	Encoder
	Decoder
}

// Decoder is the interface that must be implemented by all serialization formats.
type Decoder interface {
	ReadMessageBegin() (name string, typeID MessageType, seqid int32, err error)
	ReadMessageEnd() error
	ReadStructBegin() (name string, err error)
	ReadStructEnd() error
	ReadFieldBegin() (name string, typeID Type, id int16, err error)
	ReadFieldEnd() error
	ReadMapBegin() (keyType Type, valueType Type, size int, err error)
	ReadMapEnd() error
	ReadListBegin() (elemType Type, size int, err error)
	ReadListEnd() error
	ReadSetBegin() (elemType Type, size int, err error)
	ReadSetEnd() error
	ReadBool() (value bool, err error)
	ReadByte() (value byte, err error)
	ReadI16() (value int16, err error)
	ReadI32() (value int32, err error)
	ReadI64() (value int64, err error)
	ReadDouble() (value float64, err error)
	ReadFloat() (value float32, err error)
	ReadString() (value string, err error)
	ReadBinary() (value []byte, err error)
	Skip(fieldType Type) (err error)
}

// Encoder is the interface that must be implemented by all serialization formats.
type Encoder interface {
	WriteMessageBegin(name string, typeID MessageType, seqid int32) error
	WriteMessageEnd() error
	WriteStructBegin(name string) error
	WriteStructEnd() error
	WriteFieldBegin(name string, typeID Type, id int16) error
	WriteFieldEnd() error
	WriteFieldStop() error
	WriteMapBegin(keyType Type, valueType Type, size int) error
	WriteMapEnd() error
	WriteListBegin(elemType Type, size int) error
	WriteListEnd() error
	WriteSetBegin(elemType Type, size int) error
	WriteSetEnd() error
	WriteBool(value bool) error
	WriteByte(value byte) error
	WriteI16(value int16) error
	WriteI32(value int32) error
	WriteI64(value int64) error
	WriteDouble(value float64) error
	WriteFloat(value float32) error
	WriteString(value string) error
	WriteBinary(value []byte) error
	Flush() (err error)
}

// The maximum recursive depth the skip() function will traverse
const DEFAULT_RECURSION_DEPTH = 64

// SkipDefaultDepth skips over the next data element from the provided Protocol object.
func SkipDefaultDepth(prot Decoder, typeID Type) error {
	return Skip(prot, typeID, DEFAULT_RECURSION_DEPTH)
}

// Skip skips over the next data element from the provided Protocol object.
func Skip(self Decoder, fieldType Type, maxDepth int) error {
	if maxDepth <= 0 {
		return NewProtocolExceptionWithType(DEPTH_LIMIT, errors.New("Depth limit exceeded"))
	}
	switch fieldType {
	case BOOL:
		_, err := self.ReadBool()
		return err
	case BYTE:
		_, err := self.ReadByte()
		return err
	case I16:
		_, err := self.ReadI16()
		return err
	case I32:
		_, err := self.ReadI32()
		return err
	case I64:
		_, err := self.ReadI64()
		return err
	case DOUBLE:
		_, err := self.ReadDouble()
		return err
	case FLOAT:
		_, err := self.ReadFloat()
		return err
	case STRING:
		_, err := self.ReadString()
		return err
	case STRUCT:
		if _, err := self.ReadStructBegin(); err != nil {
			return err
		}
		for {
			_, typeID, _, _ := self.ReadFieldBegin()
			if typeID == STOP {
				break
			}
			err := Skip(self, typeID, maxDepth-1)
			if err != nil {
				return err
			}
			self.ReadFieldEnd()
		}
		return self.ReadStructEnd()
	case MAP:
		keyType, valueType, size, err := self.ReadMapBegin()
		if err != nil {
			return err
		}
		for i := 0; i < size; i++ {
			err := Skip(self, keyType, maxDepth-1)
			if err != nil {
				return err
			}
			self.Skip(valueType)
		}
		return self.ReadMapEnd()
	case SET:
		elemType, size, err := self.ReadSetBegin()
		if err != nil {
			return err
		}
		for i := 0; i < size; i++ {
			err := Skip(self, elemType, maxDepth-1)
			if err != nil {
				return err
			}
		}
		return self.ReadSetEnd()
	case LIST:
		elemType, size, err := self.ReadListBegin()
		if err != nil {
			return err
		}
		for i := 0; i < size; i++ {
			err := Skip(self, elemType, maxDepth-1)
			if err != nil {
				return err
			}
		}
		return self.ReadListEnd()
	default:
		return fmt.Errorf("unable to skip over unknown type id %d", fieldType)
	}
}

// Flusher is the interface that wraps the basic Flush method
type Flusher interface {
	Flush() (err error)
}

// ReadSizer is a Reader with an additional Len() capability (to get remaining bytes count).
type ReadSizer interface {
	io.Reader
	// https://pkg.go.dev/bytes#Reader.Len
	// Len returns the number of bytes of the unread portion of the slice.
	Len() int
}

// ReadWriteSizer is a ReadWriter with an additional Len() capability (to get remaining bytes count).
type ReadWriteSizer interface {
	io.ReadWriter
	// https://pkg.go.dev/bytes#Reader.Len
	// Len returns the number of bytes of the unread portion of the slice.
	Len() int
}

// Compile-time check to ensure that 'bytes.Reader' and 'bytes.Buffer' implement ReadSizer.
var _ ReadSizer = (*bytes.Reader)(nil)
var _ ReadSizer = (*bytes.Buffer)(nil)

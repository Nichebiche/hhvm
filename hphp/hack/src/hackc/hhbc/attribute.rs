// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the "hack" directory of this source tree.

use ffi::Vector;
use naming_special_names::user_attributes as ua;
use naming_special_names_rust as naming_special_names;
use serde::Serialize;

use crate::ClassName;
use crate::TypedValue;

/// Attributes with a name from [naming_special_names::user_attributes] and
/// a series of arguments.  Emitter code can match on an attribute as follows:
/// ```
/// use naming_special_names::user_attributes as ua;
/// fn is_memoized(attr: &Attribute) -> bool {
///     attr.is(ua::memoized)
/// }
/// fn has_dynamically_callable(attrs: &Vec<Attribute>) {
///     attrs.iter().any(|a| a.name == ua::DYNAMICALLY_CALLABLE)
/// }
/// ```

#[derive(Clone, Debug, Eq, PartialEq, Hash, Serialize)]
#[repr(C)]
pub struct Attribute {
    pub name: ClassName,
    pub arguments: Vector<TypedValue>,
}

impl Attribute {
    pub fn new(name: impl AsRef<str>, arguments: Vec<TypedValue>) -> Self {
        Self {
            name: ClassName::intern(name.as_ref()),
            arguments: arguments.into(),
        }
    }

    pub fn is<F: Fn(&str) -> bool>(&self, f: F) -> bool {
        f(self.name.as_str())
    }
}

fn is(s: &str, attr: &Attribute) -> bool {
    attr.is(|x| x == s)
}

fn has<F>(attrs: &[Attribute], f: F) -> bool
where
    F: Fn(&Attribute) -> bool,
{
    attrs.iter().any(f)
}

pub fn is_no_injection(attrs: impl AsRef<[Attribute]>) -> bool {
    is_native_arg(native_arg::NO_INJECTION, attrs)
}

pub fn is_native_opcode_impl(attrs: impl AsRef<[Attribute]>) -> bool {
    is_native_arg(native_arg::OP_CODE_IMPL, attrs)
}

fn is_native_arg(s: &str, attrs: impl AsRef<[Attribute]>) -> bool {
    attrs.as_ref().iter().any(|attr| {
        attr.is(ua::is_native)
            && attr.arguments.as_ref().iter().any(|tv| match *tv {
                TypedValue::String(s0) => s0.as_bytes() == s.as_bytes(),
                _ => false,
            })
    })
}

fn is_memoize_with(attrs: impl AsRef<[Attribute]>, arg: &str) -> bool {
    attrs.as_ref().iter().any(|attr| {
        ua::is_memoized(attr.name.as_str())
            && attr.arguments.as_ref().iter().any(|tv| match *tv {
                TypedValue::String(s0) => s0.as_bytes() == arg.as_bytes(),
                _ => false,
            })
    })
}

pub fn is_keyed_by_ic_memoize(attrs: impl AsRef<[Attribute]>) -> bool {
    is_memoize_with(attrs, "KeyedByIC")
}

pub fn is_not_keyed_by_ic_and_leak_ic(attrs: impl AsRef<[Attribute]>) -> bool {
    is_memoize_with(attrs, "NotKeyedByICAndLeakIC__DO_NOT_USE")
}

fn is_foldable(attr: &Attribute) -> bool {
    is(ua::IS_FOLDABLE, attr)
}

fn is_dynamically_constructible(attr: &Attribute) -> bool {
    is(ua::DYNAMICALLY_CONSTRUCTIBLE, attr)
}

fn is_dynamically_referenced(attr: &Attribute) -> bool {
    is(ua::DYNAMICALLY_REFERENCED, attr) && attr.arguments.len() == 0
}

fn is_sealed(attr: &Attribute) -> bool {
    is(ua::SEALED, attr)
}

fn is_const(attr: &Attribute) -> bool {
    is(ua::CONST, attr)
}

fn is_meth_caller(attr: &Attribute) -> bool {
    is("__MethCaller", attr)
}

fn is_provenance_skip_frame(attr: &Attribute) -> bool {
    is(ua::PROVENANCE_SKIP_FRAME, attr)
}

fn is_dynamically_callable(attr: &Attribute) -> bool {
    is(ua::DYNAMICALLY_CALLABLE, attr)
}

fn is_enum_class(attr: &Attribute) -> bool {
    is(ua::ENUM_CLASS, attr)
}

fn is_asio_low_pri(attr: &Attribute) -> bool {
    is(ua::ASIO_LOW_PRI, attr)
}

pub fn has_asio_low_pri(attrs: &[Attribute]) -> bool {
    has(attrs, is_asio_low_pri)
}

pub fn has_enum_class(attrs: &[Attribute]) -> bool {
    has(attrs, is_enum_class)
}

pub fn has_dynamically_constructible(attrs: &[Attribute]) -> bool {
    has(attrs, is_dynamically_constructible)
}

pub fn has_dynamically_referenced(attrs: &[Attribute]) -> bool {
    has(attrs, is_dynamically_referenced)
}

pub fn has_foldable(attrs: &[Attribute]) -> bool {
    has(attrs, is_foldable)
}

pub fn has_sealed(attrs: &[Attribute]) -> bool {
    has(attrs, is_sealed)
}

pub fn has_const(attrs: &[Attribute]) -> bool {
    has(attrs, is_const)
}

pub fn has_meth_caller(attrs: &[Attribute]) -> bool {
    has(attrs, is_meth_caller)
}

pub fn has_provenance_skip_frame(attrs: &[Attribute]) -> bool {
    has(attrs, is_provenance_skip_frame)
}

pub fn has_dynamically_callable(attrs: &[Attribute]) -> bool {
    has(attrs, is_dynamically_callable)
}

pub fn deprecation_info(attrs: &[Attribute]) -> Option<&[TypedValue]> {
    attrs.iter().find_map(|attr| {
        if attr.name.as_str() == ua::DEPRECATED {
            Some(attr.arguments.as_ref())
        } else {
            None
        }
    })
}

pub mod native_arg {
    pub const OP_CODE_IMPL: &str = "OpCodeImpl";
    pub const NO_INJECTION: &str = "NoInjection";
}

#[cfg(test)]
mod tests {
    use naming_special_names::user_attributes as ua;

    use super::*;

    #[test]
    fn example_is_memoized_vs_eq_memoize() {
        let attr = Attribute {
            name: ClassName::intern(ua::MEMOIZE_LSB),
            arguments: vec![].into(),
        };
        assert!(attr.is(ua::is_memoized));
        assert!(!attr.is(|s| s == ua::MEMOIZE));
        assert!(attr.is(|s| s == ua::MEMOIZE_LSB));
    }

    #[test]
    fn example_has_dynamically_callable() {
        let mk_attr = |name: &str| Attribute {
            name: ClassName::intern(name),
            arguments: vec![].into(),
        };
        #[allow(clippy::useless_vec)]
        let attrs = vec![mk_attr(ua::CONST), mk_attr(ua::DYNAMICALLY_CALLABLE)];
        let has_result = attrs
            .iter()
            .any(|a| a.name.as_str() == ua::DYNAMICALLY_CALLABLE);
        assert!(has_result);
    }
}

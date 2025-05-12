// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the "hack" directory of this source tree.

use std::io;
use std::path::Path;
use std::path::PathBuf;

use anyhow::Result;
use bumpalo::Bump;
use file_info::FileInfo;
use file_info::Id;
use file_info::Ids;
use ocamlrep_caml_builtins::Int64;
use ocamlrep_ocamlpool::ocaml_ffi;
use oxidized::decl_parser_options::DeclParserOptions;
use oxidized::file_info::SiAddendum;
use oxidized_by_ref::direct_decl_parser::ParsedFileWithHashes;
use rayon::prelude::*;
use relative_path::RelativePath;
use unwrap_ocaml::UnwrapOcaml;

fn parsed_file_to_file_info(file: oxidized::direct_decl_parser::ParsedFileWithHashes) -> FileInfo {
    let mut info = FileInfo {
        position_free_decl_hash: file_info::HashType(Some(Int64::from(
            file.file_decls_hash.as_u64() as i64,
        ))),
        file_mode: file.mode,
        ids: Ids {
            funs: vec![],
            classes: vec![],
            typedefs: vec![],
            consts: vec![],
            modules: vec![],
        },
        comments: None,
    };
    let pos = |p: &oxidized::pos::Pos| file_info::Pos::Full(p.to_owned());
    use oxidized::shallow_decl_defs::Decl;
    for (name, decl, hash, sort_text) in file.into_iter() {
        let hash = Int64::from(hash.as_u64() as i64);
        match decl {
            Decl::Class(x) => info.ids.classes.push(Id {
                pos: pos(&x.name.0),
                name,
                decl_hash: Some(hash),
                sort_text: sort_text.map(|s| s.to_string()),
            }),
            Decl::Fun(x) => info.ids.funs.push(Id {
                pos: pos(&x.pos),
                name,
                decl_hash: Some(hash),
                sort_text: None,
            }),
            Decl::Typedef(x) => info.ids.typedefs.push(Id {
                pos: pos(&x.pos),
                name,
                decl_hash: Some(hash),
                sort_text: None,
            }),
            Decl::Const(x) => info.ids.consts.push(Id {
                pos: pos(&x.pos),
                name,
                decl_hash: Some(hash),
                sort_text: None,
            }),
            Decl::Module(x) => info.ids.modules.push(Id {
                pos: pos(&x.mdt_pos),
                name,
                decl_hash: Some(hash),
                sort_text: None,
            }),
        }
    }
    // Match OCaml ordering
    info.ids.classes.reverse();
    info.ids.funs.reverse();
    info.ids.typedefs.reverse();
    info.ids.consts.reverse();
    info.ids.modules.reverse();
    info
}

fn parsed_file_to_file_info_obr<'a>(file: ParsedFileWithHashes<'a>) -> FileInfo {
    let mut info = FileInfo {
        position_free_decl_hash: file_info::HashType(Some(Int64::from(
            file.file_decls_hash.as_u64() as i64,
        ))),
        file_mode: file.mode,
        ids: Ids {
            funs: vec![],
            classes: vec![],
            typedefs: vec![],
            consts: vec![],
            modules: vec![],
        },
        comments: None,
    };
    let pos = |p: &oxidized_by_ref::pos::Pos<'_>| file_info::Pos::Full(p.to_owned());
    use oxidized_by_ref::shallow_decl_defs::Decl;
    for &(name, decl, hash, sort_text) in file.iter() {
        let hash = Int64::from(hash.as_u64() as i64);
        match decl {
            Decl::Class(x) => info.ids.classes.push(Id {
                pos: pos(x.name.0),
                name: name.into(),
                decl_hash: Some(hash),
                sort_text: sort_text.map(|s| s.to_string()),
            }),
            Decl::Fun(x) => info.ids.funs.push(Id {
                pos: pos(x.pos),
                name: name.into(),
                decl_hash: Some(hash),
                sort_text: None,
            }),
            Decl::Typedef(x) => info.ids.typedefs.push(Id {
                pos: pos(x.pos),
                name: name.into(),
                decl_hash: Some(hash),
                sort_text: None,
            }),
            Decl::Const(x) => info.ids.consts.push(Id {
                pos: pos(x.pos),
                name: name.into(),
                decl_hash: Some(hash),
                sort_text: None,
            }),
            Decl::Module(x) => info.ids.modules.push(Id {
                pos: pos(x.mdt_pos),
                name: name.into(),
                decl_hash: Some(hash),
                sort_text: None,
            }),
        }
    }
    // Match OCaml ordering
    info.ids.classes.reverse();
    info.ids.funs.reverse();
    info.ids.typedefs.reverse();
    info.ids.consts.reverse();
    info.ids.modules.reverse();
    info
}
ocaml_ffi! {
    fn batch_index_hackrs_ffi_root_relative_paths_only(
        parser_options: DeclParserOptions,
        deregister_php_stdlib_if_hhi: bool,
        root: PathBuf,
        filenames: Vec<(RelativePath, Option<Option<Vec<u8>>>)>,
    ) -> Vec<(RelativePath, Option<(FileInfo, Int64, Vec<SiAddendum>)>)> {
        let filenames_and_contents = par_read_file_root_only(&root, filenames).unwrap_ocaml();
        filenames_and_contents
            .into_par_iter()
            .map(|(relpath, contents)| {
                let contents = match contents {
                    Some(contents) => contents,
                    None => return (relpath, None),
                };
                if parser_options.use_oxidized_by_ref_decls {
                    let arena = Bump::new();

                    let parsed_file = direct_decl_parser::parse_decls_for_typechecking_obr(
                        &parser_options,
                        relpath.clone(),
                        &contents,
                        &arena,
                    );

                    let with_hashes = ParsedFileWithHashes::new(
                        parsed_file,
                        deregister_php_stdlib_if_hhi,
                        relpath.prefix(),
                        &arena,
                    );

                    let addenda = si_addendum::get_si_addenda_obr(&with_hashes);
                    let file_decls_hash = Int64(with_hashes.file_decls_hash.as_u64() as i64);
                    let file_info = parsed_file_to_file_info_obr(with_hashes);

                    (relpath, Some((file_info, file_decls_hash, addenda)))
                } else {
                    let parsed_file = direct_decl_parser::parse_decls_for_typechecking(
                        &parser_options,
                        relpath.clone(),
                        &contents,
                    );

                    let with_hashes = oxidized::direct_decl_parser::ParsedFileWithHashes::new(
                        parsed_file,
                        deregister_php_stdlib_if_hhi,
                        relpath.prefix(),
                    );

                    let addenda = si_addendum::get_si_addenda(&with_hashes);
                    let file_decls_hash = Int64(with_hashes.file_decls_hash.as_u64() as i64);
                    let file_info = parsed_file_to_file_info(with_hashes);

                    (relpath, Some((file_info, file_decls_hash, addenda)))
                }
            })
            .collect()
    }
}

// For each file in filenames, return a tuple of its path followed by `Some` of
// its contents if the file is found, otherwise `None`.
// The filenames are either [("file1.php", None); ("absent.php", None)] to
// indicate that we here in Rust should try to fetch the contents,
// or [("file1.php", Some(Some(present))); ("absent.php", Some(None))] to
// indicate that the content was supplied by our ocaml caller (used for
// testing only, since the ocaml TestDisk isn't available to Rust).
fn par_read_file_root_only(
    root: &Path,
    filenames: Vec<(RelativePath, Option<Option<Vec<u8>>>)>,
) -> Result<Vec<(RelativePath, Option<Vec<u8>>)>> {
    filenames
        .into_par_iter()
        .map(|(relpath, test_contents)| {
            if let Some(test_contents) = test_contents {
                Ok((relpath, test_contents))
            } else {
                let prefix = relpath.prefix();
                let abspath = match prefix {
                    relative_path::Prefix::Root => root.join(relpath.path()),
                    _ => panic!("should only be reading files relative to root"),
                };
                match std::fs::read(abspath) {
                    Ok(text) => Ok((relpath, Some(text))),
                    Err(e) if e.kind() == io::ErrorKind::NotFound => Ok((relpath, None)),
                    Err(e) => Err(e.into()),
                }
            }
        })
        .collect()
}

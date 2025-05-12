(*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *)

open Hh_prelude
open Hack
open Src

let is_enum_or_enum_class = function
  | Ast_defs.Cenum
  | Ast_defs.Cenum_class _ ->
    true
  | Ast_defs.(Cinterface | Cclass _ | Ctrait) -> false

let ends_in_newline source_text =
  let last_char =
    Full_fidelity_source_text.(get source_text (source_text.length - 1))
  in
  Char.equal '\n' last_char || Char.equal '\r' last_char

let has_tabs_or_multibyte_codepoints source_text =
  let open Full_fidelity_source_text in
  let check_codepoint (num, found) _index = function
    | `Uchar u -> (num + 1, found || Uchar.equal u (Uchar.of_char '\t'))
    | `Malformed _ -> (num + 1, true)
  in
  let (num_chars, found_tab_or_malformed) =
    Uutf.String.fold_utf_8 check_codepoint (0, false) source_text.text
  in
  found_tab_or_malformed || num_chars < source_text.length

(* Split name or subnamespace from its parent namespace, and return
   either Some (parent, name), or None if the name has no parent namespace.
   The trailing slash is removed from the parent. *)
let split_name (s : string) : (string * string) option =
  match String.rindex s '\\' with
  | None -> None
  | Some pos ->
    let name_start = pos + 1 in
    let name =
      String.sub s ~pos:name_start ~len:(String.length s - name_start)
    in
    let parent_namespace = String.sub s ~pos:0 ~len:(name_start - 1) in
    if String.is_empty parent_namespace || String.is_empty name then
      None
    else
      Some (parent_namespace, name)

module Token = Full_fidelity_positioned_syntax.Token

let tokens_to_pos_id st ~hd ~tl =
  let path = st.Full_fidelity_source_text.file_path in
  let start_offset = Token.leading_start_offset hd in
  let name = String.concat ~sep:"\\" (List.map (hd :: tl) ~f:Token.text) in
  let end_offset = start_offset + String.length name in
  let pos =
    Full_fidelity_source_text.relative_pos path st start_offset end_offset
  in
  (pos, name)

exception Ast_error

exception Empty_namespace

let namespace_ast_to_pos_id ns_ast st =
  let open Full_fidelity_positioned_syntax in
  let f item =
    match item.syntax with
    | ListItem { list_item = { syntax = Token t; _ }; _ } -> t
    | _ -> raise Ast_error
  in
  let (hd, tl) =
    match ns_ast with
    | Token t -> (t, [])
    | QualifiedName
        { qualified_name_parts = { syntax = SyntaxList (hd :: tl); _ } } ->
      (f hd, List.map ~f tl)
    | Missing -> raise Empty_namespace
    | _ -> raise Ast_error
  in
  tokens_to_pos_id st ~hd ~tl

let remove_generated_tparams tparams =
  let param_name Aast_defs.{ tp_name = (_, name); _ } = name in
  Typing_print.split_desugared_ctx_tparams_gen ~tparams ~param_name |> snd

(* Remove leading slash, if present, so names such as
   Exception and \Exception are captured by the same fact *)
let make_name name = Name.Key (Utils.strip_ns name)

let rec make_namespaceqname ns =
  let open NamespaceQName in
  Key
    (match split_name ns with
    | None -> { name = make_name ns; parent = None }
    | Some (parent_ns, namespace) ->
      {
        name = make_name namespace;
        parent = Some (make_namespaceqname parent_ns);
      })

let make_qname qname =
  let open QName in
  Key
    (match split_name qname with
    | None -> { name = make_name qname; namespace_ = None }
    | Some (ns, name) ->
      { name = make_name name; namespace_ = Some (make_namespaceqname ns) })

let make_constraint_kind = function
  | Ast_defs.Constraint_as -> ConstraintKind.As
  | Ast_defs.Constraint_eq -> ConstraintKind.Equal
  | Ast_defs.Constraint_super -> ConstraintKind.Super

let make_visibility = function
  | Aast.Private -> Visibility.Private
  | Aast.Protected -> Visibility.Protected
  | Aast.Public -> Visibility.Public
  | Aast.Internal -> Visibility.Internal
  | Aast.ProtectedInternal -> Visibility.ProtectedInternal

let make_type_const_kind = function
  | Aast.TCAbstract _ -> TypeConstKind.Abstract
  | Aast.TCConcrete _ -> TypeConstKind.Concrete

let make_byte_span pos =
  ByteSpan.{ start = fst (Pos.info_raw pos); length = Pos.length pos }

let make_variance =
  let open Variance in
  function
  | Ast_defs.Contravariant -> Contravariant
  | Ast_defs.Covariant -> Covariant
  | Ast_defs.Invariant -> Invariant

let make_reify_kind =
  let open ReifyKind in
  function
  | Ast_defs.Erased -> Erased
  | Ast_defs.Reified -> Reified
  | Ast_defs.SoftReified -> SoftReified

let class_hint_to_qname (_, chint) =
  match chint with
  | Aast_defs.Happly ((_, cname), _) -> Some (make_qname cname)
  | _ ->
    Hh_logger.log
      "Shouldn't happen: expected class hint bug got %s"
      (Aast_defs.show_hint_ chint);
    None

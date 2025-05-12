(*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *)

open Hh_prelude

type t = {
  param_pos: Pos.t;
  derive_prompt: unit -> string option;
  edit_span: Pos.t;
}

let derive_prompt env e () = Simplihack_interpreter.eval env e |> Result.ok

let find ctx tast =
  let user_attribute env edit_span acc ua =
    let attribute_name = snd ua.Aast.ua_name in
    if
      String.equal
        attribute_name
        Naming_special_names.UserAttributes.uaSimpliHack
    then
      match ua.Aast.ua_params with
      | [arg] ->
        let param_pos = Pos.btw (fst ua.Aast.ua_name) (snd3 arg) in
        { param_pos; edit_span; derive_prompt = derive_prompt env arg } :: acc
      | [arg1; arg2] ->
        let param_pos = Pos.btw (fst ua.Aast.ua_name) (snd3 arg2) in
        { param_pos; edit_span; derive_prompt = derive_prompt env arg1 } :: acc
      | _ -> acc
    else
      acc
  in
  let visitor =
    object
      inherit [_] Tast_visitor.reduce as super

      method zero = []

      method plus = ( @ )

      method! on_class_ env cls =
        let acc = super#on_class_ env cls in
        List.fold
          ~init:acc
          ~f:(user_attribute env cls.Aast.c_span)
          cls.Aast.c_user_attributes

      method! on_method_ env meth =
        let acc = super#on_method_ env meth in
        List.fold
          ~init:acc
          ~f:(user_attribute env meth.Aast.m_span)
          meth.Aast.m_user_attributes

      method! on_fun_ env func =
        let acc = super#on_fun_ env func in
        List.fold
          ~init:acc
          ~f:(user_attribute env func.Aast.f_span)
          func.Aast.f_user_attributes

      method! on_class_var env cv =
        let acc = super#on_class_var env cv in
        List.fold
          ~init:acc
          ~f:(user_attribute env cv.Aast.cv_span)
          cv.Aast.cv_user_attributes

      method! on_typedef env x =
        let acc = super#on_typedef env x in
        List.fold
          ~init:acc
          ~f:(user_attribute env x.Aast.t_span)
          x.Aast.t_user_attributes

      method! on_class_const env x =
        let acc = super#on_class_const env x in
        List.fold
          ~init:acc
          ~f:(user_attribute env x.Aast.cc_span)
          x.Aast.cc_user_attributes

      method! on_fun_param env x =
        let acc = super#on_fun_param env x in
        match x.Aast.param_visibility with
        | None -> acc
        | Some _ ->
          List.fold
            ~init:acc
            ~f:(user_attribute env x.Aast.param_pos)
            x.Aast.param_user_attributes

      method! on_file_attribute env x =
        let acc = super#on_file_attribute env x in
        List.fold
          ~init:acc
          ~f:(user_attribute env Pos.none)
          x.Aast.fa_user_attributes

      method! on_tparam env x =
        let acc = super#on_tparam env x in
        List.fold
          ~init:acc
          ~f:(user_attribute env (fst x.Aast.tp_name))
          x.Aast.tp_user_attributes

      method! on_class_typeconst_def env x =
        let acc = super#on_class_typeconst_def env x in
        List.fold
          ~init:acc
          ~f:(user_attribute env x.Aast.c_tconst_span)
          x.Aast.c_tconst_user_attributes

      method! on_module_def env x =
        let acc = super#on_module_def env x in
        List.fold
          ~init:acc
          ~f:(user_attribute env x.Aast.md_span)
          x.Aast.md_user_attributes
    end
  in
  visitor#go ctx tast

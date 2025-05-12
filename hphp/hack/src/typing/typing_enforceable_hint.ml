(*
 * Copyright (c) 2018, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *)

open Hh_prelude
open Typing_defs
module Env = Typing_env
module Reason = Typing_reason
module Cls = Folded_class
module SN = Naming_special_names

let validator =
  object (this)
    inherit Type_validator.type_validator as super

    (* Only comes about because naming has reported an error and left Hany *)
    method! on_tany acc _ = acc

    method! on_tprim acc r prim =
      match prim with
      | Aast.Tvoid -> this#invalid acc r "the `void` type"
      | Aast.Tnoreturn -> this#invalid acc r "the `noreturn` type"
      | _ -> acc

    method! on_tfun acc r _fun_type = this#invalid acc r "a function type"

    method! on_tvar acc r _id = this#invalid acc r "an unknown type"

    method! on_typeconst acc class_ typeconst =
      match typeconst.ttc_kind with
      | TCConcrete _ -> super#on_typeconst acc class_ typeconst
      | TCAbstract _
        when (* get_typeconst_enforceability should always return Some here, since we
                know the typeconst exists (else we wouldn't be in this method).
                But since we have to map it to a bool anyway, we just use
                Option.value_map. *)
             Option.value_map
               ~f:snd
               ~default:false
               (Cls.get_typeconst_enforceability
                  class_
                  (snd typeconst.ttc_name)) ->
        super#on_typeconst acc class_ typeconst
      | TCAbstract _ ->
        let (pos, tconst) = typeconst.ttc_name in
        let r = Reason.witness_from_decl pos in
        this#invalid acc r
        @@ "the abstract type constant "
        ^ tconst
        ^ " because it is not marked `<<__Enforceable>>`"

    method! on_tgeneric acc r name = this#check_generic acc r name

    method! on_newtype acc r sid _ as_cstr _super_cstr _ =
      if String.equal (snd sid) SN.Classes.cSupportDyn then
        this#on_type acc (with_reason as_cstr r)
      else
        this#invalid acc r "a `newtype`"

    method! on_tlike acc _r ty = this#on_type acc ty

    method! on_class acc r cls tyl =
      match Env.get_class acc.Type_validator.env (snd cls) with
      | Decl_entry.Found tc ->
        (match Cls.kind tc with
        | Ast_defs.Ctrait -> this#invalid acc r "a trait name"
        | _ ->
          let tparams = Cls.tparams tc in
          begin
            match tyl with
            | [] -> acc
            (* this case should really be handled by the fold2,
               but we still allow class hints without args in certain places *)
            | targs ->
              List.Or_unequal_lengths.(
                begin
                  match
                    List.fold2
                      ~init:acc
                      targs
                      tparams
                      ~f:(fun acc targ tparam ->
                        let inside_reified_class_generic_position =
                          acc
                            .Type_validator
                             .inside_reified_class_generic_position
                        in
                        if this#is_wildcard targ then begin
                          if inside_reified_class_generic_position then
                            this#invalid
                              acc
                              r
                              "a reified type containing a wildcard (`_`)"
                          else
                            acc
                        end else if
                              Aast.(equal_reify_kind tparam.tp_reified Reified)
                            then
                          let old_inside_reified_class_generic_position =
                            inside_reified_class_generic_position
                          in
                          let acc =
                            this#on_type
                              {
                                acc with
                                Type_validator
                                .inside_reified_class_generic_position = true;
                              }
                              targ
                          in
                          {
                            acc with
                            Type_validator.inside_reified_class_generic_position =
                              old_inside_reified_class_generic_position;
                          }
                        else if inside_reified_class_generic_position then
                          this#on_type acc targ
                        else
                          let error_message =
                            "a type with an erased generic type argument"
                          in
                          this#invalid acc r error_message)
                  with
                  | Ok new_acc -> new_acc
                  | Unequal_lengths -> acc (* arity error elsewhere *)
                end)
          end)
      | Decl_entry.DoesNotExist
      | Decl_entry.NotYetAvailable ->
        acc

    method! on_alias acc r _id tyl ty =
      match List.filter ~f:(fun ty -> not @@ this#is_wildcard ty) tyl with
      | [] -> this#on_type acc ty
      | _ ->
        this#invalid
          acc
          r
          "a type with generics, because generics are erased at runtime"

    method! on_tunion acc r tyl =
      match tyl with
      | [] -> this#invalid acc r "the `nothing` type"
      | _ -> super#on_tunion acc r tyl

    method! on_tintersection acc r _ =
      this#invalid
        acc
        r
        "an intersection type, which is restricted to coeffects"

    method is_wildcard ty =
      match get_node ty with
      | Twildcard -> true
      | _ -> false

    method check_for_wildcards acc tyl s =
      match List.filter tyl ~f:this#is_wildcard with
      | [] -> acc
      | tyl ->
        this#invalid_list
          acc
          (List.map tyl ~f:(fun ty ->
               ( Typing_defs_core.get_reason ty,
                 "_ in a " ^ s ^ " (use `mixed` instead)" )))

    method! on_ttuple acc r { t_required; t_extra } =
      let acc = List.fold_left t_required ~f:this#on_type ~init:acc in
      match t_extra with
      | Textra { t_optional; t_variadic } ->
        (* HHVM doesn't currently support is/as on open tuples, so let's reject it in Hack *)
        if (not (is_nothing t_variadic)) || not (List.is_empty t_optional) then
          this#invalid acc r
          @@ "a tuple type with optional or variadic elements"
        else
          this#check_for_wildcards acc t_required "tuple"
      | Tsplat _ ->
        (* HHVM doesn't currently support is/as on type splats, so let's reject it in Hack *)
        this#invalid acc r @@ "a tuple type with a splat element"

    method! on_tshape acc _ { s_fields = fdm; _ } =
      let tyl = TShapeMap.values fdm |> List.map ~f:(fun s -> s.sft_ty) in
      let acc = List.fold_left tyl ~init:acc ~f:this#on_type in
      this#check_for_wildcards acc tyl "shape"

    method! on_tclass_ptr acc r _ty =
      (* TODO(T199611023) allow when we enforce inner type *)
      this#invalid acc r "a class pointer type"

    method check_generic acc r name =
      (* No need to look at type arguments of generic var, as higher-kinded type params
         cannot be enforcable *)
      (* TODO(T70069116) implement enforcability check *)
      match
        ( Env.get_reified acc.Type_validator.env name,
          Env.get_enforceable acc.Type_validator.env name )
      with
      | (Aast.Erased, _) ->
        this#invalid acc r "an erased generic type parameter"
      | (Aast.SoftReified, _) ->
        this#invalid acc r "a soft reified generic type parameter"
      | (Aast.Reified, false) ->
        (* If a reified generic is an argument to a reified class it does not
         * need to be enforceable *)
        if acc.Type_validator.inside_reified_class_generic_position then
          acc
        else
          this#invalid
            acc
            r
            "a reified type parameter that is not marked `<<__Enforceable>>`"
      | (Aast.Reified, true) -> acc
  end

let validate_hint = validator#validate_hint ?reification:None

let validate_type = validator#validate_type ?reification:None

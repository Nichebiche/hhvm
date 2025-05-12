(*
 * Copyright (c) 2015, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *)

open Hh_prelude

(* Order symbols from innermost to outermost *)
let by_nesting x y =
  if Pos.contains x.SymbolOccurrence.pos y.SymbolOccurrence.pos then
    if Pos.contains y.SymbolOccurrence.pos x.SymbolOccurrence.pos then
      0
    else
      1
  else
    -1

let rec take_best_suggestions l =
  match l with
  | first :: rest ->
    (* Check if we should stop finding suggestions. For example, in
       "foo($bar)" it's not useful to look outside the local variable "$bar". *)
    let stop =
      match first.SymbolOccurrence.type_ with
      | SymbolOccurrence.LocalVar
      | SymbolOccurrence.Method _
      | SymbolOccurrence.Class _ ->
        true
      | _ -> false
    in
    if stop then
      (* We're stopping here, but also include the other suggestions for
         this span. *)
      first :: List.take_while rest ~f:(fun x -> by_nesting first x = 0)
    else
      first :: take_best_suggestions rest
  | [] -> []

(** Given a file and a position, return a list of symbols at that position,
    plus information about the definition of that symbol if found. *)
let go_quarantined
    ~(ctx : Provider_context.t) ~(entry : Provider_context.entry) pos =
  let (symbols : _ SymbolOccurrence.t list) =
    IdentifySymbolService.go_quarantined
      ~ctx
      ~entry
      pos
      ~use_declaration_spans:false
  in
  let symbols = take_best_suggestions (List.sort ~compare:by_nesting symbols) in
  (* TODO(ljw): shouldn't the following be quarantined also? *)
  List.map symbols ~f:(fun symbol ->
      let ast =
        Ast_provider.compute_ast ~popt:(Provider_context.get_popt ctx) ~entry
      in
      let symbol_definition = ServerSymbolDefinition.go ctx (Some ast) symbol in
      (symbol, symbol_definition))

let go_quarantined_absolute
    ~(ctx : Provider_context.t) ~(entry : Provider_context.entry) pos :
    (string SymbolOccurrence.t * string SymbolDefinition.t option) list =
  go_quarantined ~ctx ~entry pos
  |> List.map ~f:(fun (occurrence, definition) ->
         let occurrence = SymbolOccurrence.to_absolute occurrence in
         let definition =
           Option.map ~f:SymbolDefinition.to_absolute definition
         in
         (occurrence, definition))

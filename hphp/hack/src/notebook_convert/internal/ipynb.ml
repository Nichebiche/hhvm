(*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the "hack" directory of this source tree.
 *
 *)
open Hh_prelude

type cell =
  | Non_hack of {
      cell_type: string;
      contents: string;
      cell_bento_metadata: Hh_json.json option;
    }
  | Hack of {
      contents: string;
      cell_bento_metadata: Hh_json.json option;
    }
[@@deriving show]

type t = {
  cells: cell list;
  kernelspec: Hh_json.json;
}

(* Normalize the "source" field (string list) into a single string.
   * Note that Jupyter docs don't specify whether such source lines should
   * end in "\n" or not and we receive notebooks in either format,
   * so we allow either and try to do the right thing.
   * https://ipython.org/ipython-doc/3/notebook/nbformat.html
*)
let cell_contents_of_source_lines (source_lines : string list) : string =
  source_lines
  |> List.map ~f:(fun s ->
         if String.is_suffix ~suffix:"\n" s then
           String.sub s ~pos:0 ~len:(String.length s - 1)
         else
           s)
  |> String.concat ~sep:"\n"

let is_most_likely_hack
    (cell_bento_metadata : Hh_json.json option)
    ~(contents : string)
    ~(cell_type : string) : bool =
  (* we try not to make hard assumptions about the metadata: assume it's hack *)
  let bad_or_missing_metadata_value = true in
  String.equal cell_type "code"
  && (not (String.is_prefix contents ~prefix:"%%"))
  &&
  match cell_bento_metadata with
  | None -> bad_or_missing_metadata_value
  | Some cell_bento_metadata -> begin
    try
      let obj = Hh_json.get_object_exn cell_bento_metadata in
      let language =
        List.Assoc.find_exn obj "language" ~equal:String.equal
        |> Hh_json.get_string_exn
      in
      String.equal language "hack"
    with
    | _ -> bad_or_missing_metadata_value
  end

let ipynb_of_json (ipynb_json : Hh_json.json) : (t, string) Result.t =
  let cell_of_json_exn (cell_json : Hh_json.json) : cell =
    let find_exn = List.Assoc.find_exn ~equal:String.equal in
    let obj = Hh_json.get_object_exn cell_json in
    let type_json = find_exn obj "cell_type" in
    let contents =
      let source_json = find_exn obj "source" in
      let source_lines_json = Hh_json.get_array_exn source_json in
      let source_lines = List.map source_lines_json ~f:Hh_json.get_string_exn in
      cell_contents_of_source_lines source_lines
    in
    let cell_bento_metadata =
      List.Assoc.find obj "metadata" ~equal:String.equal
    in
    let cell_type =
      match type_json with
      | JSON_String s -> s
      | _ -> "unknown"
    in
    if is_most_likely_hack cell_bento_metadata ~contents ~cell_type then
      Hack { contents; cell_bento_metadata }
    else
      (* Python cells start with `%%`. In future, other cells might, too, so have cell_type be unknown *)
      Non_hack { cell_type; contents; cell_bento_metadata }
  in
  try
    let find_exn = List.Assoc.find_exn ~equal:String.equal in
    let obj = Hh_json.get_object_exn ipynb_json in
    let cells_json = find_exn obj "cells" in
    let metadata = Hh_json.get_object_exn @@ find_exn obj "metadata" in
    let kernelspec = find_exn metadata "kernelspec" in
    let cells_jsons = Hh_json.get_array_exn cells_json in
    let cells = cells_jsons |> List.map ~f:cell_of_json_exn in
    Ok { cells; kernelspec }
  with
  | e -> Error (Exn.to_string e)

(** Software we work with that consumes notebooks
* expects that:
* - Every string in the "source" array (except possibly the last one) ends in "\n".
* - There are no empty strings (`""`) in the "source" array
* Note that, afaict, these requirements are not officially specified:
* https://ipython.org/ipython-doc/3/notebook/nbformat.html
*)
let normalize_output_lines_in_source_array (source : string list) : string list
    =
  let length = List.length source in
  let is_last index = index = length - 1 in
  List.filter_mapi source ~f:(fun i s ->
      if String.is_empty s then
        None
      else if String.is_suffix ~suffix:"\n" s || is_last i then
        Some s
      else
        (* The thing that consumes notebooks expects that every line in the "source" array
         * (except possibly the last line) ends in "\n".
         *)
        Some (s ^ "\n"))

let make_ipynb_cell
    ~(cell_type : string)
    ~(source : string list)
    ~(cell_bento_metadata : Hh_json.json option) =
  let source =
    source
    |> normalize_output_lines_in_source_array
    |> List.map ~f:Hh_json.string_
  in
  Hh_json.(
    JSON_Object
      [
        ("cell_type", JSON_String cell_type);
        ("source", JSON_Array source);
        ("outputs", JSON_Array []);
        ("execution_count", JSON_Null);
        ( "metadata",
          match cell_bento_metadata with
          | Some cell_bento_metadata -> cell_bento_metadata
          | None -> JSON_Object [] );
      ])

let ipynb_to_json ({ cells; kernelspec } : t) : Hh_json.json =
  let json_cells =
    List.map cells ~f:(function
        | Non_hack { cell_type; contents; cell_bento_metadata } ->
          make_ipynb_cell
            ~cell_type
            ~source:(String.split_lines contents)
            ~cell_bento_metadata
        | Hack { contents; cell_bento_metadata } ->
          make_ipynb_cell
            ~cell_type:"code"
            ~source:(String.split_lines contents)
            ~cell_bento_metadata)
  in
  Hh_json.(
    let nb_format = JSON_Number "4" in
    JSON_Object
      [
        ("cells", JSON_Array json_cells);
        ( "metadata",
          JSON_Object [("kernelspec", kernelspec); ("orig_nbformat", nb_format)]
        );
        ("nbformat", nb_format);
      ])

let cell_of_chunk
    Notebook_chunk.{ id = _; chunk_kind; contents; cell_bento_metadata } : cell
    =
  match chunk_kind with
  | Notebook_chunk.Hack -> Hack { contents; cell_bento_metadata }
  | Notebook_chunk.(Non_hack { cell_type }) ->
    Non_hack { cell_type; contents; cell_bento_metadata }

let combine_bento_cell_metadata_exn
    (md1 : Hh_json.json option) (md2 : Hh_json.json option) :
    Hh_json.json option =
  let json_equal a b =
    String.equal
      (Hh_json.json_to_string ~sort_keys:true a)
      (Hh_json.json_to_string ~sort_keys:true b)
  in
  match (md1, md2) with
  | (Some md1, Some md2) when not @@ json_equal md1 md2 ->
    failwith
      "Inconsistent cell metadata: two cells with the same id had different metadata"
  | _ -> Option.first_some md1 md2

(** Partial function: can only combine cells with the same cell type *)
let combine_cells_exn (cell1 : cell) (cell2 : cell) : cell =
  match (cell1, cell2) with
  | ( Hack { contents = contents1; cell_bento_metadata = cell_bento_metadata1 },
      Hack { contents = contents2; cell_bento_metadata = cell_bento_metadata2 }
    ) ->
    (* A Hack cell could be split between declarations and top-level statements.
       The metadata will likely be equal anyway.
    *)
    let cell_bento_metadata =
      combine_bento_cell_metadata_exn cell_bento_metadata1 cell_bento_metadata2
    in
    Hack { contents = contents1 ^ contents2; cell_bento_metadata }
  | ( Non_hack
        {
          cell_type = cell_type1;
          contents = contents1;
          cell_bento_metadata = cell_bento_metadata1;
        },
      Non_hack
        {
          cell_type = cell_type2;
          contents = contents2;
          cell_bento_metadata = cell_bento_metadata2;
        } )
    when String.equal cell_type1 cell_type2 ->
    let cell_bento_metadata =
      combine_bento_cell_metadata_exn cell_bento_metadata1 cell_bento_metadata2
    in
    Non_hack
      {
        cell_type = cell_type1;
        contents = contents1 ^ contents2;
        cell_bento_metadata;
      }
  | _ ->
    failwith
      (Printf.sprintf
         "Should be unreachable. attempted to combine cells with incompatible cell types. Got cell1: %s and cell2: %s"
         (show_cell cell1)
         (show_cell cell2))

let ipynb_of_chunks
    (chunks : Notebook_chunk.t list) ~(kernelspec : Hh_json.json) :
    (t, Notebook_convert_error.t) result =
  try
    let chunks_grouped_by_id =
      List.sort_and_group
        chunks
        ~compare:(fun
                   Notebook_chunk.{ id = id1; _ }
                   Notebook_chunk.{ id = id2; _ }
                 -> Notebook_chunk.Id.compare id1 id2)
    in
    let cells =
      List.map chunks_grouped_by_id ~f:(fun chunks ->
          chunks
          |> List.map ~f:cell_of_chunk
          (* reduce_exn is safe because groups created by sort_and_group are non-empty *)
          |> List.reduce_exn ~f:combine_cells_exn)
    in
    let cells =
      (* Our Jupyter notebooks implementation cannot handle an empty cell list,
       * so we add an empty cell if the user's notebook has no cells.
       *)
      if List.is_empty cells then
        [Hack { contents = ""; cell_bento_metadata = None }]
      else
        cells
    in
    Ok { cells; kernelspec }
  with
  | e ->
    (* blame the user: failure during parsing is almost certainly their fault,
       such as corrupted //@bento-cell:$JSON_HERE
    *)
    Error (Notebook_convert_error.Invalid_input (Exn.to_string e))

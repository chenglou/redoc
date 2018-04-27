
open Outcometree;

let rec collectArgs = (coll, typ) => switch typ.Types.desc {
| Types.Tarrow(label, arg, result, _) => collectArgs([(label, arg), ...coll], result)
| Tlink(inner) => collectArgs(coll, inner)
| Tsubst(inner) => collectArgs(coll, inner)
| _ => (coll, typ)
};

type pathType = PModule | PModuleType | PValue | PType;

module T = {
  type stringifier = {
    path: (stringifier, Path.t, pathType) => Pretty.doc,
    expr: (stringifier, Types.type_expr) => Pretty.doc,
    ident: (stringifier, Ident.t) => Pretty.doc,
    decl: (stringifier, string, string, Types.type_declaration) => Pretty.doc,
    value: (stringifier, string, string, Types.type_expr) => Pretty.doc,
  };
};
open T;

let break = Pretty.line("");
let space = Pretty.line(" ");
let dedent = Pretty.back(2, "");

let str = Pretty.text;
let (@!) = Pretty.append;

let sepd_list = (sep, items, loop) => {
  let rec recur = items => switch items {
    | [] => Pretty.empty
    | [one] => loop(one)
    | [one, ...more] => loop(one) @! sep @! recur(more)
  };
  recur(items)
};

let commad_list = (loop, items) => {
  sepd_list(str(",") @! space, items, loop)
};

let indentGroup = doc => Pretty.indent(2, Pretty.group(doc));

let tuple_list = (items, loop) => {
  str("(") @! indentGroup(break @!
  commad_list(loop, items) @!
  dedent) @!
  str(")")
};

let replace = (one, two, text) => Str.global_replace(Str.regexp_string(one), two, text);
let htmlEscape = text => replace("<", "&lt;", text) |> replace(">", "&gt;");

let print_expr = (stringifier, typ) => {
  let loop = stringifier.expr(stringifier);
  open Types;
  switch (typ.desc) {
  | Tvar(None) => str("'a")
  | Tvar(Some(s)) => str("'" ++ s)
  | Tarrow(label, arg, result, _) => {
    let (args, result) = collectArgs([(label, arg)], result);
    let args = List.rev(args);
    switch args {
    | [("", typ)] => {
      loop(typ)
    }
    | _ => {

    str("(") @!
    indentGroup(
      break @!
    commad_list(((label, typ)) => {
      if (label == "") {
        loop(typ)
      } else {
        str("~" ++ label ++ ": ") @! loop(typ)
      }
    }, args)
    @! dedent
    ) @! str(")")
    }
    }
     @! str(" => ") @!
    loop(result);
  }
  | Ttuple(items) => tuple_list(items, loop)
  | Tconstr(path, args, _) => {
    stringifier.path(stringifier, path, PType) @!
    switch args {
    | [] => Pretty.empty
    | args => tuple_list(args, loop)
    }
  }
  | Tlink(inner) => loop(inner)
  | Tsubst(inner) => loop(inner)
  | Tfield(_, _, _, _)
  | Tnil
  | Tvariant(_)
  | Tunivar(_)
  | Tpoly(_, _)
  | Tpackage(_, _, _)
  | Tobject(_, _) => str({Printtyp.type_expr(Format.str_formatter, typ); Format.flush_str_formatter()} |> htmlEscape)
  }
};

let print_constructor = (loop, {Types.cd_id: {name}, cd_args, cd_res}) => {
  str(name) @!
  (switch cd_args {
  | [] => Pretty.empty
  | args => tuple_list(args, loop)
  }) @!
  (switch cd_res {
  | None => Pretty.empty
  | Some(typ) => {
    str(": ") @!
    loop(typ)
  }
  })
};

let print_attr = (printer, {Types.ld_id, ld_mutable, ld_type}) => {
  switch ld_mutable {
  | Asttypes.Immutable => Pretty.empty
  | Mutable => str("mut ")
  } @!
  printer.ident(printer, ld_id) @!
  str( ": ") @!
  printer.expr(printer, ld_type);
};

let print_value = (stringifier, realName, name, decl) => {
  str("let ") @!
  str(~len=String.length(realName), name) @!
  str(": ") @! stringifier.expr(stringifier, decl)
};

let print_decl = (stringifier, realName, name, decl) => {
  open Types;
  str("type ") @!
  str(~len=String.length(realName), name) @!
  switch decl.type_params {
  | [] => Pretty.empty
  | args => tuple_list(args, stringifier.expr(stringifier))
  } @!
  switch decl.type_kind {
  | Type_abstract => Pretty.empty
  | Type_open => str(" = ..")
  | Type_record(labels, representation) => {
    str(" = {") @! indentGroup(break @!
    commad_list(print_attr(stringifier), labels)
     @! dedent) @! str("}")
  }
  | Type_variant(constructors) => {
    str(" = ") @! indentGroup(break @! str("| ") @!
      sepd_list(space @! str("| "), constructors, print_constructor(stringifier.expr(stringifier)))
    ) @! break
  }
  } @!
  switch decl.type_manifest {
  | None => Pretty.empty
  | Some(manifest) => {
    str(" = ") @!
    stringifier.expr(stringifier, manifest)
  }
  };
};

let default = {
  ident: (_, {Ident.name}) => str(name),
  path: (stringifier, path, pathType) => switch path {
    | Path.Pident(ident) => stringifier.ident(stringifier, ident)
    | Pdot(path, name, _) => {stringifier.path(stringifier, path, pathType) @! str("." ++ name)}
    | Papply(_, _) => str("<apply>")
  },
  value: print_value,
  expr: print_expr,
  decl: print_decl,
};
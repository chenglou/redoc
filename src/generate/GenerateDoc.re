
open State.Model.Docs;
open CmtFindStamps.T;

let rec findByName = (allDocs, name) => {
  switch allDocs {
  | [] => None
  | [(n, _, _) as doc, ...rest] when n == name => Some(doc)
  | [_, ...rest] => findByName(rest, name)
  }
};

let rec findTypeByName = (allDocs, name) => {
  switch allDocs {
  | [] => None
  | [(n, _, Type(_)) as doc, ...rest] when n == name => Some(doc)
  | [_, ...rest] => findByName(rest, name)
  }
};

let rec findValueByName = (allDocs, name) => {
  switch allDocs {
  | [] => None
  | [(n, _, Value(_)) as doc, ...rest] when n == name => Some(doc)
  | [_, ...rest] => findByName(rest, name)
  }
};

let isUpperCase = t => t >= 'A' && t <= 'Z';

let rec processPath = (stampsToPaths, collector, path, ptype) => {
  switch path {
  | Path.Pident({name, stamp: 0}) => (name, collector, ptype)
  | Path.Pident({name, stamp}) => switch (List.assoc(stamp, stampsToPaths)) {
  | exception Not_found => ("<global>", [name], ptype)
  | (modName, inner, pp) => (modName, inner @ collector, ptype)
  }
  | Path.Pdot(inner, name, _) => processPath(stampsToPaths, [name, ...collector], inner, ptype)
  | Papply(_, _) => assert(false)
  }
};

open PrintType.T;
let printer = (formatHref, stampsToPaths) => {
  ...PrintType.default,
  path: (printer, path, pathType) => {
    let (@!) = Pretty.append;
    let fullPath = processPath(stampsToPaths, [], path, pathType);
    let full = formatHref(fullPath);
    let show = name => {
      switch full {
      | None => Pretty.text(name)
      | Some(full) => {
        let tag = Printf.sprintf({|<a href="%s">%s</a>|}, full, name);
        Pretty.text(~len=String.length(name), tag)
      }
      }
    };
    switch path {
    | Pident({name}) => show(name)
    | Pdot(inner, name, _) => {
      switch (full) {
      | Some(full) when name != "t" => {
        Printf.sprintf({|<a href="%s" title="%s">%s</a>|}, full, Path.name(path), name) |> Pretty.text(~len=String.length(name))
      }
      | _ => {
        printer.path(printer, inner, PModule) @! Pretty.text(".") @! show(name)}
      }
      }
    | Papply(_, _) => Pretty.text("<papply>")
    };
  }
};

let ptypePrefix = ptype => {
  switch ptype {
  | PModule => "module-"
  | PType => "type-"
  | PValue => "value-"
  | PModuleType => "moduletype-"
  }
};

let makeId = (items, ptype) => {
  /* open CmtFindDocItems.T; */
  ptypePrefix(ptype) ++ String.concat(".", items)
};

let defaultMain = (~addHeading=false, name) => {
  (addHeading ? "# " ++ name ++ "\n\n" : "") ++
  "<span class='missing'>This module does not have a toplevel documentation block.</span>\n\n@all";
};

let prettyString = doc => {
  let buffer = Buffer.create(100);
  Pretty.print(~width=60, ~output=(text => Buffer.add_string(buffer, text)), ~indent=(num => {
    Buffer.add_string(buffer, "\n");
    for (i in 1 to num) { Buffer.add_string(buffer, " ") }
  }), doc);
  Buffer.to_bytes(buffer) |> Bytes.to_string
};

let cleanForLink = text => Str.global_replace(Str.regexp("[^a-zA-Z0-9_.-]"), "-", text);

let uniqueItems = items => {
  let m = Hashtbl.create(100);
  items |> List.filter(((name, _, t)) => {
    switch t {
    | Value(_) => if (Hashtbl.mem(m, name)) { false } else { Hashtbl.add(m, name, true); true }
    | _ => true
    }
  })
};

let trackToc = (~lower=false, tocLevel, override) => {
  let tocItems = ref([]);
  let addToc = item => tocItems := [item, ...tocItems^];
  let addTocs = items => tocItems := items @ tocItems^;
  let lastLevel = ref(0);

  let rec fullOverride =  (element) => switch element {
    | Omd.H1(inner) => Some(addHeader(1, inner))
    | H2(inner) => Some(addHeader(2, inner))
    | H3(inner) => Some(addHeader(3, inner))
    | H4(inner) => Some(addHeader(4, inner))
    | H5(inner) => Some(addHeader(5, inner))
    | _ => override(addTocs, lastLevel, fullOverride, element)
  }
  and addHeader = (num, inner) => {
    lastLevel := num + 1;
    let id = cleanForLink(Omd.to_text(inner));
    let id = lower ? String.lowercase(id) : id;
    let html = Omd.to_html(~override=override(addTocs, lastLevel, fullOverride), inner);
    addToc((tocLevel + num, html, id, "header"));
    Printf.sprintf({|<a href="#%s" id="%s"><h%d>%s</h%d></a>|}, id, id, num, html, num)
  };

  (tocItems, fullOverride)
};

open Infix;

let div = (cls, body) => "<div class='" ++ cls ++ "'>" ++ body ++ "</div>";

let marked = (override, text) => Omd.to_html(~override, Omd.of_string(text));

let link = (id, text) => Printf.sprintf({|<a href="#%s" id="%s">%s</a>|}, id, id, text);

type t = (~override: (Omd.element) => option(string)=?, list(string), string, option(docItem), Omd.t) => string;

let rec generateDoc = (printer, processDocString: t, path, tocLevel, (name, docstring: option(Omd.t), content)) => {
  /* open! CmtFindDocItems.T; */
  let id = makeId(path @ [name]);
  let (middle, tocs) = switch content {
  | Module(items) => {
    let (post, body, tocs) = switch items {
    | Items(items) => {
      let (html, tocs) = docsForModule(printer, processDocString, path @ [name], tocLevel + 1, name, docstring |? Omd.of_string(defaultMain(name)), items);
      ("", div("body module-body", html), tocs)
    }
    | Alias(modulePath) => (" : " ++ prettyString(printer.path(printer, modulePath, PModule)), fold(docstring, "", (doc) => div("body module-body", processDocString(path @ [name], name, Some(Module(Alias(modulePath))), doc))), [])
    };
    (Printf.sprintf({|<h4 class='item module'>module %s%s</h4>%s|}, link(id(PModule), name), post, body),
    tocs @ [(tocLevel, name, id(PModule), "module")]
    )
  }
  | Include(maybePath, contents) => {
    /* TODO hyperlink the path */
    let name = fold(maybePath, "", Path.name);
    let (html, tocs) = docsForModule(printer, processDocString, path, tocLevel, name, docstring |? [Omd.Paragraph([Omd.Text("@all")])], contents);
    (Printf.sprintf({|<h4 class='item module'>include %s</h4><div class='body module-body include-body'>%s</div>|}, name, html), tocs)
  }
  | Value(typ) => {
    let link = link(id(PValue), name);
    let t = printer.value(printer, name, link, typ) |> prettyString;
    let rendered = switch docstring {
    | None => {
      processDocString(path @ [name], name, Some(Value(typ)), []) |> ignore; /* hack */
      /* TODO should I be so loud about missing docs? */
      /* "<span class='missing'>No documentation for this value</span>" */
      ""
    }
    | Some(text) => processDocString(path @ [name], name, Some(Value(typ)), text)
    };
    (Printf.sprintf("<h4 class='item'>%s</h4>\n\n<div class='body %s'>", t, docstring == None ? "body-empty" : "")
     ++ rendered ++ "</div>", [(tocLevel, name, id(PValue), "value")])
  }
  | Type(typ) => {
    let link = link(id(PType), name);
    let t = printer.decl(printer, name, link, typ) |> prettyString;
    let rendered = switch docstring {
    | None => {
      processDocString(path @ [name], name, Some(Type(typ)), []) |> ignore; /* hack */
      /* "<span class='missing'>No documentation for this type</span>" */
      ""
    }
    | Some(text) => processDocString(path @ [name], name, Some(Type(typ)), text)
    };
    ("<h4 class='item'>" ++ String.trim(t) ++ "</h4>\n\n<div class='body " ++ (docstring == None ? "body-empty" : "") ++ "'>" ++ rendered ++ "</div>", [(tocLevel, name, id(PType), "type")])
  }
  | StandaloneDoc(doc) => (processDocString(path, "", None, doc), [])
  };
  (div("doc-item", middle), tocs)
}

and docsForModule = (printer, processDocString, path, tocLevel, name, docString, contents) => {
  open Omd;

  let shownItems = Hashtbl.create(100);
  let shownAll = ref(false);

  let docItems = List.rev(uniqueItems(contents));

  let processMagics = (addTocs, lastLevel, recur, element) => switch element {
  | Paragraph([Text(t)]) => {
    if (String.trim(t) == "@all") {
      Some((List.map(doc => {
        let (html, tocs) = generateDoc(printer, processDocString, path, tocLevel + lastLevel^, doc);
        shownAll := true;
        addTocs(tocs);
        html
      }, docItems) |> String.concat("\n\n")) ++ "\n")
    } else if (Str.string_match(Str.regexp("^@doc [^\n]+"), t, 0)) {
      Some({
        let text = Str.matched_string(t);
        let raw = String.sub(text, 5, String.length(text) - 5);
        let items = Str.split(Str.regexp_string(","), raw) |> List.map(String.trim);
        (items |> List.map(name => fold(findByName(docItems, name), "Invalid doc item referenced: " ++ name, (doc) => {
          Hashtbl.add(shownItems, doc, true);
          let (html, tocs) = generateDoc(printer, processDocString, path, tocLevel + lastLevel^, doc);
          addTocs(tocs);
          html
        }))) |> String.concat("\n\n");
      })
    } else if (String.trim(t) == "@includes") {
      Some({
        let items = docItems |> List.filter(((_, _, docItem)) => switch docItem {
        | Include(_) => true
        | _ => false
        });
        items |> List.map(doc => {
          Hashtbl.add(shownItems, doc, true);
          let (html, tocs) = generateDoc(printer, processDocString, path, tocLevel + lastLevel^, doc);
          addTocs(tocs);
          html
        }) |> String.concat("\n\n")
      })
    } else if (String.trim(t) == "@rest") {
      let itemsLeft = docItems |> List.filter(item => !Hashtbl.mem(shownItems, item));
      if (itemsLeft == [] || shownAll^) {
        Some("")
      } else {
        Some("<p>other items defined</p>" ++ (itemsLeft |> List.map(doc => {
          Hashtbl.add(shownItems, doc, true);
          let (html, tocs) = generateDoc(printer, processDocString, path, tocLevel + lastLevel^, doc);
          addTocs(tocs);
          html
        }) |> String.concat("\n\n")))
      }
    } else {
      None
    }
  }
  | _ => None
  };

  let (tocItems, override) = trackToc(tocLevel, processMagics);

  (processDocString(~override, path, name, Some(Module(Items([]))), docString @ [Omd.Paragraph([Omd.Text("@rest")])]), tocItems^)
};

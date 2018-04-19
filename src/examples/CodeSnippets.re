
let codeBlockPrefix = "DOCRE_CODE_BLOCK_";

let (/+) = Filename.concat;

/* type codeContext = Normal | Node | Window | Iframe | Canvas | Div | Log;
let contextString = c => switch c {
| Normal => "normal"
| Log => "log"
| Node => "node"
| Window => "window"
| Iframe => "iframe"
| Canvas => "canvas"
| Div => "div"
};

type codeOptions = {
  context: codeContext,
  shouldParseFail: bool,
  shouldTypeFail: bool,
  shouldRaise: bool,
  prefix: int,
  suffix: int,
  noEdit: bool,
  dontRun: bool,
  isolate: bool,
  sharedAs: option(string),
  uses: list(string),
  hide: bool,
}; */

let matchOption = (text, option) => if (Str.string_match(Str.regexp("^" ++ option ++ "(\\([^)]+\\))$"), text, 0)) {
  Some(Str.matched_group(1, text));
} else {
  None
};

let parseCodeOptions = lang => {
  open State.Model;
  let parts = Str.split(Str.regexp_string(";"), lang);
  if (List.mem("skip", parts)
  || List.mem("bash", parts)
  || List.mem("txt", parts)
  || List.mem("js", parts)
  || List.mem("javascript", parts)
  || List.mem("sh", parts)) {
    None
  } else {
    let options = List.fold_left((options, item) => {
      switch item {
      | "window" => {...options, context: Window}
      | "canvas" => {...options, context: Canvas}
      | "iframe" => {...options, context: Iframe}
      | "log" => {...options, context: Log}
      | "div" => {...options, context: Div}

      | "raises" => {...options, expectation: Raise}
      | "parse-fail" => {...options, expectation: ParseFail}
      | "type-fail" => {...options, expectation: TypeFail}
      /* | "isolate" => {...options, isolate: true} */
      | "no-run" => {...options, expectation: DontRun}
      | "no-edit" => {...options, codeDisplay: {...options.codeDisplay, noEdit: true}}
      | "hide" => {...options, codeDisplay: {...options.codeDisplay, hide: true}}
      | "reason" | "re" => {...options, lang: Reason}
      | "ocaml" | "ml" => {...options, lang: OCaml}
      | "txt" => {...options, lang: Txt}
      | _ => {
        switch (matchOption(item, "shared")) {
        | Some(name) => {...options, sharedAs: Some(name)}
        | None => switch (matchOption(item, "use")) {
          | Some(name) => {...options, uses: [name, ...options.uses]}
          | None => switch (matchOption(item, "prefix")) {
            | Some(content) => {...options, codeDisplay: {...options.codeDisplay, prefix: int_of_string(content)}}
            | None => switch (matchOption(item, "suffix")) {
              | Some(content) => {...options, codeDisplay: {...options.codeDisplay, suffix: int_of_string(content)}}
              | None => {
                if (parts == [item]) {
                  {...options, lang: OtherLang(item)}
                } else {
                  print_endline("Skipping unexpected code option: " ++ item);
                  options
                }
              }
            }

          }
        }
        }
      }
      }
    }, defaultOptions, parts);
    if (options.lang == Reason || options.lang == OCaml) {
      Some(options)
    } else {
      None
    }
  }
};

type codeBlock = {
  el: Omd.element,
  id: int,
  fileName: string,
  options: State.Model.codeOptions,
  content: string,
};

/* type compilationStatus =
  /* | Skipped */
  | ParseError(string)
  | TypeError(string, string) /* error & cmt file */
  | Success(string, string) /* cmt & js files */
  ; TODO maybe do the bundling earlier, so that it can be processed in parallel? */


type compiledBlock = {
  block: codeBlock,
  status: State.Model.compilationResult,
  /* reasonSource: string, */
};

let sprintf = Printf.sprintf;
let html = Omd_utils.htmlentities;
let escapeScript = text => Str.global_replace(Str.regexp_string("</script"), "<\\/script", text);

let shouldBundle = expectation => switch expectation {
| State.Model.Succeed | Raise => true
| _ => false
};

let highlight = (~editingEnabled, {id, content, options}, status, bundle) => {
  open State.Model;
  let cmt = switch status {
  | ParseError(_) => None
  | TypeError(_, cmt) => Some(cmt)
  | Success(cmt, _) => Some(cmt)
  };
  let (pre, code, post) = switch cmt {
  | None => {
    let (pre, _, code, post, _) = CodeHighlight.codeSections(content);
    (pre, html(code), post)
  }
  | Some(cmt) => CodeHighlight.highlight(content, cmt)
  };

  let after = switch status {
  | ParseError(text) => sprintf({|<div class='parse-error'>Parse Error:\n%s</div>|}, html(text))
  | TypeError(text, _) => sprintf({|<div class='type-error'>Type Error:\n%s</div>|}, html(text))
  | Success(cmt, js) => !shouldBundle(options.expectation) ? "" : Printf.sprintf({|%s<script type='docre-bundle' data-block-id='%d'>%s</script>|},
      switch options.context {
      | Node => ""
      | _ => sprintf({|<div data-block-id='%d' data-context=%S class='block-target'></div>|}, id, contextString(options.context))
      },
      id,
      bundle(js)
    )
  };

  let preCode = pre == "" ? "" : "<div class='code-pre'>" ++ html(pre) ++ "</div>";
  let postCode = post == "" ? "" : "<div class='code-post'>" ++ html(post) ++ "</div>";

  sprintf(
    {|<div class='code-block'>
  %s
  <pre class='code' data-block-id='%d' id='block-%d'><code>%s</code></pre>
  %s
  %s
  %s
</div>|},
    preCode,
    id,
    id,
    code,
    postCode,
    (!editingEnabled || options.codeDisplay.noEdit) ? "" : sprintf({|<script type='docre-source' data-block-id="%d">%s</script>|}, id, escapeScript(content)),
    after
  )
};

let splitLines = text => Str.split(Str.regexp_string("\n"), text);

let sliceToEnd = (s, num) => String.length(s) < num ? s : String.sub(s, num, String.length(s) - num);

/* let removeHashes = text => Str.global_replace(Str.regexp("^#"), " ", text); */
let removeHashes = text => {
  let rec loop = lines => switch lines {
  | [] => []
  | ["", ...rest] => ["", ...loop(rest)]
  | [one, ...rest] when one.[0] == '#' => [" " ++ sliceToEnd(one, 1), ...loop(rest)]
  | [one, ...rest] when one.[0] == '!' && String.length(one) >= 2 && one.[1] == '#' => ["  " ++ sliceToEnd(one, 2), ...loop(rest)]
  | _ => lines
  };
  let front = loop(splitLines(text));
  String.concat("\n", List.rev(loop(List.rev(front))))
};

let hashAll = text => splitLines(text) |> List.map(t => (CodeHighlight.isHashed(t) ? t : "#" ++ t)) |> String.concat("\n");
let slice = (s, pre, post) => String.sub(s, pre, String.length(s) - pre + post);
let startsWith = (prefix, string) => {
  let lp = String.length(prefix);
  lp <= String.length(string) && String.sub(string, 0, lp) == prefix
};

let fullContent = (getShared, {State.Model.codeDisplay: {prefix, suffix}} as options, content) => {
  let content = prefix == 0 && suffix == 0 ? content : {
    let lines = splitLines(content) |> Array.of_list;
    for (i in 0 to prefix - 1) {
      lines[i] = "#" ++ lines[i];
    };
    let ln = Array.length(lines);
    for (i in 0 to suffix - 1) {
      lines[ln - 1 - i] = "#" ++ lines[ln - 1 - i];
    };
    String.concat("\n", Array.to_list(lines))
  };

  List.fold_left((content, name) => {
    switch (getShared(name)) {
    | exception Not_found => {
      print_endline("Unknown shared " ++ name);
      content ++ " /* unknown shared " ++ name ++ " */"
    }
    | text => {
      let text = text |> hashAll;
      let rx = Str.regexp_string("#%{code}%");
      switch (Str.search_forward(rx, text, 0)) {
      | exception Not_found => text ++ "\n" ++ content
      | _ => Str.replace_first(rx, content, text)
      }
    }
  }
  }, content, options.uses);

};

let getCodeBlocks = (markdowns, cmts) => {
  let codeBlocks = ref((0, []));
  let shared = ref([]);
  let addBlock = (el, fileName, lang, content) => {
    let options = parseCodeOptions(lang);
    switch (options) {
    | None => ()
    | Some(options) => {
      let content = fullContent(name => List.assoc(name, shared^), options, content);

      switch (options.sharedAs) {
      | None => {
        let (id, blocks) = codeBlocks^;
        codeBlocks := (id + 1, [{el, id, fileName, options, content}, ...blocks]);
      }
      | Some(name) => shared := [(name, content), ...shared^];
      };
    }
    }
  };

  let rec collect = (fileName, md) => Omd.Representation.visit(el => switch el {
    | Omd.Html_comment(text) when startsWith("<!--!", text) => {
      collect(fileName, Omd.of_string(slice(text, 5, -3)));
      None
    }
    | Omd.Html_comment(text) => {
      None
    }
    | Omd.Code_block(lang, contents) => {
      addBlock(el, fileName, lang, contents);
      None
    }
    | _ => None
  }, md) |> ignore;

  markdowns |> List.iter(((path, source, contents, name)) => {
    collect(path, contents);
  });

  cmts |> List.iter(((name, cmt, _, topDoc, allDocs)) => {
    Infix.(topDoc |?>> collect(cmt) |> ignore);
    allDocs |> List.iter(CmtFindDocItems.iter(((name, docString, _)) => {
      switch docString {
      | None => ()
      | Some(docString) => collect(cmt ++ " > " ++ name, docString)
      }
    }))
  });

  codeBlocks^ |> snd
};

open Infix;

let snippetLoader = (name, basePath, snippetPath) => Printf.sprintf({|
var path = require('path');
var Module = require('module');
var oldResolveFilename = Module._resolveFilename;
Module._resolveFilename = function (request, parent, isMain) {
  var name = '%s/';
  var base = '%s';
  if (request.indexOf(name) === 0) {
    return oldResolveFilename.call(this, path.join(base, request.slice(name.length)), parent, isMain);
  }

  return oldResolveFilename.call(this, request, parent, isMain);
};
require('%s')
|}, name, basePath, snippetPath);

let optMap = (fn, items) => List.fold_left((result, item) => switch (fn(item)) { | None => result | Some(res) => [res, ...result]}, [], items);

let getSourceDirectories = (base, config) => {
  let rec handleItem = (current, item) => {
    switch item {
    | Json.Array(contents) => List.map(handleItem(current), contents) |> List.concat
    | Json.String(text) => [current /+ text]
    | Json.Object(_) =>
      let dir = Json.get("dir", item) |?> Json.string |? "Must specify directory";
      let backend = item |> Json.get("backend") |?> Json.array |?>> optMap(Json.string) |? ["js"];
      /* print_endline("Backend? " ++ String.concat(" & ", backend)); */
      let typ = item |> Json.get("type") |?> Json.string |? "lib";
      if (typ == "dev" || !List.mem("js", backend)) {
        []
      } else {
        [current /+ dir, ...switch (item |> Json.get("subdirs")) {
        | None => []
        | Some(Json.True) => Files.collectDirs(base /+ current /+ dir) |> List.map(Files.relpath(base))
        | Some(item) => handleItem(current /+ dir, item)
        }]
      }
    | _ => failwith("Invalid subdirs entry")
    };
  };
  config |> Json.get("sources") |?>> handleItem("") |? []
};

let isNative = config => Json.get("entries", config) != None || Json.get("allowed-build-kinds", config) != None;

let getDependencyDirs = (base, config) => {
  let deps = config |> Json.get("bs-dependencies") |?> Json.array |? [] |> optMap(Json.string);
  deps |> List.map(name => {
    let loc = base /+ "node_modules" /+ name;
    switch (Files.readFile(loc /+ "bsconfig.json")) {
    | Some(text) =>
      let inner = Json.parse(text);
      let allowedKinds = inner |> Json.get("allowed-build-kinds") |?> Json.array |?>> List.map(Json.string |.! "allowed-build-kinds must be strings") |? ["js"];
      let isNative = isNative(inner);
      /* TODO get directories from config */

  let compilationBase = isNative ? "lib/bs/js" : (
    Files.ifExists(loc /+ "lib/ocaml") |? (loc /+ "lib/bs")
  );
      if (List.mem("js", allowedKinds)) {
        getSourceDirectories(loc, inner) |> List.map(name => (
          compilationBase /+ name,
          loc /+ "lib/js" /+ name,
        ));
      } else {
        []
      }
    | None =>
      print_endline("Skipping dependency: " ++ name);
      []
    }
  }) |> List.concat
};

let invert = (f, a) => !f(a);

let writeDeps = (dest, dependencyDirs, depsToLoad, stdlibRequires, bsRoot, base) => {
  let out = open_out(dest);
  /* print_endline("Deps : " ++ String.concat(", ", depsToLoad)); */

  let depsMap = dependencyDirs |> List.map(((dir, jsDir)) => Files.readDirectory(dir) |> List.filter(name => Filename.check_suffix(name, ".cmi")) |> List.map(name => {
    let path = dir /+ name;
    let cmj = Filename.chop_extension(name) ++ ".cmj";
    let js = Filename.chop_extension(name) ++ ".js";

    output_string(out, "ocaml.load_module(\"/static/cmis/" ++ name ++ "\", ");
    SerializeBinary.pp_string(out, Files.readFile(path) |! "file not readable " ++ path);
    output_string(out, ",\n\"" ++ cmj ++ "\", ");
    SerializeBinary.pp_string(out, Files.readFile(Filename.chop_extension(path) ++ ".cmj") |! "cmj not readable " ++ path);
    output_string(out, ");\n");

    ("stdlib" /+ String.uncapitalize(Filename.chop_extension(name)), jsDir /+ js)
  })) |> List.concat;
  let depsMap = depsMap @ (stdlibRequires |> List.map(path => {
    ("stdlib" /+ String.uncapitalize(Filename.chop_extension(Filename.basename(path))), path)
  }));
  output_string(out, "window.bsRequirePaths = {\n");
  depsMap |> List.iter(((bsRequire, path)) => {
    output_string(
      out,
      Printf.sprintf({|"%s": "%s",
|},
        bsRequire,
        Files.relpath(base, path)
      )
    );
  });
  output_string(out, "}\n");
  close_out(out);
};

let refmtCommand = (base, re, refmt) => {
  Printf.sprintf({|cat %s | %s --print binary > %s.ast && %s %s.ast %s_ppx.ast|},
  re,
  refmt,
  re,
  base /+ "node_modules/bs-platform/lib/reactjs_jsx_ppx_2.exe",
  re,
  re
  )
};

let justBscCommand = (base, re, includes) => {
  Printf.sprintf(
    {|%s -w -A %s -impl %s|},
    base /+ "node_modules/.bin/bsc",
    includes |> List.map(Printf.sprintf("-I %S")) |> String.concat(" "),
    re
  )
};

let processBlock = (base, tmp, name, refmt, options, reasonContent, dependencyDirs) => {
  let re = tmp /+ name ++ ".re";
  let cmt = tmp /+ name ++ ".re_ppx.cmt";
  let js = tmp /+ name ++ ".re_ppx.js";

  Files.writeFile(re, reasonContent) |> ignore;

  let cmd = refmtCommand(base, re, refmt);
  let (output, err, success) = Commands.execFull(cmd);
  open State.Model;
  if (!success) {
    let out = String.concat("\n", output) ++ String.concat("\n", err);
    if (options.expectation != State.Model.ParseFail) {
      print_endline("Failed to parse " ++ re);
      print_endline(out);
      print_endline(reasonContent);
      print_newline();
    };
    let out = Str.global_replace(Str.regexp_string(re), "<snippet>", out);
    ParseError(out);
  } else {
    let includes = dependencyDirs |> List.map(fst);
    let cmd = justBscCommand(base, re ++ "_ppx.ast", includes);
    let (output, err, success) = Commands.execFull(cmd);
    if (!success) {
      let out = String.concat("\n", output) ++ String.concat("\n", err);
      if (options.expectation != State.Model.TypeFail) {
        print_endline(cmd);
        print_endline("Failed to compile " ++ re);
        print_endline(out);
        print_endline(reasonContent);
        print_newline();
      };
      let out = Str.global_replace(Str.regexp_string(re), "<snippet>", out);
      TypeError(out, cmt);
    } else { Success(cmt, js) };
  };
};

let compileSnippets = (~bsRoot, base, dest, blocks) => {

  let config = Json.parse(Files.readFile(base /+ "bsconfig.json") |! "No bsconfig.json found");
  let isNative = isNative(config);

  let compilationBase = isNative ? "lib/bs/js" : (
    Files.ifExists(base /+ "lib/ocaml") |? (base /+ "lib/bs")
  );

  let mine = getSourceDirectories(base, config) |> List.map(name => (
    compilationBase /+ name,
    base /+ "lib/js" /+ name
  )) |> List.filter(((compiled, sourced)) => Files.exists(compiled));
  let dependencyDirs = mine @ getDependencyDirs(base, config);
  let depsToLoad = dependencyDirs |> List.map(((dir, jsDir)) => Files.readDirectory(dir) |> List.filter(name => Filename.check_suffix(name, ".cmi")) |> List.map(name => dir /+ name)) |> List.concat;
  let stdlib = bsRoot /+ "lib/js";
  let stdlibRequires = Files.exists(stdlib) ? (Files.readDirectory(stdlib) |> List.filter(invert(startsWith("node_"))) |> List.map(name => stdlib /+ name)) : [];

  writeDeps(dest /+ "bucklescript-deps.js", dependencyDirs, depsToLoad, stdlibRequires, bsRoot, base);

  let tmp = base /+ "node_modules/.docre";
  Files.mkdirp(tmp);

  let blockFileName = id => codeBlockPrefix ++ string_of_int(id);
  let refmt = Files.ifExists(bsRoot /+ "lib/refmt3.exe") |? bsRoot /+ "lib/refmt.exe";

  let blocksByEl = Hashtbl.create(100);
  let blocks = blocks |> List.map(({el, id, fileName, options, content} as block) => {

    let name = blockFileName(id);

    let reasonContent = removeHashes(content) ++ " /* " ++ fileName ++ " */";

    let status = processBlock(base, tmp, name, refmt, options, reasonContent, dependencyDirs);
    Hashtbl.replace(blocksByEl, el, {status, block});
    {status, block}
  });

  (blocksByEl, blocks, stdlibRequires)
};

let escape = text => text
|> Str.global_replace(Str.regexp_string("\\"), "\\\\")
|> Str.global_replace(Str.regexp_string("\n"), " ")
|> Str.global_replace(Str.regexp_string("\""), "\\\"")
;

let shouldTest = expectation => switch expectation {
| State.Model.Succeed | Raise => true
| _ => false
};

/* TODO allow package-global settings, like "run this in node" */
let process = (~bsRoot, ~editingEnabled, ~test, markdowns, cmts, base, dest) =>  {

  let blocks = getCodeBlocks(markdowns, cmts);
  let packageJson = Json.parse(Files.readFile(base /+ "package.json") |! "No package.json in " ++ base);
  let packageJsonName = packageJson |> Json.get("name") |?> Json.string |! "Missing name in package.json";

  let (blocksByEl, blocks, stdlibRequires) = compileSnippets(~bsRoot, base, dest, blocks);

  if (test) {
    print_endline("Running tests");

    /* TODO run in parallel - maybe all in the same node process?? */
    blocks |> List.iter(({status, block: {options, fileName, id}}) => {
      if (test && shouldTest(options.expectation) && options.context == Normal) {
        print_endline(string_of_int(id) ++ " - " ++ fileName);
        switch status {
        | Success(_, js) =>
        let cmd = Printf.sprintf("node -e \"%s\"", snippetLoader(packageJsonName, Files.absify(base), js) |> escape);
        let (output, err, success) = Commands.execFull(cmd);
        /* let (output, err, success) = Commands.execFull("node " ++ js ++ ""); */
        if (options.expectation == State.Model.Raise) {
          if (success) {
            print_endline(String.concat("\n", output));
            print_endline(String.concat("\n", err));
            print_endline("Expected to fail but didnt " ++ string_of_int(id) ++ " in " ++ fileName);
          }
        } else {
          if (!success) {
            print_endline(String.concat("\n", output));
            print_endline(String.concat("\n", err));
            print_endline("Failed to run " ++ string_of_int(id) ++ " in " ++ fileName);
          };
        }
        | _ => print_endline("Not running because of error")
        }
      };
      ()
    });
  };

  let allJsFiles: list(string) = blocks |> optMap(({status}) => switch status {
    | Success(_, js) => Some(js) | _ => None
  });

  let bundle = try (Packre.Pack.process(
    ~mode=Packre.Types.JustExternals,
    ~renames=[(packageJsonName, base)],
    ~extraRequires=stdlibRequires,
    ~base,
    allJsFiles
  )) {
    | _ => "alert('Failed to bundle')"
  };
  Files.writeFile(dest /+ "all-deps.js", bundle ++ ";window.loadedAllDeps = true;") |> ignore;

  (element) => switch element {
  | Omd.Code_block(lang, content) => {
    switch (Hashtbl.find(blocksByEl, element)) {
    | exception Not_found => {
      let options = parseCodeOptions(lang);
      switch options {
      | Some({sharedAs: Some(_)}) => Some("")
      | _ => None
      }
    }
    | {block, status} => {
      if (block.options.State.Model.codeDisplay.hide) {
        Some("")
      } else {
        Some(highlight(~editingEnabled, block, status, js => Packre.Pack.process(~mode=Packre.Types.ExternalEverything, ~renames=[(packageJsonName, base)], ~base, [js])))
      }
    }
    }
  }
  | _ => None
  };
};
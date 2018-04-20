
open Infix;

let writeEditorSupport = (static, directory, (browserCompilerPath, compilerDepsBuffer)) => {
  Files.copyExn(~source=browserCompilerPath, ~dest=directory /+ "bucklescript.js");
  let out = open_out(directory /+ "bucklescript-deps.js");
  Buffer.output_buffer(out, compilerDepsBuffer);
  close_out(out);

  ["jsx-ppx.js",
  "refmt.js",
  "codemirror-5.36.0/lib/codemirror.js",
  "codemirror-5.36.0/lib/codemirror.css",
  "codemirror-5.36.0/mode/rust/rust.js",
  "codemirror-5.36.0/addon/mode/simple.js"]
  |> List.iter(name => {
    Files.copyExn(~source=static /+ name, ~dest=directory /+ Filename.basename(name));
  });
};

let makeSearchPage = (~markdowns, ~names, dest, searchables) => {
  let path = dest /+ "search.html";
  let rel = Files.relpath(Filename.dirname(path));
  let markdowns = List.map(({State.Model.title, destPath, contents}) => (destPath, title), markdowns);
  let projectListing = names |> List.map(name => (rel(dest /+ "api" /+ name ++ ".html"), name));
  let main = Printf.sprintf({|
    <input placeholder="Search the docs" id="search-input"/>
    <style>%s</style>
    <div id="search-results"></div>
    <link rel=stylesheet href="search.css">
    <script defer src="searchables.json.index.js"></script>
    <script defer src="elasticlunr.js"></script>
    <script defer src="search.js"></script>
  |}, DocsTemplate.searchStyle);
  let html = Docs.page(~sourceUrl=None, ~relativeToRoot=rel(dest), "Search", [], projectListing, markdowns, main);
  Files.writeFile(path, html) |> ignore;
  Files.writeFile(dest /+ "search.js", SearchScript.js) |> ignore;
  Files.writeFile(dest /+ "elasticlunr.js", ElasticRaw.raw) |> ignore;
  Files.writeFile(dest /+ "searchables.json", Search.serializeSearchables(searchables^)) |> ignore;
  MakeIndex.run(dest /+ "elasticlunr.js", dest /+ "searchables.json")
};

let outputCustom = (dest, markdowns, searchHref, repo, processDocString, names, {State.Model.title, destPath, sourcePath, contents}) => {
  let path = dest /+ destPath;

  let rel = Files.relpath(Filename.dirname(path));
  let (tocItems, override) = GenerateDoc.trackToc(~lower=true, 0, Markdown.linkifyMarkdown(path, dest));
  let searchPrinter = GenerateDoc.printer(searchHref, []);
  let m: (string, string, ~override:(Omd.element => option(string))=?, list(string), string, option(State.Model.Docs.docItem)) => Omd.t => string = processDocString(searchPrinter);
  let main = m(path, title, ~override=override, [], title, None, contents);

  let sourceUrl = repo |?> (url => sourcePath |?>> (sourcePath => url /+ sourcePath));

  let markdowns = List.map(({State.Model.title, destPath}) => (rel(dest /+ destPath), title), markdowns);
  let projectListing = names |> List.map(name => (rel(dest /+ "api" /+ name ++ ".html"), name));
  let html = Docs.page(~sourceUrl, ~relativeToRoot=rel(dest), title, List.rev(tocItems^), projectListing, markdowns, main);

  Files.writeFile(path, html) |> ignore;
};

let outputModule = (dest, codeBlocksMap, markdowns, searchHref, repo, processDocString, names, {State.Model.name, sourcePath, docs, items, stamps}) => {
  let output = dest /+ "api" /+ name ++ ".html";
  let rel = Files.relpath(Filename.dirname(output));

  let markdowns = List.map(({State.Model.title, destPath, contents}) => (rel(dest /+ destPath), title), markdowns);

  let searchPrinter = GenerateDoc.printer(searchHref, stamps);
  let sourceUrl = repo |?>> (url => url /+ sourcePath);
  let text = Docs.generate(
    ~sourceUrl,
    ~relativeToRoot=rel(dest),
    ~processDocString=processDocString(searchPrinter, output, name),
    name,
    docs,
    stamps,
    items,
    names,
    markdowns
  );

  Files.writeFileExn(output, text);
};

let makeCodeBlocksMap = codeBlocks => {
  let map = Hashtbl.create(100);
  codeBlocks |> List.iter(({State.Model.lang, html, raw, page, filePath: name, compilationResult} as block) => {
    Hashtbl.replace(map, (lang, raw), block)
  });
  map
};

let searchHref = (names, doc) => {
  switch (Docs.formatHref("", names, doc)) {
  | None => None
  | Some(href) => Some("./api/" ++ href)
  }
};

open State;

let package = (
  {State.Model.name, repo, custom, sidebar, modules},
  compilationResults,
  {State.Input.directory, template, search},
  {State.Input.static}
) => {
  Files.mkdirp(directory);

  let codeBlocks = compilationResults |?>> (((codeBlocks, bundles)) => {
    Files.copyExn(~source=static /+ "block-script.js", ~dest=directory /+ "block-script.js");
    bundles |?< ((runtimeDeps, compilerDeps)) => {
      Files.writeFileExn(directory /+ "all-deps.js", runtimeDeps ++ ";window.loadedAllDeps = true;");
      /* This is where we handle stuff for the editor. should be named "editorArtifacts" or something */
      compilerDeps |?< writeEditorSupport(static, directory);
    };
    codeBlocks;
  }) |? [];

  let cssLoc = Filename.concat(directory, "styles.css");
  let jsLoc = Filename.concat(directory, "script.js");

  Files.copyExn(static /+ "styles.css", cssLoc);
  Files.copyExn(static /+ "script.js", jsLoc);

  let codeBlocksMap = makeCodeBlocksMap(codeBlocks);

  let names = modules |> List.map(({Model.sourcePath, name}) => name);

  let (searchables, processDocString) = Markdown.makeDocStringProcessor(directory, element => switch element {
  | Omd.Code_block(lang, content) => {
    switch (Hashtbl.find(codeBlocksMap, (lang, content))) {
    | exception Not_found => {
      switch (CodeSnippets.parseCodeOptions(lang, State.Model.defaultOptions)) {
      | Some({codeDisplay: {hide: true}}) => Some("")
      | _ => None
      }
    }
    | {raw, html} => Some(html)
    }
  }
  | _ => None
  });

  Files.mkdirp(directory /+ "api");

  modules |> List.iter(outputModule(directory, codeBlocks, custom, searchHref(names), repo, processDocString, names));

  custom |> List.iter(outputCustom(directory, custom, searchHref(names), repo, processDocString, names));

  makeSearchPage(~markdowns=custom, ~names, directory, searchables);

  print_endline("Ok packaged folks " ++ directory);

  let localUrl = "file://" ++ Files.absify(directory) /+ "index.html";
  print_newline();
  print_endline("Complete! Docs are available in " ++ directory ++ "\nOpen " ++ localUrl ++ " in your browser to view");
  print_newline();
};
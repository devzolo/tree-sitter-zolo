fn main() {
    let src_dir = std::path::Path::new("src");

    let mut c_config = cc::Build::new();
    c_config.std("c11").include(src_dir);
    c_config
        .flag_if_supported("-Wno-unused-parameter")
        .flag_if_supported("-Wno-unused-but-set-variable")
        .flag_if_supported("-Wno-trigraphs");
    #[cfg(target_env = "msvc")]
    c_config.flag("-utf-8");

    let parser_path = src_dir.join("parser.c");
    c_config.file(&parser_path);
    println!("cargo:rerun-if-changed={}", parser_path.to_str().unwrap());
    // Large generated tables are immutable C includes beside parser.c. They
    // participate in the same translation unit and in Cargo's rebuild inputs.
    for entry in std::fs::read_dir(src_dir).expect("generated parser sources") {
        let path = entry.expect("generated parser source entry").path();
        if path.extension().is_some_and(|extension| extension == "inc") {
            println!("cargo:rerun-if-changed={}", path.display());
        }
    }

    // `_markup_lt` — whether a `<` opens markup or is a comparison is a
    // lexer decision in the Zolo compiler too, so it is one here. Without
    // this the parser fails to link.
    let scanner_path = src_dir.join("scanner.c");
    c_config.file(&scanner_path);
    println!("cargo:rerun-if-changed={}", scanner_path.to_str().unwrap());

    c_config.compile("tree-sitter-zolo");
}

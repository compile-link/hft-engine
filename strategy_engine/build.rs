fn main() {
    prost_build::compile_protos(&["../proto/market_data.proto"], &["../proto"])
        .expect("failed to compile proto");
}
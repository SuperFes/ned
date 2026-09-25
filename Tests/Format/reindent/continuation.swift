func f() {
    let x = 1 +
        2
    let y = items
        .map { $0 + 1 }
        .filter {
            $0 > 0
        }
    z = cond
        ? a
        : b
    return a &&
        b
}
struct V {
    @State
    var x = 1
    var body: some View {
        VStack {
            Text("a")
        }
        .padding()
    }
}

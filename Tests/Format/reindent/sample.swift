import Foundation

struct Point {
    let x: Int
    var doubled: Int {
        get {
            return x * 2
        }
    }

    func area(width: Int) throws -> Int {
        guard width >= 0 else {
            throw AreaError.negative
        }
        if width == 0 {
            return 0
        } else if width < 10 {
            return x * width
        }
        else {
            switch width {
            case 10, 20:
                return x
            default:
                let scaled = [1, 2, 3].map { value in
                    value * width
                }
                return scaled.reduce(0, +)
            }
        }
    }
}

func load() {
    do {
        let data = try Data(contentsOf: url)
        print(data)
    } catch let error {
        print(error)
    }
    for i in 0..<3 {
        while i > 0 {
            break
        }
    }
}

mod demo {
    pub struct Point {
        pub x: i32,
        pub y: i32,
    }

    pub enum Shape {
        Circle,
        Square,
    }

    pub fn total(values: &[i32]) -> i32 {
        let mut sum = 0;
        for v in values {
            sum += v;
        }
        match sum {
            0 => 0,
            n => n,
        }
    }
}

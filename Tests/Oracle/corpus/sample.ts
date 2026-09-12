interface Sized {
    size(): number;
}

class Widget implements Sized {
    constructor(private name: string) {
    }

    size(): number {
        return this.name.length;
    }
}

const defaults = {
    width: 10,
    height: 20,
};

function total(values: number[]): number {
    let sum = 0;
    for (const v of values) {
        sum += v;
    }
    return sum;
}

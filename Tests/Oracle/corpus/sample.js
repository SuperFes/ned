class Widget {
    constructor(name) {
        this.name = name;
    }

    shout() {
        return this.name.toUpperCase();
    }
}

const defaults = {
    width: 10,
    height: 20,
};

function total(values) {
    let sum = 0;
    for (const v of values) {
        sum += v;
    }
    return sum;
}
